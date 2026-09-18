/****************************************************************************
 * apps/examples/velaride/velaride_shell.c
 *
 * VelaRide 二维手表导航壳层。
 *
 * 交互结构：表盘层左右切换表盘；纵向进入骑行、提醒与 AI 复盘。导航器只
 * 维护当前、目标与备用三个全屏 slot。拖动期间页面进入低细节预览，落页后
 * 才恢复完整内容和动态 timer，避免在每个触摸样本上重画复杂页面。
 *
 * 表盘生命周期和 simple/dial 视觉方向参考 SiFli-SDK watch_v9：
 * example/multimedia/lvgl/watch_v9/src/gui_apps/clock/
 * Copyright 2026 SiFli Technologies (Nanjing) Co., Ltd, Apache-2.0.
 * 本文件为面向 NuttX/LVGL 9.1 的独立实现，不依赖 RT-Thread gui_app_fwk。
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/input/buttons.h>
#include <nuttx/mtd/configdata.h>
#include <nuttx/mtd/mtd.h>
#include <lvgl/lvgl.h>

#include "velaride_sport_ui.h"
#include "velaride_ride_session.h"
#include "velaride_timekeeper.h"

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#  include <uv.h>
#endif

#define SCR_W 390
#define SCR_H 450
#define AXIS_LOCK_PX 12
#define COMMIT_X_PX 86
#define COMMIT_Y_PX 99
#define SETTLE_MS 140
#define FACE_COUNT 5
#define HONEY_ICON_COUNT 19
#define HONEY_CELL_SIZE 92
#define HONEY_CONTENT_SIZE 720
#define HONEY_ICON_DIAMETER 112
#define BUTTON_POLL_MS 20
#define BUTTON_DOUBLE_MS 360
#define PREVIEW_DELAY_MS 180
#define PREVIEW_NEXT_DELAY_MS 16
#define PREVIEW_COUNT FACE_COUNT
#define PREVIEW_DATA_SIZE (SCR_W * SCR_H * 2)
#define REVIEW_TRANSFER_MAX 260

#include "velaride_dial_rgb565.inc"
#include "velaride_sport_face_rgb565.inc"

static const lv_image_dsc_t g_dial_bg =
{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565,
  .header.flags = 0,
  .header.w = 390,
  .header.h = 390,
  .header.stride = 390 * 2,
  .data_size = sizeof(velaride_dial_rgb565),
  .data = velaride_dial_rgb565,
};

static const lv_image_dsc_t g_sport_face_bg =
{
  .header.magic = LV_IMAGE_HEADER_MAGIC,
  .header.cf = LV_COLOR_FORMAT_RGB565,
  .header.flags = 0,
  .header.w = SCR_W,
  .header.h = SCR_H,
  .header.stride = SCR_W * 2,
  .data_size = sizeof(velaride_sport_face_rgb565),
  .data = velaride_sport_face_rgb565,
};

static char *g_review_transfer;
static size_t g_review_transfer_len;

enum scene_id_e
{
  SCENE_FACE_DIGITAL = 0,
  SCENE_FACE_SIMPLE,
  SCENE_FACE_DIAL,
  SCENE_FACE_NUMERAL,
  SCENE_FACE_SPORT,
  SCENE_RIDE,
  SCENE_ALERT,
  SCENE_REVIEW,
  SCENE_COUNT
};

enum axis_e
{
  AXIS_NONE = 0,
  AXIS_H,
  AXIS_V
};

struct scene_slot_s
{
  lv_obj_t *root;
  lv_obj_t *detail;
  lv_obj_t *primary;
  lv_obj_t *secondary;
  lv_obj_t *hand_h;
  lv_obj_t *hand_m;
  lv_obj_t *hand_s;
  lv_point_precise_t hand_h_pts[2];
  lv_point_precise_t hand_m_pts[2];
  lv_point_precise_t hand_s_pts[2];
  enum scene_id_e scene;
};

struct nav_shell_s
{
  struct scene_slot_s slots[3];
  struct scene_slot_s preview_slot;
  struct scene_slot_s *current;
  struct scene_slot_s *target;
  struct scene_slot_s *spare;
  lv_obj_t *reveal;
  enum axis_e axis;
  int32_t drag_x;
  int32_t drag_y;
  int32_t target_origin;
  int32_t reveal_value;
  enum scene_id_e reveal_scene;
  int8_t target_direction;
  uint8_t face_index;
  int8_t function_index;
  bool dragging;
  bool transition_active;
  bool commit_transition;
  lv_anim_t settle_anim;
  lv_timer_t *data_timer;
  lv_timer_t *preview_timer;
  lv_draw_buf_t preview_buf[PREVIEW_COUNT];
  void *preview_mem[PREVIEW_COUNT];
  enum scene_id_e preview_scene[PREVIEW_COUNT];
  bool preview_ready[PREVIEW_COUNT];
  uint8_t preview_count;
  uint8_t preview_next;
  uint64_t perf_first_us;
  uint64_t perf_last_us;
  uint32_t perf_frames;
  uint32_t perf_max_gap_us;
};

struct honey_icon_s
{
  const char *symbol;
  uint32_t color;
  int16_t center_x;
  int16_t center_y;
  uint8_t diameter;
  lv_point_t text_size_20;
  lv_point_t text_size_24;
};

struct honey_menu_s
{
  lv_obj_t *root;
  struct honey_icon_s icons[HONEY_ICON_COUNT];
  int selected;
  int32_t offset_x;
  int32_t offset_y;
  int32_t velocity_x;
  int32_t velocity_y;
  int32_t drag_sum_x;
  int32_t drag_sum_y;
  lv_point_t last_point;
  int button_fd;
  btn_buttonset_t button_last;
  uint64_t button_first_us;
  bool button_single_pending;
  bool active;
  bool dragging;
  bool moved;
  bool lod;
  bool perf_active;
  uint64_t perf_first_us;
  uint64_t perf_last_us;
  uint32_t perf_frames;
  uint32_t perf_max_gap_us;
};

static struct nav_shell_s g_nav;
static struct honey_menu_s g_menu;

static void preview_schedule(void);

static uint64_t monotonic_us(void)
{
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      return 0;
    }

  return (uint64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            lv_color_t color, const lv_font_t *font,
                            int x, int y, int width)
{
  lv_obj_t *label = lv_label_create(parent);

  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, color, 0);
  if (font != NULL)
    {
      lv_obj_set_style_text_font(label, font, 0);
    }

  if (width > 0)
    {
      lv_obj_set_width(label, width);
      lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }

  lv_obj_set_pos(label, x, y);
  lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return label;
}

static lv_color_t scene_color(enum scene_id_e scene)
{
  static const uint32_t colors[SCENE_COUNT] =
  {
    0x071827, 0x111827, 0x111827, 0x000000, 0x03080d,
    0x08271c, 0x2b190b, 0x21102f
  };

  return lv_color_hex(colors[scene]);
}

static const char *scene_preview_title(enum scene_id_e scene)
{
  static const char *titles[SCENE_COUNT] =
  {
    "VELARIDE", "SIMPLE", "DIAL", "NUMERAL", "SPORT",
    "RIDING", "SAFETY", "AI REVIEW"
  };

  return titles[scene];
}

static void slot_reset(struct scene_slot_s *slot, enum scene_id_e scene,
                       bool preview)
{
  lv_obj_t *title;

  lv_obj_clean(slot->root);
  lv_obj_set_pos(slot->root, 0, 0);
  lv_obj_set_style_bg_color(slot->root, scene_color(scene), 0);
  lv_obj_set_style_bg_opa(slot->root, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(slot->root, 0, 0);
  lv_obj_set_style_radius(slot->root, 0, 0);
  lv_obj_set_style_pad_all(slot->root, 0, 0);
  lv_obj_remove_flag(slot->root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(slot->root, LV_OBJ_FLAG_CLICKABLE |
                              LV_OBJ_FLAG_EVENT_BUBBLE |
                              LV_OBJ_FLAG_PRESS_LOCK);

  slot->scene = scene;
  slot->detail = NULL;
  slot->primary = NULL;
  slot->secondary = NULL;
  slot->hand_h = NULL;
  slot->hand_m = NULL;
  slot->hand_s = NULL;

  if (preview)
    {
      title = make_label(slot->root, scene_preview_title(scene),
                         lv_color_hex(0x94a3b8),
                         &lv_font_montserrat_24, 0, 198, SCR_W);
      LV_UNUSED(title);
      return;
    }

  slot->detail = lv_obj_create(slot->root);
  lv_obj_remove_style_all(slot->detail);
  lv_obj_set_size(slot->detail, SCR_W, SCR_H);
  lv_obj_set_pos(slot->detail, 0, 0);
  lv_obj_remove_flag(slot->detail,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void create_digital_face(struct scene_slot_s *slot)
{
  make_label(slot->detail, "VELARIDE", lv_color_hex(0x38bdf8),
             &lv_font_montserrat_20, 0, 75, SCR_W);
  slot->primary = make_label(slot->detail, "--:--", lv_color_white(),
                             &lv_font_montserrat_48, 0, 128, SCR_W);
  make_label(slot->detail, "RIDE SAFETY", lv_color_hex(0x64748b),
             &lv_font_montserrat_16, 0, 205, SCR_W);
  make_label(slot->detail, "12.5 km     45 min     180 kcal",
             lv_color_hex(0xcbd5e1), &lv_font_montserrat_16,
             0, 265, SCR_W);
  make_label(slot->detail, "swipe left / up", lv_color_hex(0x475569),
             &lv_font_montserrat_16, 0, 365, SCR_W);
}

static void set_hand(lv_obj_t *line, lv_point_precise_t pts[2],
                     int angle_deg, int length)
{
  int32_t sx = lv_trigo_sin(angle_deg);
  int32_t cy = lv_trigo_cos(angle_deg);

  pts[0].x = SCR_W / 2;
  pts[0].y = SCR_H / 2;
  pts[1].x = SCR_W / 2 + sx * length / 32767;
  pts[1].y = SCR_H / 2 - cy * length / 32767;
  lv_line_set_points(line, pts, 2);
}

static lv_obj_t *make_hand(struct scene_slot_s *slot,
                           lv_point_precise_t pts[2],
                           lv_color_t color, int width)
{
  lv_obj_t *line = lv_line_create(slot->detail);

  lv_obj_set_size(line, SCR_W, SCR_H);
  lv_obj_set_pos(line, 0, 0);
  lv_obj_set_style_line_color(line, color, 0);
  lv_obj_set_style_line_width(line, width, 0);
  lv_obj_set_style_line_rounded(line, true, 0);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_line_set_points(line, pts, 2);
  return line;
}

static void create_simple_face(struct scene_slot_s *slot)
{
  int i;
  lv_obj_t *ring;

  make_label(slot->detail, "SIMPLE", lv_color_hex(0x94a3b8),
             &lv_font_montserrat_16, 0, 45, SCR_W);

  ring = lv_obj_create(slot->detail);
  lv_obj_remove_style_all(ring);
  lv_obj_set_size(ring, 304, 304);
  lv_obj_center(ring);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ring, 2, 0);
  lv_obj_set_style_border_color(ring, lv_color_white(), 0);
  lv_obj_set_style_border_opa(ring, LV_OPA_80, 0);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  for (i = 0; i < 12; i++)
    {
      int angle = i * 30;
      int x = SCR_W / 2 + lv_trigo_sin(angle) * 142 / 32767;
      int y = SCR_H / 2 - lv_trigo_cos(angle) * 142 / 32767;
      lv_obj_t *tick = lv_obj_create(slot->detail);

      lv_obj_remove_style_all(tick);
      lv_obj_set_size(tick, i % 3 == 0 ? 8 : 5, i % 3 == 0 ? 8 : 5);
      lv_obj_set_style_radius(tick, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(tick,
                                i % 3 == 0 ? lv_color_white() :
                                             lv_color_hex(0x64748b), 0);
      lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
      lv_obj_set_pos(tick, x - 4, y - 4);
      lv_obj_remove_flag(tick, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

  slot->hand_h = make_hand(slot, slot->hand_h_pts,
                           lv_color_hex(0xe2e8f0), 8);
  slot->hand_m = make_hand(slot, slot->hand_m_pts,
                           lv_color_hex(0x38bdf8), 5);
  slot->hand_s = make_hand(slot, slot->hand_s_pts,
                           lv_color_hex(0xf43f5e), 2);

  make_label(slot->detail, "SiFli simple style", lv_color_hex(0x64748b),
             &lv_font_montserrat_16, 0, 375, SCR_W);
}

static void create_dial_face(struct scene_slot_s *slot)
{
  lv_obj_t *background = lv_image_create(slot->detail);

  lv_image_set_src(background, &g_dial_bg);
  lv_obj_align(background, LV_ALIGN_CENTER, 0, 0);
  lv_obj_remove_flag(background,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  slot->hand_h = make_hand(slot, slot->hand_h_pts,
                           lv_color_hex(0x111827), 8);
  slot->hand_m = make_hand(slot, slot->hand_m_pts,
                           lv_color_hex(0x0369a1), 5);
  slot->hand_s = make_hand(slot, slot->hand_s_pts,
                           lv_color_hex(0xf43f5e), 2);

  make_label(slot->detail, "SiFli dial / VelaRide", lv_color_hex(0xfef3c7),
             &lv_font_montserrat_16, 0, 405, SCR_W);
}

static void create_numeral_face(struct scene_slot_s *slot)
{
  static const char *numbers[12] =
  {
    "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
  };
  int i;

  for (i = 0; i < 60; i++)
    {
      int angle = i * 6;
      int radius = i % 5 == 0 ? 174 : 178;
      int x = SCR_W / 2 + lv_trigo_sin(angle) * radius / 32767;
      int y = SCR_H / 2 - lv_trigo_cos(angle) * radius / 32767;
      lv_obj_t *tick = lv_obj_create(slot->detail);

      lv_obj_remove_style_all(tick);
      lv_obj_set_size(tick, i % 5 == 0 ? 4 : 2, i % 5 == 0 ? 12 : 7);
      lv_obj_set_style_bg_color(tick,
                                i % 5 == 0 ? lv_color_hex(0x9ca3af) :
                                             lv_color_hex(0x4b5563), 0);
      lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
      lv_obj_set_style_transform_rotation(tick, angle * 10, 0);
      lv_obj_set_pos(tick, x - 2, y - 6);
      lv_obj_remove_flag(tick,
                         LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

  for (i = 0; i < 12; i++)
    {
      int angle = i * 30;
      int x = SCR_W / 2 + lv_trigo_sin(angle) * 132 / 32767;
      int y = SCR_H / 2 - lv_trigo_cos(angle) * 132 / 32767;
      lv_obj_t *number = make_label(slot->detail, numbers[i], lv_color_white(),
                                    &lv_font_montserrat_48,
                                    x - 40, y - 30, 80);
      lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);
    }

  slot->hand_h = make_hand(slot, slot->hand_h_pts, lv_color_white(), 12);
  slot->hand_m = make_hand(slot, slot->hand_m_pts, lv_color_white(), 9);
  slot->hand_s = make_hand(slot, slot->hand_s_pts,
                           lv_color_hex(0xff9f0a), 3);
}

static void create_sport_face(struct scene_slot_s *slot)
{
  lv_obj_t *background = lv_image_create(slot->detail);

  lv_image_set_src(background, &g_sport_face_bg);
  lv_obj_set_pos(background, 0, 0);
  lv_obj_remove_flag(background,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

  slot->primary = make_label(slot->detail, "--:--", lv_color_hex(0xffd60a),
                             &lv_font_montserrat_48, 25, 91, 190);
  lv_obj_set_style_text_align(slot->primary, LV_TEXT_ALIGN_LEFT, 0);
}

static void create_feature_page(struct scene_slot_s *slot)
{
  if (slot->scene == SCENE_RIDE)
    {
      make_label(slot->detail, "RIDING", lv_color_hex(0x4ade80),
                 &lv_font_montserrat_20, 0, 65, SCR_W);
      slot->primary = make_label(slot->detail, "00:00:00", lv_color_white(),
                                 &lv_font_montserrat_48, 0, 125, SCR_W);
      make_label(slot->detail, "18.6 km/h        4.2 km",
                 lv_color_hex(0xcbd5e1), &lv_font_montserrat_20,
                 0, 225, SCR_W);
      make_label(slot->detail, "swipe down to watchface",
                 lv_color_hex(0x64748b), &lv_font_montserrat_16,
                 0, 365, SCR_W);
    }
  else if (slot->scene == SCENE_ALERT)
    {
      make_label(slot->detail, "!", lv_color_hex(0xfbbf24),
                 &lv_font_montserrat_48, 0, 70, SCR_W);
      make_label(slot->detail, "SAFETY", lv_color_hex(0xfbbf24),
                 &lv_font_montserrat_20, 0, 155, SCR_W);
      make_label(slot->detail, "Take a 5 min break",
                 lv_color_hex(0xfef3c7), &lv_font_montserrat_24,
                 0, 225, SCR_W);
      make_label(slot->detail, "riding 40 min",
                 lv_color_hex(0x94a3b8), &lv_font_montserrat_16,
                 0, 300, SCR_W);
    }
  else
    {
      make_label(slot->detail, "AI REVIEW", lv_color_hex(0xc084fc),
                 &lv_font_montserrat_20, 0, 70, SCR_W);
      make_label(slot->detail,
                 "Steady pace this session.\nRest every 40 minutes.",
                 lv_color_hex(0xe2e8f0), &lv_font_montserrat_20,
                 35, 155, SCR_W - 70);
      make_label(slot->detail, "swipe down to return",
                 lv_color_hex(0x64748b), &lv_font_montserrat_16,
                 0, 350, SCR_W);
    }
}

static void build_scene(struct scene_slot_s *slot, enum scene_id_e scene,
                        bool preview)
{
  slot_reset(slot, scene, preview);
  if (preview)
    {
      return;
    }

  if (scene == SCENE_FACE_DIGITAL)
    {
      create_digital_face(slot);
    }
  else if (scene == SCENE_FACE_SIMPLE)
    {
      create_simple_face(slot);
    }
  else if (scene == SCENE_FACE_DIAL)
    {
      create_dial_face(slot);
    }
  else if (scene == SCENE_FACE_NUMERAL)
    {
      create_numeral_face(slot);
    }
  else if (scene == SCENE_FACE_SPORT)
    {
      create_sport_face(slot);
    }
  else
    {
      create_feature_page(slot);
    }
}

static enum scene_id_e current_scene(void)
{
  if (g_nav.function_index < 0)
    {
      return (enum scene_id_e)(SCENE_FACE_DIGITAL + g_nav.face_index);
    }

  return (enum scene_id_e)(SCENE_RIDE + g_nav.function_index);
}

static bool choose_target(enum axis_e axis, int direction,
                          enum scene_id_e *scene)
{
  if (axis == AXIS_H)
    {
      if (g_nav.function_index >= 0)
        {
          return false;
        }

      int next = g_nav.face_index + (direction < 0 ? 1 : -1);
      if (next < 0)
        {
          next = FACE_COUNT - 1;
        }
      else if (next >= FACE_COUNT)
        {
          next = 0;
        }

      *scene = (enum scene_id_e)(SCENE_FACE_DIGITAL + next);
      return true;
    }

  if (direction < 0)
    {
      if (g_nav.function_index < 0)
        {
          *scene = SCENE_RIDE;
          return true;
        }

      if (g_nav.function_index < 2)
        {
          *scene = (enum scene_id_e)(SCENE_RIDE +
                                     g_nav.function_index + 1);
          return true;
        }
    }
  else
    {
      if (g_nav.function_index == 0)
        {
          *scene = (enum scene_id_e)(SCENE_FACE_DIGITAL + g_nav.face_index);
          return true;
        }

      if (g_nav.function_index > 0)
        {
          *scene = (enum scene_id_e)(SCENE_RIDE +
                                     g_nav.function_index - 1);
          return true;
        }
    }

  return false;
}

static void preload_neighbor_scenes(void)
{
  enum scene_id_e neighbors[2];
  enum scene_id_e scene;
  struct scene_slot_s *slots[2] = {g_nav.target, g_nav.spare};
  int count = 0;
  int i;

  if (g_nav.function_index < 0 &&
      choose_target(AXIS_H, -1, &scene))
    {
      neighbors[count++] = scene;
    }

  if (g_nav.function_index < 0 && count < 2 &&
      choose_target(AXIS_H, 1, &scene) && neighbors[0] != scene)
    {
      neighbors[count++] = scene;
    }

  if (count < 2 && choose_target(AXIS_V, -1, &scene) &&
      (count == 0 || neighbors[0] != scene))
    {
      neighbors[count++] = scene;
    }

  if (count < 2 && choose_target(AXIS_V, 1, &scene) &&
      (count == 0 || neighbors[0] != scene))
    {
      neighbors[count++] = scene;
    }

  for (i = 0; i < 2; i++)
    {
      if (i < count)
        {
          build_scene(slots[i], neighbors[i], false);
        }

      lv_obj_add_flag(slots[i]->root, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(slots[i]->root, 0, 0);
    }

  preview_schedule();
}

static void preview_timer_cb(lv_timer_t *timer)
{
  uint8_t index;
  uint64_t start;
  uint64_t end;
  lv_result_t result;

  lv_timer_pause(timer);
  if (g_nav.dragging || g_nav.transition_active || g_menu.active ||
      g_nav.preview_next >= g_nav.preview_count)
    {
      return;
    }

  index = g_nav.preview_next;
  build_scene(&g_nav.preview_slot,
              (enum scene_id_e)(SCENE_FACE_DIGITAL + index), false);

  if (g_nav.preview_mem[index] == NULL)
    {
      g_nav.preview_mem[index] = lv_malloc(PREVIEW_DATA_SIZE + LV_DRAW_BUF_ALIGN);
      if (g_nav.preview_mem[index] == NULL ||
          lv_draw_buf_init(&g_nav.preview_buf[index], SCR_W, SCR_H,
                           LV_COLOR_FORMAT_RGB565, SCR_W * 2,
                           g_nav.preview_mem[index],
                           PREVIEW_DATA_SIZE + LV_DRAW_BUF_ALIGN) !=
            LV_RESULT_OK)
        {
          printf("VELARIDE_PREVIEW: allocate/init failed\n");
          g_nav.preview_ready[index] = false;
          return;
        }

      printf("VELARIDE_PREVIEW: buffer[%u]=%p/%u\n", index,
             g_nav.preview_buf[index].data, PREVIEW_DATA_SIZE);
    }

  start = monotonic_us();
  result = lv_snapshot_take_to_draw_buf(g_nav.preview_slot.root,
                                        LV_COLOR_FORMAT_RGB565,
                                        &g_nav.preview_buf[index]);
  end = monotonic_us();
  if (result == LV_RESULT_OK)
    {
      g_nav.preview_scene[index] = g_nav.preview_slot.scene;
      g_nav.preview_ready[index] = true;
      printf("VELARIDE_PREVIEW: index=%u scene=%d ready in=%" PRIu64
             "ms\n", index, g_nav.preview_scene[index],
             (end - start) / 1000);
    }
  else
    {
      g_nav.preview_ready[index] = false;
      printf("VELARIDE_PREVIEW: index=%u scene=%d failed in=%" PRIu64
             "ms\n", index, g_nav.preview_slot.scene,
             (end - start) / 1000);
    }

  g_nav.preview_next++;
  if (g_nav.preview_next < g_nav.preview_count)
    {
      lv_timer_set_period(timer, PREVIEW_NEXT_DELAY_MS);
      lv_timer_reset(timer);
      lv_timer_resume(timer);
    }
}

static void preview_schedule(void)
{
  int i;

  g_nav.preview_count = PREVIEW_COUNT;
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      if (!g_nav.preview_ready[i])
        {
          g_nav.preview_next = i;
          break;
        }
    }

  if (g_nav.preview_timer != NULL && i < PREVIEW_COUNT)
    {
      lv_timer_set_period(g_nav.preview_timer, PREVIEW_DELAY_MS);
      lv_timer_reset(g_nav.preview_timer);
      lv_timer_resume(g_nav.preview_timer);
    }
}

static void set_slot_lod(struct scene_slot_s *slot, bool lod)
{
  if (slot->detail == NULL)
    {
      return;
    }

  if (lod)
    {
      lv_obj_add_flag(slot->detail, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_remove_flag(slot->detail, LV_OBJ_FLAG_HIDDEN);
    }
}

static void reveal_area_for_value(int32_t value, lv_area_t *area)
{
  int32_t progress = LV_ABS(value);

  area->x1 = 0;
  area->y1 = 0;
  area->x2 = SCR_W - 1;
  area->y2 = SCR_H - 1;
  if (g_nav.axis == AXIS_H)
    {
      progress = LV_MIN(progress, SCR_W);
      if (g_nav.target_direction < 0)
        {
          area->x1 = SCR_W - progress;
        }
      else
        {
          area->x2 = progress - 1;
        }
    }
  else
    {
      progress = LV_MIN(progress, SCR_H);
      if (g_nav.target_direction < 0)
        {
          area->y1 = SCR_H - progress;
        }
      else
        {
          area->y2 = progress - 1;
        }
    }
}

static void reveal_draw_cb(lv_event_t *event)
{
  lv_area_t area;
  lv_area_t clip_saved;
  lv_area_t clip_target;
  lv_area_t image_area;
  lv_draw_image_dsc_t image_dsc;
  lv_layer_t *layer;
  int preview_index = -1;
  int i;

  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN ||
      g_nav.axis == AXIS_NONE || g_nav.reveal_value == 0)
    {
      return;
    }

  reveal_area_for_value(g_nav.reveal_value, &area);
  if (area.x1 > area.x2 || area.y1 > area.y2)
    {
      return;
    }

  layer = lv_event_get_layer(event);
  clip_saved = layer->_clip_area;
  if (!_lv_area_intersect(&clip_target, &clip_saved, &area))
    {
      return;
    }

  layer->_clip_area = clip_target;
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      if (g_nav.preview_ready[i] &&
          g_nav.preview_scene[i] == g_nav.reveal_scene)
        {
          preview_index = i;
          break;
        }
    }

  if (preview_index >= 0)
    {
      image_area.x1 = 0;
      image_area.y1 = 0;
      image_area.x2 = SCR_W - 1;
      image_area.y2 = SCR_H - 1;
      lv_draw_image_dsc_init(&image_dsc);
      image_dsc.src = &g_nav.preview_buf[preview_index];
      lv_draw_image(layer, &image_dsc, &image_area);
    }
  else
    {
      lv_obj_redraw(layer, g_nav.target->root);
    }

  layer->_clip_area = clip_saved;
}

static void place_slots(int32_t value)
{
  lv_area_t old_area;
  lv_area_t new_area;
  lv_area_t dirty;

  if (value == g_nav.reveal_value)
    {
      return;
    }

  reveal_area_for_value(g_nav.reveal_value, &old_area);
  reveal_area_for_value(value, &new_area);
  if (g_nav.axis == AXIS_H)
    {
      if (g_nav.target_direction < 0)
        {
          dirty.x1 = LV_MIN(old_area.x1, new_area.x1);
          dirty.x2 = LV_MAX(old_area.x1, new_area.x1) - 1;
        }
      else
        {
          dirty.x1 = LV_MIN(old_area.x2, new_area.x2) + 1;
          dirty.x2 = LV_MAX(old_area.x2, new_area.x2);
        }

      dirty.y1 = 0;
      dirty.y2 = SCR_H - 1;
    }
  else
    {
      dirty.x1 = 0;
      dirty.x2 = SCR_W - 1;
      if (g_nav.target_direction < 0)
        {
          dirty.y1 = LV_MIN(old_area.y1, new_area.y1);
          dirty.y2 = LV_MAX(old_area.y1, new_area.y1) - 1;
        }
      else
        {
          dirty.y1 = LV_MIN(old_area.y2, new_area.y2) + 1;
          dirty.y2 = LV_MAX(old_area.y2, new_area.y2);
        }
    }

  g_nav.reveal_value = value;
  lv_obj_invalidate_area(g_nav.reveal, &dirty);
}

static void settle_exec(void *var, int32_t value)
{
  LV_UNUSED(var);
  place_slots(value);
}

static void update_current_scene(void)
{
  time_t now = time(NULL);
  struct tm tm_now;
  char buf[32];

  if (g_nav.dragging || g_nav.transition_active)
    {
      return;
    }

  localtime_r(&now, &tm_now);
  if (g_nav.current->scene == SCENE_FACE_DIGITAL &&
      g_nav.current->primary != NULL)
    {
      snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
      lv_label_set_text(g_nav.current->primary, buf);
    }
  else if (g_nav.current->scene == SCENE_FACE_SPORT &&
           g_nav.current->primary != NULL)
    {
      snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
      lv_label_set_text(g_nav.current->primary, buf);
    }
  else if (g_nav.current->scene == SCENE_FACE_SIMPLE ||
            g_nav.current->scene == SCENE_FACE_DIAL ||
            g_nav.current->scene == SCENE_FACE_NUMERAL)
    {
      set_hand(g_nav.current->hand_h, g_nav.current->hand_h_pts,
               (tm_now.tm_hour % 12) * 30 + tm_now.tm_min / 2, 82);
      set_hand(g_nav.current->hand_m, g_nav.current->hand_m_pts,
               tm_now.tm_min * 6, 118);
      set_hand(g_nav.current->hand_s, g_nav.current->hand_s_pts,
               tm_now.tm_sec * 6, 132);
    }
}

static void settle_done(lv_anim_t *anim)
{
  struct scene_slot_s *old_current;

  LV_UNUSED(anim);
  if (g_nav.commit_transition)
    {
      old_current = g_nav.current;
      g_nav.current = g_nav.target;
      g_nav.target = g_nav.spare;
      g_nav.spare = old_current;

      if (g_nav.axis == AXIS_H)
        {
          if (g_nav.target_direction < 0)
            {
              g_nav.face_index = (g_nav.face_index + 1) % FACE_COUNT;
            }
          else
            {
              g_nav.face_index = (g_nav.face_index + FACE_COUNT - 1) % FACE_COUNT;
            }
        }
      else if (g_nav.target_direction < 0)
        {
          g_nav.function_index++;
        }
      else
        {
          g_nav.function_index--;
        }

      lv_obj_set_pos(g_nav.current->root, 0, 0);
      lv_obj_remove_flag(g_nav.current->root, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_nav.current->root);
      lv_obj_move_foreground(g_nav.reveal);
    }
  else
    {
      lv_obj_set_pos(g_nav.current->root, 0, 0);
    }

  lv_obj_add_flag(g_nav.target->root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_nav.spare->root, LV_OBJ_FLAG_HIDDEN);
  g_nav.reveal_value = 0;
  set_slot_lod(g_nav.current, false);
  g_nav.axis = AXIS_NONE;
  g_nav.dragging = false;
  g_nav.transition_active = false;
  update_current_scene();

  if (g_nav.perf_frames > 1 && g_nav.perf_last_us > g_nav.perf_first_us)
    {
      uint32_t rate_x10 = (uint32_t)((uint64_t)(g_nav.perf_frames - 1) *
                          10000000 /
                          (g_nav.perf_last_us - g_nav.perf_first_us));
      printf("VELARIDE_PERF: nav=%" PRIu32 ".%" PRIu32
             "Hz max_gap=%" PRIu32 "ms frames=%" PRIu32 "\n",
             rate_x10 / 10, rate_x10 % 10,
             g_nav.perf_max_gap_us / 1000, g_nav.perf_frames);
    }
  else
    {
      printf("VELARIDE_PERF: nav=0.0Hz max_gap=0ms frames=%" PRIu32
             "\n", g_nav.perf_frames);
    }

  preload_neighbor_scenes();
}

static void start_settle(bool commit)
{
  int32_t start = g_nav.axis == AXIS_H ? g_nav.drag_x : g_nav.drag_y;
  int32_t end = 0;

  g_nav.commit_transition = commit;
  g_nav.transition_active = true;
  if (commit)
    {
      end = -g_nav.target_origin;
    }

  lv_anim_init(&g_nav.settle_anim);
  lv_anim_set_var(&g_nav.settle_anim, &g_nav);
  lv_anim_set_exec_cb(&g_nav.settle_anim, settle_exec);
  lv_anim_set_values(&g_nav.settle_anim, start, end);
  lv_anim_set_duration(&g_nav.settle_anim, SETTLE_MS);
  lv_anim_set_path_cb(&g_nav.settle_anim, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&g_nav.settle_anim, settle_done);
  lv_anim_start(&g_nav.settle_anim);
}

static void begin_axis(enum axis_e axis, int direction)
{
  enum scene_id_e scene;
  struct scene_slot_s *tmp;

  if (!choose_target(axis, direction, &scene))
    {
      return;
    }

  if (g_nav.spare->scene == scene && g_nav.target->scene != scene)
    {
      tmp = g_nav.target;
      g_nav.target = g_nav.spare;
      g_nav.spare = tmp;
    }

  if (g_nav.target->scene != scene)
    {
      build_scene(g_nav.target, scene, false);
      lv_obj_add_flag(g_nav.target->root, LV_OBJ_FLAG_HIDDEN);
    }

  g_nav.axis = axis;
  g_nav.reveal_scene = scene;
  g_nav.reveal_value = 0;
  g_nav.target_direction = direction;
  g_nav.target_origin = axis == AXIS_H ?
                        (direction < 0 ? SCR_W : -SCR_W) :
                        (direction < 0 ? SCR_H : -SCR_H);
  lv_obj_add_flag(g_nav.target->root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_nav.reveal);
  place_slots(axis == AXIS_H ? g_nav.drag_x : g_nav.drag_y);
}

static void gesture_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_PRESSED)
    {
      g_nav.dragging = true;
      g_nav.axis = AXIS_NONE;
      g_nav.drag_x = 0;
      g_nav.drag_y = 0;
      g_nav.perf_first_us = 0;
      g_nav.perf_last_us = 0;
      g_nav.perf_frames = 0;
      g_nav.perf_max_gap_us = 0;
      lv_anim_delete(&g_nav, settle_exec);
    }
  else if (code == LV_EVENT_PRESSING && g_nav.dragging)
    {
      lv_indev_t *indev = lv_indev_active();
      lv_point_t vect;

      if (indev == NULL)
        {
          return;
        }

      lv_indev_get_vect(indev, &vect);
      g_nav.drag_x += vect.x;
      g_nav.drag_y += vect.y;

      if (g_nav.axis == AXIS_NONE &&
          (LV_ABS(g_nav.drag_x) > AXIS_LOCK_PX ||
           LV_ABS(g_nav.drag_y) > AXIS_LOCK_PX))
        {
          if (LV_ABS(g_nav.drag_x) * 10 > LV_ABS(g_nav.drag_y) * 13)
            {
              begin_axis(AXIS_H, g_nav.drag_x < 0 ? -1 : 1);
            }
          else if (LV_ABS(g_nav.drag_y) * 10 > LV_ABS(g_nav.drag_x) * 13)
            {
              begin_axis(AXIS_V, g_nav.drag_y < 0 ? -1 : 1);
            }
        }

      if (g_nav.axis == AXIS_H)
        {
          place_slots(LV_CLAMP(-SCR_W, g_nav.drag_x, SCR_W));
        }
      else if (g_nav.axis == AXIS_V)
        {
          place_slots(LV_CLAMP(-SCR_H, g_nav.drag_y, SCR_H));
        }
    }
  else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
           g_nav.dragging)
    {
      if (g_nav.axis == AXIS_H)
        {
          start_settle(LV_ABS(g_nav.drag_x) >= COMMIT_X_PX);
        }
      else if (g_nav.axis == AXIS_V)
        {
          start_settle(LV_ABS(g_nav.drag_y) >= COMMIT_Y_PX);
        }
      else
        {
          g_nav.dragging = false;
        }
    }
}

static void refresh_ready_cb(lv_event_t *event)
{
  uint64_t now;

  LV_UNUSED(event);
  if (g_menu.active && g_menu.perf_active)
    {
      now = monotonic_us();
      if (now != 0)
        {
          if (g_menu.perf_frames == 0)
            {
              g_menu.perf_first_us = now;
            }
          else
            {
              uint64_t gap = now - g_menu.perf_last_us;
              if (gap > g_menu.perf_max_gap_us)
                {
                  g_menu.perf_max_gap_us = gap > UINT32_MAX ? UINT32_MAX :
                                                            (uint32_t)gap;
                }
            }

          g_menu.perf_last_us = now;
          g_menu.perf_frames++;
        }
    }

  if (!g_nav.dragging && !g_nav.transition_active)
    {
      return;
    }

  now = monotonic_us();
  if (now == 0)
    {
      return;
    }

  if (g_nav.perf_frames == 0)
    {
      g_nav.perf_first_us = now;
    }
  else
    {
      uint64_t gap = now - g_nav.perf_last_us;
      if (gap > g_nav.perf_max_gap_us)
        {
          g_nav.perf_max_gap_us = gap > UINT32_MAX ? UINT32_MAX :
                                                        (uint32_t)gap;
        }
    }

  g_nav.perf_last_us = now;
  g_nav.perf_frames++;
}

static void data_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  update_current_scene();
}

static void honey_update_icons(void)
{
  int best = -1;
  int best_d2 = INT32_MAX;
  int i;

  for (i = 0; i < HONEY_ICON_COUNT; i++)
    {
      struct honey_icon_s *icon = &g_menu.icons[i];
      int screen_x = icon->center_x + g_menu.offset_x;
      int screen_y = icon->center_y + g_menu.offset_y;
      int dx = screen_x - SCR_W / 2;
      int dy = screen_y - SCR_H / 2;
      int d2 = dx * dx + dy * dy;
      int abs_x = LV_ABS(dx);
      int abs_y = LV_ABS(dy);
      int distance = LV_MAX(abs_x, abs_y) + LV_MIN(abs_x, abs_y) / 2;

      icon->diameter = LV_CLAMP(42, 98 - distance * 56 / 230, 98);

      if (d2 < best_d2)
        {
          best_d2 = d2;
          best = i;
        }
    }

  g_menu.selected = best;
}

static void honey_apply_offset(int32_t x, int32_t y)
{
  int32_t min_x = SCR_W - HONEY_CONTENT_SIZE;
  int32_t min_y = SCR_H - HONEY_CONTENT_SIZE;

  g_menu.offset_x = LV_CLAMP(min_x, x, 0);
  g_menu.offset_y = LV_CLAMP(min_y, y, 0);
  honey_update_icons();
  lv_obj_invalidate(g_menu.root);
}

static void honey_draw_cb(lv_event_t *event)
{
  static bool first_draw = true;
  lv_layer_t *layer;
  int i;

  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN_END)
    {
      return;
    }

  if (first_draw)
    {
      printf("VELARIDE_MENU: draw end offset=%" PRId32 ",%" PRId32 "\n",
             g_menu.offset_x, g_menu.offset_y);
      first_draw = false;
    }

  layer = lv_event_get_layer(event);
  for (i = 0; i < HONEY_ICON_COUNT; i++)
    {
      struct honey_icon_s *icon = &g_menu.icons[i];
      int diameter = icon->diameter;
      int radius = diameter / 2;
      int cx = icon->center_x + g_menu.offset_x;
      int cy = icon->center_y + g_menu.offset_y;
      lv_area_t area;
      lv_draw_rect_dsc_t rect_dsc;
      lv_draw_label_dsc_t label_dsc;
      lv_point_t text_size;
      lv_area_t text_area;

      if (cx + radius < 0 || cx - radius >= SCR_W ||
          cy + radius < 0 || cy - radius >= SCR_H)
        {
          continue;
        }

      area.x1 = cx - radius;
      area.y1 = cy - radius;
      area.x2 = cx + radius;
      area.y2 = cy + radius;
      lv_draw_rect_dsc_init(&rect_dsc);
      rect_dsc.radius = LV_RADIUS_CIRCLE;
      rect_dsc.bg_opa = LV_OPA_COVER;
      rect_dsc.bg_color = lv_color_hex(icon->color);
      if (icon->color == 0xffffff)
        {
          rect_dsc.border_width = 2;
          rect_dsc.border_opa = LV_OPA_COVER;
          rect_dsc.border_color = lv_color_hex(0xd1d5db);
        }

      lv_draw_rect(layer, &rect_dsc, &area);

      lv_draw_label_dsc_init(&label_dsc);
      label_dsc.text = icon->symbol;
      label_dsc.font = diameter >= 74 ? &lv_font_montserrat_24 :
                                       &lv_font_montserrat_20;
      label_dsc.color = icon->color == 0xffffff ? lv_color_black() :
                                                  lv_color_white();
      text_size = diameter >= 74 ? icon->text_size_24 : icon->text_size_20;
      text_area.x1 = cx - text_size.x / 2;
      text_area.y1 = cy - text_size.y / 2;
      text_area.x2 = text_area.x1 + text_size.x - 1;
      text_area.y2 = text_area.y1 + text_size.y - 1;
      lv_draw_label(layer, &label_dsc, &text_area);
    }
}

static int honey_hit_test(lv_point_t point)
{
  int best = -1;
  int best_d2 = INT32_MAX;
  int i;

  for (i = 0; i < HONEY_ICON_COUNT; i++)
    {
      struct honey_icon_s *icon = &g_menu.icons[i];
      int cx = icon->center_x + g_menu.offset_x;
      int cy = icon->center_y + g_menu.offset_y;
      int dx = point.x - cx;
      int dy = point.y - cy;
      int d2 = dx * dx + dy * dy;
      int radius = icon->diameter / 2;

      if (d2 <= radius * radius && d2 < best_d2)
        {
          best = i;
          best_d2 = d2;
        }
    }

  return best;
}

static void honey_gesture_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_PRESSED)
    {
      lv_indev_t *indev = lv_indev_active();

      g_menu.dragging = true;
      g_menu.moved = false;
      g_menu.lod = false;
      g_menu.perf_active = true;
      g_menu.perf_first_us = 0;
      g_menu.perf_last_us = 0;
      g_menu.perf_frames = 0;
      g_menu.perf_max_gap_us = 0;
      g_menu.velocity_x = 0;
      g_menu.velocity_y = 0;
      g_menu.drag_sum_x = 0;
      g_menu.drag_sum_y = 0;
      if (indev != NULL)
        {
          lv_indev_get_point(indev, &g_menu.last_point);
        }
    }
  else if (code == LV_EVENT_PRESSING && g_menu.dragging)
    {
      lv_indev_t *indev = lv_indev_active();
      lv_point_t point;
      lv_point_t vect;

      if (indev == NULL)
        {
          return;
        }

      lv_indev_get_point(indev, &point);
      vect.x = point.x - g_menu.last_point.x;
      vect.y = point.y - g_menu.last_point.y;
      g_menu.last_point = point;
      if (LV_ABS(vect.x) + LV_ABS(vect.y) > 0)
        {
          g_menu.drag_sum_x += vect.x;
          g_menu.drag_sum_y += vect.y;
          if (LV_ABS(g_menu.drag_sum_x) + LV_ABS(g_menu.drag_sum_y) > 8)
            {
              g_menu.moved = true;
            }
          g_menu.velocity_x = vect.x;
          g_menu.velocity_y = vect.y;
          honey_apply_offset(g_menu.offset_x + vect.x,
                             g_menu.offset_y + vect.y);
        }
    }
  else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
           g_menu.dragging)
    {
      g_menu.dragging = false;
      if (!g_menu.moved && code == LV_EVENT_RELEASED)
        {
          int index = honey_hit_test(g_menu.last_point);
          if (index >= 0)
            {
              honey_apply_offset(SCR_W / 2 - g_menu.icons[index].center_x,
                                 SCR_H / 2 - g_menu.icons[index].center_y);
            }
        }

    }
}

static void honey_inertia_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (!g_menu.active || g_menu.dragging)
    {
      return;
    }

  if (LV_ABS(g_menu.velocity_x) <= 1 && LV_ABS(g_menu.velocity_y) <= 1)
    {
      g_menu.velocity_x = 0;
      g_menu.velocity_y = 0;
      if (g_menu.perf_active)
        {
          if (g_menu.perf_frames > 1 &&
              g_menu.perf_last_us > g_menu.perf_first_us)
            {
              uint32_t rate_x10 =
                (uint32_t)((uint64_t)(g_menu.perf_frames - 1) * 10000000 /
                           (g_menu.perf_last_us - g_menu.perf_first_us));
              printf("VELARIDE_MENU_PERF: move=%" PRIu32 ".%" PRIu32
                     "Hz max_gap=%" PRIu32 "ms frames=%" PRIu32 "\n",
                     rate_x10 / 10, rate_x10 % 10,
                     g_menu.perf_max_gap_us / 1000, g_menu.perf_frames);
            }
          else
            {
              printf("VELARIDE_MENU_PERF: move=0.0Hz max_gap=0ms frames=%"
                     PRIu32 "\n", g_menu.perf_frames);
            }

          g_menu.perf_active = false;
        }
      return;
    }

  honey_apply_offset(g_menu.offset_x + g_menu.velocity_x,
                     g_menu.offset_y + g_menu.velocity_y);
  g_menu.velocity_x = g_menu.velocity_x * 3 / 4;
  g_menu.velocity_y = g_menu.velocity_y * 3 / 4;
}

static void build_honey_menu(lv_obj_t *screen)
{
  static const char *symbols[HONEY_ICON_COUNT] =
  {
    LV_SYMBOL_HOME, LV_SYMBOL_GPS, LV_SYMBOL_AUDIO, LV_SYMBOL_BELL,
    LV_SYMBOL_CALL, LV_SYMBOL_SETTINGS, LV_SYMBOL_PLAY, LV_SYMBOL_IMAGE,
    LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, LV_SYMBOL_CHARGE, LV_SYMBOL_LIST,
    LV_SYMBOL_EYE_OPEN, "31", "RIDE", "AI", "S", "+", "..."
  };
  static const uint32_t colors[HONEY_ICON_COUNT] =
  {
    0xff3b30, 0x0a84ff, 0xff2d55, 0xff9f0a, 0x30d158,
    0x8e8e93, 0x0a84ff, 0xbf5af2, 0x64d2ff, 0x5e5ce6,
    0xffd60a, 0xff453a, 0x32d74b, 0xffffff, 0xb8f23d,
    0x7d5fff, 0xff6b00, 0x00c7be, 0x48484a
  };
  static const int8_t axial[HONEY_ICON_COUNT][2] =
  {
    {0, 0}, {1, 0}, {0, 1}, {-1, 1}, {-1, 0}, {0, -1}, {1, -1},
    {2, 0}, {1, 1}, {0, 2}, {-1, 2}, {-2, 2}, {-2, 1}, {-2, 0},
    {-1, -1}, {0, -2}, {1, -2}, {2, -2}, {2, -1}
  };
  int center_x = HONEY_CONTENT_SIZE / 2;
  int center_y = HONEY_CONTENT_SIZE / 2;
  int i;

  memset(&g_menu, 0, sizeof(g_menu));
  g_menu.button_fd = -1;
  g_menu.selected = -1;

  g_menu.root = lv_obj_create(screen);
  lv_obj_remove_style_all(g_menu.root);
  lv_obj_set_size(g_menu.root, SCR_W, SCR_H);
  lv_obj_set_pos(g_menu.root, 0, 0);
  lv_obj_set_style_bg_color(g_menu.root, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_menu.root, LV_OPA_COVER, 0);
  lv_obj_remove_flag(g_menu.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(g_menu.root, honey_gesture_cb, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(g_menu.root, honey_draw_cb,
                      LV_EVENT_DRAW_MAIN_END, NULL);

  for (i = 0; i < HONEY_ICON_COUNT; i++)
    {
      struct honey_icon_s *icon = &g_menu.icons[i];
      int q = axial[i][0];
      int r = axial[i][1];
      int x = center_x + HONEY_CELL_SIZE * q + HONEY_CELL_SIZE * r / 2;
      int y = center_y + HONEY_CELL_SIZE * 866 * r / 1000;

      icon->center_x = x;
      icon->center_y = y;
      icon->symbol = symbols[i];
      icon->color = colors[i];
      icon->diameter = HONEY_ICON_DIAMETER;
      lv_text_get_size(&icon->text_size_20, icon->symbol,
                       &lv_font_montserrat_20, 0, 0,
                       LV_COORD_MAX, LV_TEXT_FLAG_NONE);
      lv_text_get_size(&icon->text_size_24, icon->symbol,
                       &lv_font_montserrat_24, 0, 0,
                       LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    }

  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
  honey_apply_offset(SCR_W / 2 - center_x, SCR_H / 2 - center_y);
}

static void switch_shell_mode(void)
{
  g_menu.active = !g_menu.active;
  if (g_menu.active)
    {
      lv_obj_remove_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_menu.root);
      honey_update_icons();
    }
  else
    {
      lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_nav.reveal);
      lv_obj_invalidate(g_nav.current->root);
    }
}

static void sport_exit_cb(void *arg)
{
  LV_UNUSED(arg);
  g_menu.active = false;
  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_nav.current->root);
  lv_obj_move_foreground(g_nav.reveal);
  lv_obj_invalidate(g_nav.current->root);
  preview_schedule();
}

static void enter_sport_mode(void)
{
  if (velaride_sport_ui_active())
    {
      return;
    }

  g_menu.active = false;
  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
  if (g_nav.preview_timer != NULL)
    {
      lv_timer_pause(g_nav.preview_timer);
    }
  velaride_sport_ui_enter();
}

static void button_timer_cb(lv_timer_t *timer)
{
  btn_buttonset_t state = 0;
  uint64_t now;
  ssize_t nread;

  LV_UNUSED(timer);
  if (g_menu.button_fd < 0)
    {
      return;
    }

  nread = read(g_menu.button_fd, &state, sizeof(state));
  if (nread == sizeof(state))
    {
      if ((state & 1) != 0 && (g_menu.button_last & 1) == 0)
        {
          now = monotonic_us();
          if (g_menu.button_single_pending &&
              now - g_menu.button_first_us <= BUTTON_DOUBLE_MS * 1000)
            {
              g_menu.button_single_pending = false;
              enter_sport_mode();
            }
          else
            {
              g_menu.button_first_us = now;
              g_menu.button_single_pending = true;
            }
        }

      g_menu.button_last = state;
    }

  if (g_menu.button_single_pending)
    {
      now = monotonic_us();
      if (now - g_menu.button_first_us > BUTTON_DOUBLE_MS * 1000)
        {
          g_menu.button_single_pending = false;
          if (!velaride_sport_ui_active())
            {
              switch_shell_mode();
            }
        }
    }
}

static void build_shell(void)
{
  lv_obj_t *screen = lv_screen_active();
  int i;

  memset(&g_nav, 0, sizeof(g_nav));
  g_nav.face_index = 0;
  g_nav.function_index = -1;

  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_event_cb(screen, gesture_cb, LV_EVENT_ALL, NULL);

  for (i = 0; i < 3; i++)
    {
      g_nav.slots[i].root = lv_obj_create(screen);
      lv_obj_set_size(g_nav.slots[i].root, SCR_W, SCR_H);
      lv_obj_set_pos(g_nav.slots[i].root, 0, 0);
      if (i != 0)
        {
          lv_obj_add_flag(g_nav.slots[i].root, LV_OBJ_FLAG_HIDDEN);
        }
    }

  g_nav.preview_slot.root = lv_obj_create(screen);
  lv_obj_set_size(g_nav.preview_slot.root, SCR_W, SCR_H);
  lv_obj_set_pos(g_nav.preview_slot.root, 0, 0);
  lv_obj_add_flag(g_nav.preview_slot.root, LV_OBJ_FLAG_HIDDEN);

  g_nav.reveal = lv_obj_create(screen);
  lv_obj_remove_style_all(g_nav.reveal);
  lv_obj_set_size(g_nav.reveal, SCR_W, SCR_H);
  lv_obj_set_pos(g_nav.reveal, 0, 0);
  lv_obj_remove_flag(g_nav.reveal,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_nav.reveal, reveal_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

  g_nav.current = &g_nav.slots[0];
  g_nav.target = &g_nav.slots[1];
  g_nav.spare = &g_nav.slots[2];
  build_scene(g_nav.current, current_scene(), false);
  preload_neighbor_scenes();
  build_honey_menu(screen);
  velaride_sport_ui_init(screen, sport_exit_cb, NULL);
  g_menu.button_fd = open("/dev/buttons", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (g_menu.button_fd < 0)
    {
      printf("velaride: cannot open /dev/buttons errno=%d\n", errno);
    }
  else
    {
      read(g_menu.button_fd, &g_menu.button_last,
           sizeof(g_menu.button_last));
    }

  lv_display_add_event_cb(lv_display_get_default(), refresh_ready_cb,
                          LV_EVENT_REFR_READY, NULL);
  g_nav.data_timer = lv_timer_create(data_timer_cb, 1000, NULL);
  g_nav.preview_timer = lv_timer_create(preview_timer_cb,
                                         PREVIEW_DELAY_MS, NULL);
  preview_schedule();
  lv_timer_create(button_timer_cb, BUTTON_POLL_MS, NULL);
  lv_timer_create(honey_inertia_timer_cb, 16, NULL);
  update_current_scene();
}

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
static void frame_timer_cb(uv_timer_t *timer)
{
  LV_UNUSED(timer);
  _lv_display_refr_timer(NULL);
}
#endif

static bool wait_for_device(const char *path, uint32_t timeout_ms)
{
  uint32_t waited = 0;

  while (waited < timeout_ms)
    {
      int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd >= 0)
        {
          close(fd);
          printf("velaride: device ready %s after %" PRIu32 "ms\n",
                 path, waited);
          return true;
        }

      usleep(100000);
      waited += 100;
    }

  printf("velaride: device timeout %s errno=%d\n", path, errno);
  return false;
}

static int velaride_hex_value(char ch)
{
  if (ch >= '0' && ch <= '9')
    {
      return ch - '0';
    }

  if (ch >= 'a' && ch <= 'f')
    {
      return ch - 'a' + 10;
    }

  if (ch >= 'A' && ch <= 'F')
    {
      return ch - 'A' + 10;
    }

  return -1;
}

static int velaride_decode_hex(const char *hex, char *output, size_t size)
{
  size_t hex_len;
  size_t i;

  if (hex == NULL || output == NULL || size == 0)
    {
      return -EINVAL;
    }

  hex_len = strlen(hex);
  if ((hex_len & 1) != 0 || hex_len / 2 >= size)
    {
      return -E2BIG;
    }

  for (i = 0; i < hex_len; i += 2)
    {
      int high = velaride_hex_value(hex[i]);
      int low = velaride_hex_value(hex[i + 1]);

      if (high < 0 || low < 0)
        {
          return -EINVAL;
        }

      output[i / 2] = (char)((high << 4) | low);
    }

  output[hex_len / 2] = '\0';
  return hex_len / 2;
}

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  lv_nuttx_uv_t uv_info;
  uv_loop_t ui_loop;
  uv_timer_t frame_timer;
  void *uv_data;
  int ret;
#endif

  if (argc > 1 && strcmp(argv[1], "time-save") == 0)
    {
      int time_ret = velaride_timekeeper_save_now();
      return time_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 2 && strcmp(argv[1], "time-set") == 0)
    {
      struct timespec ts;
      char *end;
      int64_t epoch = strtoll(argv[2], &end, 10);
      int time_ret;

      if (*argv[2] == '\0' || *end != '\0' || epoch < INT64_C(1704067200))
        {
          printf("VELARIDE_TIME: invalid epoch\n");
          return EXIT_FAILURE;
        }

      ts.tv_sec = (time_t)epoch;
      ts.tv_nsec = 0;
      time_ret = clock_settime(CLOCK_REALTIME, &ts);
      if (time_ret == 0)
        {
          time_ret = velaride_timekeeper_save_now();
        }

      printf("VELARIDE_TIME: set epoch=%" PRId64 " ret=%d\n",
             epoch, time_ret);
      return time_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 1 && strcmp(argv[1], "time-read") == 0)
    {
      int64_t epoch;
      int time_ret = velaride_timekeeper_read(&epoch);

      if (time_ret < 0)
        {
          printf("VELARIDE_TIME: read failed=%d\n", time_ret);
          return EXIT_FAILURE;
        }

      printf("VELARIDE_TIME: saved epoch=%" PRId64 "\n", epoch);
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "review-demo") == 0)
    {
      static const char demo_review[] =
        "本次骑行节奏较稳。没有明显冲击，建议继续注意补水和定时休息。";
      int review_ret = velaride_ride_session_save_review(demo_review);

      printf("VELARIDE_REVIEW: demo persist=%d\n", review_ret);
      return review_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 2 && strcmp(argv[1], "review-hex") == 0)
    {
      char review[261];
      int decode_ret = velaride_decode_hex(argv[2], review, sizeof(review));
      int review_ret;

      if (decode_ret <= 0)
        {
          printf("VELARIDE_REVIEW: decode=%d\n", decode_ret);
          return EXIT_FAILURE;
        }

      review_ret = velaride_ride_session_save_review(review);
      printf("VELARIDE_REVIEW: bytes=%d persist=%d\n",
             decode_ret, review_ret);
      return review_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 1 && strcmp(argv[1], "review-begin") == 0)
    {
      if (g_review_transfer == NULL)
        {
          g_review_transfer = malloc(REVIEW_TRANSFER_MAX + 1);
          if (g_review_transfer == NULL)
            {
              printf("VELARIDE_REVIEW: transfer allocation failed\n");
              return EXIT_FAILURE;
            }
        }

      g_review_transfer_len = 0;
      g_review_transfer[0] = '\0';
      printf("VELARIDE_REVIEW: transfer begin\n");
      return EXIT_SUCCESS;
    }

  if (argc > 2 && strcmp(argv[1], "review-part") == 0)
    {
      char decoded[33];
      int decode_ret = velaride_decode_hex(argv[2], decoded,
                                           sizeof(decoded));

      if (g_review_transfer == NULL || decode_ret <= 0 ||
          g_review_transfer_len + (size_t)decode_ret > REVIEW_TRANSFER_MAX)
        {
          printf("VELARIDE_REVIEW: part decode=%d total=%u\n",
                 decode_ret, (unsigned int)g_review_transfer_len);
          return EXIT_FAILURE;
        }

      memcpy(g_review_transfer + g_review_transfer_len,
             decoded, decode_ret);
      g_review_transfer_len += decode_ret;
      g_review_transfer[g_review_transfer_len] = '\0';
      printf("VELARIDE_REVIEW: part=%d total=%u\n",
             decode_ret, (unsigned int)g_review_transfer_len);
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "review-end") == 0)
    {
      int review_ret;

      if (g_review_transfer == NULL || g_review_transfer_len == 0)
        {
          printf("VELARIDE_REVIEW: empty transfer\n");
          return EXIT_FAILURE;
        }

      review_ret = velaride_ride_session_save_review(g_review_transfer);
      printf("VELARIDE_REVIEW: transfer bytes=%u persist=%d\n",
             (unsigned int)g_review_transfer_len, review_ret);
      g_review_transfer_len = 0;
      free(g_review_transfer);
      g_review_transfer = NULL;
      return review_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 1 && strcmp(argv[1], "review-read") == 0)
    {
      char review[261];
      int review_ret = velaride_ride_session_load_review(review,
                                                         sizeof(review));

      printf("VELARIDE_REVIEW: read=%d text=%s\n",
             review_ret, review_ret == 0 ? review : "");
      return review_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 1 && strcmp(argv[1], "ride-status") == 0)
    {
      struct velaride_ride_snapshot_s snapshot;

      velaride_ride_session_get(&snapshot);
      printf("{\"active\":%s,\"paused\":%s,\"sensor_ready\":%s,"
             "\"motion\":%d,\"alert\":%d,\"elapsed_s\":%" PRIu32
             ",\"moving_s\":%" PRIu32 ",\"still_s\":%" PRIu32
             ",\"pause_count\":%u,\"impact_count\":%u,"
             "\"rest_count\":%u}\n",
             snapshot.active ? "true" : "false",
             snapshot.paused ? "true" : "false",
             snapshot.sensor_ready ? "true" : "false",
             snapshot.motion, snapshot.alert, snapshot.elapsed_s,
             snapshot.moving_s, snapshot.still_s, snapshot.pause_count,
             snapshot.impact_count, snapshot.rest_count);
      return EXIT_SUCCESS;
    }

  if (argc > 3 && strcmp(argv[1], "phone-data") == 0)
    {
      char *speed_end;
      char *distance_end;
      double speed = strtod(argv[2], &speed_end);
      double distance = strtod(argv[3], &distance_end);

      if (*argv[2] == '\0' || *speed_end != '\0' ||
          *argv[3] == '\0' || *distance_end != '\0' ||
          speed < 0.0 || distance < 0.0)
        {
          printf("{\"ok\":false,\"error\":\"bad_phone_data\"}\n");
          return EXIT_FAILURE;
        }

      velaride_ride_session_set_phone_data(speed, distance);
      printf("{\"ok\":true,\"event\":\"phone_data\","
             "\"speed\":%.1f,\"distance\":%.2f}\n",
             speed, distance);
      return EXIT_SUCCESS;
    }

  if (argc > 1 && (strcmp(argv[1], "ride-pause") == 0 ||
                   strcmp(argv[1], "ride-resume") == 0))
    {
      struct velaride_ride_snapshot_s snapshot;
      bool pause = strcmp(argv[1], "ride-pause") == 0;

      velaride_ride_session_get(&snapshot);
      if (!snapshot.active)
        {
          printf("{\"ok\":false,\"error\":\"no_active_ride\"}\n");
          return EXIT_FAILURE;
        }

      velaride_ride_session_set_paused(pause);
      printf("{\"ok\":true,\"paused\":%s}\n",
             pause ? "true" : "false");
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "ride-latest") == 0)
    {
      struct velaride_ride_record_s record;
      int load_ret = velaride_ride_session_load_latest(&record);

      if (load_ret < 0)
        {
          printf("{\"ok\":false,\"error\":%d}\n", load_ret);
          return EXIT_FAILURE;
        }

      printf("{\"ok\":true,\"sequence\":%" PRIu32
             ",\"start_epoch\":%" PRId64 ",\"end_epoch\":%" PRId64
             ",\"elapsed_s\":%" PRIu32 ",\"moving_s\":%" PRIu32
             ",\"still_s\":%" PRIu32 ",\"pause_count\":%u,"
             "\"impact_count\":%u,\"rest_count\":%u}\n",
             record.sequence, record.start_epoch, record.end_epoch,
             record.elapsed_s, record.moving_s, record.still_s,
             record.pause_count, record.impact_count, record.rest_count);
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "persist-read") == 0)
    {
      struct velaride_ride_record_s record;
      int persist_ret = velaride_ride_session_load_latest(&record);

      if (persist_ret < 0)
        {
          printf("VELARIDE_PERSIST: read failed=%d\n", persist_ret);
          return EXIT_FAILURE;
        }

      printf("VELARIDE_PERSIST: seq=%" PRIu32 " end=%" PRId64
             " elapsed=%" PRIu32 " moving=%" PRIu32
             " still=%" PRIu32 " pause=%u impact=%u rest=%u\n",
             record.sequence, record.end_epoch, record.elapsed_s,
             record.moving_s, record.still_s, record.pause_count,
             record.impact_count, record.rest_count);
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "persist-write") == 0)
    {
      struct velaride_ride_record_s record;
      struct velaride_ride_record_s old;
      int persist_ret;

      memset(&record, 0, sizeof(record));
      if (velaride_ride_session_load_latest(&old) == 0)
        {
          record.sequence = old.sequence + 1;
        }
      else
        {
          record.sequence = 1;
        }

      record.end_epoch = (int64_t)time(NULL);
      record.elapsed_s = 321;
      record.start_epoch = record.end_epoch - record.elapsed_s;
      record.moving_s = 234;
      record.still_s = 87;
      record.pause_count = 3;
      record.impact_count = 2;
      record.rest_count = 1;
      persist_ret = velaride_ride_session_save_record(&record);
      printf("VELARIDE_PERSIST: write seq=%" PRIu32 " result=%d\n",
             record.sequence, persist_ret);
      return persist_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 1 && strcmp(argv[1], "persist-probe") == 0)
    {
      struct config_data_s probe;
      uint32_t value = 0x56315244;
      uint32_t verify = 0;
      int config_fd;
      int set_ret;
      int get_ret;

      config_fd = open("/dev/config0", O_RDWR | O_CLOEXEC);
      printf("VELARIDE_PERSIST_PROBE: open=%d errno=%d\n",
             config_fd, config_fd < 0 ? errno : 0);
      if (config_fd < 0)
        {
          return EXIT_FAILURE;
        }

      memset(&probe, 0, sizeof(probe));
      strlcpy(probe.name, "velaride.probe", sizeof(probe.name));
      probe.configdata = (uint8_t *)&value;
      probe.len = sizeof(value);
      set_ret = ioctl(config_fd, CFGDIOC_SETCONFIG,
                      (unsigned long)&probe);
      printf("VELARIDE_PERSIST_PROBE: set=%d errno=%d\n",
             set_ret, set_ret < 0 ? errno : 0);

      memset(&probe, 0, sizeof(probe));
      strlcpy(probe.name, "velaride.probe", sizeof(probe.name));
      probe.configdata = (uint8_t *)&verify;
      probe.len = sizeof(verify);
      get_ret = ioctl(config_fd, CFGDIOC_GETCONFIG,
                      (unsigned long)&probe);
      printf("VELARIDE_PERSIST_PROBE: get=%d errno=%d len=%zu value=%08" PRIx32
             "\n", get_ret, get_ret < 0 ? errno : 0, probe.len, verify);
      close(config_fd);
      return set_ret == 0 && get_ret == 0 && verify == value ?
             EXIT_SUCCESS : EXIT_FAILURE;
    }

  if (argc > 1 && strcmp(argv[1], "persist-format") == 0)
    {
      int config_fd = open("/dev/config0", O_RDWR | O_CLOEXEC);
      int erase_ret;

      printf("VELARIDE_PERSIST_FORMAT: open=%d errno=%d\n",
             config_fd, config_fd < 0 ? errno : 0);
      if (config_fd < 0)
        {
          return EXIT_FAILURE;
        }

      erase_ret = ioctl(config_fd, MTDIOC_BULKERASE, 0);
      printf("VELARIDE_PERSIST_FORMAT: erase=%d errno=%d\n",
             erase_ret, erase_ret < 0 ? errno : 0);
      close(config_fd);
      return erase_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
  if (!wait_for_device("/dev/lcd0", 10000) ||
      !wait_for_device("/dev/input0", 10000))
    {
      return EXIT_FAILURE;
    }

  /* Restore wall clock before the first watch-face labels are created. */

  velaride_timekeeper_restore();

  if (lv_is_initialized())
    {
      printf("velaride: LVGL already initialized\n");
      return EXIT_FAILURE;
    }

  lv_init();
  lv_nuttx_dsc_init(&info);
#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif
#ifdef CONFIG_LV_USE_NUTTX_TOUCHSCREEN
  info.input_path = "/dev/input0";
#endif
  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      return EXIT_FAILURE;
    }

  if (result.indev == NULL)
    {
      printf("velaride: touchscreen input unavailable\n");
    }

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  memset(&ui_loop, 0, sizeof(ui_loop));
  if (uv_loop_init(&ui_loop) < 0)
    {
      return EXIT_FAILURE;
    }

  memset(&uv_info, 0, sizeof(uv_info));
  uv_info.loop = &ui_loop;
  uv_info.disp = result.disp;
  uv_info.indev = result.indev;
  uv_data = lv_nuttx_uv_init_partial(&uv_info,
                                     LV_NUTTX_UV_INIT_TIMER |
                                     LV_NUTTX_UV_INIT_INPUT);
  if (uv_data == NULL)
    {
      return EXIT_FAILURE;
    }
#endif

  build_shell();
  lv_obj_invalidate(lv_screen_active());
  _lv_display_refr_timer(NULL);

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
  lv_display_delete_refr_timer(result.disp);
  memset(&frame_timer, 0, sizeof(frame_timer));
  ret = uv_timer_init(&ui_loop, &frame_timer);
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  uv_timer_start(&frame_timer, frame_timer_cb, 0, 16);
  printf("velaride: 2D watch shell enabled\n");
  uv_run(&ui_loop, UV_RUN_DEFAULT);
#else
  while (1)
    {
      uint32_t wait = lv_timer_handler();
      usleep((wait ? wait : 1) * 1000);
    }
#endif

  return EXIT_SUCCESS;
}
