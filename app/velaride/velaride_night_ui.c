/****************************************************************************
 * Inherited style filter; no widget recreation or framebuffer processing.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include <stdatomic.h>
#include "velaride_night_ui.h"

static atomic_bool g_render_active;
static void (*g_night_changed)(void);

lv_color_t velaride_night_color(lv_color_t color)
{
  uint8_t value;
  if (!atomic_load_explicit(&g_render_active, memory_order_relaxed)) return color;
  value = color.red > color.green ? color.red : color.green;
  if (color.blue > value) value = color.blue;
  /* Idempotent: max channel is unchanged, so nested filtering is harmless. */
  return lv_color_make(value, value * 3 / 10, value / 5);
}

static lv_color_t night_filter(const lv_color_filter_dsc_t *dsc,
                               lv_color_t color, lv_opa_t opacity)
{
  LV_UNUSED(dsc);
  LV_UNUSED(opacity);
  return velaride_night_color(color);
}

static const lv_color_filter_dsc_t g_night_filter = {.filter_cb = night_filter};

void velaride_night_ui_on_change(void (*callback)(void))
{
  g_night_changed = callback;
}

bool velaride_night_ui_apply(bool exempt)
{
  struct velaride_night_s night;
  bool active;
  velaride_night_exempt(exempt);
  velaride_night_get(&night);
  active = night.active && !exempt;
  if (atomic_exchange_explicit(&g_render_active, active, memory_order_relaxed) == active)
    return false;
  lv_obj_set_style_color_filter_dsc(lv_screen_active(), active ? &g_night_filter : NULL, 0);
  lv_obj_set_style_color_filter_opa(lv_screen_active(), active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_color_filter_dsc(lv_layer_top(), active ? &g_night_filter : NULL, 0);
  lv_obj_set_style_color_filter_opa(lv_layer_top(), active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_invalidate(lv_screen_active());
  lv_obj_invalidate(lv_layer_top());
  if (g_night_changed != NULL) g_night_changed();
  return true;
}
