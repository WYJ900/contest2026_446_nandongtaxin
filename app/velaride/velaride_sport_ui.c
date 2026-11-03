/****************************************************************************
 * apps/examples/velaride/velaride_sport_ui.c
 *
 * VelaRide cycling workflow and device pages.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <lvgl/lvgl.h>

#include "velaride_sport_ui.h"
#include "velaride_ride_session.h"
#include "velaride_device.h"
#include "velaride_compass.h"
#include "velaride_icons.h"
#include "velaride_theme.h"
#include "velaride_night_ui.h"
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
#include "velaride_countdown.h"
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
#include "velaride_alarm.h"
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
#include "velaride_sound.h"
#endif

#define SPORT_W 390
#define SPORT_H 450
#define SETTING_LIST_Y 112
#define RIDE_ROWS 4
#define HOLD_END_MS 900
#define COUNTDOWN_MS 3000
#define GOAL_STEP_MIN 5
#define GOAL_MAX_MIN 180
#define GOAL_LESS (-1)
#define GOAL_MORE (-2)
#define SETTING_ROW_H 72
#define SETTING_ROW_GAP 10
#define BG_COLOR VR_BG
#define TEXT_COLOR VR_INK
#define MUTED_COLOR VR_INK2
#define ACCENT_COLOR (velaride_accent()->color)

/* Button roles. Colors come from the theme so the wearer's accent applies
 * everywhere, while warning and danger stay fixed.
 */

#define BTN_PRIMARY   1
#define BTN_SECONDARY 2
#define BTN_TILE      3
#define BTN_WARN      4
#define BTN_DANGER    5
#define BTN_BACK      6

enum sport_page_e
{
  SPORT_PAGE_HIDDEN = 0,
  SPORT_PAGE_SELECT,
  SPORT_PAGE_GOAL,
  SPORT_PAGE_COUNTDOWN,
  SPORT_PAGE_ACTIVE,
  SPORT_PAGE_CONFIRM,
  SPORT_PAGE_ALERT,
  SPORT_PAGE_GOAL_DONE,
  SPORT_PAGE_REVIEW,
  SPORT_PAGE_INFO,
};

enum ride_view_e
{
  RIDE_VIEW_CONTROL = 0,
  RIDE_VIEW_DATA,
  RIDE_VIEW_HEADING,
  RIDE_VIEW_COUNT
};

struct sport_ui_s
{
  lv_obj_t *root;
  lv_obj_t *clock;
  lv_obj_t *body;
  lv_obj_t *time_label;
  lv_obj_t *metric_label;
  lv_obj_t *status_label;
  lv_obj_t *goal_label;
  lv_obj_t *progress;
  lv_obj_t *setting_buttons[3];
  lv_obj_t *auto_button;
  lv_obj_t *accent_dots[VELARIDE_ACCENT_COUNT];
  lv_obj_t *compass_face;
  lv_obj_t *chip;
  lv_obj_t *row_name[RIDE_ROWS];
  lv_obj_t *row_value[RIDE_ROWS];
  lv_obj_t *hold_btn;
  lv_obj_t *hold_arc;
  lv_obj_t *heading_label;
  lv_obj_t *heading_name;
  lv_obj_t *tape;
  lv_obj_t *scroll;
  lv_obj_t *review_card;
  lv_obj_t *review_button;
  lv_timer_t *hold_timer;
  uint32_t hold_start;
  float heading;
  int ride_view;
  bool locked;
  lv_timer_t *tool_timer;
  uint32_t tool_tick;
  uint8_t light_mode;
  bool light_on;
  bool sw_running;
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  lv_obj_t *countdown_adjust[6];
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  lv_obj_t *alarm_adjust[4];
  uint8_t alarm_hour;
  uint8_t alarm_minute;
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  uint8_t spoken_count;
  lv_obj_t *sound_buttons[8];
  lv_obj_t *sound_slider;
  lv_obj_t *sound_volume_label;
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  lv_obj_t *night_modes[3];
  lv_obj_t *night_slider;
  lv_obj_t *night_value;
#endif
  uint32_t sw_acc_ms;
  uint32_t sw_start;
  uint32_t sw_laps[3];
  int sw_lap_count;
  float level_x;
  float level_y;
  bool level_valid;
  bool info_from_settings;
  lv_timer_t *countdown_timer;
  lv_timer_t *session_timer;
  velaride_sport_exit_cb_t exit_cb;
  void *exit_arg;
  enum sport_page_e page;
  enum velaride_alert_e shown_alert;
  enum velaride_info_e info;
  uint32_t goal_s;
  uint32_t countdown_start;
  uint8_t sport_type;
  bool session_started;
  bool goal_announced;
  bool has_record;
  bool review_ai;
  struct velaride_ride_record_s latest_record;
  int persist_result;
};

static struct sport_ui_s g_sport;

static void show_select(void);
static void show_goal(void);
static void pause_cb(lv_event_t *event);
static void update_info(void);
static void show_active(void);
static void show_confirm(void);
static void show_review(void);
static void show_alert(enum velaride_alert_e alert);
static void show_goal_done(void);

static const char *sport_name(void)
{
  static const char *names[] = {"户外骑行", "室内骑行", "自由训练"};
  return names[g_sport.sport_type < 3 ? g_sport.sport_type : 0];
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, uint32_t color,
                       const lv_font_t *font, int x, int y, int width)
{
  lv_obj_t *obj = lv_label_create(parent);
  lv_label_set_text(obj, text);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_width(obj, width);
  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(obj, x, y);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

static lv_obj_t *button(const char *text, uint32_t color, int x, int y,
                        int width, int height, lv_event_cb_t cb,
                        intptr_t value)
{
  lv_obj_t *obj = lv_button_create(g_sport.root);
  lv_obj_t *caption;
  uint32_t bg;
  uint32_t ink;

  switch (color)
    {
      case BTN_PRIMARY:
        bg = velaride_accent()->color;
        ink = velaride_accent()->ink;
        break;
      case BTN_TILE:
        bg = VR_TILE;
        ink = TEXT_COLOR;
        break;
      case BTN_WARN:
        bg = 0x2e1d02;
        ink = VR_WARN;
        break;
      case BTN_DANGER:
        bg = 0x2e0c0a;
        ink = VR_DANGER;
        break;
      case BTN_BACK:
        bg = BG_COLOR;
        ink = velaride_accent()->color;
        break;
      default:
        bg = VR_TILE2;
        ink = TEXT_COLOR;
        break;
    }

  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, width, height);
  lv_obj_set_style_radius(obj, color == BTN_TILE ? 20 : height / 2, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_set_style_transform_scale(obj, 243, LV_STATE_PRESSED);
  lv_obj_set_style_transform_pivot_x(obj, width / 2, 0);
  lv_obj_set_style_transform_pivot_y(obj, height / 2, 0);
  lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)value);
  caption = lv_label_create(obj);
  lv_label_set_text(caption, text);
  lv_obj_set_style_text_font(caption, height >= 56 ? &velaride_font_cn_28 :
                                      &velaride_font_cn_20, 0);
  lv_obj_set_style_text_color(caption, lv_color_hex(ink), 0);
  lv_obj_center(caption);
  return obj;
}

static void update_clock(void)
{
  char text[16];
  struct tm local;
  time_t now = time(NULL);

  if (g_sport.clock == NULL)
    {
      return;
    }
  if (now < 1704067200 || localtime_r(&now, &local) == NULL)
    {
      lv_label_set_text(g_sport.clock, "--:--");
      return;
    }

  snprintf(text, sizeof(text), "%02d:%02d", local.tm_hour, local.tm_min);
  lv_label_set_text(g_sport.clock, text);
}

static void back_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  velaride_sport_ui_back();
}

static void clear_page_state(enum sport_page_e page)
{
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  if (g_sport.page == SPORT_PAGE_COUNTDOWN && page != SPORT_PAGE_COUNTDOWN)
    {
      velaride_sound_stop_voice();
    }
#endif
  velaride_compass_enable(false);
  if (g_sport.hold_timer != NULL)
    {
      lv_timer_pause(g_sport.hold_timer);
    }

  if (g_sport.tool_timer != NULL)
    {
      lv_timer_pause(g_sport.tool_timer);
    }
  lv_obj_clean(g_sport.root);
  memset(g_sport.setting_buttons, 0, sizeof(g_sport.setting_buttons));
  memset(g_sport.accent_dots, 0, sizeof(g_sport.accent_dots));
  g_sport.auto_button = NULL;
  g_sport.compass_face = NULL;
  g_sport.page = page;
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  velaride_night_ui_apply(page == SPORT_PAGE_INFO &&
                          g_sport.info == VELARIDE_INFO_LIGHT);
#endif
  g_sport.clock = NULL;
  g_sport.body = NULL;
  g_sport.time_label = NULL;
  g_sport.metric_label = NULL;
  g_sport.status_label = NULL;
  g_sport.goal_label = NULL;
  g_sport.progress = NULL;
  g_sport.chip = NULL;
  memset(g_sport.row_name, 0, sizeof(g_sport.row_name));
  memset(g_sport.row_value, 0, sizeof(g_sport.row_value));
  g_sport.hold_btn = NULL;
  g_sport.hold_arc = NULL;
  g_sport.heading_label = NULL;
  g_sport.heading_name = NULL;
  g_sport.tape = NULL;
  g_sport.scroll = NULL;
  g_sport.review_card = NULL;
  g_sport.review_button = NULL;
  lv_obj_set_style_bg_color(g_sport.root, lv_color_hex(BG_COLOR), 0);
  lv_obj_set_style_bg_grad_dir(g_sport.root, LV_GRAD_DIR_NONE, 0);
}

static void clear_page(enum sport_page_e page, const char *title)
{
  clear_page_state(page);
  lv_obj_align(lv_obj_get_child(button(LV_SYMBOL_LEFT " 返回", BTN_BACK,
                                       18, 14, 96, 40, back_cb, 0), 0),
               LV_ALIGN_LEFT_MID, 14, 0);
  g_sport.clock = label(g_sport.root, "--:--", TEXT_COLOR,
                        &velaride_num_24, 272, 24, 92);
  label(g_sport.root, title, ACCENT_COLOR, &velaride_font_cn_28,
        18, 62, SPORT_W - 36);
  update_clock();
}

static lv_obj_t *scroll_body(const char *text, int y, int height)
{
  lv_obj_t *card = lv_obj_create(g_sport.root);
  lv_obj_t *body;

  lv_obj_set_pos(card, 18, y);
  lv_obj_set_size(card, SPORT_W - 36, height);
  lv_obj_set_style_bg_color(card, lv_color_hex(VR_TILE), 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_pad_all(card, 14, 0);
  lv_obj_set_scroll_dir(card, LV_DIR_VER);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLL_CHAIN);
  body = label(card, text, TEXT_COLOR, &velaride_font_cn_20,
                0, 0, SPORT_W - 64);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_line_space(body, 7, 0);
  return body;
}

static void reveal(void)
{
  lv_obj_remove_flag(g_sport.root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_sport.root);
}

static void leave(void)
{
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop_voice();
#endif
  velaride_compass_enable(false);
  lv_timer_pause(g_sport.countdown_timer);
  g_sport.page = SPORT_PAGE_HIDDEN;
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  velaride_night_ui_apply(false);
#endif
  lv_obj_add_flag(g_sport.root, LV_OBJ_FLAG_HIDDEN);
  if (g_sport.exit_cb != NULL)
    {
      g_sport.exit_cb(g_sport.exit_arg);
    }
}

/* Ride flow, following the design draft: type -> goal -> 3-2-1 -> three
 * swipeable ride views (controls / data / heading) -> summary. Ride pages
 * have no top bar; the hardware key goes back.
 */

static void ride_time_text(char *text, size_t size, uint32_t seconds)
{
  if (seconds >= 3600)
    {
      snprintf(text, size, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
               seconds / 3600, seconds / 60 % 60, seconds % 60);
    }
  else
    {
      snprintf(text, size, "%" PRIu32 ":%02" PRIu32,
               seconds / 60, seconds % 60);
    }
}

static lv_obj_t *left_label(lv_obj_t *parent, const char *text,
                            uint32_t color, const lv_font_t *font,
                            int x, int y)
{
  lv_obj_t *obj = lv_label_create(parent);

  lv_label_set_text(obj, text);
  lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(obj, font, 0);
  lv_obj_set_pos(obj, x, y);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

static lv_obj_t *circle(lv_obj_t *parent, int x, int y, int size,
                        uint32_t color, lv_opa_t opa)
{
  lv_obj_t *obj = lv_obj_create(parent);

  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, size, size);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(obj, opa, 0);
  lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}

/* Clean page with no top bar, optionally tinted towards the top. */

static void clear_bare(enum sport_page_e page, uint32_t tint)
{
  clear_page_state(page);
  lv_obj_set_style_bg_color(g_sport.root, lv_color_hex(tint ? tint : BG_COLOR),
                            0);
  lv_obj_set_style_bg_grad_color(g_sport.root, lv_color_hex(BG_COLOR), 0);
  lv_obj_set_style_bg_grad_dir(g_sport.root, tint ? LV_GRAD_DIR_VER :
                                                    LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_main_stop(g_sport.root, 0, 0);
  lv_obj_set_style_bg_grad_stop(g_sport.root, 170, 0);
}

/* Full-width pill with a 28 px caption. */

static lv_obj_t *pill(const char *text, int role, int y, lv_event_cb_t cb,
                      intptr_t value)
{
  return button(text, role, 30, y, SPORT_W - 60, 62, cb, value);
}

/* Row with a round icon, title and note; the caption stays child 0. */

static lv_obj_t *icon_row(lv_obj_t *parent, int y, enum velaride_icon_e icon,
                          bool accent, const char *title, const char *note,
                          lv_event_cb_t cb, intptr_t value)
{
  lv_obj_t *row = button(title, BTN_TILE, 18, y, SPORT_W - 36, 86, cb, value);
  lv_obj_t *dot;
  lv_obj_t *small;

  lv_obj_set_parent(row, parent);
  lv_obj_set_style_radius(row, 30, 0);
  lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_TOP_LEFT, 84, 14);
  dot = circle(row, 16, 19, 48, accent ? ACCENT_COLOR : VR_TILE2,
               LV_OPA_COVER);
  velaride_icon_create(dot, icon, 11, 11, 26,
                       accent ? velaride_accent()->ink : TEXT_COLOR);
  small = lv_label_create(row);
  lv_label_set_text(small, note);
  lv_obj_set_style_text_font(small, &velaride_font_cn_20, 0);
  lv_obj_set_style_text_color(small, lv_color_hex(MUTED_COLOR), 0);
  lv_obj_align(small, LV_ALIGN_BOTTOM_LEFT, 84, -14);
  return row;
}

static void finish_ride(void)
{
  lv_timer_pause(g_sport.hold_timer);
  g_sport.persist_result =
    velaride_ride_session_finish(&g_sport.latest_record);
  g_sport.session_started = false;
  g_sport.has_record = true;
  g_sport.review_ai = false;
  g_sport.locked = false;
  show_review();
}

static void finish_cb(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);

  if (action == 1)
    {
      finish_ride();
    }
  else
    {
      show_active();
    }
}

static void show_confirm(void)
{
  struct velaride_ride_snapshot_s ride;
  char elapsed[24];

  velaride_ride_session_get(&ride);
  ride_time_text(elapsed, sizeof(elapsed), ride.elapsed_s);
  clear_page(SPORT_PAGE_CONFIRM, "结束骑行？");
  g_sport.time_label = label(g_sport.root, elapsed, ACCENT_COLOR,
                             &velaride_num_60, 0, 128, SPORT_W);
  label(g_sport.root, ride.paused ? "当前已暂停" : "确认期间仍在计时",
        MUTED_COLOR, &velaride_font_cn_20, 18, 208, SPORT_W - 36);
  pill("继续骑行", BTN_PRIMARY, 268, finish_cb, 2);
  pill("结束并保存", BTN_DANGER, 344, finish_cb, 1);
}

/* --- Ride views ---------------------------------------------------------- */

static void ride_dots(void)
{
  int x = SPORT_W / 2 - (18 + 7 * (RIDE_VIEW_COUNT - 1) +
                         8 * (RIDE_VIEW_COUNT - 1)) / 2;
  int i;

  for (i = 0; i < RIDE_VIEW_COUNT; i++)
    {
      bool on = i == g_sport.ride_view;
      lv_obj_t *dot = circle(g_sport.root, x, 418, 7, on ? ACCENT_COLOR :
                             0x3a3a3e, LV_OPA_COVER);

      lv_obj_set_size(dot, on ? 18 : 7, 7);
      x += (on ? 18 : 7) + 8;
    }
}

static void ride_header(void)
{
  left_label(g_sport.root, sport_name(), ACCENT_COLOR, &velaride_font_cn_20,
             44, 34);
  g_sport.chip = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.chip);
  lv_obj_set_size(g_sport.chip, LV_SIZE_CONTENT, 30);
  lv_obj_set_style_pad_hor(g_sport.chip, 12, 0);
  lv_obj_set_style_radius(g_sport.chip, 15, 0);
  lv_obj_set_style_bg_opa(g_sport.chip, LV_OPA_20, 0);
  lv_obj_align(g_sport.chip, LV_ALIGN_TOP_RIGHT, -40, 30);
  lv_obj_remove_flag(g_sport.chip, LV_OBJ_FLAG_CLICKABLE |
                                   LV_OBJ_FLAG_SCROLLABLE);
  g_sport.status_label = lv_label_create(g_sport.chip);
  lv_obj_set_style_text_font(g_sport.status_label, &velaride_font_cn_20, 0);
  lv_obj_center(g_sport.status_label);
}

static void update_chip(const struct velaride_ride_snapshot_s *ride)
{
  uint32_t color = ride->paused || !ride->sensor_ready ? VR_WARN :
                   ride->motion == VELARIDE_MOTION_MOVING ? VR_OK : VR_INK2;
  const char *text = ride->paused ? "已暂停" :
                     !ride->sensor_ready ? "仅计时" :
                     ride->motion == VELARIDE_MOTION_MOVING ? "运动中" :
                     ride->motion == VELARIDE_MOTION_STILL ? "静止" :
                                                             "识别中";

  if (g_sport.chip == NULL)
    {
      return;
    }

  lv_obj_set_style_bg_color(g_sport.chip, lv_color_hex(color), 0);
  lv_obj_set_style_text_color(g_sport.status_label, lv_color_hex(color), 0);
  if (strcmp(lv_label_get_text(g_sport.status_label), text) != 0)
    {
      lv_label_set_text(g_sport.status_label, text);
    }
}

static void set_label(lv_obj_t *obj, const char *text)
{
  if (obj != NULL && strcmp(lv_label_get_text(obj), text) != 0)
    {
      lv_label_set_text(obj, text);
    }
}

static void build_data_view(void)
{
  static const char *names[RIDE_ROWS] = {"运动", "静止", "目标剩余", "冲击"};
  int i;

  ride_header();
  g_sport.time_label = label(g_sport.root, "0:00", ACCENT_COLOR,
                             &velaride_num_92, 0, 74, SPORT_W);
  for (i = 0; i < RIDE_ROWS; i++)
    {
      int y = 196 + i * 50;
      lv_obj_t *line = lv_obj_create(g_sport.root);

      lv_obj_remove_style_all(line);
      lv_obj_set_pos(line, 34, y);
      lv_obj_set_size(line, SPORT_W - 68, 1);
      lv_obj_set_style_bg_color(line, lv_color_hex(VR_LINE), 0);
      lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
      g_sport.row_name[i] = left_label(g_sport.root, names[i], MUTED_COLOR,
                                       &velaride_font_cn_20, 38, y + 14);
      g_sport.row_value[i] = label(g_sport.root, "", TEXT_COLOR,
                                   &velaride_num_30, 196, y + 8, 156);
      lv_obj_set_style_text_align(g_sport.row_value[i],
                                  LV_TEXT_ALIGN_RIGHT, 0);
    }
}

static void hold_event_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_PRESSED)
    {
      g_sport.hold_start = lv_tick_get();
      lv_timer_reset(g_sport.hold_timer);
      lv_timer_resume(g_sport.hold_timer);
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      lv_timer_pause(g_sport.hold_timer);
      if (g_sport.hold_arc != NULL)
        {
          lv_arc_set_value(g_sport.hold_arc, 0);
        }
    }
}

static void hold_timer_cb(lv_timer_t *timer)
{
  uint32_t held = lv_tick_elaps(g_sport.hold_start);

  if (g_sport.page != SPORT_PAGE_ACTIVE || g_sport.hold_arc == NULL)
    {
      lv_timer_pause(timer);
      return;
    }

  if (held >= HOLD_END_MS)
    {
      finish_ride();
      return;
    }

  lv_arc_set_value(g_sport.hold_arc, (int32_t)(held * 100 / HOLD_END_MS));
}

static void lock_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  g_sport.locked = true;
  g_sport.ride_view = RIDE_VIEW_DATA;
  show_active();
}

static lv_obj_t *control(int x, int y, uint32_t fg, uint32_t bg,
                         enum velaride_icon_e icon, const char *glyph,
                         const char *caption, lv_event_cb_t cb)
{
  lv_obj_t *obj = lv_button_create(g_sport.root);

  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, 104, 104);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_transform_scale(obj, 236, LV_STATE_PRESSED);
  lv_obj_set_style_transform_pivot_x(obj, 52, 0);
  lv_obj_set_style_transform_pivot_y(obj, 52, 0);
  if (cb != NULL)
    {
      lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
    }

  if (glyph != NULL)
    {
      lv_obj_t *mark = lv_label_create(obj);

      lv_label_set_text(mark, glyph);
      lv_obj_set_style_text_font(mark, &lv_font_montserrat_48, 0);
      lv_obj_set_style_text_color(mark, lv_color_hex(fg), 0);
      lv_obj_center(mark);
    }
  else
    {
      velaride_icon_create(obj, icon, 32, 32, 40, fg);
    }

  label(g_sport.root, caption, MUTED_COLOR, &velaride_font_cn_20,
        x - 28, y + 112, 160);
  return obj;
}

static void build_control_view(void)
{
  struct velaride_ride_snapshot_s ride;
  lv_obj_t *end;

  velaride_ride_session_get(&ride);
  left_label(g_sport.root, sport_name(), MUTED_COLOR, &velaride_font_cn_20,
             44, 34);
  g_sport.time_label = label(g_sport.root, "", ACCENT_COLOR,
                             &velaride_num_24, 236, 32, 116);
  lv_obj_set_style_text_align(g_sport.time_label, LV_TEXT_ALIGN_RIGHT, 0);
  control(60, 78, ride.paused ? VR_OK : VR_WARN,
          ride.paused ? 0x0b2914 : 0x2e1d02, VR_ICON_RIDE,
          ride.paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
          ride.paused ? "继续" : "暂停", pause_cb);

  end = control(226, 78, VR_DANGER, 0x2e0c0a, VR_ICON_RIDE, LV_SYMBOL_STOP,
                "按住结束", NULL);
  g_sport.hold_btn = end;
  lv_obj_add_event_cb(end, hold_event_cb, LV_EVENT_ALL, NULL);
  g_sport.hold_arc = lv_arc_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.hold_arc);
  lv_obj_set_size(g_sport.hold_arc, 116, 116);
  lv_obj_set_pos(g_sport.hold_arc, 220, 72);
  lv_arc_set_rotation(g_sport.hold_arc, 270);
  lv_arc_set_bg_angles(g_sport.hold_arc, 0, 360);
  lv_arc_set_range(g_sport.hold_arc, 0, 100);
  lv_arc_set_value(g_sport.hold_arc, 0);
  lv_obj_set_style_arc_width(g_sport.hold_arc, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_sport.hold_arc, lv_color_hex(VR_DANGER),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_sport.hold_arc, true, LV_PART_INDICATOR);
  lv_obj_remove_flag(g_sport.hold_arc, LV_OBJ_FLAG_CLICKABLE |
                                       LV_OBJ_FLAG_SCROLLABLE);

  control(143, 250, 0x6ec6ff, 0x0b2230, VR_ICON_SAFETY, NULL, "防误触锁",
          lock_cb);
}

static const char *cardinal(float heading)
{
  static const char *names[] =
    {"北", "东北", "东", "东南", "南", "西南", "西", "西北"};

  return names[((int)lroundf(heading / 45.0f) % 8 + 8) % 8];
}

static void tape_draw_cb(lv_event_t *event)
{
  lv_obj_t *obj = lv_event_get_target(event);
  lv_layer_t *layer = lv_event_get_layer(event);
  float heading = g_sport.heading;
  lv_draw_line_dsc_t line;
  lv_draw_label_dsc_t text;
  lv_area_t area;
  int first;
  int d;

  lv_obj_get_coords(obj, &area);
  lv_draw_line_dsc_init(&line);
  line.round_start = 1;
  line.round_end = 1;
  lv_draw_label_dsc_init(&text);
  text.font = &velaride_font_cn_20;
  text.align = LV_TEXT_ALIGN_CENTER;
  first = ((int)floorf((heading - 50) / 5)) * 5;
  for (d = first; d <= heading + 50; d += 5)
    {
      int x = area.x1 + SPORT_W / 2 + (int)lroundf((d - heading) * 4);
      bool major = d % 45 == 0;

      line.color = velaride_night_color(lv_color_hex(major ? VR_INK : 0x55555a));
      line.width = major ? 3 : 2;
      line.p1.x = x;
      line.p1.y = area.y1 + 8;
      line.p2.x = x;
      line.p2.y = area.y1 + (major ? 40 : d % 15 == 0 ? 28 : 20);
      lv_draw_line(layer, &line);
      if (major)
        {
          lv_area_t box = {x - 40, area.y1 + 52, x + 40, area.y1 + 76};

          text.text = cardinal((float)d);
          text.color = ((d % 360) + 360) % 360 == 0 ? vr_accent() :
                                                      lv_color_hex(VR_INK2);
          text.color = velaride_night_color(text.color);
          lv_draw_label(layer, &text, &box);
        }
    }

  line.color = velaride_night_color(vr_accent());
  line.width = 3;
  line.p1.x = area.x1 + SPORT_W / 2;
  line.p1.y = area.y1;
  line.p2.x = area.x1 + SPORT_W / 2;
  line.p2.y = area.y1 + 48;
  lv_draw_line(layer, &line);
}

static void build_heading_view(void)
{
  velaride_compass_enable(true);
  left_label(g_sport.root, "朝向", ACCENT_COLOR, &velaride_font_cn_20,
             44, 34);
  g_sport.time_label = label(g_sport.root, "", MUTED_COLOR,
                             &velaride_num_24, 236, 32, 116);
  lv_obj_set_style_text_align(g_sport.time_label, LV_TEXT_ALIGN_RIGHT, 0);
  g_sport.heading_label = label(g_sport.root, "--", TEXT_COLOR,
                                &velaride_num_92, 0, 86, SPORT_W);
  g_sport.heading_name = label(g_sport.root, "", TEXT_COLOR,
                               &velaride_font_cn_28, 0, 196, SPORT_W);
  g_sport.tape = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.tape);
  lv_obj_set_pos(g_sport.tape, 0, 262);
  lv_obj_set_size(g_sport.tape, SPORT_W, 84);
  lv_obj_remove_flag(g_sport.tape, LV_OBJ_FLAG_CLICKABLE |
                                   LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_sport.tape, tape_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_flag(g_sport.tape, LV_OBJ_FLAG_HIDDEN);
  g_sport.heading = -1000;
}

static void update_heading(void)
{
  struct velaride_compass_s compass;
  char text[16];

  velaride_compass_get(&compass);
  if (!compass.calibrated || !compass.heading_valid)
    {
      set_label(g_sport.heading_label, "--");
      set_label(g_sport.heading_name, !compass.calibrated ?
                "需先校准指南针" : "磁场异常，远离金属");
      lv_obj_add_flag(g_sport.tape, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  if (fabsf(compass.heading - g_sport.heading) < 1.0f)
    {
      return;
    }

  g_sport.heading = compass.heading;
  snprintf(text, sizeof(text), "%u°",
           (unsigned int)lroundf(compass.heading) % 360);
  set_label(g_sport.heading_label, text);
  set_label(g_sport.heading_name, cardinal(compass.heading));
  lv_obj_remove_flag(g_sport.tape, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(g_sport.tape);
}

static void lock_overlay(void)
{
  lv_obj_t *layer = lv_obj_create(g_sport.root);
  lv_obj_t *badge;

  lv_obj_remove_style_all(layer);
  lv_obj_set_size(layer, SPORT_W, SPORT_H);
  lv_obj_add_flag(layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE |
                            LV_OBJ_FLAG_GESTURE_BUBBLE);
  badge = lv_obj_create(layer);
  lv_obj_remove_style_all(badge);
  lv_obj_set_size(badge, 232, 36);
  lv_obj_align(badge, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_set_style_radius(badge, 18, 0);
  lv_obj_set_style_bg_color(badge, lv_color_hex(0x0b2230), 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  label(badge, "已锁定 · 按键解锁", 0x6ec6ff, &velaride_font_cn_20,
        0, 7, 232);
}

static void update_active(void)
{
  struct velaride_ride_snapshot_s ride;
  char text[32];

  velaride_ride_session_get(&ride);
  ride_time_text(text, sizeof(text), ride.elapsed_s);
  set_label(g_sport.time_label, text);
  if (g_sport.ride_view == RIDE_VIEW_HEADING)
    {
      update_heading();
      return;
    }

  if (g_sport.ride_view != RIDE_VIEW_DATA)
    {
      return;
    }

  update_chip(&ride);
  lv_obj_set_style_opa(g_sport.time_label, ride.paused ? LV_OPA_50 :
                                                         LV_OPA_COVER, 0);
  ride_time_text(text, sizeof(text), ride.moving_s);
  set_label(g_sport.row_value[0], text);
  ride_time_text(text, sizeof(text), ride.still_s);
  set_label(g_sport.row_value[1], text);
  if (ride.goal_s == 0)
    {
      set_label(g_sport.row_name[2], "暂停");
      snprintf(text, sizeof(text), "%u 次", ride.pause_count);
    }
  else if (ride.goal_reached)
    {
      set_label(g_sport.row_name[2], "目标");
      snprintf(text, sizeof(text), "已达成");
    }
  else
    {
      set_label(g_sport.row_name[2], "目标剩余");
      ride_time_text(text, sizeof(text), ride.goal_s - ride.elapsed_s);
    }

  set_label(g_sport.row_value[2], text);
  snprintf(text, sizeof(text), "%u 次", ride.impact_count);
  set_label(g_sport.row_value[3], text);
}

static void show_active(void)
{
  clear_bare(SPORT_PAGE_ACTIVE, 0);
  if (g_sport.ride_view == RIDE_VIEW_CONTROL)
    {
      build_control_view();
    }
  else if (g_sport.ride_view == RIDE_VIEW_HEADING)
    {
      build_heading_view();
    }
  else
    {
      build_data_view();
    }

  if (!g_sport.locked)
    {
      ride_dots();
    }

  update_active();
  if (g_sport.locked)
    {
      lock_overlay();
    }
}

static void ride_view_set(int view)
{
  view = LV_CLAMP(0, view, RIDE_VIEW_COUNT - 1);
  if (view != g_sport.ride_view)
    {
      g_sport.ride_view = view;
      show_active();
    }
}

static void pause_cb(lv_event_t *event)
{
  struct velaride_ride_snapshot_s ride;

  LV_UNUSED(event);
  velaride_ride_session_get(&ride);
  velaride_ride_session_set_paused(!ride.paused);
  show_active();
}

static void root_gesture_cb(lv_event_t *event)
{
  lv_indev_t *indev = lv_indev_active();
  lv_dir_t dir;

  LV_UNUSED(event);
  if (indev == NULL || g_sport.page != SPORT_PAGE_ACTIVE || g_sport.locked)
    {
      return;
    }

  dir = lv_indev_get_gesture_dir(indev);
  if (dir == LV_DIR_LEFT)
    {
      ride_view_set(g_sport.ride_view + 1);
    }
  else if (dir == LV_DIR_RIGHT)
    {
      ride_view_set(g_sport.ride_view - 1);
    }
}

/* --- Alerts and goal ------------------------------------------------------ */

static void alert_cb(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);

  velaride_ride_session_ack_alert();
  if (action == 1)
    {
      velaride_ride_session_set_paused(true);
    }

  g_sport.ride_view = RIDE_VIEW_DATA;
  show_active();
}

static void alert_page(enum sport_page_e page, uint32_t tint, uint32_t color,
                       enum velaride_icon_e icon, const char *glyph,
                       const char *title, const char *note)
{
  lv_obj_t *dot;

  clear_bare(page, tint);
  dot = circle(g_sport.root, SPORT_W / 2 - 42, 50, 84, color, LV_OPA_20);
  if (glyph != NULL)
    {
      lv_obj_t *mark = lv_label_create(dot);

      lv_label_set_text(mark, glyph);
      lv_obj_set_style_text_font(mark, &lv_font_montserrat_48, 0);
      lv_obj_set_style_text_color(mark, lv_color_hex(color), 0);
      lv_obj_center(mark);
    }
  else
    {
      velaride_icon_create(dot, icon, 20, 20, 44, color);
    }

  label(g_sport.root, title, TEXT_COLOR, &velaride_font_cn_28,
        18, 154, SPORT_W - 36);
  label(g_sport.root, note, MUTED_COLOR, &velaride_font_cn_20,
        30, 198, SPORT_W - 60);
}

static void show_alert(enum velaride_alert_e alert)
{
  g_sport.locked = false;
  g_sport.shown_alert = alert;
  if (alert == VELARIDE_ALERT_IMPACT)
    {
      alert_page(SPORT_PAGE_ALERT, 0x3b0d0a, VR_DANGER, VR_ICON_SAFETY, "!",
                 "检测到剧烈冲击", "你还好吗？");
      lv_obj_t *ok = pill("我没事", BTN_SECONDARY, 280, alert_cb, 0);
      lv_obj_set_style_bg_color(ok, lv_color_white(), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(ok, 0),
                                  lv_color_black(), 0);
      pill("暂停记录", BTN_DANGER, 356, alert_cb, 1);
    }
  else if (alert == VELARIDE_ALERT_STILL)
    {
      alert_page(SPORT_PAGE_ALERT, 0x2a2105, VR_WARN, VR_ICON_PULSE, NULL,
                 "持续静止", "已静止一段时间，要暂停记录吗？");
      pill("暂停记录", BTN_WARN, 280, alert_cb, 1);
      pill("继续骑行", BTN_SECONDARY, 356, alert_cb, 0);
    }
  else
    {
      alert_page(SPORT_PAGE_ALERT, 0x3a2405, VR_WARN, VR_ICON_MOON, NULL,
                 "该休息一下了", "已连续运动 45 分钟");
      pill("暂停休息", BTN_WARN, 280, alert_cb, 1);
      pill("继续骑行", BTN_SECONDARY, 356, alert_cb, 0);
    }
}

static void goal_done_cb(lv_event_t *event)
{
  if ((intptr_t)lv_event_get_user_data(event) == 1)
    {
      show_confirm();
    }
  else
    {
      g_sport.ride_view = RIDE_VIEW_DATA;
      show_active();
    }
}

static void show_goal_done(void)
{
  char text[64];
  lv_obj_t *dot;

  g_sport.goal_announced = true;
  clear_bare(SPORT_PAGE_GOAL_DONE, 0);
  dot = circle(g_sport.root, SPORT_W / 2 - 42, 50, 84, ACCENT_COLOR,
               LV_OPA_COVER);
  lv_obj_t *mark = lv_label_create(dot);
  lv_label_set_text(mark, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(mark, &lv_font_montserrat_48, 0);
  lv_obj_set_style_text_color(mark, lv_color_hex(velaride_accent()->ink), 0);
  lv_obj_center(mark);
  label(g_sport.root, "目标达成", TEXT_COLOR, &velaride_font_cn_28,
        18, 154, SPORT_W - 36);
  snprintf(text, sizeof(text), "%" PRIu32 " 分钟记录目标已完成",
           g_sport.goal_s / 60);
  label(g_sport.root, text, MUTED_COLOR, &velaride_font_cn_20,
        30, 198, SPORT_W - 60);
  pill("继续骑行", BTN_PRIMARY, 280, goal_done_cb, 0);
  pill("结束骑行", BTN_SECONDARY, 356, goal_done_cb, 1);
}

/* --- Start: type, goal, countdown ------------------------------------------ */

static void countdown_cb(lv_timer_t *timer)
{
  uint32_t elapsed = lv_tick_elaps(g_sport.countdown_start);
  char text[4];

  if (g_sport.page != SPORT_PAGE_COUNTDOWN)
    {
      lv_timer_pause(timer);
      return;
    }

  if (elapsed >= COUNTDOWN_MS)
    {
      lv_timer_pause(timer);
      velaride_ride_session_start_with_goal(g_sport.goal_s);
      g_sport.session_started = true;
      g_sport.goal_announced = false;
      g_sport.ride_view = RIDE_VIEW_DATA;
      lv_timer_reset(g_sport.session_timer);
      show_active();
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      velaride_sound_request(VR_SOUND_GO);
#endif
      return;
    }

  snprintf(text, sizeof(text), "%u",
           (unsigned)((COUNTDOWN_MS - elapsed + 999) / 1000));
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  {
    uint8_t count = (COUNTDOWN_MS - elapsed + 999) / 1000;
    if (count != g_sport.spoken_count)
      {
        g_sport.spoken_count = count;
        velaride_sound_request(count == 2 ? VR_SOUND_TWO : VR_SOUND_ONE);
      }
  }
#endif
  set_label(g_sport.time_label, text);
  lv_arc_set_value(g_sport.hold_arc,
                   (int32_t)((COUNTDOWN_MS - elapsed) * 100 / COUNTDOWN_MS));
}

static void start_cb(lv_event_t *event)
{
  int sensor;

  LV_UNUSED(event);
  sensor = velaride_ride_session_probe_sensor();
  clear_bare(SPORT_PAGE_COUNTDOWN, 0);
  g_sport.hold_arc = lv_arc_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.hold_arc);
  lv_obj_set_size(g_sport.hold_arc, 270, 270);
  lv_obj_set_pos(g_sport.hold_arc, 60, 40);
  lv_arc_set_rotation(g_sport.hold_arc, 270);
  lv_arc_set_bg_angles(g_sport.hold_arc, 0, 360);
  lv_arc_set_range(g_sport.hold_arc, 0, 100);
  lv_arc_set_value(g_sport.hold_arc, 100);
  lv_obj_set_style_arc_width(g_sport.hold_arc, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_color(g_sport.hold_arc, lv_color_hex(ACCENT_COLOR),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_opa(g_sport.hold_arc, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_width(g_sport.hold_arc, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_sport.hold_arc, lv_color_hex(ACCENT_COLOR),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_sport.hold_arc, true, LV_PART_INDICATOR);
  lv_obj_remove_flag(g_sport.hold_arc, LV_OBJ_FLAG_CLICKABLE |
                                       LV_OBJ_FLAG_SCROLLABLE);
  g_sport.time_label = label(g_sport.root, "3", ACCENT_COLOR,
                             &velaride_num_136, 0,
                             175 - velaride_num_136.line_height / 2,
                             SPORT_W);
  label(g_sport.root, sensor == 0 ? "运动检测已就绪" :
        "运动检测不可用，将仅计时", sensor == 0 ? MUTED_COLOR : VR_WARN,
        &velaride_font_cn_20, 18, 326, SPORT_W - 36);
  button("取消", BTN_SECONDARY, 125, 366, 140, 50, back_cb, 0);
  g_sport.countdown_start = lv_tick_get();
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  g_sport.spoken_count = 3;
  velaride_sound_request(VR_SOUND_THREE);
#endif
  lv_timer_reset(g_sport.countdown_timer);
  lv_timer_resume(g_sport.countdown_timer);
}

static void goal_set_cb(lv_event_t *event)
{
  intptr_t value = (intptr_t)lv_event_get_user_data(event);

  if (value == GOAL_LESS || value == GOAL_MORE)
    {
      int32_t minutes = (int32_t)(g_sport.goal_s / 60) +
                        (value == GOAL_MORE ? GOAL_STEP_MIN : -GOAL_STEP_MIN);

      g_sport.goal_s = (uint32_t)LV_CLAMP(0, minutes, GOAL_MAX_MIN) * 60;
    }
  else
    {
      g_sport.goal_s = (uint32_t)value * 60;
    }

  show_goal();
}

static void type_cb(lv_event_t *event)
{
  g_sport.sport_type = (uint8_t)(intptr_t)lv_event_get_user_data(event);
  show_goal();
}

static void show_select(void)
{
  struct velaride_ride_record_s latest;
  char note[48];

  clear_page(SPORT_PAGE_SELECT, "骑行");
  if (velaride_ride_session_load_latest(&latest) == 0)
    {
      char time[16];

      ride_time_text(time, sizeof(time), latest.elapsed_s);
      snprintf(note, sizeof(note), "上次 %s", time);
    }
  else
    {
      snprintf(note, sizeof(note), "记录运动与提醒");
    }

  icon_row(g_sport.root, 108, VR_ICON_RIDE, true, "户外骑行", note,
           type_cb, 0);
  icon_row(g_sport.root, 204, VR_ICON_HOME, false, "室内骑行",
           "不记录朝向", type_cb, 1);
  icon_row(g_sport.root, 300, VR_ICON_PULSE, false, "自由训练",
           "只记录运动与静止", type_cb, 2);
}

static void show_goal(void)
{
  static const uint8_t presets[] = {0, 15, 30, 60};
  static const char *names[] = {"不限", "15", "30", "60"};
  char text[12];
  unsigned int i;

  clear_page(SPORT_PAGE_GOAL, sport_name());
  button("-", BTN_SECONDARY, 30, 128, 64, 64, goal_set_cb, GOAL_LESS);
  button("+", BTN_SECONDARY, SPORT_W - 94, 128, 64, 64, goal_set_cb,
         GOAL_MORE);
  if (g_sport.goal_s == 0)
    {
      label(g_sport.root, "不限时", TEXT_COLOR, &velaride_font_cn_28,
            94, 138, SPORT_W - 188);
      label(g_sport.root, "按你的节奏骑", MUTED_COLOR, &velaride_font_cn_20,
            94, 186, SPORT_W - 188);
    }
  else
    {
      snprintf(text, sizeof(text), "%" PRIu32, g_sport.goal_s / 60);
      label(g_sport.root, text, TEXT_COLOR, &velaride_num_92,
            94, 106, SPORT_W - 188);
      label(g_sport.root, "分钟目标", MUTED_COLOR, &velaride_font_cn_20,
            94, 206, SPORT_W - 188);
    }

  for (i = 0; i < sizeof(presets); i++)
    {
      bool on = g_sport.goal_s == presets[i] * 60u;

      button(names[i], on ? BTN_PRIMARY : BTN_TILE, 30 + i * 84, 250, 78, 52,
             goal_set_cb, presets[i]);
    }

  lv_obj_t *go = pill("开始", BTN_PRIMARY, 330, start_cb, 0);
  LV_UNUSED(go);
}

/* --- Summary ---------------------------------------------------------------- */

static void review_done_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  leave();
}

static void retry_save_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  g_sport.persist_result =
    velaride_ride_session_save_record(&g_sport.latest_record);
  show_review();
}

static void update_review_body(void)
{
  char text[512];

  if (g_sport.body == NULL)
    {
      return;
    }

  if (!g_sport.has_record)
    {
      snprintf(text, sizeof(text),
               "还没有骑行记录\n开始一次骑行，结束后就能在这里查看结果。");
    }
  else if (g_sport.persist_result != 0)
    {
      snprintf(text, sizeof(text),
               "请先保存本次记录，再通过网页生成建议。");
    }
  else if (velaride_ride_session_load_review(text, sizeof(text)) < 0)
    {
      snprintf(text, sizeof(text), "本次建议尚未同步\n"
               "在电脑网关连接手表，读取记录后生成并回传。");
    }

  set_label(g_sport.body, text);
}

static void review_card_size_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  if (g_sport.review_button != NULL && g_sport.review_card != NULL)
    {
      lv_obj_align_to(g_sport.review_button, g_sport.review_card,
                      LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
    }
}

static void stat_cell(lv_obj_t *parent, int x, int y, const char *name,
                      const char *value)
{
  left_label(parent, name, MUTED_COLOR, &velaride_font_cn_20, x, y);
  left_label(parent, value, TEXT_COLOR, &velaride_num_30, x, y + 24);
}

static void show_review(void)
{
  const struct velaride_ride_record_s *rec = &g_sport.latest_record;
  lv_obj_t *scroll;
  lv_obj_t *card;
  char text[64];
  char value[24];
  int y;

  clear_bare(SPORT_PAGE_REVIEW, 0);
  scroll = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(scroll);
  lv_obj_set_size(scroll, SPORT_W, SPORT_H);
  lv_obj_set_style_pad_bottom(scroll, 40, 0);
  lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLL_MOMENTUM |
                          LV_OBJ_FLAG_SCROLL_ELASTIC);
  g_sport.scroll = scroll;

  if (!g_sport.has_record)
    {
      left_label(scroll, "骑行记录", ACCENT_COLOR, &velaride_font_cn_28, 40, 40);
      g_sport.body = left_label(scroll, "", TEXT_COLOR, &velaride_font_cn_20,
                                40, 96);
      lv_obj_set_width(g_sport.body, SPORT_W - 80);
      lv_label_set_long_mode(g_sport.body, LV_LABEL_LONG_WRAP);
      update_review_body();
      g_sport.review_button = button("完成", BTN_PRIMARY, 30, 340,
                                     SPORT_W - 60, 62, review_done_cb, 0);
      lv_obj_set_parent(g_sport.review_button, scroll);
      return;
    }

  snprintf(text, sizeof(text), "%s · %s", sport_name(),
           g_sport.persist_result == 0 ? "已保存" : "未保存");
  left_label(scroll, text, g_sport.persist_result == 0 ? ACCENT_COLOR :
             VR_DANGER, &velaride_font_cn_20, 40, 34);
  ride_time_text(value, sizeof(value), rec->elapsed_s);
  left_label(scroll, value, TEXT_COLOR, &velaride_num_60, 36, 62);

  y = 132;
  if (rec->start_epoch >= 1704067200 && rec->end_epoch >= rec->start_epoch)
    {
      struct tm start;
      struct tm end;
      time_t a = (time_t)rec->start_epoch;
      time_t b = (time_t)rec->end_epoch;

      localtime_r(&a, &start);
      localtime_r(&b, &end);
      snprintf(text, sizeof(text), "%d月%d日 %02d:%02d – %02d:%02d",
               start.tm_mon + 1, start.tm_mday, start.tm_hour, start.tm_min,
               end.tm_hour, end.tm_min);
      left_label(scroll, text, MUTED_COLOR, &velaride_font_cn_20, 40, y);
    }

  y += 40;
  ride_time_text(value, sizeof(value), rec->moving_s);
  stat_cell(scroll, 40, y, "运动", value);
  ride_time_text(value, sizeof(value), rec->still_s);
  stat_cell(scroll, 208, y, "静止", value);
  snprintf(value, sizeof(value), "%u", rec->pause_count);
  stat_cell(scroll, 40, y + 70, "暂停次数", value);
  snprintf(value, sizeof(value), "%u", rec->impact_count);
  stat_cell(scroll, 208, y + 70, "冲击次数", value);
  snprintf(value, sizeof(value), "%u", rec->rest_count);
  stat_cell(scroll, 40, y + 140, "休息提醒", value);
  snprintf(value, sizeof(value), "#%" PRIu32, rec->sequence);
  stat_cell(scroll, 208, y + 140, "记录编号", value);

  y += 218;
  if (rec->gps.time_ms > 0)
    {
      snprintf(text, sizeof(text), "GPS  %.2f km", rec->gps.distance_m / 1000.0);
      left_label(scroll, text, ACCENT_COLOR, &velaride_font_cn_20, 40, y);
      snprintf(text, sizeof(text), "平均 %.2f / 最高 %.2f km/h",
               rec->gps.avg_speed_centi / 100.0,
               rec->gps.max_speed_centi / 100.0);
      left_label(scroll, text, TEXT_COLOR, &velaride_font_cn_20, 40, y + 30);
      snprintf(text, sizeof(text), "GPS 采样时长 %lu s",
               (unsigned long)(rec->gps.time_ms / 1000));
      left_label(scroll, text, MUTED_COLOR, &velaride_font_cn_20, 40, y + 60);
      y += 100;
    }
  else
    {
      left_label(scroll, "GPS  --", MUTED_COLOR,
                 &velaride_font_cn_20, 40, y);
      y += 40;
    }

  card = lv_obj_create(scroll);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, 22, y);
  lv_obj_set_size(card, SPORT_W - 44, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(card, 22, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(VR_TILE), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_set_style_pad_row(card, 6, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  left_label(card, "AI 建议", ACCENT_COLOR, &velaride_font_cn_20, 0, 0);
  g_sport.body = left_label(card, "", TEXT_COLOR, &velaride_font_cn_20, 0, 0);
  lv_obj_set_width(g_sport.body, SPORT_W - 76);
  lv_label_set_long_mode(g_sport.body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(g_sport.body, 6, 0);
  g_sport.review_card = card;
  update_review_body();

  /* The button follows the card, whose height depends on the text. */

  g_sport.review_button = button(g_sport.persist_result == 0 ? "完成" :
                                 "重试保存", BTN_PRIMARY, 30, 0,
                                 SPORT_W - 60, 62,
                                 g_sport.persist_result == 0 ?
                                 review_done_cb : retry_save_cb, 0);
  lv_obj_set_parent(g_sport.review_button, scroll);
  lv_obj_align_to(g_sport.review_button, card, LV_ALIGN_OUT_BOTTOM_MID, 0, 16);
  lv_obj_add_event_cb(card, review_card_size_cb, LV_EVENT_SIZE_CHANGED, NULL);
  if (g_sport.review_ai)
    {
      lv_obj_update_layout(scroll);
      lv_obj_scroll_to_view(card, LV_ANIM_OFF);
    }
}

/* --- Tool pages: compass, night light, stopwatch, level ------------------ */

#define DIAL_X 195
#define DIAL_Y 248
#define DIAL_R 112
#define LEVEL_X 195
#define LEVEL_Y 250
#define LEVEL_R 120
#define LEVEL_BUBBLE 40
#define LEVEL_MAX_DEG 30.0f
#define SW_LAPS 3

static void dial_line(lv_layer_t *layer, float deg, int r1, int r2,
                      int width, lv_color_t color)
{
  float a = deg * 0.0174532925f;
  lv_draw_line_dsc_t line;

  lv_draw_line_dsc_init(&line);
  line.color = velaride_night_color(color);
  line.width = width;
  line.round_start = 1;
  line.round_end = 1;
  line.p1.x = DIAL_X + sinf(a) * r1;
  line.p1.y = DIAL_Y - cosf(a) * r1;
  line.p2.x = DIAL_X + sinf(a) * r2;
  line.p2.y = DIAL_Y - cosf(a) * r2;
  lv_draw_line(layer, &line);
}

/* The dial turns so that north points north; the marker at the top shows
 * the direction the watch faces. Without a trusted heading it stays still
 * and dimmed.
 */

static void compass_draw_cb(lv_event_t *event)
{
  static const char *names[] = {"北", "东", "南", "西"};
  lv_layer_t *layer = lv_event_get_layer(event);
  struct velaride_compass_s compass;
  lv_draw_label_dsc_t text;
  bool live;
  float turn;
  int i;

  velaride_compass_get(&compass);
  live = compass.calibrated && compass.heading_valid && !compass.calibrating;
  turn = live ? -compass.heading : 0;
  for (i = 0; i < 72; i++)
    {
      bool major = i % 6 == 0;

      dial_line(layer, turn + i * 5, DIAL_R, DIAL_R - (major ? 16 : 8),
                major ? 3 : 2,
                i == 0 && live ? vr_accent() :
                lv_color_hex(!live ? 0x3a3a3e : major ? VR_INK : 0x4a4a4f));
    }

  lv_draw_label_dsc_init(&text);
  text.font = &velaride_font_cn_20;
  text.align = LV_TEXT_ALIGN_CENTER;
  for (i = 0; i < 4; i++)
    {
      float a = (turn + i * 90) * 0.0174532925f;
      int x = DIAL_X + (int)(sinf(a) * (DIAL_R - 36));
      int y = DIAL_Y - (int)(cosf(a) * (DIAL_R - 36));
      lv_area_t box = {x - 20, y - 12, x + 20, y + 12};

      text.text = names[i];
      text.color = !live ? lv_color_hex(VR_INK3) :
                   i == 0 ? vr_accent() : lv_color_hex(VR_INK);
      text.color = velaride_night_color(text.color);
      lv_draw_label(layer, &text, &box);
    }

  /* Fixed marker above the dial. */

  dial_line(layer, 0, DIAL_R + 14, DIAL_R + 2, 4, vr_accent());
}

static void compass_action_cb(lv_event_t *event)
{
  uintptr_t action = (uintptr_t)lv_event_get_user_data(event);

  if (action == 0)
    {
      velaride_compass_calibrate();
    }
  else if (action == 1)
    {
      velaride_compass_finish();
    }
  else
    {
      velaride_compass_cancel();
    }

  update_info();
}

static void build_compass(void)
{
  velaride_compass_enable(true);
  g_sport.compass_face = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.compass_face);
  lv_obj_set_pos(g_sport.compass_face, DIAL_X - DIAL_R - 16,
                 DIAL_Y - DIAL_R - 16);
  lv_obj_set_size(g_sport.compass_face, (DIAL_R + 16) * 2,
                  (DIAL_R + 16) * 2);
  lv_obj_remove_flag(g_sport.compass_face, LV_OBJ_FLAG_SCROLLABLE |
                                           LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_sport.compass_face, compass_draw_cb,
                      LV_EVENT_DRAW_MAIN, NULL);

  /* Calibration progress around the dial. */

  g_sport.hold_arc = lv_arc_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.hold_arc);
  lv_obj_set_size(g_sport.hold_arc, (DIAL_R + 26) * 2, (DIAL_R + 26) * 2);
  lv_obj_set_pos(g_sport.hold_arc, DIAL_X - DIAL_R - 26, DIAL_Y - DIAL_R - 26);
  lv_arc_set_rotation(g_sport.hold_arc, 270);
  lv_arc_set_bg_angles(g_sport.hold_arc, 0, 360);
  lv_arc_set_range(g_sport.hold_arc, 0, 80);
  lv_arc_set_value(g_sport.hold_arc, 0);
  lv_obj_set_style_arc_width(g_sport.hold_arc, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(g_sport.hold_arc, lv_color_hex(ACCENT_COLOR),
                             LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(g_sport.hold_arc, true, LV_PART_INDICATOR);
  lv_obj_remove_flag(g_sport.hold_arc, LV_OBJ_FLAG_CLICKABLE |
                                       LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_sport.hold_arc, LV_OBJ_FLAG_HIDDEN);

  g_sport.heading_label = label(g_sport.root, "--", TEXT_COLOR,
                                &velaride_num_44, DIAL_X - 80,
                                DIAL_Y - 30, 160);
  g_sport.heading_name = label(g_sport.root, "", MUTED_COLOR,
                               &velaride_font_cn_20, DIAL_X - 80,
                               DIAL_Y + 18, 160);
  g_sport.body = label(g_sport.root, "", MUTED_COLOR, &velaride_font_cn_20,
                       18, 374, SPORT_W - 36);
  g_sport.setting_buttons[0] = button("校准", BTN_SECONDARY, 125, 398, 140,
                                      44, compass_action_cb, 0);
  g_sport.setting_buttons[1] = button("完成", BTN_PRIMARY, 30, 398, 160, 44,
                                      compass_action_cb, 1);
  g_sport.setting_buttons[2] = button("取消", BTN_SECONDARY, 200, 398, 160,
                                      44, compass_action_cb, 2);
  g_sport.heading = -1000;
}

static void show_hidden(lv_obj_t *obj, bool show)
{
  if (show)
    {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_compass(char *text, size_t size)
{
  struct velaride_compass_s compass;
  bool live;
  char deg[16];

  velaride_compass_get(&compass);
  live = compass.calibrated && compass.heading_valid && !compass.calibrating;
  show_hidden(g_sport.setting_buttons[0], !compass.calibrating);
  show_hidden(g_sport.setting_buttons[1], compass.calibrating);
  show_hidden(g_sport.setting_buttons[2], compass.calibrating);
  show_hidden(g_sport.hold_arc, compass.calibrating);
  if (compass.calibrating)
    {
      lv_arc_set_value(g_sport.hold_arc, LV_MIN(compass.collected, 80));
      snprintf(text, size, "%s", compass.calibration_error < 0 ?
               "转动不足，请继续" : "远离金属，各方向慢慢转动手表");
      snprintf(deg, sizeof(deg), "%u%%", LV_MIN(compass.collected, 80) *
                                         100 / 80);
      set_label(g_sport.heading_label, deg);
      set_label(g_sport.heading_name, "已采集");
    }
  else if (!compass.valid)
    {
      snprintf(text, size, "地磁采样暂不可用，请稍候");
      set_label(g_sport.heading_label, "--");
      set_label(g_sport.heading_name, "");
    }
  else if (!compass.calibrated)
    {
      snprintf(text, size, "首次使用请先校准");
      set_label(g_sport.heading_label, "--");
      set_label(g_sport.heading_name, "未校准");
    }
  else if (!compass.heading_valid)
    {
      snprintf(text, size, "磁场异常，请远离金属");
      set_label(g_sport.heading_label, "--");
      set_label(g_sport.heading_name, "");
    }
  else
    {
      snprintf(text, size, "%s", compass.save_error < 0 ?
               "校准未保存，请重新校准" : "请平放使用");
      snprintf(deg, sizeof(deg), "%u°",
               (unsigned int)lroundf(compass.heading) % 360);
      set_label(g_sport.heading_label, deg);
      set_label(g_sport.heading_name, cardinal(compass.heading));
    }

  if (!live || fabsf(compass.heading - g_sport.heading) >= 1.0f)
    {
      g_sport.heading = live ? compass.heading : -1000;
      lv_obj_invalidate(g_sport.compass_face);
    }
}

/* Night light: the screen itself is the light. Flashing stays at 2 Hz,
 * below the rate that is a concern for photosensitive viewers.
 */

static const struct
{
  const char *name;
  uint32_t color;
  uint32_t ink;
  bool flash;
} g_lights[] =
{
  {"白光常亮", 0xffffff, 0x555555, false},
  {"红光闪烁", 0xff2a1a, 0xffffff, true},
  {"红光常亮", 0xff2a1a, 0xffffff, false},
};

#define LIGHT_COUNT (sizeof(g_lights) / sizeof(g_lights[0]))

static void light_apply(bool on)
{
  lv_obj_set_style_bg_color(g_sport.tape, on ?
    lv_color_hex(g_lights[g_sport.light_mode].color) : lv_color_black(), 0);
  lv_obj_set_style_text_color(g_sport.body,
    lv_color_hex(on ? g_lights[g_sport.light_mode].ink : VR_INK2), 0);
  lv_obj_set_style_text_color(g_sport.heading_name,
    lv_color_hex(on ? g_lights[g_sport.light_mode].ink : VR_INK2), 0);
}

static void light_tap_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  g_sport.light_mode = (g_sport.light_mode + 1) % LIGHT_COUNT;
  g_sport.light_on = true;
  g_sport.tool_tick = lv_tick_get();
  set_label(g_sport.body, g_lights[g_sport.light_mode].name);
  light_apply(true);
}

static void build_light(void)
{
  /* Covers the top bar; the hardware key leaves. */

  g_sport.tape = lv_obj_create(g_sport.root);
  lv_obj_remove_style_all(g_sport.tape);
  lv_obj_set_size(g_sport.tape, SPORT_W, SPORT_H);
  lv_obj_set_style_bg_opa(g_sport.tape, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_sport.tape, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g_sport.tape, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(g_sport.tape, light_tap_cb, LV_EVENT_CLICKED, NULL);
  g_sport.body = label(g_sport.tape, g_lights[g_sport.light_mode].name,
                       VR_INK2, &velaride_font_cn_28, 0, 360, SPORT_W);
  g_sport.heading_name = label(g_sport.tape, "轻点切换 · 按键退出", VR_INK2,
                               &velaride_font_cn_20, 0, 400, SPORT_W);
  g_sport.light_on = true;
  g_sport.tool_tick = lv_tick_get();
  light_apply(true);
}

static void light_tick(void)
{
  if (!g_lights[g_sport.light_mode].flash ||
      lv_tick_elaps(g_sport.tool_tick) < 250)
    {
      return;
    }

  g_sport.tool_tick = lv_tick_get();
  g_sport.light_on = !g_sport.light_on;
  light_apply(g_sport.light_on);
}

/* Stopwatch. Keeps running while the page is closed. */

static uint32_t sw_elapsed(void)
{
  return g_sport.sw_acc_ms +
         (g_sport.sw_running ? lv_tick_elaps(g_sport.sw_start) : 0);
}

static void sw_format(uint32_t ms, char *main, size_t main_size,
                      char *cs, size_t cs_size)
{
  uint32_t s = ms / 1000;

  if (s >= 3600)
    {
      snprintf(main, main_size, "%" PRIu32 ":%02" PRIu32 ":%02" PRIu32,
               s / 3600, s / 60 % 60, s % 60);
    }
  else
    {
      snprintf(main, main_size, "%02" PRIu32 ":%02" PRIu32, s / 60, s % 60);
    }

  snprintf(cs, cs_size, ".%02" PRIu32, ms / 10 % 100);
}

static void sw_buttons(void)
{
  lv_obj_t *left = g_sport.setting_buttons[0];
  lv_obj_t *right = g_sport.setting_buttons[1];
  bool running = g_sport.sw_running;

  lv_label_set_text(lv_obj_get_child(left, 0), running ? "计圈" : "复位");
  lv_label_set_text(lv_obj_get_child(right, 0), running ? "停止" : "开始");
  lv_obj_set_style_bg_color(right, lv_color_hex(running ? 0x2e0c0a :
                                                          0x0b2914), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(right, 0),
                              lv_color_hex(running ? VR_DANGER : VR_OK), 0);
}

static void sw_laps(void)
{
  char main[16];
  char cs[8];
  char text[40];
  int i;

  for (i = 0; i < SW_LAPS; i++)
    {
      int n = g_sport.sw_lap_count - i;

      if (n <= 0)
        {
          set_label(g_sport.row_name[i], "");
          set_label(g_sport.row_value[i], "");
          continue;
        }

      snprintf(text, sizeof(text), "第 %d 圈", n);
      set_label(g_sport.row_name[i], text);
      sw_format(g_sport.sw_laps[i], main, sizeof(main), cs, sizeof(cs));
      snprintf(text, sizeof(text), "%s%s", main, cs);
      set_label(g_sport.row_value[i], text);
    }
}

static void sw_left_cb(lv_event_t *event)
{
  int i;

  LV_UNUSED(event);
  if (g_sport.sw_running)
    {
      for (i = SW_LAPS - 1; i > 0; i--)
        {
          g_sport.sw_laps[i] = g_sport.sw_laps[i - 1];
        }

      g_sport.sw_laps[0] = sw_elapsed();
      g_sport.sw_lap_count++;
    }
  else
    {
      g_sport.sw_acc_ms = 0;
      g_sport.sw_lap_count = 0;
    }

  sw_laps();
}

static void sw_right_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  if (g_sport.sw_running)
    {
      g_sport.sw_acc_ms = sw_elapsed();
      g_sport.sw_running = false;
    }
  else
    {
      g_sport.sw_start = lv_tick_get();
      g_sport.sw_running = true;
    }

  sw_buttons();
}

static void sw_tick(void)
{
  char main[16];
  char cs[8];

  sw_format(sw_elapsed(), main, sizeof(main), cs, sizeof(cs));
  set_label(g_sport.time_label, main);
  set_label(g_sport.heading_name, cs);
  lv_obj_align_to(g_sport.heading_name, g_sport.time_label,
                  LV_ALIGN_OUT_RIGHT_BOTTOM, 2, -12);
}

static void build_stopwatch(void)
{
  int i;

  g_sport.time_label = left_label(g_sport.root, "00:00", TEXT_COLOR,
                                  &velaride_num_92, 0, 0);
  lv_obj_align(g_sport.time_label, LV_ALIGN_TOP_MID, -30, 110);
  g_sport.heading_name = left_label(g_sport.root, ".00", MUTED_COLOR,
                                    &velaride_num_44, 0, 0);
  g_sport.setting_buttons[0] = button("复位", BTN_SECONDARY, 56, 230, 100, 100,
                                      sw_left_cb, 0);
  g_sport.setting_buttons[1] = button("开始", BTN_SECONDARY, 234, 230, 100,
                                      100, sw_right_cb, 0);
  for (i = 0; i < SW_LAPS; i++)
    {
      int y = 344 + i * 32;

      g_sport.row_name[i] = left_label(g_sport.root, "", MUTED_COLOR,
                                       &velaride_font_cn_20, 56, y);
      g_sport.row_value[i] = label(g_sport.root, "", TEXT_COLOR,
                                   &velaride_num_24, 176, y, 158);
      lv_obj_set_style_text_align(g_sport.row_value[i],
                                  LV_TEXT_ALIGN_RIGHT, 0);
    }

  sw_buttons();
  sw_laps();
  lv_obj_update_layout(g_sport.root);
  sw_tick();
}

/* Level: tilt from the accelerometer's gravity vector. The bubble moves to
 * the high side, like a spirit level.
 *
 * Sensor axes on this board, both checked on the watch: sensor +X points
 * to the bottom of the screen and +Y to the left edge.
 */

static void ring(int r, uint32_t color)
{
  lv_obj_t *obj = circle(g_sport.root, LEVEL_X - r, LEVEL_Y - r, r * 2,
                         BG_COLOR, LV_OPA_TRANSP);

  lv_obj_set_style_border_width(obj, 2, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(color), 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
}

static void build_level(void)
{
  lv_obj_t *line;

  ring(LEVEL_R, VR_TILE2);
  ring(LEVEL_R / 2, VR_TILE2);
  ring(LEVEL_BUBBLE / 2 + 4, 0x4a4a4f);
  line = circle(g_sport.root, LEVEL_X - LEVEL_R, LEVEL_Y - 1, LEVEL_R * 2,
                VR_TILE2, LV_OPA_COVER);
  lv_obj_set_size(line, LEVEL_R * 2, 2);
  lv_obj_set_style_radius(line, 0, 0);
  line = circle(g_sport.root, LEVEL_X - 1, LEVEL_Y - LEVEL_R, 2,
                VR_TILE2, LV_OPA_COVER);
  lv_obj_set_size(line, 2, LEVEL_R * 2);
  lv_obj_set_style_radius(line, 0, 0);
  g_sport.tape = circle(g_sport.root, LEVEL_X - LEVEL_BUBBLE / 2,
                        LEVEL_Y - LEVEL_BUBBLE / 2, LEVEL_BUBBLE,
                        ACCENT_COLOR, LV_OPA_90);
  g_sport.body = label(g_sport.root, "", TEXT_COLOR, &velaride_num_24,
                       18, 396, SPORT_W - 36);
  g_sport.level_x = 0;
  g_sport.level_y = 0;
  g_sport.level_valid = false;
}

static void level_tick(void)
{
  int16_t ax;
  int16_t ay;
  int16_t az;
  float tx;
  float ty;
  float k;
  char text[48];
  int32_t dx;
  int32_t dy;

  if (lv_tick_elaps(g_sport.tool_tick) < 80)
    {
      return;
    }

  g_sport.tool_tick = lv_tick_get();
  if (velaride_ride_session_read_accel(&ax, &ay, &az) < 0)
    {
      set_label(g_sport.body, "运动传感器不可用");
      return;
    }

  /* tx > 0: right edge higher; ty > 0: bottom edge higher. */

  tx = atan2f(-ay, sqrtf((float)ax * ax + (float)az * az)) * 57.29578f;
  ty = atan2f(ax, sqrtf((float)ay * ay + (float)az * az)) * 57.29578f;
  k = g_sport.level_valid ? 0.35f : 1.0f;
  g_sport.level_x += (tx - g_sport.level_x) * k;
  g_sport.level_y += (ty - g_sport.level_y) * k;
  g_sport.level_valid = true;

  dx = (int32_t)lroundf(LV_CLAMP(-1.0f, g_sport.level_x / LEVEL_MAX_DEG,
                                 1.0f) * (LEVEL_R - LEVEL_BUBBLE / 2));
  dy = (int32_t)lroundf(LV_CLAMP(-1.0f, g_sport.level_y / LEVEL_MAX_DEG,
                                 1.0f) * (LEVEL_R - LEVEL_BUBBLE / 2));
  lv_obj_set_pos(g_sport.tape, LEVEL_X - LEVEL_BUBBLE / 2 + dx,
                 LEVEL_Y - LEVEL_BUBBLE / 2 + dy);
  lv_obj_set_style_bg_color(g_sport.tape, lv_color_hex(
    fabsf(g_sport.level_x) < 1.0f && fabsf(g_sport.level_y) < 1.0f ?
    VR_OK : ACCENT_COLOR), 0);
  snprintf(text, sizeof(text), "X %.1f°   Y %.1f°",
           (double)g_sport.level_x, (double)g_sport.level_y);
  set_label(g_sport.body, text);
}

static void tool_timer_cb(lv_timer_t *timer)
{
  if (g_sport.page != SPORT_PAGE_INFO)
    {
      lv_timer_pause(timer);
      return;
    }

  if (g_sport.info == VELARIDE_INFO_STOPWATCH)
    {
      sw_tick();
    }
  else if (g_sport.info == VELARIDE_INFO_LIGHT)
    {
      light_tick();
    }
  else if (g_sport.info == VELARIDE_INFO_LEVEL)
    {
      level_tick();
    }
}

#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
static void update_countdown(void)
{
  struct velaride_countdown_s countdown;
  char text[24];
  uint32_t seconds;
  bool editing;
  int i;

  velaride_countdown_get(lv_tick_get(), &countdown);
  seconds = (countdown.remaining_ms + 999) / 1000;
  snprintf(text, sizeof(text), "%02" PRIu32 ":%02" PRIu32,
           seconds / 60, seconds % 60);
  set_label(g_sport.time_label, text);
  editing = countdown.state == VELARIDE_COUNTDOWN_IDLE ||
            countdown.state == VELARIDE_COUNTDOWN_DONE;
  set_label(g_sport.status_label,
            countdown.state == VELARIDE_COUNTDOWN_DONE ? "时间到" :
            countdown.state == VELARIDE_COUNTDOWN_RUNNING ? "正在计时" :
            countdown.state == VELARIDE_COUNTDOWN_PAUSED ? "已暂停" :
                                                        "设置时长");
  set_label(lv_obj_get_child(g_sport.setting_buttons[0], 0),
            countdown.state == VELARIDE_COUNTDOWN_RUNNING ? "暂停" :
            countdown.state == VELARIDE_COUNTDOWN_PAUSED ? "继续" :
            countdown.state == VELARIDE_COUNTDOWN_DONE ? "再次开始" : "开始");
  set_label(lv_obj_get_child(g_sport.setting_buttons[1], 0),
            countdown.state == VELARIDE_COUNTDOWN_DONE ? "知道了" : "取消");
  for (i = 0; i < 6; i++)
    {
      if (editing)
        {
          lv_obj_remove_state(g_sport.countdown_adjust[i], LV_STATE_DISABLED);
        }
      else
        {
          lv_obj_add_state(g_sport.countdown_adjust[i], LV_STATE_DISABLED);
        }
    }

  lv_bar_set_value(g_sport.progress,
                   (int)(countdown.remaining_ms / countdown.duration_s),
                   LV_ANIM_OFF);
}

static void countdown_action_cb(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);
  struct velaride_countdown_s countdown;
  uint32_t now = lv_tick_get();

  velaride_countdown_get(now, &countdown);
  if (action > 0)
    {
      velaride_countdown_set((uint32_t)action * 60);
    }
  else if (action == -1 || action == -2)
    {
      int minutes = (int)((countdown.duration_s + 59) / 60);
      minutes += action == -1 ? -1 : 1;
      velaride_countdown_set((uint32_t)LV_CLAMP(1, minutes, 180) * 60);
    }
  else if (action == -3)
    {
      if (countdown.state == VELARIDE_COUNTDOWN_RUNNING)
        {
          velaride_countdown_pause(now);
        }
      else
        {
          velaride_countdown_start(now);
        }
    }
  else if (action == -4)
    {
      velaride_countdown_cancel();
    }

  update_countdown();
}

static void build_countdown(void)
{
  static const char *names[] = {"1 分钟", "5 分钟", "10 分钟", "25 分钟"};
  static const int minutes[] = {1, 5, 10, 25};
  int i;

  g_sport.time_label = label(g_sport.root, "05:00", TEXT_COLOR,
                             &velaride_num_60, 18, 112, 354);
  g_sport.status_label = label(g_sport.root, "设置时长", ACCENT_COLOR,
                               &velaride_font_cn_20, 18, 184, 354);
  g_sport.progress = lv_bar_create(g_sport.root);
  lv_obj_set_pos(g_sport.progress, 40, 220);
  lv_obj_set_size(g_sport.progress, 310, 6);
  lv_bar_set_range(g_sport.progress, 0, 1000);
  lv_obj_set_style_bg_color(g_sport.progress, vr_accent(), LV_PART_INDICATOR);
  for (i = 0; i < 4; i++)
    {
      g_sport.countdown_adjust[i] = button(names[i], BTN_TILE,
                                          18 + i * 89, 250, 87, 44,
                                          countdown_action_cb, minutes[i]);
    }

  g_sport.countdown_adjust[4] = button("-1", BTN_TILE, 18, 308, 72, 50,
                                      countdown_action_cb, -1);
  g_sport.countdown_adjust[5] = button("+1", BTN_TILE, 300, 308, 72, 50,
                                      countdown_action_cb, -2);
  g_sport.setting_buttons[0] = button("开始", BTN_PRIMARY, 102, 308, 186,
                                      50, countdown_action_cb, -3);
  g_sport.setting_buttons[1] = button("取消", BTN_SECONDARY, 102, 370,
                                      186, 44, countdown_action_cb, -4);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  label(g_sport.root, "到时亮屏 · 铃声按声音设置", MUTED_COLOR,
#else
  label(g_sport.root, "到时亮屏提示 · 静音", MUTED_COLOR,
#endif
        &velaride_font_cn_20, 18, 425, 354);
  update_countdown();
}
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
static void update_alarm(void)
{
  struct velaride_alarm_s alarm;
  char text[16];
  int i;
  velaride_alarm_get(&alarm);
  snprintf(text, sizeof(text), "%02u:%02u", g_sport.alarm_hour,
           g_sport.alarm_minute);
  set_label(g_sport.time_label, text);
  set_label(g_sport.status_label, alarm.saving ? "正在保存" :
            alarm.save_error != 0 ? "保存失败，请重试" :
            alarm.ringing ? "时间到了" :
            alarm.needs_confirmation ? "请先核对右上角时间" :
            (alarm.hour != g_sport.alarm_hour ||
             alarm.minute != g_sport.alarm_minute) ? "尚未保存" :
            alarm.enabled ? "每天提醒 · 已开启" : "已关闭");
  set_label(lv_obj_get_child(g_sport.setting_buttons[0], 0),
            alarm.needs_confirmation ? "确认时间并开启" :
            alarm.save_error != 0 ? "重试保存" : "保存并开启");
  set_label(lv_obj_get_child(g_sport.setting_buttons[1], 0),
            alarm.ringing ? "知道了" : "关闭闹钟");
  for (i = 0; i < 6; i++)
    {
      lv_obj_t *obj = i < 4 ? g_sport.alarm_adjust[i] :
                              g_sport.setting_buttons[i - 4];
      if (alarm.saving)
        {
          lv_obj_add_state(obj, LV_STATE_DISABLED);
        }
      else
        {
          lv_obj_remove_state(obj, LV_STATE_DISABLED);
        }
    }
}

static void alarm_action_cb(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);
  struct velaride_alarm_s alarm;
  velaride_alarm_get(&alarm);
  if (alarm.saving)
    {
      return;
    }
  switch (action)
    {
      case 0:
        g_sport.alarm_hour = (g_sport.alarm_hour + 23) % 24;
        break;
      case 1:
        g_sport.alarm_hour = (g_sport.alarm_hour + 1) % 24;
        break;
      case 2:
        g_sport.alarm_minute = (g_sport.alarm_minute + 59) % 60;
        break;
      case 3:
        g_sport.alarm_minute = (g_sport.alarm_minute + 1) % 60;
        break;
      case 4:
        if (velaride_alarm_set(g_sport.alarm_hour, g_sport.alarm_minute,
                               true, time(NULL)) < 0)
          {
            set_label(g_sport.status_label, "请先校准手表时间");
            return;
          }
        break;
      case 5:
        if (alarm.ringing)
          {
            velaride_alarm_ack();
          }
        else
          {
            velaride_alarm_set(alarm.hour, alarm.minute, false, time(NULL));
          }
        break;
    }
  update_alarm();
}

static void build_alarm(void)
{
  struct velaride_alarm_s alarm;
  static const char *names[] = {"时 -", "时 +", "分 -", "分 +"};
  int i;
  velaride_alarm_get(&alarm);
  g_sport.alarm_hour = alarm.hour;
  g_sport.alarm_minute = alarm.minute;
  g_sport.time_label = label(g_sport.root, "07:00", TEXT_COLOR,
                             &velaride_num_60, 18, 115, 354);
  g_sport.status_label = label(g_sport.root, "", ACCENT_COLOR,
                               &velaride_font_cn_20, 18, 195, 354);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  label(g_sport.root, "每天一次 · 铃声最长 10 秒", MUTED_COLOR,
#else
  label(g_sport.root, "每天一次 · 亮屏提醒 · 静音", MUTED_COLOR,
#endif
        &velaride_font_cn_20, 18, 235, 354);
  for (i = 0; i < 4; i++)
    {
      g_sport.alarm_adjust[i] = button(names[i], BTN_TILE, 18 + i * 89,
                                      274, 87, 44, alarm_action_cb, i);
      lv_obj_add_event_cb(g_sport.alarm_adjust[i], alarm_action_cb,
                          LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)i);
    }
  g_sport.setting_buttons[0] = button("保存并开启", BTN_PRIMARY, 40, 334,
                                      310, 48, alarm_action_cb, 4);
  g_sport.setting_buttons[1] = button("关闭闹钟", BTN_SECONDARY, 100, 394,
                                      190, 40, alarm_action_cb, 5);
  update_alarm();
}
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
static void update_sound(void)
{
  struct velaride_sound_s sound;
  char text[64];
  int i;

  velaride_sound_get(&sound);
  set_label(g_sport.status_label, sound.save_error != 0 ? "设置保存失败，请重试" :
            sound.saving ? "正在保存" : sound.dnd ? "勿扰中，声音已暂停" :
            sound.enabled && sound.volume == 0 ? "音量为 0，已静音" :
            sound.enabled && sound.last_error == -EBUSY ? "音频正忙，稍后再试" :
            sound.enabled && sound.last_error < 0 &&
            sound.last_error != -ECANCELED ? "声音暂不可用，请重试" :
            sound.enabled ? "闹钟铃声最多 10 秒" : "静音，保留亮屏提醒");
  for (i = 0; i < 2; i++)
    {
      bool selected = sound.enabled == (i == 1);
      lv_obj_set_style_bg_color(g_sport.sound_buttons[i],
                                selected ? vr_accent() : lv_color_hex(VR_TILE2), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(g_sport.sound_buttons[i], 0),
                                  selected ? vr_accent_ink() :
                                  lv_color_hex(TEXT_COLOR), 0);
    }
  snprintf(text, sizeof(text), "音量  %u%%", sound.volume);
  set_label(g_sport.sound_volume_label, text);
  if (!lv_slider_is_dragged(g_sport.sound_slider) &&
      lv_slider_get_value(g_sport.sound_slider) != sound.volume)
    {
      lv_slider_set_value(g_sport.sound_slider, sound.volume, LV_ANIM_OFF);
    }
  snprintf(text, sizeof(text), "模式语音  %s", sound.voice ? "已开启" : "已关闭");
  set_label(lv_obj_get_child(g_sport.sound_buttons[4], 0), text);
  snprintf(text, sizeof(text), "提醒铃声  %s", sound.rings ? "已开启" : "已关闭");
  set_label(lv_obj_get_child(g_sport.sound_buttons[5], 0), text);
  if (sound.enabled && sound.volume > 0 && !sound.dnd &&
      (sound.voice || sound.rings))
    {
      lv_obj_remove_state(g_sport.sound_buttons[6], LV_STATE_DISABLED);
    }
  else
    {
      lv_obj_add_state(g_sport.sound_buttons[6], LV_STATE_DISABLED);
    }
}

static void sound_action_cb(lv_event_t *event)
{
  int action = (int)(intptr_t)lv_event_get_user_data(event);
  struct velaride_sound_s sound;

  velaride_sound_get(&sound);
  switch (action)
    {
      case 0: sound.enabled = false; break;
      case 1: sound.enabled = true; break;
      case 2:
        velaride_sound_set_volume(sound.volume > 5 ? sound.volume - 5 : 0);
        break;
      case 3:
        velaride_sound_set_volume(sound.volume < 95 ? sound.volume + 5 : 100);
        break;
      case 4: sound.voice = !sound.voice; break;
      case 5: sound.rings = !sound.rings; break;
      case 6:
        velaride_sound_request(sound.voice ? VR_SOUND_RIDE_MODE :
                                             VR_SOUND_COUNTDOWN);
        break;
      case 7: velaride_sound_stop(VR_SOUND_NONE); break;
    }
  if (action < 6 && action != 2 && action != 3)
    {
      velaride_sound_set(sound.enabled, sound.voice, sound.rings, sound.volume);
    }
  update_sound();
}

static void sound_slider_cb(lv_event_t *event)
{
  uint8_t value = (uint8_t)lv_slider_get_value(lv_event_get_target(event));
  velaride_sound_set_volume(value);
  update_sound();
}

static void build_sound(void)
{
  g_sport.status_label = label(g_sport.root, "", MUTED_COLOR,
                               &velaride_font_cn_20, 18, 108, 354);
  g_sport.sound_buttons[0] = button("静音", BTN_TILE, 25, 145, 164, 48,
                                    sound_action_cb, 0);
  g_sport.sound_buttons[1] = button("有声", BTN_TILE, 201, 145, 164, 48,
                                    sound_action_cb, 1);
  g_sport.sound_buttons[2] = button("-5", BTN_TILE, 25, 211, 48, 44,
                                    sound_action_cb, 2);
  g_sport.sound_buttons[3] = button("+5", BTN_TILE, 317, 211, 48, 44,
                                    sound_action_cb, 3);
  g_sport.sound_volume_label = label(g_sport.root, "", TEXT_COLOR,
                                     &velaride_font_cn_20, 84, 202, 222);
  g_sport.sound_slider = lv_slider_create(g_sport.root);
  lv_obj_set_pos(g_sport.sound_slider, 89, 239);
  lv_obj_set_size(g_sport.sound_slider, 212, 8);
  lv_slider_set_range(g_sport.sound_slider, 0, 100);
  lv_obj_remove_flag(g_sport.sound_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_ext_click_area(g_sport.sound_slider, 16);
  lv_obj_set_style_bg_color(g_sport.sound_slider, vr_accent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_sport.sound_slider, vr_accent(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(g_sport.sound_slider, 8, LV_PART_KNOB);
  lv_obj_add_event_cb(g_sport.sound_slider, sound_slider_cb,
                      LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(g_sport.sound_buttons[2], sound_action_cb,
                      LV_EVENT_LONG_PRESSED_REPEAT, (void *)2);
  lv_obj_add_event_cb(g_sport.sound_buttons[3], sound_action_cb,
                      LV_EVENT_LONG_PRESSED_REPEAT, (void *)3);
  g_sport.sound_buttons[4] = button("模式语音", BTN_SECONDARY, 25, 264, 340,
                                    46, sound_action_cb, 4);
  g_sport.sound_buttons[5] = button("提醒铃声", BTN_SECONDARY, 25, 319, 340,
                                    46, sound_action_cb, 5);
  g_sport.sound_buttons[6] = button("试听", BTN_PRIMARY, 25, 383, 164, 46,
                                    sound_action_cb, 6);
  g_sport.sound_buttons[7] = button("停止声音", BTN_SECONDARY, 201, 383,
                                    164, 46, sound_action_cb, 7);
  update_sound();
}
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
static void update_night(void)
{
  struct velaride_night_s night;
  char text[96];
  velaride_night_get(&night);
  set_label(g_sport.status_label, night.save_error ? "设置保存失败，请重试" :
            night.saving ? "正在保存" : night.mode == VR_NIGHT_OFF ? "使用原有显示" :
            night.mode == VR_NIGHT_ON ? "夜骑显示已开启" :
            !night.light_valid ? "光感不可用，保持当前显示" :
            night.active ? "暗光中 · 夜骑显示" : "光线明亮 · 原有显示");
  for (int i = 0; i < 3; i++)
    {
      bool selected = night.mode == i;
      lv_obj_set_style_bg_color(g_sport.night_modes[i],
                                selected ? vr_accent() : lv_color_hex(VR_TILE2), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(g_sport.night_modes[i], 0),
                                  selected ? vr_accent_ink() : lv_color_hex(TEXT_COLOR), 0);
    }
  snprintf(text, sizeof(text), "夜间亮度上限  %u%%", night.limit);
  set_label(g_sport.night_value, text);
  if (!lv_slider_is_dragged(g_sport.night_slider) &&
      lv_slider_get_value(g_sport.night_slider) != night.limit)
    lv_slider_set_value(g_sport.night_slider, night.limit, LV_ANIM_OFF);
  if (night.light_valid)
    snprintf(text, sizeof(text), "环境光  %.1f lux", (double)night.lux);
  else
    snprintf(text, sizeof(text), "环境光  暂不可用");
  set_label(g_sport.body, text);
}

static void night_action_cb(lv_event_t *event)
{
  int action = (int)(intptr_t)lv_event_get_user_data(event);
  struct velaride_night_s night;
  velaride_night_get(&night);
  if (action < 3) night.mode = action;
  else if (action == 3) night.limit = night.limit > 25 ? night.limit - 5 : 20;
  else night.limit = night.limit < 35 ? night.limit + 5 : 40;
  velaride_night_set(night.mode, night.limit);
  velaride_night_ui_apply(false);
  update_night();
}

static void night_slider_cb(lv_event_t *event)
{
  struct velaride_night_s night;
  velaride_night_get(&night);
  velaride_night_set(night.mode, lv_slider_get_value(lv_event_get_target(event)));
  update_night();
}

static void build_night(void)
{
  static const char *names[] = {"关闭", "开启", "自动"};
  g_sport.status_label = label(g_sport.root, "", MUTED_COLOR,
                               &velaride_font_cn_20, 18, 110, 354);
  for (int i = 0; i < 3; i++)
    g_sport.night_modes[i] = button(names[i], BTN_TILE, 25 + 117 * i, 153,
                                    106, 48, night_action_cb, i);
  g_sport.night_value = label(g_sport.root, "", TEXT_COLOR,
                              &velaride_font_cn_20, 18, 225, 354);
  button("-5", BTN_TILE, 25, 260, 48, 44, night_action_cb, 3);
  button("+5", BTN_TILE, 317, 260, 48, 44, night_action_cb, 4);
  g_sport.night_slider = lv_slider_create(g_sport.root);
  lv_obj_set_pos(g_sport.night_slider, 90, 279);
  lv_obj_set_size(g_sport.night_slider, 210, 8);
  lv_slider_set_range(g_sport.night_slider, 20, 40);
  lv_obj_set_ext_click_area(g_sport.night_slider, 16);
  lv_obj_remove_flag(g_sport.night_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_color(g_sport.night_slider, vr_accent(), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_sport.night_slider, vr_accent(), LV_PART_KNOB);
  lv_obj_set_style_pad_all(g_sport.night_slider, 8, LV_PART_KNOB);
  lv_obj_add_event_cb(g_sport.night_slider, night_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  g_sport.body = label(g_sport.root, "", MUTED_COLOR,
                        &velaride_font_cn_20, 18, 326, 354);
  label(g_sport.root, "自动：持续变暗后开启\n关闭后恢复原显示，夜骑灯不受影响",
        MUTED_COLOR, &velaride_font_cn_20, 25, 373, 340);
  update_night();
}
#endif

static void update_info(void)
{
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  if (g_sport.info == VELARIDE_INFO_COUNTDOWN)
    {
      update_clock();
      update_countdown();
      return;
    }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  if (g_sport.info == VELARIDE_INFO_ALARM)
    {
      update_clock();
      update_alarm();
      return;
    }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  if (g_sport.info == VELARIDE_INFO_SOUND)
    {
      update_clock();
      update_sound();
      return;
    }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  if (g_sport.info == VELARIDE_INFO_NIGHT)
    {
      update_clock();
      update_night();
      return;
    }
#endif
  char text[512];
  struct velaride_ride_snapshot_s ride;

  if (g_sport.info == VELARIDE_INFO_LIGHT ||
      g_sport.info == VELARIDE_INFO_STOPWATCH ||
      g_sport.info == VELARIDE_INFO_LEVEL)
    {
      return;
    }

  velaride_ride_session_get(&ride);
  if (g_sport.info == VELARIDE_INFO_CONNECTION)
    {
      double speed;
      double distance;
      char phone[96] = "GPS  -- km/h\n         -- km";

      if (velaride_ride_session_get_phone_data(&speed, &distance))
        {
          snprintf(phone, sizeof(phone), "GPS  %.1f km/h\n         %.2f km",
                   speed, distance);
        }

      snprintf(text, sizeof(text), "%s\n\n%s\n\n"
               "在手机 VelaRide App 中\n开启定位同步，保持前台。",
               velaride_ride_session_connected() ? "蓝牙已连接" : "蓝牙未连接",
               phone);
    }
  else if (g_sport.info == VELARIDE_INFO_DEVICE)
    {
      struct velaride_device_snapshot_s device;
      velaride_device_get(&device);
      snprintf(text, sizeof(text), "%s  /  %s",
               device.usb_valid && device.usb_connected ? "USB 已接入" : "供电检测中",
               velaride_ride_session_connected() ? "蓝牙已连接" : "蓝牙未连接");
    }
  else if (g_sport.info == VELARIDE_INFO_COMPASS)
    {
      update_compass(text, sizeof(text));
    }
  else if (g_sport.info == VELARIDE_INFO_DISPLAY)
    {
      struct velaride_device_snapshot_s device;
      struct velaride_display_s display;
      char light[64];

      velaride_device_get(&device);
      velaride_display_get(&display);
      if (device.light_valid)
        {
          snprintf(light, sizeof(light), "环境光  %lu lux",
                   (unsigned long)(device.light_lux + 0.5f));
        }
      else
        {
          snprintf(light, sizeof(light), "环境光  暂不可用");
        }
      snprintf(text, sizeof(text), "%s\n亮度 %u%% / 自动调光%s\n%s",
               light, display.applied, display.automatic ? "开" : "关",
               display.apply_error < 0 ? "亮度调整失败，请重试" :
               display.save_error < 0 ? "设置未保存，请重试" :
               display.save_pending ? "正在保存设置" :
               display.automatic && !device.light_valid ?
               "光线不可用，使用手动亮度" : "设置已保存");
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
      {
        struct velaride_night_s night;
        velaride_night_get(&night);
        if (night.active && !night.exempt)
          snprintf(text + strlen(text), sizeof(text) - strlen(text),
                   "\n夜骑模式限制至 %u%%", night.limit);
      }
#endif
      static const uint8_t levels[] = {20, 50, 80};
      for (int i = 0; i < 3; i++)
        {
          lv_obj_set_style_border_width(g_sport.setting_buttons[i],
            !display.automatic && display.manual == levels[i] ? 2 : 0, 0);
          lv_obj_set_style_border_color(g_sport.setting_buttons[i],
            lv_color_hex(ACCENT_COLOR), 0);
        }
      lv_label_set_text(lv_obj_get_child(g_sport.auto_button, 0),
                        display.automatic ? "自动调光  已开启" : "自动调光  已关闭");
      lv_obj_set_style_bg_color(g_sport.auto_button,
        lv_color_hex(display.automatic ? ACCENT_COLOR : VR_TILE2), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(g_sport.auto_button, 0),
        lv_color_hex(display.automatic ? velaride_accent()->ink : TEXT_COLOR),
        0);
    }
  else if (g_sport.info == VELARIDE_INFO_HARDWARE)
    {
      struct velaride_device_snapshot_s device;
      char voltage[64];

      velaride_device_get(&device);
      if (device.adc_valid)
        {
          snprintf(voltage, sizeof(voltage), "参考电压  %ld.%03ld V",
                   (long)device.reference_mv / 1000,
                   (long)device.reference_mv % 1000);
        }
      else
        {
          snprintf(voltage, sizeof(voltage), "电压采样  暂不可用");
        }

      snprintf(text, sizeof(text), "供电检测  %s\n%s\n\n"
               "运动检测  %s\n蓝牙连接  %s\n\n"
               "电压仅供参考，电量待电池校准。USB 接入不代表电池正在充电。\n\n"
               "速度、距离和心率未接入。",
               !device.usb_valid ? "未知" : device.usb_connected ?
                                    "USB 已接入" : "USB 未接入", voltage,
               ride.sensor_ready ? "已就绪" : "不可用",
               velaride_ride_session_connected() ? "已连接" : "未连接");
    }
  else if (g_sport.info == VELARIDE_INFO_APPEARANCE)
    {
      struct velaride_ui_prefs_s prefs;

      velaride_ui_prefs_get(&prefs);
      snprintf(text, sizeof(text), "%s",
               prefs.save_error < 0 ? "设置未保存，请重新选择" :
               prefs.save_pending ? "正在保存" :
               "主题色用于时间、主按钮和进度\n警示颜色不随主题变化");
    }
  else if (g_sport.info == VELARIDE_INFO_WAKE)
    {
      struct velaride_wake_prefs_s prefs;

      velaride_wake_prefs_get(&prefs);
      snprintf(text, sizeof(text), "%s",
               prefs.save_error < 0 ? "设置未保存，请重新选择" :
               prefs.save_pending ? "正在保存" :
               "骑行中和夜骑灯打开时屏幕常亮\n熄屏后触摸或按键先亮屏，不会误点");
    }
  else if (g_sport.info == VELARIDE_INFO_SAFETY)
    {
      snprintf(text, sizeof(text), "本地提醒无需联网\n\n"
               "冲击后短时持续静止\n连续静止 15 秒\n累计运动 45 分钟\n\n"
               "收到提醒后，可选择我没事或暂停记录。这里只提醒，不会自动求救。");
    }
  else
    {
      snprintf(text, sizeof(text), "表盘下单击功能键打开菜单，点击骑行进入。\n\n"
               "页面上点击返回，或单击功能键返回。骑行中结束需要确认。\n\n"
               "复盘文字可上下滑动。时间不对时，在电脑网页重新连接手表校时。");
    }

  if (g_sport.body != NULL &&
      strcmp(lv_label_get_text(g_sport.body), text) != 0)
    {
      lv_label_set_text(g_sport.body, text);
    }
}

static void session_timer_cb(lv_timer_t *timer)
{
  struct velaride_ride_snapshot_s ride;

  LV_UNUSED(timer);
  if (g_sport.page == SPORT_PAGE_HIDDEN)
    {
      return;
    }
  update_clock();
  if (g_sport.page == SPORT_PAGE_REVIEW)
    {
      update_review_body();
      return;
    }
  if (g_sport.page == SPORT_PAGE_INFO)
    {
      return;
    }
  if (!g_sport.session_started)
    {
      return;
    }

  velaride_ride_session_second_tick();
  velaride_ride_session_get(&ride);
  if (ride.alert != VELARIDE_ALERT_NONE &&
      (g_sport.page == SPORT_PAGE_ACTIVE ||
       g_sport.page == SPORT_PAGE_GOAL_DONE ||
       (g_sport.page == SPORT_PAGE_ALERT &&
        ride.alert != g_sport.shown_alert)))
    {
      show_alert(ride.alert);
      return;
    }
  if (g_sport.page == SPORT_PAGE_ACTIVE)
    {
      if (ride.goal_reached && !g_sport.goal_announced)
        {
          show_goal_done();
        }
      else
        {
          update_active();
        }
    }
}

static void sensor_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (g_sport.session_started)
    {
      velaride_ride_session_sensor_tick();
    }
}

void velaride_sport_ui_back(void)
{
  switch (g_sport.page)
    {
      case SPORT_PAGE_GOAL:
        show_select();
        break;
      case SPORT_PAGE_COUNTDOWN:
        lv_timer_pause(g_sport.countdown_timer);
        show_goal();
        break;
      case SPORT_PAGE_ACTIVE:
        if (g_sport.locked)
          {
            g_sport.locked = false;
            show_active();
          }
        else
          {
            show_confirm();
          }
        break;
      case SPORT_PAGE_CONFIRM:
      case SPORT_PAGE_GOAL_DONE:
        show_active();
        break;
      case SPORT_PAGE_ALERT:
        velaride_ride_session_ack_alert();
        g_sport.ride_view = RIDE_VIEW_DATA;
        show_active();
        break;
      case SPORT_PAGE_INFO:
        if (g_sport.info_from_settings)
          {
            velaride_sport_ui_show_info(VELARIDE_INFO_DEVICE);
          }
        else
          {
            leave();
          }
        break;
      case SPORT_PAGE_HIDDEN:
        break;
      default:
        leave();
        break;
    }
}

static void info_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  if (g_sport.page == SPORT_PAGE_INFO) update_info();
  if (g_sport.page == SPORT_PAGE_ACTIVE &&
      g_sport.ride_view == RIDE_VIEW_HEADING)
    {
      update_heading();
    }
}

void velaride_sport_ui_init(lv_obj_t *screen,
                            velaride_sport_exit_cb_t exit_cb,
                            void *exit_arg)
{
  memset(&g_sport, 0, sizeof(g_sport));
  g_sport.exit_cb = exit_cb;
  g_sport.exit_arg = exit_arg;
  velaride_ride_session_init();
  velaride_device_init();
  g_sport.root = lv_obj_create(screen);
  lv_obj_remove_style_all(g_sport.root);
  lv_obj_set_size(g_sport.root, SPORT_W, SPORT_H);
  lv_obj_set_style_bg_color(g_sport.root, lv_color_hex(BG_COLOR), 0);
  lv_obj_set_style_bg_opa(g_sport.root, LV_OPA_COVER, 0);
  lv_obj_remove_flag(g_sport.root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_sport.root, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
  g_sport.countdown_timer = lv_timer_create(countdown_cb, 100, NULL);
  lv_timer_pause(g_sport.countdown_timer);
  g_sport.session_timer = lv_timer_create(session_timer_cb, 1000, NULL);
  g_sport.hold_timer = lv_timer_create(hold_timer_cb, 30, NULL);
  g_sport.tool_timer = lv_timer_create(tool_timer_cb, 40, NULL);
  lv_timer_pause(g_sport.tool_timer);
  lv_timer_pause(g_sport.hold_timer);
  g_sport.goal_s = 30 * 60;
  g_sport.ride_view = RIDE_VIEW_DATA;
  /* Children of the screen pass gestures on to their parent by default;
   * stop here so ride views receive the swipe.
   */

  lv_obj_remove_flag(g_sport.root, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_sport.root, root_gesture_cb, LV_EVENT_GESTURE, NULL);
  lv_timer_create(sensor_timer_cb, 250, NULL);
  lv_timer_create(info_timer_cb, 250, NULL);
}

void velaride_sport_ui_enter(void)
{
  if (g_sport.root == NULL)
    {
      return;
    }
  if (g_sport.has_record && g_sport.persist_result < 0)
    {
      show_review();
    }
  else if (g_sport.session_started)
    {
      show_active();
    }
  else
    {
      lv_timer_pause(g_sport.countdown_timer);
      show_select();
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      velaride_sound_request(VR_SOUND_RIDE_MODE);
#endif
    }
  reveal();
}

bool velaride_sport_ui_active(void)
{
  return g_sport.page != SPORT_PAGE_HIDDEN;
}

bool velaride_sport_ui_keep_awake(void)
{
  struct velaride_ride_snapshot_s ride;

  if (g_sport.page == SPORT_PAGE_INFO &&
      g_sport.info == VELARIDE_INFO_LIGHT)
    {
      return true;
    }

  velaride_ride_session_get(&ride);
  return ride.active;
}

#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
bool velaride_sport_ui_night_exempt(void)
{
  return g_sport.page == SPORT_PAGE_INFO && g_sport.info == VELARIDE_INFO_LIGHT;
}
#endif

static void show_saved(bool ai)
{
  if (g_sport.root == NULL || g_sport.session_started)
    {
      return;
    }

  lv_timer_pause(g_sport.countdown_timer);
  if (!g_sport.has_record || g_sport.persist_result == 0)
    {
      g_sport.has_record =
        velaride_ride_session_load_latest(&g_sport.latest_record) == 0;
      g_sport.persist_result = 0;
    }
  g_sport.review_ai = ai;
  show_review();
  reveal();
}

void velaride_sport_ui_show_latest(void)
{
  show_saved(true);
}

void velaride_sport_ui_show_records(void)
{
  show_saved(false);
}

static void settings_open_cb(lv_event_t *event)
{
  velaride_sport_ui_show_info((enum velaride_info_e)(uintptr_t)
                              lv_event_get_user_data(event));
  g_sport.info_from_settings = true;
}

static void button_add_icon(lv_obj_t *obj, enum velaride_icon_e icon)
{
  lv_obj_align(lv_obj_get_child(obj, 0), LV_ALIGN_CENTER, 11, 0);
  lv_obj_t *glyph = velaride_icon_create(obj, icon, 0, 0, 22, TEXT_COLOR);
  lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 10, 0);
}

/* Settings rows live in a vertically scrolling list below the title. */

static lv_obj_t *setting_list(void)
{
  lv_obj_t *list = lv_obj_create(g_sport.root);

  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, 0, SETTING_LIST_Y);
  lv_obj_set_size(list, SPORT_W, SPORT_H - SETTING_LIST_Y);
  lv_obj_set_style_pad_bottom(list, 36, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLL_MOMENTUM |
                        LV_OBJ_FLAG_SCROLL_ELASTIC);
  return list;
}

static void setting_row(lv_obj_t *list, const char *text,
                        enum velaride_icon_e icon,
                        enum velaride_info_e page, int index)
{
  lv_obj_t *obj = button(text, BTN_TILE, 18,
                         index * (SETTING_ROW_H + SETTING_ROW_GAP),
                         354, SETTING_ROW_H, settings_open_cb, page);
  lv_obj_set_parent(obj, list);
  lv_obj_set_style_radius(obj, SETTING_ROW_H / 2 - 4, 0);
  lv_obj_align(lv_obj_get_child(obj, 0), LV_ALIGN_LEFT_MID, 66, 0);
  lv_obj_t *dot = lv_obj_create(obj);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 40, 40);
  lv_obj_align(dot, LV_ALIGN_LEFT_MID, 14, 0);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(ACCENT_COLOR), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  velaride_icon_create(dot, icon, 8, 8, 24, velaride_accent()->ink);
  lv_obj_t *arrow = lv_label_create(obj);
  lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(arrow, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(arrow, lv_color_hex(VR_INK3), 0);
  lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -18, 0);
}

static lv_obj_t *setting_list(void);

static void accent_dot_style(lv_obj_t *dot, int index, bool selected)
{
  lv_obj_t *mark = lv_obj_get_child(dot, 0);

  lv_obj_set_style_outline_width(dot, selected ? 3 : 0, 0);
  lv_obj_set_style_outline_pad(dot, 4, 0);
  lv_obj_set_style_outline_color(dot,
    lv_color_hex(g_velaride_accents[index].color), 0);
  if (selected)
    {
      lv_obj_remove_flag(mark, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void accent_set_cb(lv_event_t *event)
{
  uintptr_t index = (uintptr_t)lv_event_get_user_data(event);
  struct velaride_ui_prefs_s prefs;

  velaride_ui_prefs_get(&prefs);
  if (index == prefs.accent ||
      velaride_ui_prefs_set(index, prefs.menu, prefs.transition) < 0)
    {
      return;
    }

  /* Rebuild the page so the title, back key and dots take the new color. */

  velaride_sport_ui_show_info(VELARIDE_INFO_APPEARANCE);
  g_sport.info_from_settings = true;
}

static void transition_set_cb(lv_event_t *event)
{
  uintptr_t mode = (uintptr_t)lv_event_get_user_data(event);
  struct velaride_ui_prefs_s prefs;

  velaride_ui_prefs_get(&prefs);
  if (mode == prefs.transition ||
      velaride_ui_prefs_set(prefs.accent, prefs.menu, mode) < 0)
    {
      return;
    }

  velaride_sport_ui_show_info(VELARIDE_INFO_APPEARANCE);
  g_sport.info_from_settings = true;
}

static lv_obj_t *section_label(lv_obj_t *parent, const char *text, int y)
{
  lv_obj_t *obj = label(parent, text, VR_INK3, &velaride_font_cn_20,
                        34, y, 320);

  lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, 0);
  return obj;
}

/* One selectable row: title, a short note and a radio mark. */

static void menu_style_set_cb(lv_event_t *event)
{
  uintptr_t menu = (uintptr_t)lv_event_get_user_data(event);
  struct velaride_ui_prefs_s prefs;

  velaride_ui_prefs_get(&prefs);
  if (menu == prefs.menu ||
      velaride_ui_prefs_set(prefs.accent, menu, prefs.transition) < 0)
    {
      return;
    }

  velaride_sport_ui_show_info(VELARIDE_INFO_APPEARANCE);
  g_sport.info_from_settings = true;
}

static void option_row(lv_obj_t *list, const char *title, const char *note,
                       lv_event_cb_t cb, uintptr_t value, bool selected,
                       int y)
{
  lv_obj_t *row = button(title, BTN_TILE, 18, y, 354, 76, cb, value);
  lv_obj_t *caption = lv_obj_get_child(row, 0);
  lv_obj_t *small;
  lv_obj_t *radio;

  lv_obj_set_parent(row, list);
  lv_obj_set_style_radius(row, 34, 0);
  lv_obj_align(caption, LV_ALIGN_TOP_LEFT, 22, 10);
  small = lv_label_create(row);
  lv_label_set_text(small, note);
  lv_obj_set_style_text_font(small, &velaride_font_cn_20, 0);
  lv_obj_set_style_text_color(small, lv_color_hex(MUTED_COLOR), 0);
  lv_obj_align(small, LV_ALIGN_BOTTOM_LEFT, 22, -10);

  radio = lv_obj_create(row);
  lv_obj_remove_style_all(radio);
  lv_obj_set_size(radio, 28, 28);
  lv_obj_align(radio, LV_ALIGN_RIGHT_MID, -22, 0);
  lv_obj_set_style_radius(radio, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(radio, selected ? 8 : 2, 0);
  lv_obj_set_style_border_color(radio, selected ?
    lv_color_hex(ACCENT_COLOR) : lv_color_hex(0x3a3a3e), 0);
  lv_obj_remove_flag(radio, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void build_appearance(void)
{
  static const char *titles[VELARIDE_TRANSITION_COUNT] =
    {"跟手滑动", "百叶窗", "圆形展开"};
  static const char *notes[VELARIDE_TRANSITION_COUNT] =
    {"默认，最省电", "展示方案，分片展开", "从中心向外展开"};
  struct velaride_ui_prefs_s prefs;
  lv_obj_t *list = setting_list();
  lv_obj_t *name;
  int i;

  velaride_ui_prefs_get(&prefs);
  section_label(list, "主题颜色", 0);
  for (i = 0; i < VELARIDE_ACCENT_COUNT; i++)
    {
      lv_obj_t *dot = lv_obj_create(list);
      lv_obj_t *mark;

      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, 56, 56);
      lv_obj_set_pos(dot, 42 + i * 84, 38);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(dot,
        lv_color_hex(g_velaride_accents[i].color), 0);
      lv_obj_set_style_transform_scale(dot, 230, LV_STATE_PRESSED);
      lv_obj_set_style_transform_pivot_x(dot, 28, 0);
      lv_obj_set_style_transform_pivot_y(dot, 28, 0);
      lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_event_cb(dot, accent_set_cb, LV_EVENT_CLICKED,
                          (void *)(uintptr_t)i);
      mark = lv_label_create(dot);
      lv_label_set_text(mark, LV_SYMBOL_OK);
      lv_obj_set_style_text_font(mark, &lv_font_montserrat_24, 0);
      lv_obj_set_style_text_color(mark,
        lv_color_hex(g_velaride_accents[i].ink), 0);
      lv_obj_center(mark);
      g_sport.accent_dots[i] = dot;
      accent_dot_style(dot, i, i == prefs.accent);
    }

  name = label(list, velaride_accent()->name, TEXT_COLOR,
               &velaride_font_cn_20, 30, 110, 330);
  LV_UNUSED(name);
  section_label(list, "菜单样式", 156);
  option_row(list, "蜂窝", "图标排成蜂窝，可自由拖动", menu_style_set_cb,
             VELARIDE_MENU_HONEY, prefs.menu == VELARIDE_MENU_HONEY, 190);
  option_row(list, "列表", "按名称上下滚动", menu_style_set_cb,
             VELARIDE_MENU_LIST, prefs.menu == VELARIDE_MENU_LIST, 276);
  section_label(list, "转场动效", 372);
  for (i = 0; i < VELARIDE_TRANSITION_COUNT; i++)
    {
      option_row(list, titles[i], notes[i], transition_set_cb, i,
                 i == prefs.transition, 406 + i * 86);
    }

  g_sport.body = label(list, "", MUTED_COLOR, &velaride_font_cn_20,
                       30, 406 + VELARIDE_TRANSITION_COUNT * 86 + 6, 330);
}

static void wake_set_cb(lv_event_t *event)
{
  uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
  struct velaride_wake_prefs_s prefs;
  bool raise;
  uint8_t sleep;

  /* 0/1: raise off/on; 10 + n: sleep option n. */

  velaride_wake_prefs_get(&prefs);
  raise = value < 10 ? value != 0 : prefs.raise;
  sleep = value >= 10 ? value - 10 : prefs.sleep;
  if ((raise == prefs.raise && sleep == prefs.sleep) ||
      velaride_wake_prefs_set(raise, sleep) < 0)
    {
      return;
    }

  velaride_sport_ui_show_info(VELARIDE_INFO_WAKE);
  g_sport.info_from_settings = true;
}

static void build_wake(void)
{
  static const char *titles[VELARIDE_SLEEP_COUNT] =
    {"10 秒", "20 秒", "常亮"};
  static const char *notes[VELARIDE_SLEEP_COUNT] =
    {"最省电", "默认", "仅演示用，耗电较多"};
  struct velaride_wake_prefs_s prefs;
  lv_obj_t *list = setting_list();
  int i;

  velaride_wake_prefs_get(&prefs);
  section_label(list, "抬腕亮屏", 0);
  option_row(list, "开启", "抬起手腕看表时自动亮屏", wake_set_cb, 1,
             prefs.raise, 34);
  option_row(list, "关闭", "只在触摸或按键时亮屏", wake_set_cb, 0,
             !prefs.raise, 120);
  section_label(list, "无操作后熄屏", 216);
  for (i = 0; i < VELARIDE_SLEEP_COUNT; i++)
    {
      option_row(list, titles[i], notes[i], wake_set_cb, 10 + i,
                 i == prefs.sleep, 250 + i * 86);
    }

  g_sport.body = label(list, "", MUTED_COLOR, &velaride_font_cn_20,
                       30, 250 + VELARIDE_SLEEP_COUNT * 86 + 6, 330);
}

static void display_set_cb(lv_event_t *event)
{
  uintptr_t value = (uintptr_t)lv_event_get_user_data(event);
  struct velaride_display_s display;

  velaride_display_get(&display);
  velaride_display_set(value == 0 ? display.manual : value,
                       value == 0 ? !display.automatic : false);
  update_info();
}

void velaride_sport_ui_show_info(enum velaride_info_e info)
{
  static const char *titles[] =
    {"安全提醒", "连接与校时", "设置", "使用帮助", "屏幕设置",
     "指南针", "设备状态", "外观", "夜骑灯", "秒表", "水平仪",
     "亮屏与熄屏"
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
     , "倒计时"
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
     , "闹钟"
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
     , "声音与提示"
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
     , "夜骑模式"
#endif
    };

  if (g_sport.root == NULL || g_sport.session_started ||
      info < VELARIDE_INFO_SAFETY ||
      (unsigned int)info >= sizeof(titles) / sizeof(titles[0]))
    {
      return;
    }
  if (info == VELARIDE_INFO_HARDWARE)
    {
      velaride_ride_session_probe_sensor();
    }

  lv_timer_pause(g_sport.countdown_timer);
  g_sport.info = info;
  g_sport.info_from_settings = false;
  clear_page(SPORT_PAGE_INFO, titles[info]);
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  if (info == VELARIDE_INFO_COUNTDOWN)
    {
      build_countdown();
    }
  else
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  if (info == VELARIDE_INFO_ALARM)
    {
      build_alarm();
    }
  else
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  if (info == VELARIDE_INFO_SOUND)
    {
      build_sound();
    }
  else
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  if (info == VELARIDE_INFO_NIGHT)
    {
      build_night();
    }
  else
#endif
  if (info == VELARIDE_INFO_DEVICE)
    {
      lv_obj_t *list = setting_list();

      setting_row(list, "外观", VR_ICON_FACE, VELARIDE_INFO_APPEARANCE, 0);
      setting_row(list, "屏幕设置", VR_ICON_SUN, VELARIDE_INFO_DISPLAY, 1);
      setting_row(list, "亮屏与熄屏", VR_ICON_MOON, VELARIDE_INFO_WAKE, 2);
      setting_row(list, "指南针", VR_ICON_COMPASS, VELARIDE_INFO_COMPASS, 3);
      setting_row(list, "连接与校时", VR_ICON_BLUETOOTH,
                  VELARIDE_INFO_CONNECTION, 4);
      setting_row(list, "设备状态", VR_ICON_DEVICE, VELARIDE_INFO_HARDWARE, 5);
      setting_row(list, "使用帮助", VR_ICON_HELP, VELARIDE_INFO_HELP, 6);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      setting_row(list, "声音与提示", VR_ICON_SOUND, VELARIDE_INFO_SOUND, 7);
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
      setting_row(list, "夜骑模式", VR_ICON_MOON, VELARIDE_INFO_NIGHT,
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
                  8);
#else
                  7);
#endif
#endif
      g_sport.body = label(list, "", MUTED_COLOR, &velaride_font_cn_20,
                           18,
#if defined(CONFIG_EXAMPLES_VELARIDE_SOUND) && defined(CONFIG_EXAMPLES_VELARIDE_NIGHT)
                           9 * (SETTING_ROW_H + SETTING_ROW_GAP) + 6,
#elif defined(CONFIG_EXAMPLES_VELARIDE_SOUND) || defined(CONFIG_EXAMPLES_VELARIDE_NIGHT)
                           8 * (SETTING_ROW_H + SETTING_ROW_GAP) + 6,
#else
                           7 * (SETTING_ROW_H + SETTING_ROW_GAP) + 6,
#endif
                           354);
    }
  else if (info == VELARIDE_INFO_COMPASS)
    {
      build_compass();
    }
  else if (info == VELARIDE_INFO_LIGHT || info == VELARIDE_INFO_STOPWATCH ||
           info == VELARIDE_INFO_LEVEL)
    {
      if (info == VELARIDE_INFO_LIGHT)
        {
          build_light();
        }
      else if (info == VELARIDE_INFO_STOPWATCH)
        {
          build_stopwatch();
        }
      else
        {
          build_level();
        }

      g_sport.tool_tick = lv_tick_get();
      lv_timer_set_period(g_sport.tool_timer,
                          info == VELARIDE_INFO_STOPWATCH ? 40 : 50);
      lv_timer_reset(g_sport.tool_timer);
      lv_timer_resume(g_sport.tool_timer);
    }
  else if (info == VELARIDE_INFO_APPEARANCE)
    {
      build_appearance();
    }
  else if (info == VELARIDE_INFO_WAKE)
    {
      build_wake();
    }
  else if (info == VELARIDE_INFO_DISPLAY)
    {
      g_sport.body = label(g_sport.root, "", MUTED_COLOR,
                           &velaride_font_cn_20, 25, 110, 340);
      g_sport.setting_buttons[0] = button("柔和", BTN_TILE, 25, 224, 106, 48, display_set_cb, 20);
      g_sport.setting_buttons[1] = button("标准", BTN_TILE, 142, 224, 106, 48, display_set_cb, 50);
      g_sport.setting_buttons[2] = button("明亮", BTN_TILE, 259, 224, 106, 48, display_set_cb, 80);
      button_add_icon(g_sport.setting_buttons[0], VR_ICON_MOON);
      button_add_icon(g_sport.setting_buttons[1], VR_ICON_SUN);
      button_add_icon(g_sport.setting_buttons[2], VR_ICON_SUN);
      g_sport.auto_button = button("自动调光", BTN_SECONDARY, 25, 290, 340, 52,
                                   display_set_cb, 0);
      button_add_icon(g_sport.auto_button, VR_ICON_AUTO);
      label(g_sport.root, "点击档位改为手动\n遇强光快速增亮，变暗更平顺",
            MUTED_COLOR, &velaride_font_cn_20, 25, 355, 340);
    }
  else
    {
      g_sport.body = scroll_body("", 110, 258);
      button("知道了", BTN_PRIMARY, 70, 392, 250, 44, review_done_cb, 0);
    }
  update_info();
  reveal();
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  if (info == VELARIDE_INFO_STOPWATCH)
    {
      velaride_sound_request(VR_SOUND_STOPWATCH_MODE);
    }
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  else if (info == VELARIDE_INFO_ALARM)
    {
      velaride_sound_request(VR_SOUND_ALARM_MODE);
    }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  else if (info == VELARIDE_INFO_COUNTDOWN)
    {
      velaride_sound_request(VR_SOUND_COUNTDOWN_MODE);
    }
#endif
#endif
}
