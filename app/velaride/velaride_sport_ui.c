/****************************************************************************
 * apps/examples/velaride/velaride_sport_ui.c
 *
 * VelaRide cycling workout setup and active-session UI.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <lvgl/lvgl.h>

#include "velaride_sport_ui.h"
#include "velaride_ride_session.h"

#define SPORT_W 390
#define SPORT_H 450

extern const lv_font_t velaride_font_cn_20;
extern const lv_font_t velaride_font_cn_24;

enum sport_page_e
{
  SPORT_PAGE_HIDDEN = 0,
  SPORT_PAGE_INTRO,
  SPORT_PAGE_SELECT,
  SPORT_PAGE_GOAL,
  SPORT_PAGE_SETTINGS,
  SPORT_PAGE_ACTIVE,
  SPORT_PAGE_CONFIRM,
  SPORT_PAGE_ALERT,
  SPORT_PAGE_REVIEW,
};

struct sport_ui_s
{
  lv_obj_t *root;
  lv_obj_t *title;
  lv_obj_t *body;
  lv_obj_t *time_label;
  lv_obj_t *metric_label;
  lv_obj_t *pause_button;
  lv_timer_t *intro_timer;
  lv_timer_t *session_timer;
  lv_timer_t *sensor_timer;
  velaride_sport_exit_cb_t exit_cb;
  void *exit_arg;
  enum sport_page_e page;
  uint32_t elapsed_s;
  uint8_t sport_type;
  uint8_t goal_type;
  uint8_t zone;
  uint8_t sets;
  bool paused;
  bool session_started;
  struct velaride_ride_record_s latest_record;
  int persist_result;
};

static struct sport_ui_s g_sport;

static const char *sport_type_name(void)
{
  static const char *names[] =
  {
    "户外骑行",
    "户外骑行",
    "室内骑行",
    "自由训练",
  };

  return g_sport.sport_type < sizeof(names) / sizeof(names[0]) ?
         names[g_sport.sport_type] : names[0];
}

static void sport_format_session(char *time_text, size_t time_size,
                                 char *metrics, size_t metrics_size)
{
  struct velaride_ride_snapshot_s ride;
  const char *motion;
  double speed;
  double distance;

  velaride_ride_session_get(&ride);
  motion = !ride.sensor_ready ? "传感器未就绪" :
           ride.motion == VELARIDE_MOTION_MOVING ? "运动中" :
           ride.motion == VELARIDE_MOTION_STILL ? "静止" : "检测中";
  if (!velaride_ride_session_get_phone_data(&speed, &distance))
    {
      speed = ride.motion == VELARIDE_MOTION_MOVING ? 18.6 : 0.0;
      distance = ride.moving_s * 18.6 / 3600.0;
    }
  snprintf(time_text, time_size, "%02" PRIu32 ":%02" PRIu32,
           ride.elapsed_s / 60, ride.elapsed_s % 60);
  snprintf(metrics, metrics_size,
           "速度 %.1f km/h  距离 %.1f km\n状态 %s   心率 64 bpm",
           speed, distance, motion);
}

static void sport_opa_exec(void *object, int32_t value)
{
  lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)value, 0);
}

static lv_obj_t *sport_label(lv_obj_t *parent, const char *text,
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

static lv_obj_t *sport_button(lv_obj_t *parent, const char *text,
                              uint32_t color, int x, int y, int w, int h,
                              lv_event_cb_t cb, intptr_t value)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_size(button, w, h);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_style_radius(button, 18, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(button, 0, 0);
  lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED,
                      (void *)value);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_font(label, &velaride_font_cn_20, 0);
  lv_obj_center(label);
  return button;
}

static void sport_clear(uint32_t background)
{
  lv_obj_clean(g_sport.root);
  lv_obj_set_style_bg_color(g_sport.root, lv_color_hex(background), 0);
  lv_obj_set_style_bg_opa(g_sport.root, LV_OPA_COVER, 0);
  g_sport.title = NULL;
  g_sport.body = NULL;
  g_sport.time_label = NULL;
  g_sport.metric_label = NULL;
  g_sport.pause_button = NULL;
}

static void sport_show_select(void);
static void sport_show_goal(void);
static void sport_show_settings(void);
static void sport_show_active(void);
static void sport_show_alert(enum velaride_alert_e alert);
static void sport_show_review(void);

static void intro_timer_cb(lv_timer_t *timer)
{
  lv_timer_pause(timer);
  sport_show_select();
}

static void exit_now(void)
{
  if (g_sport.session_timer != NULL)
    {
      lv_timer_pause(g_sport.session_timer);
    }

  g_sport.page = SPORT_PAGE_HIDDEN;
  lv_obj_add_flag(g_sport.root, LV_OBJ_FLAG_HIDDEN);
  if (g_sport.exit_cb != NULL)
    {
      g_sport.exit_cb(g_sport.exit_arg);
    }
}

static void exit_button_cb(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);

  if (action == 1)
    {
      g_sport.persist_result =
        velaride_ride_session_finish(&g_sport.latest_record);
      g_sport.session_started = false;
      if (g_sport.session_timer != NULL)
        {
          lv_timer_pause(g_sport.session_timer);
        }

      sport_show_review();
      return;
    }

  if (action == 2)
    {
      sport_show_active();
      return;
    }

  sport_clear(0x08131c);
  g_sport.page = SPORT_PAGE_CONFIRM;
  sport_label(g_sport.root, "结束本次运动？", lv_color_hex(0xff6b35),
              &velaride_font_cn_24, 0, 92, SPORT_W);
  sport_label(g_sport.root, "本次骑行将保存\n用于 AI 复盘",
              lv_color_hex(0xcbd5e1), &velaride_font_cn_20,
              24, 160, SPORT_W - 48);
  sport_button(g_sport.root, "结束", 0xc2410c, 35, 278, 145, 64,
               exit_button_cb, 1);
  sport_button(g_sport.root, "继续", 0x14532d, 210, 278, 145, 64,
               exit_button_cb, 2);
}

static void pause_button_cb(lv_event_t *event)
{
  struct velaride_ride_snapshot_s ride;

  LV_UNUSED(event);
  velaride_ride_session_get(&ride);
  g_sport.paused = !ride.paused;
  velaride_ride_session_set_paused(g_sport.paused);
  lv_obj_t *label = lv_obj_get_child(g_sport.pause_button, 0);
  lv_label_set_text(label, g_sport.paused ? "继续" : "暂停");
  lv_obj_set_style_bg_color(g_sport.pause_button,
                            lv_color_hex(g_sport.paused ? 0x16a34a : 0xf59e0b),
                            0);
}

static void session_timer_cb(lv_timer_t *timer)
{
  char text[32];
  char metrics[128];
  LV_UNUSED(timer);
  struct velaride_ride_snapshot_s ride;

  if (g_sport.page == SPORT_PAGE_REVIEW)
    {
      char review[384];

      if (g_sport.body != NULL &&
          velaride_ride_session_load_review(review, sizeof(review)) == 0 &&
          strcmp(lv_label_get_text(g_sport.body), review) != 0)
        {
          lv_label_set_text(g_sport.title, "AI 骑行复盘");
          lv_label_set_text(g_sport.body, review);
        }

      return;
    }

  if (!g_sport.session_started)
    {
      return;
    }

  velaride_ride_session_second_tick();
  velaride_ride_session_get(&ride);
  g_sport.elapsed_s = ride.elapsed_s;
  g_sport.paused = ride.paused;
  if (g_sport.page == SPORT_PAGE_ACTIVE && ride.alert != VELARIDE_ALERT_NONE)
    {
      sport_show_alert(ride.alert);
      return;
    }

  if (g_sport.page != SPORT_PAGE_ACTIVE)
    {
      return;
    }

  sport_format_session(text, sizeof(text), metrics, sizeof(metrics));
  if (g_sport.time_label != NULL)
    {
      lv_label_set_text(g_sport.time_label, text);
    }
  if (g_sport.metric_label != NULL)
    {
      lv_label_set_text(g_sport.metric_label, metrics);
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

static void start_button_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  velaride_ride_session_start();
  g_sport.session_started = true;
  sport_show_active();
}

static void alert_button_cb(lv_event_t *event)
{
  intptr_t action = (intptr_t)lv_event_get_user_data(event);

  velaride_ride_session_ack_alert();
  if (action == 1)
    {
      velaride_ride_session_set_paused(true);
    }

  sport_show_active();
}

static void review_done_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  exit_now();
}

static void setting_button_cb(lv_event_t *event)
{
  intptr_t value = (intptr_t)lv_event_get_user_data(event);
  if (value >= 10)
    {
      g_sport.sets = value - 10;
    }
  else
    {
      g_sport.zone = value;
    }
  sport_show_settings();
}

static void goal_button_cb(lv_event_t *event)
{
  g_sport.goal_type = (uint8_t)(intptr_t)lv_event_get_user_data(event);
  sport_show_settings();
}

static void type_button_cb(lv_event_t *event)
{
  g_sport.sport_type = (uint8_t)(intptr_t)lv_event_get_user_data(event);
  sport_show_goal();
}

static void sport_show_select(void)
{
  sport_clear(0x07131b);
  g_sport.page = SPORT_PAGE_SELECT;
  sport_label(g_sport.root, "14:54", lv_color_white(),
              &lv_font_montserrat_16, 0, 16, SPORT_W);
  sport_label(g_sport.root, "选择运动", lv_color_hex(0xb7ff22),
              &velaride_font_cn_24, 20, 55, SPORT_W - 40);
  sport_button(g_sport.root, "户外骑行", 0x14532d,
               24, 120, 342, 82, type_button_cb, 1);
  sport_button(g_sport.root, "室内骑行", 0x2535a5,
               24, 220, 342, 82, type_button_cb, 2);
  sport_button(g_sport.root, "自由训练", 0x334155,
               24, 320, 342, 72, type_button_cb, 3);
}

static void sport_show_goal(void)
{
  sport_clear(0x07131b);
  g_sport.page = SPORT_PAGE_GOAL;
  sport_label(g_sport.root, "训练目标", lv_color_hex(0xb7ff22),
              &velaride_font_cn_24, 0, 42, SPORT_W);
  sport_button(g_sport.root, "开放目标", 0x1e3a5f,
               24, 105, 342, 70, goal_button_cb, 0);
  sport_button(g_sport.root, "30 分钟", 0x1e3a5f,
               24, 190, 342, 70, goal_button_cb, 1);
  sport_button(g_sport.root, "10 公里", 0x1e3a5f,
               24, 275, 342, 70, goal_button_cb, 2);
  sport_button(g_sport.root, "心率区间", 0x1e3a5f,
               24, 360, 342, 60, goal_button_cb, 3);
}

static void sport_show_settings(void)
{
  char summary[80];
  sport_clear(0x07131b);
  g_sport.page = SPORT_PAGE_SETTINGS;
  sport_label(g_sport.root, "骑行设置", lv_color_hex(0x60a5fa),
              &velaride_font_cn_24, 0, 34, SPORT_W);
  snprintf(summary, sizeof(summary), "心率区间 %u     %u 组",
           g_sport.zone, g_sport.sets);
  sport_label(g_sport.root, summary, lv_color_hex(0xcbd5e1),
              &velaride_font_cn_20, 0, 82, SPORT_W);
  sport_button(g_sport.root, "区间 1", 0x1e3a5f,
               24, 125, 106, 64, setting_button_cb, 1);
  sport_button(g_sport.root, "区间 2", 0x1e40af,
               142, 125, 106, 64, setting_button_cb, 2);
  sport_button(g_sport.root, "区间 3", 0x7c3aed,
               260, 125, 106, 64, setting_button_cb, 3);
  sport_button(g_sport.root, "2 组", 0x334155,
               46, 215, 138, 64, setting_button_cb, 12);
  sport_button(g_sport.root, "3 组", 0x334155,
               206, 215, 138, 64, setting_button_cb, 13);
  sport_button(g_sport.root, "开始骑行", 0xea580c,
               46, 320, 298, 72, start_button_cb, 0);
}

static void sport_show_active(void)
{
  char time_text[32];
  char metrics[128];
  struct velaride_ride_snapshot_s ride;
  bool resume_page = g_sport.page == SPORT_PAGE_CONFIRM ||
                     g_sport.page == SPORT_PAGE_ALERT;

  velaride_ride_session_get(&ride);
  bool paused = resume_page && ride.paused;

  sport_format_session(time_text, sizeof(time_text), metrics, sizeof(metrics));
  sport_clear(0x03080d);
  g_sport.page = SPORT_PAGE_ACTIVE;
  g_sport.paused = paused;
  sport_label(g_sport.root, sport_type_name(), lv_color_hex(0xb7ff22),
               &velaride_font_cn_20, 0, 20, SPORT_W);
  g_sport.time_label = sport_label(g_sport.root, time_text,
                                     lv_color_hex(0xffd60a),
                                     &lv_font_montserrat_48, 0, 68, SPORT_W);
  sport_label(g_sport.root, "运动时间", lv_color_hex(0x94a3b8),
              &velaride_font_cn_20, 0, 132, SPORT_W);
  g_sport.metric_label = sport_label(g_sport.root, metrics,
                                      lv_color_white(), &velaride_font_cn_20,
                                      20, 185, SPORT_W - 40);
  g_sport.pause_button = sport_button(g_sport.root,
                                       paused ? "继续" : "暂停",
                                       paused ? 0x16a34a : 0xf59e0b,
                                       28, 330, 150, 70,
                                       pause_button_cb, 0);
  sport_button(g_sport.root, "结束骑行", 0xb91c1c,
               212, 330, 150, 70, exit_button_cb, 0);
  if (g_sport.session_timer != NULL)
    {
      lv_timer_resume(g_sport.session_timer);
    }
}

static void sport_show_alert(enum velaride_alert_e alert)
{
  const char *title;
  const char *body;

  if (alert == VELARIDE_ALERT_IMPACT)
    {
      title = "检测到冲击";
      body = "随后持续静止\n请确认是否安全";
    }
  else if (alert == VELARIDE_ALERT_STILL)
    {
      title = "长时间静止";
      body = "是否需要暂停骑行？";
    }
  else
    {
      title = "休息提醒";
      body = "连续骑行时间较长\n建议休息一下";
    }

  sport_clear(0x2b1408);
  g_sport.page = SPORT_PAGE_ALERT;
  sport_label(g_sport.root, title, lv_color_hex(0xffb020),
              &velaride_font_cn_24, 0, 76, SPORT_W);
  sport_label(g_sport.root, body, lv_color_white(),
              &velaride_font_cn_20, 30, 146, SPORT_W - 60);
  sport_button(g_sport.root, "我没事", 0x166534,
               35, 288, 145, 68, alert_button_cb, 0);
  sport_button(g_sport.root, "暂停", 0x9a3412,
               210, 288, 145, 68, alert_button_cb, 1);
}

static void sport_show_review(void)
{
  char summary[180];

  sport_clear(0x07131b);
  g_sport.page = SPORT_PAGE_REVIEW;
  snprintf(summary, sizeof(summary),
           "总时长  %" PRIu32 " 秒\n"
           "有效运动  %" PRIu32 " 秒\n"
           "静止  %" PRIu32 " 秒\n"
           "暂停 %u 次   冲击 %u 次",
           g_sport.latest_record.elapsed_s,
           g_sport.latest_record.moving_s,
           g_sport.latest_record.still_s,
           g_sport.latest_record.pause_count,
           g_sport.latest_record.impact_count);
  g_sport.title = sport_label(g_sport.root, "本次骑行记录",
                              lv_color_hex(0xb7ff22),
                              &velaride_font_cn_24, 0, 30, SPORT_W);
  g_sport.body = sport_label(g_sport.root, summary, lv_color_white(),
                             &velaride_font_cn_20, 30, 88, SPORT_W - 60);
  sport_label(g_sport.root,
              g_sport.persist_result == 0 ? "记录已持久保存\n用于 AI 复盘" :
                                            "记录保存失败",
              lv_color_hex(g_sport.persist_result == 0 ? 0x4ade80 : 0xf87171),
              &velaride_font_cn_20, 0, 270, SPORT_W);
  sport_button(g_sport.root, "完成", 0x1d4ed8,
               70, 348, 250, 64, review_done_cb, 0);
}

void velaride_sport_ui_init(lv_obj_t *screen,
                            velaride_sport_exit_cb_t exit_cb,
                            void *exit_arg)
{
  memset(&g_sport, 0, sizeof(g_sport));
  g_sport.exit_cb = exit_cb;
  g_sport.exit_arg = exit_arg;
  g_sport.zone = 2;
  g_sport.sets = 3;
  velaride_ride_session_init();
  g_sport.root = lv_obj_create(screen);
  lv_obj_remove_style_all(g_sport.root);
  lv_obj_set_size(g_sport.root, SPORT_W, SPORT_H);
  lv_obj_set_pos(g_sport.root, 0, 0);
  lv_obj_set_style_bg_opa(g_sport.root, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_sport.root, LV_OBJ_FLAG_HIDDEN);
  g_sport.intro_timer = lv_timer_create(intro_timer_cb, 1200, NULL);
  lv_timer_pause(g_sport.intro_timer);
  g_sport.session_timer = lv_timer_create(session_timer_cb, 1000, NULL);
  lv_timer_pause(g_sport.session_timer);
  g_sport.sensor_timer = lv_timer_create(sensor_timer_cb, 250, NULL);
}

void velaride_sport_ui_enter(void)
{
  if (g_sport.root == NULL)
    {
      return;
    }

  g_sport.elapsed_s = 0;
  g_sport.session_started = false;
  g_sport.page = SPORT_PAGE_INTRO;
  sport_clear(0xff6433);
  sport_label(g_sport.root, LV_SYMBOL_PLAY, lv_color_hex(0x3b170c),
              &lv_font_montserrat_48, 0, 92, SPORT_W);
  sport_label(g_sport.root, "骑行训练", lv_color_hex(0x3b170c),
              &velaride_font_cn_24, 30, 190, SPORT_W - 60);
  sport_label(g_sport.root, "即将进入运动配置", lv_color_hex(0x65220f),
              &velaride_font_cn_20, 0, 245, SPORT_W);
  lv_obj_remove_flag(g_sport.root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_sport.root);
  lv_obj_set_style_opa(g_sport.root, LV_OPA_TRANSP, 0);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, g_sport.root);
  lv_anim_set_values(&animation, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&animation, 420);
  lv_anim_set_exec_cb(&animation, sport_opa_exec);
  lv_anim_start(&animation);
  lv_timer_reset(g_sport.intro_timer);
  lv_timer_resume(g_sport.intro_timer);
}

bool velaride_sport_ui_active(void)
{
  return g_sport.page != SPORT_PAGE_HIDDEN;
}
