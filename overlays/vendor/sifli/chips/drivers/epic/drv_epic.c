/****************************************************************************
 * vendor/sifli/chips/drivers/epic/drv_epic.c
 *
 * SiFli EPIC 2.5D GPU 驱动（NuttX 移植版，单步模式）。
 *
 * 移植自 SiFli-SDK（Apache-2.0）：
 *   rtos/rtthread/bsp/sifli/drivers/drv_epic.c        （open/close/诊断）
 *   rtos/rtthread/bsp/sifli/drivers/drv_epic_single.c （各绘制操作）
 * 逻辑保持与官方一致，便于对照上游 review；只替换了 OS 原语：
 *
 *   rt_sem_*                → nxsem_*（g_epic_sema，初值 1，做 GPU 互斥）
 *   EPIC_IRQHandler         → irq_attach(NX_IRQ(EPIC_IRQn)) 的 NuttX ISR
 *   rt_interrupt_enter/leave→ 不需要（NuttX ISR 框架自带）
 *   mpu_dcache_*            → up_clean_dcache / up_invalidate_dcache
 *   rt_flash_lock/unlock    → 空实现。RT-Thread 用它挡住"GPU 读 XIP flash
 *                             图源时另一个任务擦写 flash"。本固件渲染路径
 *                             没有并发 flash 擦写，先不处理；若日后图源
 *                             放 flash 且有后台写入，需要在 MTD 层加锁。
 *   rt_pm_request/release   → 空实现（未启用 PM）
 *
 * 硬性约束（真机实测，见 project-docs/PERF_FINDINGS.md）：
 *   - EPIC 作为 DMA master 访问不到 PSRAM（0x60000000 段），
 *     所有输入/输出 buffer 必须在 SRAM 或 XIP flash 上。
 *     上层（LVGL draw unit）负责用 drv_epic_addr_reachable() 过滤。
 *   - EZIP handle 必须先于 HAL_EPIC_Init 就位，否则空指针写入。
 *
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>   /* enter_critical_section()（非 SMP 下即 up_irq_save）*/
#include <arch/irq.h>

#include <sfconfig.h>              /* NX_IRQ()：芯片中断号 → NuttX 中断号 */

#include "bf0_hal.h"
#include "drv_epic.h"
#include "drv_epic_priv.h"

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static HAL_StatusTypeDef epic_split_render_next(uint8_t init);
static void epic_cplt_callback(EPIC_HandleTypeDef *epic);
static void cont_blend_reset(void);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* .bss 落在 SRAM，EPIC 能访问。对应官方的 L1_RET_BSS_SECT 段。 */

static EPIC_DrvTypeDef g_drv_epic;
static EPIC_DrvTypeDef *g_drv_epic_inited;

/* GPU 互斥信号量，初值 1。取到 = 独占 GPU。 */

static sem_t g_epic_sema;

/* 已 open 标志，对应官方的静态指针 epic。 */

static EPIC_HandleTypeDef *g_epic_opened;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: drv_gpu_is_cached_ram
 *
 * Description:
 *   判断内存段是否经 D-cache。SF32LB52 的 cache 只覆盖 XIP 外存；
 *   CPU 对 SRAM 的读写不经 cache。EPIC 又访问不到 PSRAM，flash 图源
 *   是只读的（CPU 从不通过 cache 写它），所以只有 PSRAM 需要维护 ——
 *   与官方 IS_DCACHED_RAM(addr) ((addr) >= PSRAM_BASE) 的口径一致。
 *
 ****************************************************************************/

uint8_t drv_gpu_is_cached_ram(uint32_t start, uint32_t len)
{
  (void)len;
  return (start >= PSRAM_BASE) ? 1 : 0;
}

static void dcache_clean(void *data, uint32_t size)
{
  if (drv_gpu_is_cached_ram((uint32_t)(uintptr_t)data, size))
    {
      up_clean_dcache((uintptr_t)data, (uintptr_t)data + size);
    }
}

static void dcache_invalidate(void *data, uint32_t size)
{
  if (drv_gpu_is_cached_ram((uint32_t)(uintptr_t)data, size))
    {
      up_invalidate_dcache((uintptr_t)data, (uintptr_t)data + size);
    }
}

/****************************************************************************
 * Name: gpu_lock / gpu_unlock
 *
 * Description:
 *   一次 EPIC 操作的进入/退出手续：登记各层 buffer 地址、做 cache
 *   维护。锁本身（信号量）由调用方通过 wait_gpu_done 先行取得。
 *
 ****************************************************************************/

#define CALC_LAYER_SIZE(layer) \
  (EPIC_ALIGN((layer)->total_width * (layer)->height * \
              HAL_EPIC_GetColorDepth((layer)->color_mode), 8) >> 3)
#define GET_LAYER_SIZE(layer) \
  (EPIC_IS_EZIP_COLOR_MODE((layer)->color_mode) ? \
   (layer)->data_size : CALC_LAYER_SIZE(layer))

static void gpu_lock(drv_epic_op_type_t ops, void *p1, void *p2, void *p3)
{
  uint32_t mask_size = 0;
  uint32_t fg_size = 0;
  uint32_t bg_size = 0;

  EPIC_ASSERT((0 == g_drv_epic.gpu_fg_addr) &&
              (0 == g_drv_epic.gpu_bg_addr) &&
              (0 == g_drv_epic.gpu_mask_addr));

  /* Get fg,bg address and output area */

  switch (ops)
    {
    case DRV_EPIC_COLOR_BLEND:
    case DRV_EPIC_LETTER_BLEND:
      {
        EPIC_LayerConfigTypeDef *fg = p1;
        EPIC_LayerConfigTypeDef *bg = p2;
        EPIC_LayerConfigTypeDef *dst = p3;
        EPIC_ASSERT(NULL != dst);

        g_drv_epic.gpu_fg_addr = (uint32_t)(uintptr_t)fg->data;
        if (bg != NULL)
          {
            g_drv_epic.gpu_bg_addr = (uint32_t)(uintptr_t)bg->data;
            bg_size = GET_LAYER_SIZE(bg);
          }

        fg_size = GET_LAYER_SIZE(fg);

        g_drv_epic.gpu_output_addr = (uint32_t)(uintptr_t)dst->data;
        g_drv_epic.gpu_output_size = CALC_LAYER_SIZE(dst);
      }
      break;

    case DRV_EPIC_IMG_ROT:
    case DRV_EPIC_TRANSFORM:
      {
        EPIC_LayerConfigTypeDef *input_layers = p1;
        uint8_t input_layer_cnt = *((uint8_t *)p2);

        EPIC_LayerConfigTypeDef *bg = input_layers + 0;
        EPIC_LayerConfigTypeDef *fg = input_layers + 1;
        EPIC_LayerConfigTypeDef *mask =
            input_layer_cnt > 2 ? input_layers + 2 : NULL;
        EPIC_LayerConfigTypeDef *dst = p3;
        EPIC_ASSERT(NULL != dst);

        if (mask)
          {
            g_drv_epic.gpu_mask_addr = (uint32_t)(uintptr_t)mask->data;
            mask_size = GET_LAYER_SIZE(mask);
          }

        g_drv_epic.gpu_fg_addr = (uint32_t)(uintptr_t)fg->data;
        g_drv_epic.gpu_bg_addr = (uint32_t)(uintptr_t)bg->data;

        bg_size = GET_LAYER_SIZE(bg);
        fg_size = GET_LAYER_SIZE(fg);

        g_drv_epic.gpu_output_addr = (uint32_t)(uintptr_t)dst->data;
        g_drv_epic.gpu_output_size = CALC_LAYER_SIZE(dst);
      }
      break;

    case DRV_EPIC_COLOR_FILL:
      {
        EPIC_LayerConfigTypeDef *mask = p1;
        EPIC_LayerConfigTypeDef *param = p3;
        EPIC_ASSERT(NULL != param);

        g_drv_epic.gpu_output_addr = (uint32_t)(uintptr_t)param->data;
        g_drv_epic.gpu_output_size = CALC_LAYER_SIZE(param);

        if (mask)
          {
            g_drv_epic.gpu_mask_addr = (uint32_t)(uintptr_t)mask->data;
            mask_size = GET_LAYER_SIZE(mask);
          }
      }
      break;

    case DRV_EPIC_IMG_COPY:
      {
        EPIC_BlendingDataType *fg = p1;
        EPIC_BlendingDataType *dst = p3;

        g_drv_epic.gpu_fg_addr = (uint32_t)(uintptr_t)fg->data;
        g_drv_epic.gpu_bg_addr = g_drv_epic.gpu_fg_addr;
        fg_size = GET_LAYER_SIZE(fg);
        bg_size = fg_size;

        g_drv_epic.gpu_output_addr = (uint32_t)(uintptr_t)dst->data;
        g_drv_epic.gpu_output_size = CALC_LAYER_SIZE(dst);
      }
      break;

    case DRV_EPIC_FILL_GRAD:
      {
        EPIC_GradCfgTypeDef *param = p3;

        g_drv_epic.gpu_output_addr = (uint32_t)(uintptr_t)param->start;
        g_drv_epic.gpu_output_size = CALC_LAYER_SIZE(param);
      }
      break;

    default:
      EPIC_ASSERT(0);
      break;
    }

  g_drv_epic.gpu_last_op = ops;

  /* DMA 启动前把 CPU 写入的最新内容刷出 cache（uncached 段自动跳过） */

  if (bg_size && g_drv_epic.gpu_bg_addr)
    {
      dcache_clean((void *)(uintptr_t)g_drv_epic.gpu_bg_addr, bg_size);
    }

  if (fg_size && g_drv_epic.gpu_fg_addr)
    {
      dcache_clean((void *)(uintptr_t)g_drv_epic.gpu_fg_addr, fg_size);
    }

  if (mask_size && g_drv_epic.gpu_mask_addr)
    {
      dcache_clean((void *)(uintptr_t)g_drv_epic.gpu_mask_addr, mask_size);
    }

  if (g_drv_epic.gpu_output_size)
    {
      dcache_clean((void *)(uintptr_t)g_drv_epic.gpu_output_addr,
                   g_drv_epic.gpu_output_size);
    }
}

static void gpu_unlock(void)
{
  /* 让 CPU 看到硬件写入的结果 */

  dcache_invalidate((void *)(uintptr_t)g_drv_epic.gpu_output_addr,
                    g_drv_epic.gpu_output_size);

  g_drv_epic.gpu_fg_addr = 0;
  g_drv_epic.gpu_bg_addr = 0;
  g_drv_epic.gpu_mask_addr = 0;

  g_drv_epic.gpu_output_addr = 0;
  g_drv_epic.gpu_output_size = 0;
}

/****************************************************************************
 * Name: print_gpu_error_info
 *
 * Description:
 *   GPU 卡死/超时诊断。官方版本的精简版：状态机、错误码、EOF 中断、
 *   各层源地址。EPIC 停在 BUSY 且 EOF_IRQ 恒 0 基本等于 buffer 在
 *   EPIC 访问不到的内存上（PSRAM）。
 *
 ****************************************************************************/

void print_gpu_error_info(void)
{
  EPIC_HandleTypeDef *epic = &g_drv_epic.epic_handle;

  g_drv_epic.gpu_timeout_cnt++;

  if (NULL == g_epic_opened)
    {
      EPIC_LOG_E("gpu not opened");
      return;
    }

  EPIC_LOG_E("state=%d hw_busy=%lu err=%lu", (int)epic->State,
             (unsigned long)(epic->Instance->STATUS & EPIC_STATUS_IA_BUSY),
             (unsigned long)epic->ErrorCode);

  EPIC_LOG_E("EOF IRQ=%lx MASK=%lx",
             (unsigned long)(epic->Instance->EOF_IRQ &
                             (EPIC_EOF_IRQ_IRQ_STATUS_Msk |
                              EPIC_EOF_IRQ_IRQ_CAUSE_Msk)),
             (unsigned long)(epic->Instance->SETTING &
                             EPIC_SETTING_EOF_IRQ_MASK));

  EPIC_LOG_E("last_op=%lu fg=%lx bg=%lx mask=%lx out=%lx",
             (unsigned long)g_drv_epic.gpu_last_op,
             (unsigned long)g_drv_epic.gpu_fg_addr,
             (unsigned long)g_drv_epic.gpu_bg_addr,
             (unsigned long)g_drv_epic.gpu_mask_addr,
             (unsigned long)g_drv_epic.gpu_output_addr);

#ifdef HAL_EZIP_MODULE_ENABLED
  {
    EZIP_HandleTypeDef *hezip = epic->hezip;

    if (hezip != NULL &&
        ((HAL_EZIP_STATE_READY != hezip->State) ||
         (hezip->Instance->EZIP_CTRL & EZIP_EZIP_CTRL_EZIP_CTRL)))
      {
        EPIC_LOG_E("ezip state=%d busy=%lx err=%lx", (int)hezip->State,
                   (unsigned long)(hezip->Instance->EZIP_CTRL &
                                   EZIP_EZIP_CTRL_EZIP_CTRL),
                   (unsigned long)hezip->ErrorCode);
      }
  }
#endif
}

/****************************************************************************
 * Name: epic_cplt_callback
 *
 * Description:
 *   EPIC 一次操作完成（HAL 中断回调，IRQ 上下文）。分块渲染没画完
 *   就续下一块；全部画完则解锁并转发用户回调。
 *
 ****************************************************************************/

static void epic_cplt_callback(EPIC_HandleTypeDef *epic)
{
  drv_epic_cplt_cbk cb;
  int err;

  if (DRV_EPIC_INVALID != g_drv_epic.split_rd.op)
    {
      HAL_StatusTypeDef hal_err = epic_split_render_next(0);
      if (HAL_OK == hal_err)
        {
          return;
        }
      else
        {
          g_drv_epic.split_rd.op = DRV_EPIC_INVALID;
          EPIC_LOG_E("epic_split_render ABORTED, err=%d", hal_err);
        }
    }

  cb = g_drv_epic.cbk;
  g_drv_epic.cbk = NULL;

  gpu_unlock();

  err = drv_gpu_release();
  EPIC_ASSERT(0 == err);
  (void)err;

  if (cb)
    {
      cb(epic);
    }
}

static void epic_abort_callback(EPIC_HandleTypeDef *epic)
{
  g_drv_epic.split_rd.op = DRV_EPIC_INVALID;
  epic_cplt_callback(epic);
}

/****************************************************************************
 * Name: grad_interp_ch / grad_interp_color
 *
 * Description:
 *   渐变分块渲染时按块位置重算四角颜色（双线性插值单通道/整色）。
 *
 ****************************************************************************/

static uint8_t grad_interp_ch(uint8_t c00, uint8_t c01, uint8_t c10,
                              uint8_t c11, int16_t x, int16_t y,
                              const EPIC_AreaTypeDef *area)
{
  int32_t w = HAL_EPIC_AreaWidth(area);
  int32_t h = HAL_EPIC_AreaHeight(area);
  int32_t x_num = x - area->x0;
  int32_t y_num = y - area->y0;

  int32_t top = (c00 * (w - x_num) + c01 * x_num) / w;
  int32_t bot = (c10 * (w - x_num) + c11 * x_num) / w;

  return (uint8_t)((top * (h - y_num) + bot * y_num) / h);
}

static EPIC_ColorDef grad_interp_color(int16_t x, int16_t y,
                                       const EPIC_AreaTypeDef *area)
{
  EPIC_ColorDef c;

  c.ch.alpha = grad_interp_ch(g_drv_epic.grad_color[0][0].ch.alpha,
                              g_drv_epic.grad_color[0][1].ch.alpha,
                              g_drv_epic.grad_color[1][0].ch.alpha,
                              g_drv_epic.grad_color[1][1].ch.alpha,
                              x, y, area);
  c.ch.color_r = grad_interp_ch(g_drv_epic.grad_color[0][0].ch.color_r,
                                g_drv_epic.grad_color[0][1].ch.color_r,
                                g_drv_epic.grad_color[1][0].ch.color_r,
                                g_drv_epic.grad_color[1][1].ch.color_r,
                                x, y, area);
  c.ch.color_g = grad_interp_ch(g_drv_epic.grad_color[0][0].ch.color_g,
                                g_drv_epic.grad_color[0][1].ch.color_g,
                                g_drv_epic.grad_color[1][0].ch.color_g,
                                g_drv_epic.grad_color[1][1].ch.color_g,
                                x, y, area);
  c.ch.color_b = grad_interp_ch(g_drv_epic.grad_color[0][0].ch.color_b,
                                g_drv_epic.grad_color[0][1].ch.color_b,
                                g_drv_epic.grad_color[1][0].ch.color_b,
                                g_drv_epic.grad_color[1][1].ch.color_b,
                                x, y, area);
  return c;
}

/****************************************************************************
 * Name: epic_split_render_next
 *
 * Description:
 *   渲染区域超过 EPIC 坐标上限（EPIC_COORDINATES_MAX=1010）时分块，
 *   每块完成后在中断回调里续下一块。390×450 屏永远不会触发，
 *   保留是为了跟官方实现完全一致。
 *
 ****************************************************************************/

static HAL_StatusTypeDef epic_split_render_next(uint8_t init)
{
  EPIC_AreaTypeDef *p_render_area = &g_drv_epic.split_rd.render_area;
  EPIC_AreaTypeDef *p_next_sub_area = &g_drv_epic.split_rd.next_render_area;
  drv_epic_op_type_t cur_op = g_drv_epic.split_rd.op;
  EPIC_AreaTypeDef cur_render_area;
  HAL_StatusTypeDef ret;

  /* Get current rendering area */

  if (init)
    {
      cur_render_area.x0 = p_render_area->x0;
      cur_render_area.x1 = MIN(p_render_area->x0 + EPIC_COORDINATES_MAX - 1,
                               p_render_area->x1);

      cur_render_area.y0 = p_render_area->y0;
      cur_render_area.y1 = MIN(p_render_area->y0 + EPIC_COORDINATES_MAX - 1,
                               p_render_area->y1);

      g_drv_epic.split_rd.next_render_area = cur_render_area;
    }
  else
    {
      cur_render_area = g_drv_epic.split_rd.next_render_area;
    }

  /* Move p_next_sub_area to next area */

  if (p_next_sub_area->x0 + EPIC_COORDINATES_MAX <= p_render_area->x1)
    {
      p_next_sub_area->x0 = p_next_sub_area->x0 + EPIC_COORDINATES_MAX;
      p_next_sub_area->x1 = MIN(p_next_sub_area->x0 +
                                EPIC_COORDINATES_MAX - 1,
                                p_render_area->x1);
    }
  else if (p_next_sub_area->y0 + EPIC_COORDINATES_MAX <= p_render_area->y1)
    {
      p_next_sub_area->x0 = p_render_area->x0;
      p_next_sub_area->x1 = MIN(p_next_sub_area->x0 +
                                EPIC_COORDINATES_MAX - 1,
                                p_render_area->x1);

      p_next_sub_area->y0 = p_next_sub_area->y0 + EPIC_COORDINATES_MAX;
      p_next_sub_area->y1 = MIN(p_next_sub_area->y0 +
                                EPIC_COORDINATES_MAX - 1,
                                p_render_area->y1);
    }
  else
    {
      g_drv_epic.split_rd.op = DRV_EPIC_INVALID; /* Render_area done. */
    }

  /* Clip dst layer to current rendering area */

  EPIC_BlendingDataType *p_dst_layer =
      (EPIC_BlendingDataType *)&g_drv_epic.output_layer;
  const EPIC_AreaTypeDef *dst_area = &g_drv_epic.split_rd.dst_area;
  clip_layer_to_area(p_dst_layer, g_drv_epic.split_rd.dst_data,
                     dst_area->x0, dst_area->y0, &cur_render_area);

  /* Start it. */

  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  switch (cur_op)
    {
    case DRV_EPIC_IMG_COPY:
      {
        EPIC_BlendingDataType *p_src_layer =
            (EPIC_BlendingDataType *)&g_drv_epic.input_layers[0];

        h_epic->XferCpltCallback = epic_cplt_callback;
        ret = HAL_EPIC_Copy_IT(h_epic, p_src_layer, p_dst_layer);
      }
      break;

    case DRV_EPIC_COLOR_FILL:
    case DRV_EPIC_IMG_ROT:
      {
        h_epic->XferCpltCallback = epic_cplt_callback;
        ret = HAL_EPIC_BlendStartEx_IT(h_epic, g_drv_epic.input_layers,
                                       g_drv_epic.input_layer_cnt,
                                       &g_drv_epic.output_layer);
      }
      break;

#ifdef DRV_EPIC_USE_HAL_ADV
    case DRV_EPIC_TRANSFORM:
      {
        ret = HAL_EPIC_Adv(h_epic, g_drv_epic.input_layers,
                           g_drv_epic.input_layer_cnt,
                           &g_drv_epic.output_layer);
      }
      break;
#endif

    case DRV_EPIC_FILL_GRAD:
      {
        EPIC_GradCfgTypeDef param;

        param.start = p_dst_layer->data;
        param.color_mode = p_dst_layer->color_mode;
        param.width = p_dst_layer->width;
        param.height = p_dst_layer->height;
        param.total_width = p_dst_layer->total_width;

        /* Recalculate corner colors for current tile position */

        param.color[0][0] = grad_interp_color(cur_render_area.x0,
                                              cur_render_area.y0,
                                              p_render_area);
        param.color[0][1] = grad_interp_color(cur_render_area.x1,
                                              cur_render_area.y0,
                                              p_render_area);
        param.color[1][0] = grad_interp_color(cur_render_area.x0,
                                              cur_render_area.y1,
                                              p_render_area);
        param.color[1][1] = grad_interp_color(cur_render_area.x1,
                                              cur_render_area.y1,
                                              p_render_area);

        h_epic->XferCpltCallback = epic_cplt_callback;
        ret = HAL_EPIC_FillGrad_IT(h_epic, &param);
      }
      break;

    default:
      EPIC_ASSERT(0);
      ret = HAL_ERROR;
      break;
    }

  return ret;
}

/****************************************************************************
 * Name: cont_blend_reset
 *
 * Description:
 *   结束连续 blend（字形批量渲染）会话，归还 GPU。
 *
 ****************************************************************************/

static void cont_blend_reset(void)
{
  if (g_drv_epic.cont_mode)
    {
      HAL_StatusTypeDef ret;
      EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;

      ret = HAL_EPIC_ContBlendStop(h_epic);

      if (HAL_OK == ret)
        {
          gpu_unlock();
          drv_gpu_release();
        }

      g_drv_epic.cont_mode = false;
    }
}

/****************************************************************************
 * Name: epic_isr / ezip_isr
 *
 * Description:
 *   NuttX 中断入口，转发给 HAL。接法照 epicbench 验证过的路径。
 *
 ****************************************************************************/

static int epic_isr(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;

  HAL_EPIC_IRQHandler((EPIC_HandleTypeDef *)arg);
  return OK;
}

#ifdef HAL_EZIP_MODULE_ENABLED
static int ezip_isr(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;

  HAL_EZIP_IRQHandler((EZIP_HandleTypeDef *)arg);
  return OK;
}
#endif

/****************************************************************************
 * Name: gpu_reset
 *
 * Description:
 *   GPU 超时后的复位恢复：软复位 EPIC/EZIP 模块并清挂起中断。
 *
 ****************************************************************************/

static void gpu_reset(void)
{
  EPIC_HandleTypeDef *epic = g_epic_opened;

  if (epic)
    {
      irqstate_t flags = enter_critical_section();

      epic->State = HAL_EPIC_STATE_READY;
      epic->ErrorCode = 0;
      epic->IntXferCpltCallback = NULL;
      HAL_RCC_ResetModule(RCC_MOD_EPIC);
#ifdef HAL_EZIP_MODULE_ENABLED
      epic->hezip->State = HAL_EZIP_STATE_READY;
      epic->hezip->ErrorCode = 0;
      epic->hezip->CpltCallback = NULL;
      HAL_RCC_ResetModule(RCC_MOD_EZIP);
      HAL_NVIC_ClearPendingIRQ(EZIP_IRQn);
#endif
      HAL_NVIC_ClearPendingIRQ(EPIC_IRQn);

      leave_critical_section(flags);
    }
}

/****************************************************************************
 * Name: wait_gpu_done
 *
 * Description:
 *   等上一次 GPU 操作完成并取得 GPU（信号量）。超时则复位重试。
 *
 ****************************************************************************/

static int wait_gpu_done(int32_t time)
{
  int err;

  cont_blend_reset();
  do
    {
      err = drv_gpu_take(time);

      if (0 != err)
        {
          EPIC_LOG_E("wait_gpu_done timeout? err=%d", err);
          gpu_reset();
          epic_abort_callback(&g_drv_epic.epic_handle);
        }
    }
  while (0 != err);

  return err;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: drv_epic_init
 *
 * Description:
 *   初始化驱动状态。官方经 INIT_PRE_APP_EXPORT 自动执行；
 *   这里由 drv_gpu_open 惰性调用。
 *
 ****************************************************************************/

static int drv_epic_init(void)
{
  if (NULL == g_drv_epic_inited)
    {
      memset(&g_drv_epic, 0, sizeof(EPIC_DrvTypeDef));
      g_drv_epic_inited = &g_drv_epic;

#ifdef HAL_EZIP_MODULE_ENABLED
      g_drv_epic.ezip_handle.Instance = EZIP;
#endif

      nxsem_init(&g_epic_sema, 0, 1);
      g_drv_epic.split_rd.op = DRV_EPIC_INVALID;
    }

  return 0;
}

/****************************************************************************
 * Name: drv_gpu_open
 *
 * Description:
 *   打开 GPU：初始化 EZIP → EPIC（顺序不能反，HAL_EPIC_Init 第一行
 *   就写 hezip->user_data），再挂 NuttX 中断。可重复调用。
 *
 ****************************************************************************/

void drv_gpu_open(void)
{
  if (NULL == g_epic_opened)
    {
      EPIC_HandleTypeDef *epic;

      if (NULL == g_drv_epic_inited)
        {
          drv_epic_init();
        }

      epic = &g_drv_epic.epic_handle;

      epic->Instance = EPIC;
      epic->RamInstance = &g_drv_epic.RamEPIC;

#ifdef HAL_EZIP_MODULE_ENABLED
      epic->hezip = &g_drv_epic.ezip_handle;
      epic->hezip->Instance = EZIP;

      /* flash_handle_query_cb 置 NULL 的理由见官方注释：NAND 坏块、
       * 双核 ITCM 访问等。本板 NOR + 单核，同样不需要。
       */

      epic->hezip->flash_handle_query_cb = NULL;
      epic->hezip->RamInstance = &g_drv_epic.RamEZIP;

      HAL_EZIP_Init(epic->hezip);
#endif

      HAL_EPIC_Init(epic);

      /* 挂 NuttX 中断向量。HAL_EPIC_Init 只开时钟配寄存器，
       * 向量表这步 HAL 不管（epicbench 验证过的接法）。
       */

      irq_attach(NX_IRQ(EPIC_IRQn), epic_isr, epic);
      up_enable_irq(NX_IRQ(EPIC_IRQn));

#ifdef HAL_EZIP_MODULE_ENABLED
      irq_attach(NX_IRQ(EZIP_IRQn), ezip_isr, epic->hezip);
      up_enable_irq(NX_IRQ(EZIP_IRQn));
#endif

      g_epic_opened = epic;
      EPIC_LOG_I("drv_gpu opened.");
    }
}

/****************************************************************************
 * Name: drv_gpu_close
 ****************************************************************************/

void drv_gpu_close(void)
{
  if (g_epic_opened)
    {
      EPIC_HandleTypeDef *epic = g_epic_opened;

      drv_gpu_check_done(GPU_BLEND_EXP_MS);
      EPIC_ASSERT(epic->State != HAL_EPIC_STATE_BUSY);

#ifdef HAL_EZIP_MODULE_ENABLED
      if (epic->hezip)
        {
          EPIC_ASSERT(epic->hezip->State != HAL_EZIP_STATE_BUSY);
          HAL_EZIP_DeInit(epic->hezip);
        }
#endif

      g_epic_opened = NULL;
      EPIC_LOG_I("drv_gpu closed.");
    }
}

/****************************************************************************
 * Name: drv_get_epic_handle / drv_get_ezip_handle
 ****************************************************************************/

EPIC_HandleTypeDef *drv_get_epic_handle(void)
{
  return &g_drv_epic.epic_handle;
}

#ifdef HAL_EZIP_MODULE_ENABLED
EZIP_HandleTypeDef *drv_get_ezip_handle(void)
{
  return &g_drv_epic.ezip_handle;
}
#endif

/****************************************************************************
 * Name: drv_gpu_take / drv_gpu_release / drv_gpu_check_done
 ****************************************************************************/

int drv_gpu_take(int32_t ms)
{
  int err = nxsem_tickwait(&g_epic_sema, MSEC2TICK((uint32_t)ms));

  if (0 != err)
    {
      print_gpu_error_info();
    }

  return err;
}

int drv_gpu_release(void)
{
  return nxsem_post(&g_epic_sema);
}

int drv_gpu_check_done(int32_t ms)
{
  int err;
  int sval = 1;

  nxsem_get_value(&g_epic_sema, &sval);

  if (sval == 0) /* Speed up return result if GPU is NOT working */
    {
      err = wait_gpu_done(ms);
      if (0 == err)
        {
          drv_gpu_release();
        }
    }
  else
    {
      err = 0;
    }

  return err;
}

bool drv_gpu_is_busy(void)
{
  if (nxsem_trywait(&g_epic_sema) < 0)
    {
      return true;
    }
  else
    {
      nxsem_post(&g_epic_sema);
      return false;
    }
}

bool drv_epic_is_busy(void)
{
  if (NULL == g_drv_epic_inited)
    {
      return false;
    }

  return drv_gpu_is_busy();
}

/****************************************************************************
 * Name: drv_epic_copy
 *
 * Description:
 *   区域拷贝。src/dst 支持不同 color format（EPIC 顺便转格式）。
 *
 ****************************************************************************/

int drv_epic_copy(const uint8_t *src, uint8_t *dst,
                  const EPIC_AreaTypeDef *src_area,
                  const EPIC_AreaTypeDef *dst_area,
                  const EPIC_AreaTypeDef *copy_area,
                  uint32_t src_cf, uint32_t dst_cf,
                  drv_epic_cplt_cbk cbk)
{
  HAL_StatusTypeDef ret;
  int err;
  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  EPIC_BlendingDataType *p_src_layer =
      (EPIC_BlendingDataType *)&g_drv_epic.input_layers[0];
  EPIC_BlendingDataType *p_dst_layer =
      (EPIC_BlendingDataType *)&g_drv_epic.output_layer;

  EPIC_ASSERT((NULL != src_area) && (NULL != dst_area) &&
              (NULL != copy_area));
  EPIC_ASSERT(HAL_EPIC_AreaIsIn(copy_area, src_area) &&
              HAL_EPIC_AreaIsIn(copy_area, dst_area));

  err = wait_gpu_done(GPU_BLEND_EXP_MS);
  if (0 != err)
    {
      return err;
    }

  HAL_EPIC_BlendDataInit(p_src_layer);
  p_src_layer->color_mode = src_cf;
  p_src_layer->total_width = HAL_EPIC_AreaWidth(src_area);
  p_src_layer->data = (uint8_t *)src;
  p_src_layer->width = HAL_EPIC_AreaWidth(src_area);
  p_src_layer->height = HAL_EPIC_AreaHeight(src_area);
  p_src_layer->x_offset = src_area->x0;
  p_src_layer->y_offset = src_area->y0;
  p_src_layer->data_size = get_layer_size(p_src_layer);

  HAL_EPIC_BlendDataInit(p_dst_layer);
  p_dst_layer->color_mode = dst_cf;
  p_dst_layer->total_width = HAL_EPIC_AreaWidth(dst_area);

  /* Clip dst layer to copy area */

  clip_layer_to_area(p_dst_layer, dst, dst_area->x0, dst_area->y0,
                     copy_area);

  gpu_lock(DRV_EPIC_IMG_COPY, p_src_layer, NULL, p_dst_layer);
  g_drv_epic.cbk = cbk;

  if ((HAL_EPIC_AreaWidth(copy_area) > EPIC_COORDINATES_MAX) ||
      (HAL_EPIC_AreaHeight(copy_area) > EPIC_COORDINATES_MAX))
    {
      memcpy(&g_drv_epic.split_rd.dst_area, dst_area,
             sizeof(EPIC_AreaTypeDef));
      g_drv_epic.split_rd.dst_data = dst;

      memcpy(&g_drv_epic.split_rd.render_area, copy_area,
             sizeof(EPIC_AreaTypeDef));
      g_drv_epic.split_rd.op = DRV_EPIC_IMG_COPY;

      ret = epic_split_render_next(1);
    }
  else
    {
      h_epic->XferCpltCallback = epic_cplt_callback;
      ret = HAL_EPIC_Copy_IT(h_epic, p_src_layer, p_dst_layer);
    }

  return (HAL_OK == ret) ? 0 : -EIO;
}

/****************************************************************************
 * Name: drv_epic_fill_ext
 *
 * Description:
 *   带可选输入层的填充/合成。input_layer_cnt=0 时是纯色填充。
 *
 ****************************************************************************/

int drv_epic_fill_ext(EPIC_LayerConfigTypeDef *input_layers,
                      uint8_t input_layer_cnt,
                      EPIC_LayerConfigTypeDef *output_canvas,
                      drv_epic_cplt_cbk cbk)
{
  HAL_StatusTypeDef ret;
  int err;
  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  EPIC_AreaTypeDef *fill_area = &g_drv_epic.split_rd.dst_area;

  EPIC_ASSERT(NULL != output_canvas);

  err = wait_gpu_done(GPU_BLEND_EXP_MS);
  if (0 != err)
    {
      return err;
    }

  gpu_lock(DRV_EPIC_COLOR_FILL,
           (3 == input_layer_cnt) ? input_layers + 2 : NULL,
           NULL, output_canvas);
  g_drv_epic.cbk = cbk;

  fill_area->x0 = output_canvas->x_offset;
  fill_area->y0 = output_canvas->y_offset;
  fill_area->x1 = output_canvas->x_offset + output_canvas->width - 1;
  fill_area->y1 = output_canvas->y_offset + output_canvas->height - 1;

  if ((HAL_EPIC_AreaWidth(fill_area) > EPIC_COORDINATES_MAX) ||
      (HAL_EPIC_AreaHeight(fill_area) > EPIC_COORDINATES_MAX))
    {
      if ((input_layer_cnt > 0) &&
          (&g_drv_epic.input_layers[0] != input_layers))
        {
          memcpy(&g_drv_epic.input_layers[0], input_layers,
                 input_layer_cnt * sizeof(g_drv_epic.input_layers[0]));
        }

      if (&g_drv_epic.output_layer != output_canvas)
        {
          memcpy(&g_drv_epic.output_layer, output_canvas,
                 sizeof(g_drv_epic.output_layer));
        }

      g_drv_epic.input_layer_cnt = input_layer_cnt;
      g_drv_epic.split_rd.dst_data = output_canvas->data;
      memcpy(&g_drv_epic.split_rd.render_area, fill_area,
             sizeof(EPIC_AreaTypeDef));
      g_drv_epic.split_rd.op = DRV_EPIC_COLOR_FILL;

      ret = epic_split_render_next(1);
    }
  else
    {
      h_epic->XferCpltCallback = epic_cplt_callback;
      ret = HAL_EPIC_BlendStartEx_IT(h_epic, input_layers,
                                     input_layer_cnt, output_canvas);
    }

  return (HAL_OK == ret) ? 0 : -EIO;
}

/****************************************************************************
 * Name: drv_epic_fill
 *
 * Description:
 *   纯色填充（可带 A2/A4/A8 mask 或整体透明度）。
 *
 ****************************************************************************/

int drv_epic_fill(uint32_t dst_cf, uint8_t *dst,
                  const EPIC_AreaTypeDef *dst_area,
                  const EPIC_AreaTypeDef *fill_area,
                  uint32_t argb8888,
                  uint32_t mask_cf, const uint8_t *mask_map,
                  const EPIC_AreaTypeDef *mask_area,
                  drv_epic_cplt_cbk cbk)
{
  int err;
  EPIC_LayerConfigTypeDef *p_output_canvas = &g_drv_epic.output_layer;
  uint8_t opa;

  EPIC_ASSERT(NULL != dst);
  EPIC_ASSERT(NULL != fill_area);
  EPIC_ASSERT(NULL != dst_area);

  err = drv_gpu_check_done(GPU_BLEND_EXP_MS);
  if (0 != err)
    {
      return err;
    }

  HAL_EPIC_LayerConfigInit(p_output_canvas);
  p_output_canvas->color_mode = dst_cf;
  p_output_canvas->total_width = HAL_EPIC_AreaWidth(dst_area);
  opa = (uint8_t)((argb8888 >> 24) & 0xff);
  p_output_canvas->color_r = (uint8_t)((argb8888 >> 16) & 0xff);
  p_output_canvas->color_g = (uint8_t)((argb8888 >> 8) & 0xff);
  p_output_canvas->color_b = (uint8_t)((argb8888 >> 0) & 0xff);
  p_output_canvas->color_en = true;

  /* Clip dst layer to filling area */

  clip_layer_to_area((EPIC_BlendingDataType *)p_output_canvas, dst,
                     dst_area->x0, dst_area->y0, fill_area);

  if (mask_map)
    {
#if defined(EPIC_SUPPORT_MONOCHROME_LAYER) && defined(EPIC_SUPPORT_MASK)
      EPIC_LayerConfigTypeDef *p_input_layers = &g_drv_epic.input_layers[0];

      HAL_EPIC_LayerConfigInit(&p_input_layers[2]);
      p_input_layers[2].data = (uint8_t *)mask_map;
      p_input_layers[2].x_offset = mask_area->x0;
      p_input_layers[2].y_offset = mask_area->y0;
      p_input_layers[2].width = HAL_EPIC_AreaWidth(mask_area);
      p_input_layers[2].total_width = p_input_layers[2].width;
      p_input_layers[2].height = HAL_EPIC_AreaHeight(mask_area);
      p_input_layers[2].color_mode = mask_cf;
      p_input_layers[2].ax_mode = ALPHA_BLEND_MASK;
      p_input_layers[2].data_size =
          get_layer_size((EPIC_BlendingDataType *)&p_input_layers[2]);

      p_input_layers[1] = *p_output_canvas;
      p_input_layers[1].data = (uint8_t *)(uintptr_t)mono_layer_addr;
      p_input_layers[1].color_mode = EPIC_INPUT_MONO;
      p_input_layers[1].alpha = opa;
      p_input_layers[1].ax_mode = ALPHA_BLEND_RGBCOLOR;

      p_input_layers[0] = *p_output_canvas;
      p_input_layers[0].color_en = false;

      p_output_canvas->color_en = false;

      err = drv_epic_fill_ext(p_input_layers, 3, p_output_canvas, cbk);
#else
      EPIC_ASSERT(0);
#endif
    }
  else if (opa != 255)
    {
      EPIC_LayerConfigTypeDef *p_input_layers = &g_drv_epic.input_layers[0];

      p_input_layers[1] = *p_output_canvas;
      p_input_layers[1].data = (uint8_t *)(uintptr_t)mono_layer_addr;
      p_input_layers[1].color_mode = EPIC_INPUT_MONO;
      p_input_layers[1].alpha = opa;
      p_input_layers[1].ax_mode = ALPHA_BLEND_RGBCOLOR;

      p_input_layers[0] = *p_output_canvas;
      p_input_layers[0].color_en = false;

      p_output_canvas->color_en = false;

      err = drv_epic_fill_ext(p_input_layers, 2, p_output_canvas, cbk);
    }
  else
    {
      err = drv_epic_fill_ext(NULL, 0, p_output_canvas, cbk);
    }

  return err;
}

/****************************************************************************
 * Name: drv_epic_fill_grad
 *
 * Description:
 *   四角颜色双线性渐变填充。
 *
 ****************************************************************************/

int drv_epic_fill_grad(EPIC_GradCfgTypeDef *param, drv_epic_cplt_cbk cbk)
{
  HAL_StatusTypeDef ret;
  int err;
  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  EPIC_AreaTypeDef *fill_area = &g_drv_epic.split_rd.dst_area;

  EPIC_ASSERT(NULL != param);
  EPIC_ASSERT(NULL != param->start);

  err = wait_gpu_done(GPU_BLEND_EXP_MS);
  if (0 != err)
    {
      return err;
    }

  gpu_lock(DRV_EPIC_FILL_GRAD, NULL, NULL, param);
  g_drv_epic.cbk = cbk;

  fill_area->x0 = 0;
  fill_area->y0 = 0;
  fill_area->x1 = param->width - 1;
  fill_area->y1 = param->height - 1;

  if ((HAL_EPIC_AreaWidth(fill_area) > EPIC_COORDINATES_MAX) ||
      (HAL_EPIC_AreaHeight(fill_area) > EPIC_COORDINATES_MAX))
    {
      EPIC_LayerConfigTypeDef *p_output_canvas = &g_drv_epic.output_layer;

      HAL_EPIC_LayerConfigInit(p_output_canvas);
      p_output_canvas->color_mode = param->color_mode;
      p_output_canvas->total_width = param->total_width;
      p_output_canvas->data = param->start;
      p_output_canvas->width = param->width;
      p_output_canvas->height = param->height;

      memcpy(g_drv_epic.grad_color, param->color,
             sizeof(g_drv_epic.grad_color));

      g_drv_epic.split_rd.dst_data = param->start;
      memcpy(&g_drv_epic.split_rd.render_area, fill_area,
             sizeof(EPIC_AreaTypeDef));
      g_drv_epic.split_rd.op = DRV_EPIC_FILL_GRAD;

      ret = epic_split_render_next(1);
    }
  else
    {
      h_epic->XferCpltCallback = epic_cplt_callback;
      ret = HAL_EPIC_FillGrad_IT(h_epic, param);
    }

  return (HAL_OK == ret) ? 0 : -EIO;
}

/****************************************************************************
 * Name: drv_epic_blend
 *
 * Description:
 *   多层合成（含旋转/缩放/mask，通用主入口）。
 *
 ****************************************************************************/

int drv_epic_blend(EPIC_LayerConfigTypeDef *input_layers,
                   uint8_t input_layer_cnt,
                   EPIC_LayerConfigTypeDef *output_canvas,
                   drv_epic_cplt_cbk cbk)
{
  HAL_StatusTypeDef ret;
  int err;
  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  EPIC_AreaTypeDef *p_blend_area = &g_drv_epic.split_rd.dst_area;

  EPIC_ASSERT((NULL != output_canvas) && (NULL != input_layers));

  err = wait_gpu_done(GPU_BLEND_EXP_MS);
  if (0 != err)
    {
      return err;
    }

  gpu_lock(DRV_EPIC_IMG_ROT, input_layers, &input_layer_cnt,
           output_canvas);
  g_drv_epic.cbk = cbk;

  p_blend_area->x0 = output_canvas->x_offset;
  p_blend_area->y0 = output_canvas->y_offset;
  p_blend_area->x1 = output_canvas->x_offset + output_canvas->width - 1;
  p_blend_area->y1 = output_canvas->y_offset + output_canvas->height - 1;

  if ((HAL_EPIC_AreaWidth(p_blend_area) > EPIC_COORDINATES_MAX) ||
      (HAL_EPIC_AreaHeight(p_blend_area) > EPIC_COORDINATES_MAX))
    {
      if ((input_layer_cnt > 0) &&
          (&g_drv_epic.input_layers[0] != input_layers))
        {
          memcpy(&g_drv_epic.input_layers[0], input_layers,
                 input_layer_cnt * sizeof(g_drv_epic.input_layers[0]));
        }

      if (&g_drv_epic.output_layer != output_canvas)
        {
          memcpy(&g_drv_epic.output_layer, output_canvas,
                 sizeof(g_drv_epic.output_layer));
        }

      g_drv_epic.input_layer_cnt = input_layer_cnt;
      g_drv_epic.split_rd.dst_data = output_canvas->data;
      memcpy(&g_drv_epic.split_rd.render_area, p_blend_area,
             sizeof(EPIC_AreaTypeDef));
      g_drv_epic.split_rd.op = DRV_EPIC_IMG_ROT;

      ret = epic_split_render_next(1);
    }
  else
    {
      h_epic->XferCpltCallback = epic_cplt_callback;
      ret = HAL_EPIC_BlendStartEx_IT(h_epic, input_layers,
                                     input_layer_cnt, output_canvas);
    }

  return (HAL_OK == ret) ? 0 : -EIO;
}

/****************************************************************************
 * Name: drv_epic_transform
 *
 * Description:
 *   同步版高级变换（HAL_EPIC_Adv），返回时已完成。
 *
 *   默认不编译：HAL_EPIC_Adv 只在厂商预编译库 libhal_epic_ex_gcc.a 里，
 *   而 LVGL draw unit 的旋转/缩放走 drv_epic_blend()。理由详见 drv_epic.h。
 *
 ****************************************************************************/

#ifdef DRV_EPIC_USE_HAL_ADV

int drv_epic_transform(EPIC_LayerConfigTypeDef *input_layers,
                       uint8_t input_layer_cnt,
                       EPIC_LayerConfigTypeDef *output_canvas,
                       drv_epic_cplt_cbk cbk)
{
  HAL_StatusTypeDef ret;
  int err;
  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  EPIC_AreaTypeDef *p_blend_area = &g_drv_epic.split_rd.dst_area;

  EPIC_ASSERT((NULL != output_canvas) && (NULL != input_layers));

  err = wait_gpu_done(GPU_BLEND_EXP_MS);
  if (0 != err)
    {
      return err;
    }

  gpu_lock(DRV_EPIC_TRANSFORM, input_layers, &input_layer_cnt,
           output_canvas);
  g_drv_epic.cbk = cbk;

  p_blend_area->x0 = output_canvas->x_offset;
  p_blend_area->y0 = output_canvas->y_offset;
  p_blend_area->x1 = output_canvas->x_offset + output_canvas->width - 1;
  p_blend_area->y1 = output_canvas->y_offset + output_canvas->height - 1;

  if ((HAL_EPIC_AreaWidth(p_blend_area) > EPIC_COORDINATES_MAX) ||
      (HAL_EPIC_AreaHeight(p_blend_area) > EPIC_COORDINATES_MAX))
    {
      if ((input_layer_cnt > 0) &&
          (&g_drv_epic.input_layers[0] != input_layers))
        {
          memcpy(&g_drv_epic.input_layers[0], input_layers,
                 input_layer_cnt * sizeof(g_drv_epic.input_layers[0]));
        }

      if (&g_drv_epic.output_layer != output_canvas)
        {
          memcpy(&g_drv_epic.output_layer, output_canvas,
                 sizeof(g_drv_epic.output_layer));
        }

      g_drv_epic.input_layer_cnt = input_layer_cnt;

      g_drv_epic.split_rd.dst_data = output_canvas->data;
      memcpy(&g_drv_epic.split_rd.render_area, p_blend_area,
             sizeof(EPIC_AreaTypeDef));
      g_drv_epic.split_rd.op = DRV_EPIC_TRANSFORM;

      ret = epic_split_render_next(1);

      while (DRV_EPIC_INVALID != g_drv_epic.split_rd.op)
        {
          HAL_StatusTypeDef hal_err = epic_split_render_next(0);
          if (HAL_OK != hal_err)
            {
              g_drv_epic.split_rd.op = DRV_EPIC_INVALID;
              EPIC_LOG_E("drv_epic_transform split_render err=%d",
                         hal_err);
              break;
            }
        }
    }
  else
    {
      ret = HAL_EPIC_Adv(h_epic, input_layers, input_layer_cnt,
                         output_canvas);
    }

  epic_cplt_callback(h_epic);

  return (HAL_OK == ret) ? 0 : -EIO;
}

#endif /* DRV_EPIC_USE_HAL_ADV */

/****************************************************************************
 * Name: drv_epic_cont_blend
 *
 * Description:
 *   连续 blend（字形批量渲染）：首次进入会话锁 GPU，之后每次
 *   Repeat 复用配置，避免每个字形一次完整的"配寄存器+等中断"。
 *   会话由 drv_epic_cont_blend_reset() 结束。
 *
 ****************************************************************************/

int drv_epic_cont_blend(EPIC_LayerConfigTypeDef *input_layers,
                        uint8_t input_layer_cnt,
                        EPIC_LayerConfigTypeDef *output_canvas)
{
  HAL_StatusTypeDef ret;
  int err;
  EPIC_HandleTypeDef *h_epic = &g_drv_epic.epic_handle;
  EPIC_LayerConfigTypeDef *fg_layer = &input_layers[1];
  EPIC_LayerConfigTypeDef *mask_layer =
      (3 == input_layer_cnt) ? &input_layers[2] : NULL;

  EPIC_ASSERT((NULL != output_canvas) && (NULL != input_layers));

  if (false == g_drv_epic.cont_mode)
    {
      err = wait_gpu_done(GPU_BLEND_EXP_MS);
      if (0 != err)
        {
          return err;
        }

      gpu_lock(DRV_EPIC_LETTER_BLEND, fg_layer, mask_layer, output_canvas);

      ret = HAL_EPIC_ContBlendStart(h_epic, fg_layer, mask_layer,
                                    output_canvas);
      g_drv_epic.cont_mode = true;
    }
  else
    {
      dcache_clean(fg_layer->data, fg_layer->data_size);
      if (mask_layer != NULL)
        {
          dcache_clean(mask_layer->data, mask_layer->data_size);
        }

      ret = HAL_EPIC_ContBlendRepeat(h_epic, fg_layer, mask_layer,
                                     output_canvas);
    }

  return (HAL_OK == ret) ? 0 : -EIO;
}

void drv_epic_cont_blend_reset(void)
{
  cont_blend_reset();
}
