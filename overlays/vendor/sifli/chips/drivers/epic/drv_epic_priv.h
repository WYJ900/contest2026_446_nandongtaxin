/****************************************************************************
 * vendor/sifli/chips/drivers/epic/drv_epic_priv.h
 *
 * drv_epic 内部数据结构与工具宏（NuttX 移植版）。
 * 移植自 SiFli-SDK rtos/rtthread/bsp/sifli/drivers/drv_epic_private.h，
 * 只保留旧版单步 API 用到的部分。
 *
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef DRV_EPIC_PRIV_H
#define DRV_EPIC_PRIV_H

#include <assert.h>
#include <debug.h>
#include <syslog.h>

#include "drv_epic.h"

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 日志。默认只留错误级；调 EPIC 问题时把 DRV_EPIC_DEBUG_LOG 打开。 */

#define EPIC_LOG_E(fmt, ...) syslog(LOG_ERR, "drv.epic: " fmt "\n", \
                                    ##__VA_ARGS__)
#define EPIC_LOG_I(fmt, ...) syslog(LOG_INFO, "drv.epic: " fmt "\n", \
                                    ##__VA_ARGS__)
#ifdef DRV_EPIC_DEBUG_LOG
#  define EPIC_LOG_D(fmt, ...) syslog(LOG_DEBUG, "drv.epic: " fmt "\n", \
                                      ##__VA_ARGS__)
#else
#  define EPIC_LOG_D(fmt, ...)
#endif

#define EPIC_ASSERT(x) DEBUGASSERT(x)

#define EPIC_ALIGN(x, a) (((x) + (a) - 1) & ~((a) - 1))

#ifndef MIN
#  define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif
#ifndef MAX
#  define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

/* 等一次 blend 完成的超时。官方值 5000ms（留足 EZIP 大图余量）。 */

#define GPU_BLEND_EXP_MS 5000

#define AreaString "x0y0x1y1=[%d,%d,%d,%d]"
#define AreaParams(area) (area)->x0, (area)->y0, (area)->x1, (area)->y1

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef struct split_render_s
{
  drv_epic_op_type_t op;
  EPIC_AreaTypeDef dst_area;
  const uint8_t *dst_data;
  EPIC_AreaTypeDef render_area;      /* Part of 'dst_area' */
  EPIC_AreaTypeDef next_render_area; /* Part of 'render_area' */
} split_render_t;

typedef struct
{
  EPIC_HandleTypeDef epic_handle;

  EPIC_TypeDef RamEPIC;
#ifdef HAL_EZIP_MODULE_ENABLED
  EZIP_TypeDef RamEZIP;
  EZIP_HandleTypeDef ezip_handle;
#endif

  uint32_t gpu_timeout_cnt;

  EPIC_LayerConfigTypeDef input_layers[MAX_EPIC_LAYER];
  uint8_t input_layer_cnt;
  EPIC_LayerConfigTypeDef output_layer;
  EPIC_ColorDef grad_color[2][2];

  split_render_t split_rd;
  drv_epic_cplt_cbk cbk;

  bool cont_mode;

  uint32_t gpu_last_op;
  uint32_t gpu_fg_addr;
  uint32_t gpu_bg_addr;
  uint32_t gpu_mask_addr;
  uint32_t gpu_output_addr;
  uint32_t gpu_output_size;
} EPIC_DrvTypeDef;

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline uint8_t *get_map_ptr_by_xy(const uint8_t *map,
                                         uint8_t pixel_depth,
                                         uint32_t total_width,
                                         int32_t x, int32_t y)
{
  uint32_t offset_pixels = total_width * y + x;

  if (0 == (pixel_depth & 0x7))
    {
      return (uint8_t *)(map + (pixel_depth >> 3) * offset_pixels);
    }
  else
    {
      uint32_t offset_bits = offset_pixels * pixel_depth;
      EPIC_ASSERT(0 == (offset_bits & 0x7));

      return (uint8_t *)(map + (offset_bits >> 3));
    }
}

static inline uint32_t get_layer_size(EPIC_BlendingDataType *p_layer)
{
  uint32_t pixel_size = HAL_EPIC_GetColorDepth(p_layer->color_mode);

  return ((pixel_size * p_layer->total_width * p_layer->height) + 7) >> 3;
}

static inline void clip_layer_to_area(EPIC_BlendingDataType *p_layer,
                                      const uint8_t *data,
                                      int16_t x, int16_t y,
                                      const EPIC_AreaTypeDef *copy_area)
{
  uint32_t pixel_size = HAL_EPIC_GetColorDepth(p_layer->color_mode);

  EPIC_ASSERT(!EPIC_IS_EZIP_COLOR_MODE(p_layer->color_mode));
  EPIC_ASSERT(!EPIC_IS_YUV_COLOR_MODE(p_layer->color_mode));

  /* Clip dst layer to copy area */

  p_layer->width = HAL_EPIC_AreaWidth(copy_area);
  p_layer->height = HAL_EPIC_AreaHeight(copy_area);
  p_layer->data = get_map_ptr_by_xy(data, pixel_size, p_layer->total_width,
                                    copy_area->x0 - x, copy_area->y0 - y);
  p_layer->x_offset = copy_area->x0;
  p_layer->y_offset = copy_area->y0;

  p_layer->data_size =
      ((pixel_size * p_layer->total_width * p_layer->height) + 7) >> 3;
}

void print_gpu_error_info(void);
bool drv_gpu_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_EPIC_PRIV_H */
