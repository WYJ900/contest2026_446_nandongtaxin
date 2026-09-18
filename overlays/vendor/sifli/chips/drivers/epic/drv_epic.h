/****************************************************************************
 * vendor/sifli/chips/drivers/epic/drv_epic.h
 *
 * SiFli EPIC 2.5D GPU 驱动（NuttX 移植版，单步模式）。
 *
 * 移植自 SiFli-SDK rtos/rtthread/bsp/sifli/drivers/drv_epic.h
 * （Apache-2.0，SiFli Technologies (Nanjing) Co., Ltd），
 * 保留官方旧版单步 API（!DRV_EPIC_NEW_API 分支），这是 LVGL v9
 * EPIC draw unit 唯一依赖的接口面。render list 模式未移植。
 *
 * 与 RT-Thread 原版的差异：
 *   - rt_err_t → int（0 成功 / 负 errno）
 *   - rt_sem → NuttX nxsem
 *   - EPIC/EZIP 中断挂 NuttX 向量表（irq_attach + up_enable_irq）
 *   - rt_flash_lock / rt_pm_request 无对应物，空实现
 *
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __DRV_EPIC_H__
#define __DRV_EPIC_H__

#include <stdint.h>
#include <stdbool.h>

#include "bf0_hal.h"

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DRV_EPIC_TIMEOUT_MS 500

/* MONO 层只需要一个 EPIC 能访问到的地址，内容不会被读出像素。
 * 官方同样直接用 SRAM 基址。
 */

#define mono_layer_addr HPSYS_RAM1_BASE

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*drv_epic_cplt_cbk)(EPIC_HandleTypeDef *);

typedef enum
{
  DRV_EPIC_COLOR_BLEND,   /* 0 */
  DRV_EPIC_COLOR_FILL,    /* 1 */
  DRV_EPIC_IMG_ROT,       /* 2 */
  DRV_EPIC_IMG_COPY,      /* 3 */
  DRV_EPIC_LETTER_BLEND,  /* 4 */
  DRV_EPIC_TRANSFORM,
  DRV_EPIC_FILL_GRAD,
  DRV_EPIC_INVALID = 0xFFFF,
} drv_epic_op_type_t;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int drv_epic_fill_ext(EPIC_LayerConfigTypeDef *input_layers,
                      uint8_t input_layer_cnt,
                      EPIC_LayerConfigTypeDef *output_canvas,
                      drv_epic_cplt_cbk cbk);

int drv_epic_fill(uint32_t dst_cf, uint8_t *dst,
                  const EPIC_AreaTypeDef *dst_area,
                  const EPIC_AreaTypeDef *fill_area,
                  uint32_t argb8888,
                  uint32_t mask_cf, const uint8_t *mask,
                  const EPIC_AreaTypeDef *mask_area,
                  drv_epic_cplt_cbk cbk);

int drv_epic_fill_grad(EPIC_GradCfgTypeDef *param,
                       drv_epic_cplt_cbk cbk);

int drv_epic_blend(EPIC_LayerConfigTypeDef *input_layers,
                   uint8_t input_layer_cnt,
                   EPIC_LayerConfigTypeDef *output_canvas,
                   drv_epic_cplt_cbk cbk);

/* drv_epic_transform 依赖 HAL_EPIC_Adv()，而后者只存在于厂商预编译库
 * chips/drivers/hal_epic_ex/libhal_epic_ex_gcc.a 里，源码不开放。
 *
 * LVGL EPIC draw unit 用不到它：图像的旋转/缩放是把 transform_cfg 填进
 * 层配置后走 drv_epic_blend() → HAL_EPIC_BlendStartEx_IT()，那条路在开源
 * HAL 里有完整实现。所以默认不编译，避免为一个没人调的接口把二进制库
 * 拖进链接。
 *
 * 如果日后要用 HAL_EPIC_Adv 的任意变换路径（hor_path/ver_path），
 * 定义 DRV_EPIC_USE_HAL_ADV 并在 CMake 里链上那个 .a。
 */

#ifdef DRV_EPIC_USE_HAL_ADV
int drv_epic_transform(EPIC_LayerConfigTypeDef *input_layers,
                       uint8_t input_layer_cnt,
                       EPIC_LayerConfigTypeDef *output_canvas,
                       drv_epic_cplt_cbk cbk);
#endif

int drv_epic_cont_blend(EPIC_LayerConfigTypeDef *input_layers,
                        uint8_t input_layer_cnt,
                        EPIC_LayerConfigTypeDef *output_canvas);

void drv_epic_cont_blend_reset(void);

int drv_epic_copy(const uint8_t *src, uint8_t *dst,
                  const EPIC_AreaTypeDef *src_area,
                  const EPIC_AreaTypeDef *dst_area,
                  const EPIC_AreaTypeDef *copy_area,
                  uint32_t src_cf, uint32_t dst_cf,
                  drv_epic_cplt_cbk cbk);

EPIC_HandleTypeDef *drv_get_epic_handle(void);

#ifdef HAL_EZIP_MODULE_ENABLED
EZIP_HandleTypeDef *drv_get_ezip_handle(void);
#endif

void drv_gpu_open(void);
void drv_gpu_close(void);

int drv_gpu_take(int32_t ms);
int drv_gpu_release(void);
int drv_gpu_check_done(int32_t ms);

#define drv_epic_take        drv_gpu_take
#define drv_epic_release     drv_gpu_release
#define drv_epic_wait_done() drv_gpu_check_done(DRV_EPIC_TIMEOUT_MS)

/* 返回指定内存段是否带 cache（0 = uncached，跳过 D-cache 维护）。
 * 本板 SRAM 不经 D-cache，PSRAM/存储走 cache；EPIC 本身访问不到 PSRAM。
 */

uint8_t drv_gpu_is_cached_ram(uint32_t start, uint32_t len);

bool drv_epic_is_busy(void);

/* EPIC 作为 DMA master 访问不到 PSRAM（0x60000000 段），真机实测
 * state 停在 BUSY、EOF_IRQ 恒 0。所有 buffer 判定统一走这个谓词。
 */

static inline bool drv_epic_addr_reachable(const void *addr)
{
  return addr != 0 && ((uint32_t)(uintptr_t)addr) < PSRAM_BASE;
}

#ifdef __cplusplus
}
#endif

#endif /* __DRV_EPIC_H__ */
