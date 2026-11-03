/****************************************************************************
 * Shared vector icons for the watch menu and settings. No bitmap assets.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_ICONS_H
#define __APPS_EXAMPLES_VELARIDE_ICONS_H

#include <lvgl/lvgl.h>
#include "velaride_night_ui.h"

enum velaride_icon_e
{
  VR_ICON_RIDE, VR_ICON_RECORD, VR_ICON_SAFETY, VR_ICON_FACE,
  VR_ICON_BLUETOOTH, VR_ICON_SETTINGS, VR_ICON_HELP, VR_ICON_SUN,
  VR_ICON_MOON, VR_ICON_AUTO, VR_ICON_COMPASS, VR_ICON_DEVICE,
  VR_ICON_SPARK, VR_ICON_INFO, VR_ICON_HOME, VR_ICON_PULSE,
  VR_ICON_BULB, VR_ICON_TIMER, VR_ICON_LEVEL
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  , VR_ICON_SOUND
#endif
};

static inline void vr_icon_line(lv_layer_t *layer, int x, int y, int size,
                                lv_color_t color, int a, int b, int c, int d)
{
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.color = velaride_night_color(color);
  line.width = size >= 28 ? 3 : 2;
  line.round_start = line.round_end = 1;
  line.p1.x = x + a * size / 24;
  line.p1.y = y + b * size / 24;
  line.p2.x = x + c * size / 24;
  line.p2.y = y + d * size / 24;
  lv_draw_line(layer, &line);
}

static inline void vr_icon_circle(lv_layer_t *layer, int x, int y, int size,
                                  lv_color_t color, int cx, int cy, int radius,
                                  int start, int end)
{
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.color = velaride_night_color(color);
  arc.width = size >= 28 ? 3 : 2;
  arc.center.x = x + cx * size / 24;
  arc.center.y = y + cy * size / 24;
  arc.radius = radius * size / 24;
  arc.start_angle = start;
  arc.end_angle = end;
  arc.rounded = 1;
  lv_draw_arc(layer, &arc);
}

static inline void velaride_icon_draw(lv_layer_t *layer,
                                      enum velaride_icon_e icon,
                                      int x, int y, int size, lv_color_t color)
{
  const int8_t *segments = NULL;
  unsigned int count = 0;
  static const int8_t ride[] = {5,17,10,9,10,9,16,17,5,17,16,17,
    10,9,18,9,18,9,20,17,17,5,19,5,19,5,18,9,8,7,12,7};
  static const int8_t record[] = {5,3,19,3,19,3,19,21,19,21,5,21,
    5,21,5,3,9,8,15,8,9,12,15,12,9,16,13,16};
  static const int8_t shield[] = {12,2,21,6,21,6,19,16,19,16,12,22,
    12,22,5,16,5,16,3,6,3,6,12,2,8,11,11,14,11,14,16,8};
  static const int8_t face[] = {8,2,16,2,8,22,16,22,12,7,12,12,12,12,16,14};
  static const int8_t bluetooth[] = {12,2,12,22,12,2,19,8,19,8,5,18,
    5,6,19,16,19,16,12,22};
  static const int8_t rays[] = {12,1,12,4,12,20,12,23,1,12,4,12,
    20,12,23,12,4,4,6,6,18,18,20,20,4,20,6,18,18,6,20,4};
  static const int8_t gear[] = {12,2,12,5,12,19,12,22,21,7,18,9,
    3,7,6,9,21,17,18,15,3,17,6,15};
  static const int8_t letter_a[] = {8,17,12,7,12,7,16,17,10,13,14,13};
  static const int8_t help[] = {12,13,15,10,15,10,15,7,15,7,12,5,
    12,5,9,7,12,13,12,15,12,18,12,18};
  static const int8_t compass[] = {16,6,14,14,14,14,6,18,6,18,10,10,
    10,10,16,6,10,10,14,14};
  static const int8_t device[] = {7,6,17,6,17,6,17,18,17,18,7,18,
    7,18,7,6,3,8,7,8,3,12,7,12,3,16,7,16,17,8,21,8,
    17,12,21,12,17,16,21,16,10,2,10,6,14,2,14,6,10,18,10,22,
    14,18,14,22};
  static const int8_t spark[] = {12,3,14,10,14,10,21,12,21,12,14,14,
    14,14,12,21,12,21,10,14,10,14,3,12,3,12,10,10,10,10,12,3,
    19,17,19,22,17,19,21,19};
  static const int8_t info[] = {12,11,12,17,12,7,12,7};
  static const int8_t home[] = {3,11,12,3,12,3,21,11,6,9,6,21,
    18,9,18,21,6,21,18,21,10,21,10,15,10,15,14,15,14,15,14,21};
  static const int8_t bulb[] = {9,17,15,17,10,20,14,20,8,14,9,17,
    16,14,15,17};
  static const int8_t timer[] = {12,13,12,9,12,13,15,15,10,2,14,2,
    12,2,12,5};
  static const int8_t level[] = {3,9,21,9,3,15,21,15,3,9,3,15,
    21,9,21,15,8,9,8,15,16,9,16,15};
  static const int8_t pulse[] = {2,12,7,12,7,12,9,6,9,6,14,19,
    14,19,16,12,16,12,22,12};

  switch (icon)
    {
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      case VR_ICON_SOUND:
        vr_icon_line(layer,x,y,size,color,4,9,8,9);
        vr_icon_line(layer,x,y,size,color,8,9,13,5);
        vr_icon_line(layer,x,y,size,color,13,5,13,19);
        vr_icon_line(layer,x,y,size,color,13,19,8,15);
        vr_icon_line(layer,x,y,size,color,8,15,4,15);
        vr_icon_line(layer,x,y,size,color,4,15,4,9);
        vr_icon_circle(layer,x,y,size,color,12,12,6,310,50);
        vr_icon_circle(layer,x,y,size,color,12,12,10,310,50);
        break;
#endif
      case VR_ICON_RIDE:
        segments = ride; count = sizeof(ride);
        vr_icon_circle(layer,x,y,size,color,5,17,4,0,360);
        vr_icon_circle(layer,x,y,size,color,20,17,3,0,360);
        break;
      case VR_ICON_RECORD: segments = record; count = sizeof(record); break;
      case VR_ICON_SAFETY: segments = shield; count = sizeof(shield); break;
      case VR_ICON_FACE:
        segments = face; count = sizeof(face);
        vr_icon_circle(layer,x,y,size,color,12,12,8,0,360);
        break;
      case VR_ICON_BLUETOOTH:
        segments = bluetooth; count = sizeof(bluetooth); break;
      case VR_ICON_SETTINGS:
        segments = gear; count = sizeof(gear);
        vr_icon_circle(layer,x,y,size,color,12,12,7,0,360);
        vr_icon_circle(layer,x,y,size,color,12,12,3,0,360);
        break;
      case VR_ICON_HELP:
        segments = help; count = sizeof(help);
        vr_icon_circle(layer,x,y,size,color,12,12,11,0,360);
        break;
      case VR_ICON_SUN:
        segments = rays; count = sizeof(rays);
        vr_icon_circle(layer,x,y,size,color,12,12,5,0,360);
        break;
      case VR_ICON_AUTO:
        /* The rays plus an "A" so it never reads as plain brightness. */
        segments = rays; count = sizeof(rays);
        for (unsigned int i = 0; i < sizeof(letter_a); i += 4)
          vr_icon_line(layer,x,y,size,color,letter_a[i],letter_a[i+1],
                       letter_a[i+2],letter_a[i+3]);
        break;
      case VR_ICON_MOON:
        vr_icon_circle(layer,x,y,size,color,12,12,10,50,270);
        vr_icon_circle(layer,x,y,size,color,17,7,10,70,210);
        break;
      case VR_ICON_COMPASS:
        segments = compass; count = sizeof(compass);
        vr_icon_circle(layer,x,y,size,color,12,12,11,0,360);
        break;
      case VR_ICON_DEVICE: segments = device; count = sizeof(device); break;
      case VR_ICON_SPARK: segments = spark; count = sizeof(spark); break;
      case VR_ICON_HOME: segments = home; count = sizeof(home); break;
      case VR_ICON_BULB:
        segments = bulb; count = sizeof(bulb);
        vr_icon_circle(layer,x,y,size,color,12,10,6,135,45);
        break;
      case VR_ICON_TIMER:
        segments = timer; count = sizeof(timer);
        vr_icon_circle(layer,x,y,size,color,12,13,8,0,360);
        break;
      case VR_ICON_LEVEL:
        segments = level; count = sizeof(level);
        vr_icon_circle(layer,x,y,size,color,12,12,2,0,360);
        break;
      case VR_ICON_PULSE: segments = pulse; count = sizeof(pulse); break;
      case VR_ICON_INFO:
        segments = info; count = sizeof(info);
        vr_icon_circle(layer,x,y,size,color,12,12,11,0,360);
        break;
    }

  for (unsigned int i = 0; i < count; i += 4)
    vr_icon_line(layer,x,y,size,color,segments[i],segments[i+1],
                 segments[i+2],segments[i+3]);
}

static inline void velaride_icon_event(lv_event_t *event)
{
  lv_obj_t *obj = lv_event_get_target(event);
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  velaride_icon_draw(lv_event_get_layer(event),
    (enum velaride_icon_e)(uintptr_t)lv_event_get_user_data(event),
    area.x1, area.y1, lv_obj_get_width(obj),
    lv_obj_get_style_text_color(obj, 0));
}

static inline lv_obj_t *velaride_icon_create(lv_obj_t *parent,
  enum velaride_icon_e icon, int x, int y, int size, uint32_t color)
{
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, size, size);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_add_event_cb(obj, velaride_icon_event, LV_EVENT_DRAW_MAIN,
                       (void *)(uintptr_t)icon);
  return obj;
}
#endif
