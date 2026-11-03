/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_NIGHT_UI_H
#define __APPS_EXAMPLES_VELARIDE_NIGHT_UI_H
#include <lvgl/lvgl.h>
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
#include "velaride_night.h"
lv_color_t velaride_night_color(lv_color_t color);
bool velaride_night_ui_apply(bool exempt);
void velaride_night_ui_on_change(void (*callback)(void));
#else
static inline lv_color_t velaride_night_color(lv_color_t color) { return color; }
#endif
#endif
