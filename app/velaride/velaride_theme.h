/****************************************************************************
 * apps/examples/velaride/velaride_theme.h
 *
 * VelaRide 色板与字体。纯黑底让 AMOLED 像素不发光；强调色由用户在
 * 设置里四选一，只用于时间、主按钮和进度。警示色固定，不随主题变化。
 *
 * 数字字体 Outfit（SIL OFL 1.1，见 OFL-Outfit.txt），已把数字替换为
 * 等宽字形，计时跳动时不左右晃。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELARIDE_VELARIDE_THEME_H
#define __APPS_EXAMPLES_VELARIDE_VELARIDE_THEME_H

#include <lvgl/lvgl.h>

#include "velaride_device.h"

#define VR_BG      0x000000
#define VR_TILE    0x161618
#define VR_TILE2   0x222225
#define VR_LINE    0x1d1d20
#define VR_INK     0xf2f2f0
#define VR_INK2    0xa3a3a6
#define VR_INK3    0x6b6b70
#define VR_WARN    0xff9f0a
#define VR_DANGER  0xff453a
#define VR_OK      0x30d158

LV_FONT_DECLARE(velaride_font_cn_20);
LV_FONT_DECLARE(velaride_font_cn_28);
LV_FONT_DECLARE(velaride_num_136);
LV_FONT_DECLARE(velaride_num_92);
LV_FONT_DECLARE(velaride_num_60);
LV_FONT_DECLARE(velaride_num_44);
LV_FONT_DECLARE(velaride_num_30);
LV_FONT_DECLARE(velaride_num_24);

struct velaride_accent_s
{
  const char *name;
  uint32_t color;
  uint32_t ink;     /* 放在强调色底上的文字颜色 */
};

static const struct velaride_accent_s g_velaride_accents[VELARIDE_ACCENT_COUNT] =
{
  {"香槟金", 0xd8b98a, 0x1a1408},
  {"日光橙", 0xff8c42, 0x1f0d02},
  {"冰川蓝", 0x6ec6ff, 0x04131f},
  {"青柠绿", 0xb7ef53, 0x132002},
};

static inline const struct velaride_accent_s *velaride_accent(void)
{
  struct velaride_ui_prefs_s prefs;

  velaride_ui_prefs_get(&prefs);
  return &g_velaride_accents[prefs.accent < VELARIDE_ACCENT_COUNT ?
                             prefs.accent : 0];
}

static inline lv_color_t vr_accent(void)
{
  return lv_color_hex(velaride_accent()->color);
}

static inline lv_color_t vr_accent_ink(void)
{
  return lv_color_hex(velaride_accent()->ink);
}

#endif
