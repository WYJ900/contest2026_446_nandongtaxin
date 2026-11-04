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
#include <math.h>
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
#include <nuttx/lcd/lcd_dev.h>
#include <nuttx/mtd/configdata.h>
#include <nuttx/mtd/mtd.h>
#include <lvgl/lvgl.h>

#include "velaride_sport_ui.h"
#include "velaride_ride_session.h"
#include "velaride_timekeeper.h"
#include "velaride_device.h"
#include "velaride_activity.h"
#include "velaride_icons.h"
#include "velaride_compass.h"
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

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
#  include <uv.h>
#endif

#define SCR_W 390
#define SCR_H 450
#define AXIS_LOCK_PX 12
#define COMMIT_FRACTION 0.4f
#define FLING_PROJECT_S 0.15f
#define FLING_SPEED 500.0f
#define DRAG_SAMPLES 6
#define DRAG_VELOCITY_MS 90
#define SPRING_K 520.0f
#define SPRING_C 41.0f
#define SPRING_REST_SPEED 40.0f
#define SPRING_MAX_SPEED 4000.0f
#define SPRING_PERIOD_MS 10
#define PRELOAD_DELAY_MS 120
#define BLINDS_COUNT 9
#define BLINDS_LAG 0.8f
#define IRIS_MAX_RADIUS 300
#define IRIS_MARGIN 2
#define IRIS_BAND 64
#define FACE_COUNT 3
#define FACE_PILL_X 30
#define FACE_PILL_Y 358
#define FACE_PILL_W 330
#define FACE_PILL_H 64
#define DIAL_CX 195
#define DIAL_CY 225
#define DIAL_HALF_W 179.0f
#define DIAL_HALF_H 209.0f
#define DIAL_CORNER 64.0f
#define RINGS_CY 288
#define HONEY_STEP 100
#define HONEY_DIAMETER 84
#define HONEY_LIMIT_X 150.0f
#define HONEY_LIMIT_Y 120.0f
#define HONEY_SPRING 180.0f
#define CC_SIZE 96
#define CC_X0 33
#define CC_STEP_X 112
#define CC_Y0 92
#define CC_STEP_Y 120
#define NOTE_MAX 8
#define NOTE_VISIBLE 4
#define NOTE_ROW_H 80
#define NOTE_Y0 84
#define NOTE_CLEAR_Y 408
#define BANNER_H 68
#define BANNER_Y 14
#define BANNER_SHOW_MS 3500
#define LIST_ROW_H 72
#define LIST_FADE_PX 64

#define BUTTON_POLL_MS 20
#define BUTTON_GUARD_MS 400
#define PREVIEW_DELAY_MS 180

/* Raise to wake. The accelerometer's +X points to the screen bottom and
 * +Y to the screen left (both confirmed on the board), so +Z points into
 * the screen: face up on a table reads -1 g on Z.
 */

#define WAKE_POLL_MS 50
#define WAKE_SCREEN_UP_SIGN (-1)
#define WAKE_FACING 0.60f       /* screen within ~53 deg of facing up */
#define WAKE_AWAY 0.25f         /* screen turned to the side or down */
#define WAKE_HOLD_MS 150        /* facing this long before waking */
#define WAKE_FROM_AWAY_MS 1500  /* ...having faced away this recently */
#define WAKE_DOWN_MS 800        /* turned away this long after a raise */
#define PREVIEW_NEXT_DELAY_MS 120
#define PREVIEW_COUNT (FACE_COUNT + 2)
#define PREVIEW_DATA_SIZE (SCR_W * SCR_H * 2)
#define PREVIEW_CACHE_ALIGN 64
#define PREVIEW_BUFFER_SIZE \
  ((PREVIEW_DATA_SIZE + PREVIEW_CACHE_ALIGN - 1) & \
   ~(PREVIEW_CACHE_ALIGN - 1))
#define REVIEW_TRANSFER_MAX 260

static volatile bool g_refresh_requested;
static volatile bool g_lcd_test_requested;
static volatile int g_lcd_control_requested;

/* Screen wake asks the frame timer to switch the panel: -1 nothing,
 * 0 display off, 1 draw the current frame and then display on.
 */

static volatile int g_screen_power_request = -1;
static bool g_lcd_test_active;
static lv_draw_buf_handlers_t g_preview_handlers;
static void launch_app(int app);

static bool g_face_record_refresh = true;

static char *g_review_transfer;
static size_t g_review_transfer_len;

enum scene_id_e
{
  SCENE_FACE_MODULAR = 0,
  SCENE_FACE_ANALOG,
  SCENE_FACE_RINGS,
  SCENE_CONTROL,
  SCENE_NOTES,
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
  lv_obj_t *link;
  lv_obj_t *dial;
  lv_obj_t *value[4];
  lv_obj_t *arc[2];
  int8_t hour;
  int8_t minute;
  int8_t second;
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
  int32_t pending_value;
  enum scene_id_e reveal_scene;
  int reveal_preview;
  int cover_preview;       /* >= 0: the centre leaves on its own snapshot */
  int8_t reveal_second;    /* second hand drawn over an analog snapshot */
  uint8_t mode;
  int8_t target_direction;
  uint8_t face_index;
  int8_t function_index;
  bool dragging;
  bool transition_active;
  bool commit_transition;
  bool caught;
  float spring_x;
  float spring_v;
  int32_t spring_target;
  uint32_t spring_tick;
  int32_t sample_value[DRAG_SAMPLES];
  uint32_t sample_tick[DRAG_SAMPLES];
  uint8_t sample_count;
  lv_timer_t *spring_timer;
  lv_timer_t *preload_timer;
  lv_timer_t *data_timer;
  lv_timer_t *preview_timer;
  lv_draw_buf_t preview_buf[PREVIEW_COUNT];
  void *preview_mem[PREVIEW_COUNT];
  enum scene_id_e preview_scene[PREVIEW_COUNT];
  bool preview_ready[PREVIEW_COUNT];
  uint8_t preview_count;
  uint8_t preview_next;
  uint32_t control_refresh;
  int8_t preview_minute;
  uint64_t perf_first_us;
  uint64_t perf_last_us;
  uint32_t perf_frames;
  uint32_t perf_max_gap_us;
  uint32_t perf_slow_frames;
};

/* Launcher entries; only apps with a finished page are listed. */

enum shell_app_e
{
  APP_RIDE = 0,
  APP_RECORDS,
  APP_REVIEW,
  APP_COMPASS,
  APP_SAFETY,
  APP_CONNECT,
  APP_DISPLAY,
  APP_SETTINGS,
  APP_HELP,
  APP_ABOUT,
  APP_LIGHT,
  APP_STOPWATCH,
  APP_LEVEL,
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  APP_COUNTDOWN,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  APP_ALARM,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  APP_SOUND,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  APP_NIGHT,
#endif
  APP_COUNT
};

struct shell_app_s
{
  const char *name;
  enum velaride_icon_e icon;
  uint32_t color;          /* 0 = the wearer's accent */
  int8_t q;                /* axial honeycomb coordinates */
  int8_t r;
};

static const struct shell_app_s g_apps[APP_COUNT] =
{
  {"骑行", VR_ICON_RIDE, 0, 0, 0},
  {"记录", VR_ICON_RECORD, 0x6c63d9, 1, 0},
  {"AI 复盘", VR_ICON_SPARK, 0xb06ad9, 0, 1},
  {"指南针", VR_ICON_COMPASS, 0x2fa79a, -1, 1},
  {"安全", VR_ICON_SAFETY, 0xe99a2c, 0, -1},
  {"连接", VR_ICON_BLUETOOTH, 0x3d6fe0, 1, -1},
  {"屏幕", VR_ICON_SUN, 0xc9a227, -1, 0},
  {"设置", VR_ICON_SETTINGS, 0x6e6e73, -1, -1},
  {"帮助", VR_ICON_HELP, 0x5b8db8, 2, -1},
  {"关于本机", VR_ICON_INFO, 0x4a4a50, 1, 1},
  {"夜骑灯", VR_ICON_BULB, 0xe5534b, 1, -2},
  {"秒表", VR_ICON_TIMER, 0x5b8db8, -1, 2},
  {"水平仪", VR_ICON_LEVEL, 0x7a9a3a, 2, 0},
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  {"倒计时", VR_ICON_TIMER, 0xc98536, -2, 1},
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  {"闹钟", VR_ICON_TIMER, 0xc95c54, -2, 0},
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  {"声音", VR_ICON_SOUND, 0x9571c9, -2, 2},
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  {"夜骑模式", VR_ICON_MOON, 0xc25448, 0, 2},
#endif
};

enum cc_item_e
{
  CC_LIGHT = 0,
  CC_DND,
  CC_AUTO,
  CC_BRIGHT,
  CC_CONNECT,
  CC_SETTINGS,
  CC_COUNT
};

static const struct
{
  const char *name;
  enum velaride_icon_e icon;
  bool toggle;
} g_cc_items[CC_COUNT] =
{
  [CC_LIGHT] = {"夜骑灯", VR_ICON_BULB, false},
  [CC_DND] = {"勿扰", VR_ICON_MOON, true},
  [CC_AUTO] = {"自动", VR_ICON_AUTO, true},
  [CC_BRIGHT] = {"亮度", VR_ICON_SUN, false},
  [CC_CONNECT] = {"连接", VR_ICON_BLUETOOTH, false},
  [CC_SETTINGS] = {"设置", VR_ICON_SETTINGS, false},
};

enum note_kind_e
{
  NOTE_REVIEW = 0,
  NOTE_REST,
  NOTE_LINK_UP,
  NOTE_LINK_DOWN,
  NOTE_ACTIVITY,
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  NOTE_COUNTDOWN,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  NOTE_ALARM,
#endif
  NOTE_KIND_COUNT
};

struct note_s
{
  enum note_kind_e kind;
  time_t when;
};

static struct
{
  struct note_s items[NOTE_MAX];
  int count;
  bool dnd;
  bool connected;
  bool rest_posted;
  bool watch_started;
  lv_obj_t *banner;
  lv_obj_t *banner_title;
  lv_obj_t *banner_body;
  lv_timer_t *banner_timer;
} g_notes;

struct honey_cell_s
{
  int16_t x;
  int16_t y;
  int16_t diameter;
};

struct honey_menu_s
{
  lv_obj_t *root;
  lv_obj_t *title;
  lv_obj_t *list;
  lv_obj_t *rows[APP_COUNT];
  lv_opa_t row_mix[APP_COUNT];
  struct honey_cell_s cells[APP_COUNT];
  int selected;
  float offset_x;
  float offset_y;
  float velocity_x;
  float velocity_y;
  lv_point_t press_point;
  lv_point_t last_point;
  uint32_t last_tick;
  uint32_t inertia_tick;
  int button_fd;
  btn_buttonset_t button_last;
  uint64_t button_action_us;
  bool button_has_action;
  bool active;
  bool list_mode;
  bool riding;
  bool return_to_menu;
  bool dragging;
  bool moved;
  bool perf_active;
  uint64_t perf_first_us;
  uint64_t perf_last_us;
  uint32_t perf_frames;
  uint32_t perf_max_gap_us;
  uint32_t perf_slow_frames;
};

static struct nav_shell_s g_nav;

/* Per-transition totals since boot, read with "velaride nav-stats". */

struct nav_stats_s
{
  uint32_t swipes;
  uint32_t frames;
  uint64_t span_us;
  uint32_t max_gap_us;
  uint32_t slow_frames;   /* gap above NAV_SLOW_GAP_US */
};

#define NAV_SLOW_GAP_US 50000

static struct nav_stats_s g_nav_stats[VELARIDE_TRANSITION_COUNT];
static struct nav_stats_s g_menu_stats[VELARIDE_MENU_COUNT];
static struct honey_menu_s g_menu;

static void preview_schedule(void);
static void create_control_center(struct scene_slot_s *slot);
static void create_note_center(struct scene_slot_s *slot);
static void cc_status(struct scene_slot_s *slot);
static void cc_style(struct scene_slot_s *slot);
static void cc_tap(lv_point_t point);
static void screen_wake(bool raise);
static void note_tap(lv_point_t point);
static void note_watch(void);
#if defined(CONFIG_EXAMPLES_VELARIDE_COUNTDOWN) || defined(CONFIG_EXAMPLES_VELARIDE_ALARM)
static void note_post(enum note_kind_e kind);
#endif

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
  LV_UNUSED(scene);
  return lv_color_hex(VR_BG);
}

static const char *scene_preview_title(enum scene_id_e scene)
{
  static const char *titles[SCENE_COUNT] =
  {
    "MODULAR", "ANALOG", "RINGS", "CONTROL", "NOTICES"
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
  slot->link = NULL;
  slot->dial = NULL;
  memset(slot->value, 0, sizeof(slot->value));
  memset(slot->arc, 0, sizeof(slot->arc));

  if (preview)
    {
      title = make_label(slot->root, scene_preview_title(scene),
                         lv_color_hex(VR_INK2),
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

/* Label updates invalidate the label even for identical text; faces refresh
 * every second, so skip unchanged strings to keep idle redraws small.
 */

static void set_text(lv_obj_t *label, const char *text)
{
  if (label != NULL && strcmp(lv_label_get_text(label), text) != 0)
    {
      lv_label_set_text(label, text);
    }
}

static void format_duration(char *text, size_t size, uint32_t seconds)
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

/* Reads NVS at most every 30 s, or right after a ride or review changes. */

static bool face_latest(struct velaride_ride_record_s *record, bool *review)
{
  static struct velaride_ride_record_s latest;
  static bool has_latest;
  static bool has_review;
  static uint32_t last_read;
  char text[REVIEW_TRANSFER_MAX + 1];

  if (g_face_record_refresh || lv_tick_elaps(last_read) >= 30000)
    {
      has_latest = velaride_ride_session_load_latest(&latest) == 0;
      has_review = velaride_ride_session_load_review(text,
                                                     sizeof(text)) == 0;
      g_face_record_refresh = false;
      last_read = lv_tick_get();
    }

  *record = latest;
  *review = has_review;
  return has_latest;
}

static bool face_local_time(struct tm *local)
{
  time_t now = time(NULL);

  return now >= 1704067200 && localtime_r(&now, local) != NULL;
}

static void face_date_text(char *text, size_t size)
{
  static const char *weekdays[] =
    {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  struct tm local;

  if (!face_local_time(&local))
    {
      snprintf(text, size, "待校时");
      return;
    }

  snprintf(text, size, "%s %d日", weekdays[local.tm_wday], local.tm_mday);
}

static lv_obj_t *face_tile(lv_obj_t *parent, int x, int y, int w, int h)
{
  lv_obj_t *tile = lv_obj_create(parent);

  lv_obj_remove_style_all(tile);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_size(tile, w, h);
  lv_obj_set_style_radius(tile, 24, 0);
  lv_obj_set_style_bg_color(tile, lv_color_hex(VR_TILE), 0);
  lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return tile;
}

static lv_obj_t *face_arc(lv_obj_t *parent, int cx, int cy, int radius,
                          int width, lv_opa_t track, lv_opa_t indicator)
{
  lv_obj_t *arc = lv_arc_create(parent);
  int size = (radius + width / 2) * 2;

  lv_obj_remove_style_all(arc);
  lv_obj_set_size(arc, size, size);
  lv_obj_set_pos(arc, cx - size / 2, cy - size / 2);
  lv_arc_set_rotation(arc, 270);
  lv_arc_set_bg_angles(arc, 0, 360);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, 0);
  lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, vr_accent(), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(arc, track, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, vr_accent(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(arc, indicator, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return arc;
}

static void update_face_detail(struct scene_slot_s *slot)
{
  struct velaride_ride_record_s latest;
  bool has_latest;
  bool has_review;
  char text[48];

  if (slot->scene > SCENE_FACE_RINGS || slot->detail == NULL)
    {
      return;
    }

  face_date_text(text, sizeof(text));
  set_text(slot->secondary, text);
  has_latest = face_latest(&latest, &has_review);

  if (slot->scene == SCENE_FACE_MODULAR)
    {
      set_text(slot->link, velaride_ride_session_connected() ?
                           "已连接" : "未连接");
      if (has_latest)
        {
          format_duration(text, sizeof(text), latest.elapsed_s);
        }
      else
        {
          snprintf(text, sizeof(text), "--");
        }

      set_text(slot->value[0], text);
      lv_arc_set_value(slot->arc[0], has_latest && latest.elapsed_s > 0 ?
                       (int32_t)((uint64_t)latest.moving_s * 1000 /
                                 latest.elapsed_s) : 0);
      set_text(slot->value[1], has_review ? "已同步" : "未同步");
    }
  else if (slot->scene == SCENE_FACE_RINGS)
    {
      uint32_t elapsed = has_latest ? latest.elapsed_s : 0;
      int32_t moving = elapsed ? (int32_t)((uint64_t)latest.moving_s *
                                           1000 / elapsed) : 0;
      int32_t still = elapsed ? (int32_t)((uint64_t)latest.still_s *
                                          1000 / elapsed) : 0;
      char value[16];

      lv_arc_set_value(slot->arc[0], moving);
      lv_arc_set_value(slot->arc[1], still);
      if (!has_latest)
        {
          set_text(slot->value[0], "还没有骑行记录");
          set_text(slot->value[1], "");
          set_text(slot->value[2], "");
          set_text(slot->value[3], "--");
          return;
        }

      format_duration(value, sizeof(value), latest.moving_s);
      snprintf(text, sizeof(text), "运动 %s", value);
      set_text(slot->value[0], text);
      format_duration(value, sizeof(value), latest.still_s);
      snprintf(text, sizeof(text), "静止 %s", value);
      set_text(slot->value[1], text);
      snprintf(text, sizeof(text), "冲击 %u 次", latest.impact_count);
      set_text(slot->value[2], text);
      snprintf(text, sizeof(text), "%" PRId32 "%%", (moving + 5) / 10);
      set_text(slot->value[3], text);
    }
}

static void face_set_time(struct scene_slot_s *slot);

static void face_pill_draw_cb(lv_event_t *event)
{
  lv_obj_t *pill = lv_event_get_target(event);
  lv_area_t area;

  lv_obj_get_coords(pill, &area);
  velaride_icon_draw(lv_event_get_layer(event), VR_ICON_RIDE,
                     area.x1 + 104, area.y1 + 19, 26, vr_accent_ink());
}

static void create_modular_face(struct scene_slot_s *slot)
{
  lv_obj_t *tile;
  lv_obj_t *pill;
  lv_obj_t *caption;

  slot->secondary = make_label(slot->detail, "", vr_accent(),
                               &velaride_font_cn_20, 44, 40, 0);
  slot->link = make_label(slot->detail, "", lv_color_hex(VR_INK2),
                          &velaride_font_cn_20, 206, 40, 140);
  lv_obj_set_style_text_align(slot->link, LV_TEXT_ALIGN_RIGHT, 0);
  slot->primary = make_label(slot->detail, "--:--", lv_color_hex(VR_INK),
                             &velaride_num_136, 0, 84, SCR_W);
  lv_obj_set_style_text_letter_space(slot->primary, -4, 0);

  tile = face_tile(slot->detail, 24, 214, 165, 90);
  slot->arc[0] = face_arc(tile, 38, 45, 19, 6, LV_OPA_20, LV_OPA_COVER);
  slot->value[0] = make_label(tile, "--", lv_color_hex(VR_INK),
                              &velaride_num_24, 70, 20, 0);
  make_label(tile, "最近骑行", lv_color_hex(VR_INK2),
             &velaride_font_cn_20, 70, 50, 0);

  tile = face_tile(slot->detail, 201, 214, 165, 90);
  slot->value[1] = make_label(tile, "", lv_color_hex(VR_INK),
                              &velaride_font_cn_20, 18, 20, 0);
  make_label(tile, "AI 复盘", lv_color_hex(VR_INK2),
             &velaride_font_cn_20, 18, 50, 0);

  pill = lv_obj_create(slot->detail);
  lv_obj_remove_style_all(pill);
  lv_obj_set_pos(pill, FACE_PILL_X, FACE_PILL_Y);
  lv_obj_set_size(pill, FACE_PILL_W, FACE_PILL_H);
  lv_obj_set_style_radius(pill, FACE_PILL_H / 2, 0);
  lv_obj_set_style_bg_color(pill, vr_accent(), 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(pill, face_pill_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
  caption = make_label(pill, "开始骑行", vr_accent_ink(),
                       &velaride_font_cn_20, 0, 0, 0);
  lv_obj_align(caption, LV_ALIGN_CENTER, 18, 0);
  face_set_time(slot);
  update_face_detail(slot);
}

/* Distance from the dial centre to the screen's rounded-rectangle edge
 * along the unit direction (|sin|, |cos|).
 */

static float dial_edge(float ax, float ay)
{
  const float hw = DIAL_HALF_W;
  const float hh = DIAL_HALF_H;
  const float r = DIAL_CORNER;
  float t = fminf(ax > 1e-4f ? hw / ax : 1e9f, ay > 1e-4f ? hh / ay : 1e9f);

  if (ax * t > hw - r && ay * t > hh - r)
    {
      float cx = hw - r;
      float cy = hh - r;
      float b = ax * cx + ay * cy;
      float c = cx * cx + cy * cy - r * r;

      t = b + sqrtf(b * b - c);
    }

  return t;
}

static void dial_line(lv_layer_t *layer, float s, float c, float r1, float r2,
                      int width, lv_color_t color, lv_opa_t opa)
{
  lv_draw_line_dsc_t line;

  lv_draw_line_dsc_init(&line);
  line.color = velaride_night_color(color);
  line.opa = opa;
  line.width = width;
  line.round_start = 1;
  line.round_end = 1;
  line.p1.x = DIAL_CX + s * r1;
  line.p1.y = DIAL_CY - c * r1;
  line.p2.x = DIAL_CX + s * r2;
  line.p2.y = DIAL_CY - c * r2;
  lv_draw_line(layer, &line);
}

static void dial_dot(lv_layer_t *layer, int radius, lv_color_t color)
{
  lv_draw_rect_dsc_t dot;
  lv_area_t area;

  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.bg_color = velaride_night_color(color);
  dot.bg_opa = LV_OPA_COVER;
  area.x1 = DIAL_CX - radius;
  area.y1 = DIAL_CY - radius;
  area.x2 = DIAL_CX + radius;
  area.y2 = DIAL_CY + radius;
  lv_draw_rect(layer, &dot, &area);
}

static void dial_hand(lv_layer_t *layer, float degrees, int length)
{
  float a = degrees * 0.0174532925f;
  float s = sinf(a);
  float c = cosf(a);

  dial_line(layer, s, c, 0, 20, 3, lv_color_hex(VR_INK), LV_OPA_COVER);
  dial_line(layer, s, c, 20, length, 11, lv_color_hex(VR_INK),
            LV_OPA_COVER);
  dial_line(layer, s, c, 27, length - 6, 4, lv_color_black(), LV_OPA_40);
}

static void dial_draw_cb(lv_event_t *event)
{
  static const char *numbers[] = {"12", "3", "6", "9"};
  struct scene_slot_s *slot = lv_event_get_user_data(event);
  lv_layer_t *layer = lv_event_get_layer(event);
  lv_draw_label_dsc_t text;
  lv_area_t area;
  char date[24];
  float a;
  int i;

  for (i = 0; i < 60; i++)
    {
      float s = sinf(i * 6 * 0.0174532925f);
      float c = cosf(i * 6 * 0.0174532925f);
      float t = dial_edge(fabsf(s), fabsf(c));
      bool major = i % 5 == 0;

      dial_line(layer, s, c, t, t - (major ? 20 : 9), major ? 4 : 2,
                lv_color_hex(major ? VR_INK : 0x4a4a4f), LV_OPA_COVER);
    }

  lv_draw_label_dsc_init(&text);
  text.font = &velaride_num_44;
  text.color = velaride_night_color(lv_color_hex(VR_INK));
  text.align = LV_TEXT_ALIGN_CENTER;
  for (i = 0; i < 4; i++)
    {
      float s = sinf(i * 90 * 0.0174532925f);
      float c = cosf(i * 90 * 0.0174532925f);
      float t = dial_edge(fabsf(s), fabsf(c)) - 52;
      int x = DIAL_CX + (int)(s * t);
      int y = DIAL_CY - (int)(c * t);

      text.text = numbers[i];
      area.x1 = x - 40;
      area.x2 = x + 40;
      area.y1 = y - velaride_num_44.line_height / 2;
      area.y2 = area.y1 + velaride_num_44.line_height;
      lv_draw_label(layer, &text, &area);
    }

  face_date_text(date, sizeof(date));
  text.font = &velaride_font_cn_20;
  text.color = velaride_night_color(vr_accent());
  text.text = date;
  area.x1 = DIAL_CX - 100;
  area.x2 = DIAL_CX + 100;
  area.y1 = DIAL_CY - 112;
  area.y2 = area.y1 + 24;
  lv_draw_label(layer, &text, &area);

  dial_hand(layer, (slot->hour % 12) * 30.0f + slot->minute * 0.5f, 110);
  dial_hand(layer, slot->minute * 6.0f, 170);

  /* A snapshot is minutes old by the time it is shown; the transition
   * paints the second hand over it at the current second instead.
   */

  if (slot != &g_nav.preview_slot)
    {
      a = slot->second * 6 * 0.0174532925f;
      dial_line(layer, sinf(a), cosf(a), -30, 185, 2, vr_accent(),
                LV_OPA_COVER);
    }

  dial_dot(layer, 7, vr_accent());
  dial_dot(layer, 3, lv_color_black());
}

static void second_hand_area(int second, lv_area_t *area)
{
  float a = second * 6 * 0.0174532925f;
  float s = sinf(a);
  float c = cosf(a);
  int32_t x1 = DIAL_CX - (int32_t)(s * 30);
  int32_t y1 = DIAL_CY + (int32_t)(c * 30);
  int32_t x2 = DIAL_CX + (int32_t)(s * 185);
  int32_t y2 = DIAL_CY - (int32_t)(c * 185);

  /* Include the centre cap, which is drawn over the hand. */

  area->x1 = LV_MIN(LV_MIN(x1, x2), DIAL_CX - 8) - 3;
  area->x2 = LV_MAX(LV_MAX(x1, x2), DIAL_CX + 8) + 3;
  area->y1 = LV_MIN(LV_MIN(y1, y2), DIAL_CY - 8) - 3;
  area->y2 = LV_MAX(LV_MAX(y1, y2), DIAL_CY + 8) + 3;
}

static void face_set_time(struct scene_slot_s *slot)
{
  struct tm local;
  char text[8];

  if (!face_local_time(&local))
    {
      memset(&local, 0, sizeof(local));
      local.tm_hour = 10;
      local.tm_min = 10;
    }

  if (slot->dial != NULL)
    {
      if (slot->hour != local.tm_hour || slot->minute != local.tm_min)
        {
          slot->hour = local.tm_hour;
          slot->minute = local.tm_min;
          slot->second = local.tm_sec;
          lv_obj_invalidate(slot->dial);
        }
      else if (slot->second != local.tm_sec)
        {
          lv_area_t area;

          second_hand_area(slot->second, &area);
          lv_obj_invalidate_area(slot->dial, &area);
          slot->second = local.tm_sec;
          second_hand_area(slot->second, &area);
          lv_obj_invalidate_area(slot->dial, &area);
        }

      return;
    }

  snprintf(text, sizeof(text), "%02d:%02d", local.tm_hour, local.tm_min);
  set_text(slot->primary, text);
}

static void create_analog_face(struct scene_slot_s *slot)
{
  slot->dial = lv_obj_create(slot->detail);
  lv_obj_remove_style_all(slot->dial);
  lv_obj_set_size(slot->dial, SCR_W, SCR_H);
  lv_obj_set_pos(slot->dial, 0, 0);
  lv_obj_remove_flag(slot->dial,
                     LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(slot->dial, dial_draw_cb, LV_EVENT_DRAW_MAIN, slot);
  slot->hour = -1;
  face_set_time(slot);
}

static void create_rings_face(struct scene_slot_s *slot)
{
  static const lv_opa_t dots[] = {LV_OPA_COVER, LV_OPA_60, LV_OPA_30};
  int i;

  slot->primary = make_label(slot->detail, "--:--", lv_color_hex(VR_INK),
                             &velaride_num_60, 38, 38, 0);
  slot->secondary = make_label(slot->detail, "", lv_color_hex(VR_INK2),
                               &velaride_font_cn_20, 40, 98, 0);
  for (i = 0; i < 3; i++)
    {
      lv_obj_t *dot = lv_obj_create(slot->detail);

      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, 8, 8);
      lv_obj_set_pos(dot, 222, 49 + i * 28);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(dot, vr_accent(), 0);
      lv_obj_set_style_bg_opa(dot, dots[i], 0);
      lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      slot->value[i] = make_label(slot->detail, "", lv_color_hex(VR_INK2),
                                  &velaride_font_cn_20, 238, 40 + i * 28, 0);
    }

  slot->arc[0] = face_arc(slot->detail, DIAL_CX, RINGS_CY, 118, 22,
                          LV_OPA_20, LV_OPA_COVER);
  slot->arc[1] = face_arc(slot->detail, DIAL_CX, RINGS_CY, 90, 22,
                          LV_OPA_10, LV_OPA_60);
  slot->value[3] = make_label(slot->detail, "--", lv_color_hex(VR_INK),
                              &velaride_num_44, 0,
                              RINGS_CY - velaride_num_44.line_height + 4,
                              SCR_W);
  make_label(slot->detail, "运动占比", lv_color_hex(VR_INK2),
             &velaride_font_cn_20, 0, RINGS_CY + 8, SCR_W);
  face_set_time(slot);
  update_face_detail(slot);
}

static void build_scene(struct scene_slot_s *slot, enum scene_id_e scene,
                        bool preview)
{
  slot_reset(slot, scene, preview);
  if (preview)
    {
      return;
    }

  if (scene == SCENE_FACE_MODULAR)
    {
      create_modular_face(slot);
    }
  else if (scene == SCENE_FACE_ANALOG)
    {
      create_analog_face(slot);
    }
  else if (scene == SCENE_FACE_RINGS)
    {
      create_rings_face(slot);
    }
  else if (scene == SCENE_CONTROL)
    {
      create_control_center(slot);
    }
  else
    {
      create_note_center(slot);
    }
}

static enum scene_id_e current_scene(void)
{
  if (g_nav.function_index < 0)
    {
      return (enum scene_id_e)(SCENE_FACE_MODULAR + g_nav.face_index);
    }

  return g_nav.function_index == 0 ? SCENE_CONTROL : SCENE_NOTES;
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

      *scene = (enum scene_id_e)(SCENE_FACE_MODULAR + next);
      return true;
    }

  if (g_nav.function_index < 0)
    {
      *scene = direction > 0 ? SCENE_CONTROL : SCENE_NOTES;
      return true;
    }

  /* From a centre only the opposite swipe leads back to the face. */

  if ((g_nav.function_index == 0 && direction < 0) ||
      (g_nav.function_index == 1 && direction > 0))
    {
      *scene = (enum scene_id_e)(SCENE_FACE_MODULAR + g_nav.face_index);
      return true;
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
              (enum scene_id_e)(SCENE_FACE_MODULAR + index), false);
  lv_obj_update_layout(g_nav.preview_slot.root);

  if (g_nav.preview_mem[index] == NULL)
    {
      void *aligned;

      g_nav.preview_mem[index] = lv_malloc(PREVIEW_BUFFER_SIZE +
                                          PREVIEW_CACHE_ALIGN - 1);
      aligned = (void *)(((uintptr_t)g_nav.preview_mem[index] +
                          PREVIEW_CACHE_ALIGN - 1) &
                         ~((uintptr_t)PREVIEW_CACHE_ALIGN - 1));
      if (g_nav.preview_mem[index] == NULL ||
          lv_draw_buf_init(&g_nav.preview_buf[index], SCR_W, SCR_H,
                           LV_COLOR_FORMAT_RGB565, SCR_W * 2,
                           aligned, PREVIEW_BUFFER_SIZE) !=
            LV_RESULT_OK)
        {
          printf("VELARIDE_PREVIEW: allocate/init failed\n");
          g_nav.preview_ready[index] = false;
          return;
        }

      /* These PSRAM snapshot buffers are rendered
       * by the CPU.  Snapshot completion must clean dirty pixels before
       * invalidation; a plain invalidate discards the software drawing.
       */

      g_nav.preview_buf[index].handlers = &g_preview_handlers;
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

/* Rebuild one scene's snapshot after its content changed. */

static void preview_invalidate(enum scene_id_e scene)
{
  g_nav.preview_ready[scene] = false;
  preview_schedule();
}

/* Face previews carry the time; redo them all, one per timer tick. */

static void preview_refresh_all(void)
{
  if (g_nav.preview_timer == NULL)
    {
      return;
    }

  g_nav.preview_count = PREVIEW_COUNT;
  g_nav.preview_next = 0;
  lv_timer_set_period(g_nav.preview_timer, PREVIEW_DELAY_MS);
  lv_timer_reset(g_nav.preview_timer);
  lv_timer_resume(g_nav.preview_timer);
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

/* Transition geometry. The reveal value follows the finger in pixels and
 * keeps the sign of the drag. A full-screen redraw takes over 100 ms on this
 * CPU renderer, so each frame only invalidates the strips that changed.
 *
 *  - slide:  one strip grows from the edge the finger left;
 *  - blinds: BLINDS_COUNT strips, each starting a little after the previous
 *            one, so the page opens in a wave;
 *  - iris:   a circle grows from the screen centre; needs a cached preview,
 *            otherwise it falls back to slide.
 */

static int32_t nav_length(void)
{
  return g_nav.axis == AXIS_H ? SCR_W : SCR_H;
}

static int32_t nav_progress(int32_t value)
{
  return LV_MIN(LV_ABS(value), nav_length());
}

static void nav_span_area(int32_t a1, int32_t a2, lv_area_t *area)
{
  if (g_nav.axis == AXIS_H)
    {
      area->x1 = a1;
      area->x2 = a2 - 1;
      area->y1 = 0;
      area->y2 = SCR_H - 1;
    }
  else
    {
      area->x1 = 0;
      area->x2 = SCR_W - 1;
      area->y1 = a1;
      area->y2 = a2 - 1;
    }
}

/* Along-axis span [a1, a2) revealed in strip k for the given progress. */

static bool nav_strip_span(int k, int count, int32_t progress,
                           int32_t *a1, int32_t *a2)
{
  int32_t length = nav_length();
  float lag = count > 1 ? BLINDS_LAG : 0.0f;
  float f = (float)progress / length * (1.0f + lag) -
            (count > 1 ? lag * k / (count - 1) : 0.0f);
  int32_t start = length * k / count;
  int32_t size = length * (k + 1) / count - start;
  int32_t covered;

  if (f <= 0.0f)
    {
      return false;
    }

  covered = f >= 1.0f ? size : (int32_t)(f * size + 0.5f);
  if (covered <= 0)
    {
      return false;
    }

  if (g_nav.target_direction < 0)
    {
      /* Content enters from the far edge; strip 0 sits at that edge. */

      *a2 = length - start;
      *a1 = *a2 - covered;
    }
  else
    {
      *a1 = start;
      *a2 = start + covered;
    }

  return true;
}

static int nav_strip_count(void)
{
  return g_nav.mode == VELARIDE_TRANSITION_BLINDS ? BLINDS_COUNT : 1;
}

static int32_t iris_radius(int32_t value)
{
  return nav_progress(value) * IRIS_MAX_RADIUS / nav_length();
}

/* Widest half-width of the circle within rows [y1, y2], or -1. */

static int32_t iris_half_width(int32_t radius, int32_t y1, int32_t y2)
{
  int32_t dy;

  if (radius <= 0)
    {
      return -1;
    }

  /* Drawing rows may straddle two bands; widen by one row. */

  dy = y1 <= SCR_H / 2 && y2 >= SCR_H / 2 ? 0 :
       LV_MAX(0, LV_MIN(LV_ABS(y1 - SCR_H / 2), LV_ABS(y2 - SCR_H / 2)) -
                 IRIS_MARGIN);
  if (dy >= radius)
    {
      return -1;
    }

  return (int32_t)sqrtf((float)(radius * radius - dy * dy)) + 1;
}

/* When the target page has a cached snapshot, the revealed pixels are copied
 * straight into each rendered block just before it is sent to the panel.
 * This replaces one LVGL image task per strip or circle row: LVGL checks
 * every new task against the earlier ones, so a circle drawn as ~150 row
 * tasks cost far more than the pixels it copies. Rows are exact, so the
 * circle edge is smooth to the pixel row.
 */

static uint16_t rgb565_mix(uint16_t back, uint16_t fore, uint32_t a)
{
  uint32_t r = ((back >> 11) * (256 - a) + (fore >> 11) * a) >> 8;
  uint32_t g = (((back >> 5) & 0x3f) * (256 - a) +
                ((fore >> 5) & 0x3f) * a) >> 8;
  uint32_t b = ((back & 0x1f) * (256 - a) + (fore & 0x1f) * a) >> 8;

  return (uint16_t)((r << 11) | (g << 5) | b);
}

/* Coverage 0..256 of a disc or a stroke whose edge is `dist` away. */

static uint32_t edge_cover(float inside)
{
  return inside >= 1.0f ? 256 : inside <= 0.0f ? 0 :
         (uint32_t)(inside * 256.0f);
}

/* Second hand and centre cap of the analog face for one copied row, drawn
 * the way dial_draw_cb does: a 2 px stroke from -30 to 185 px, an accent
 * cap of radius 7 and a black dot of radius 3 over it.
 */

static void overlay_second_hand(uint16_t *row, int32_t row_x1, int32_t y,
                                int32_t x1, int32_t x2)
{
  float a = g_nav.reveal_second * 6 * 0.0174532925f;
  float ux = sinf(a);
  float uy = -cosf(a);
  float px = DIAL_CX - ux * 30.0f;
  float py = DIAL_CY - uy * 30.0f;
  float len = 215.0f;
  float fy = y + 0.5f - py;
  float lo;
  float hi;
  uint16_t accent = lv_color_to_u16(vr_accent());
  int32_t x;

  /* Columns this row can touch: the stroke where it crosses the row,
   * widened by its half width, and the cap.
   */

  if (fabsf(uy) > 0.01f)
    {
      float t1 = LV_CLAMP(0.0f, (fy - 2.5f) / uy, len);
      float t2 = LV_CLAMP(0.0f, (fy + 2.5f) / uy, len);

      lo = px + ux * LV_MIN(t1, t2) - 3.0f;
      hi = px + ux * LV_MAX(t1, t2) + 3.0f;
    }
  else if (fabsf(fy) <= 2.5f)
    {
      lo = px + LV_MIN(0.0f, ux * len) - 3.0f;
      hi = px + LV_MAX(0.0f, ux * len) + 3.0f;
    }
  else
    {
      lo = 1.0f;
      hi = 0.0f;
    }

  if (LV_ABS(y - DIAL_CY) <= 8)
    {
      lo = LV_MIN(lo, DIAL_CX - 9.0f);
      hi = LV_MAX(hi, DIAL_CX + 9.0f);
    }

  x1 = LV_MAX(x1, (int32_t)lo);
  x2 = LV_MIN(x2, (int32_t)hi + 1);
  for (x = x1; x <= x2; x++)
    {
      float fx = x + 0.5f - px;
      float t = LV_CLAMP(0.0f, fx * ux + fy * uy, len);
      float dx = fx - ux * t;
      float dy = fy - uy * t;
      float cx = x + 0.5f - DIAL_CX;
      float cy = y + 0.5f - DIAL_CY;
      float r = sqrtf(cx * cx + cy * cy);
      uint16_t *pixel = &row[x - row_x1];
      uint32_t cover;

      cover = edge_cover(1.5f - sqrtf(dx * dx + dy * dy));
      cover = LV_MAX(cover, edge_cover(7.5f - r));
      if (cover > 0)
        {
          *pixel = rgb565_mix(*pixel, accent, cover);
        }

      cover = edge_cover(3.5f - r);
      if (cover > 0)
        {
          *pixel = rgb565_mix(*pixel, 0, cover);
        }
    }
}

static void overlay_copy(int index, uint8_t *dst, uint32_t dst_stride,
                         const lv_area_t *block, int32_t y,
                         int32_t x1, int32_t x2, int32_t src_y)
{
  const lv_draw_buf_t *src = &g_nav.preview_buf[index];
  uint8_t *row;

  x1 = LV_MAX(x1, block->x1);
  x2 = LV_MIN(x2, block->x2);
  if (x1 > x2 || y < block->y1 || y > block->y2 ||
      src_y < 0 || src_y >= SCR_H)
    {
      return;
    }

  row = dst + (y - block->y1) * dst_stride;
  memcpy(row + (x1 - block->x1) * 2,
         (const uint8_t *)src->data + src_y * src->header.stride + x1 * 2,
         (x2 - x1 + 1) * 2);

  /* Faces are only ever shown unshifted, so the hand sits at its place. */

  if (g_nav.preview_scene[index] == SCENE_FACE_ANALOG && src_y == y)
    {
      overlay_second_hand((uint16_t *)row, block->x1, y, x1, x2);
    }
}

/* Vertical slide: the revealed page moves with the finger, so the row at
 * screen y shows page row y + shift.
 */

static int32_t nav_slide_shift(void)
{
  int32_t progress = nav_progress(g_nav.reveal_value);

  if (g_nav.axis != AXIS_V || g_nav.mode != VELARIDE_TRANSITION_SLIDE)
    {
      return 0;
    }

  return g_nav.target_direction > 0 ? SCR_H - progress : progress - SCR_H;
}

static void nav_flush_start_cb(lv_event_t *event)
{
  lv_display_t *disp = lv_event_get_target(event);
  const lv_area_t *area = lv_event_get_param(event);
  lv_draw_buf_t *buf = lv_display_get_buf_active(disp);
  lv_area_t block;
  uint8_t *dst;
  uint32_t stride;
  int32_t y;

  if (g_nav.axis == AXIS_NONE ||
      (g_nav.reveal_value == 0 && g_nav.cover_preview < 0) ||
      g_nav.reveal_preview < 0 || area == NULL || buf == NULL ||
      buf->header.cf != LV_COLOR_FORMAT_RGB565)
    {
      return;
    }

  /* Partial rendering reshapes the buffer to the block; full-screen
   * rendering (host tests) keeps the whole screen in it.
   */

  block = *area;
  dst = buf->data;
  stride = buf->header.stride;
  if (buf->header.w != lv_area_get_width(area) ||
      buf->header.h != lv_area_get_height(area))
    {
      block.x1 = 0;
      block.y1 = 0;
      block.x2 = buf->header.w - 1;
      block.y2 = buf->header.h - 1;
    }

  if (g_nav.cover_preview >= 0)
    {
      /* Closing a centre: the centre slides away with the finger and
       * uncovers the face, which stays still. The live centre is hidden,
       * so every row comes from one of the two snapshots.
       */

      int32_t progress = nav_progress(g_nav.reveal_value);

      for (y = area->y1; y <= area->y2; y++)
        {
          bool face = g_nav.target_direction < 0 ?
                      y >= SCR_H - progress : y < progress;

          if (face)
            {
              overlay_copy(g_nav.reveal_preview, dst, stride, &block, y,
                           area->x1, area->x2, y);
            }
          else
            {
              overlay_copy(g_nav.cover_preview, dst, stride, &block, y,
                           area->x1, area->x2,
                           g_nav.target_direction < 0 ? y + progress :
                                                        y - progress);
            }
        }

      return;
    }

  if (g_nav.mode == VELARIDE_TRANSITION_IRIS)
    {
      int32_t radius = iris_radius(g_nav.reveal_value);

      for (y = LV_MAX(area->y1, SCR_H / 2 - radius);
           y <= LV_MIN(area->y2, SCR_H / 2 + radius); y++)
        {
          int32_t dy = y - SCR_H / 2;
          int32_t half;

          if (LV_ABS(dy) >= radius)
            {
              continue;
            }

          half = (int32_t)sqrtf((float)(radius * radius - dy * dy));
          overlay_copy(g_nav.reveal_preview, dst, stride, &block, y,
                       LV_MAX(area->x1, SCR_W / 2 - half),
                       LV_MIN(area->x2, SCR_W / 2 + half), y);
        }

      return;
    }

  int32_t shift = nav_slide_shift();

  for (int k = 0; k < nav_strip_count(); k++)
    {
      lv_area_t strip;
      int32_t a1;
      int32_t a2;

      if (!nav_strip_span(k, nav_strip_count(),
                          nav_progress(g_nav.reveal_value), &a1, &a2))
        {
          continue;
        }

      nav_span_area(a1, a2, &strip);
      if (!_lv_area_intersect(&strip, &strip, area))
        {
          continue;
        }

      for (y = strip.y1; y <= strip.y2; y++)
        {
          overlay_copy(g_nav.reveal_preview, dst, stride, &block, y,
                       strip.x1, strip.x2, y + shift);
        }
    }
}

static void reveal_draw_cb(lv_event_t *event)
{
  lv_area_t clip_saved;
  lv_area_t area;
  lv_area_t clip;
  lv_layer_t *layer;
  int32_t a1;
  int32_t a2;
  int count;
  int k;

  if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN ||
      g_nav.axis == AXIS_NONE || g_nav.reveal_value == 0 ||
      g_nav.reveal_preview >= 0)
    {
      return;
    }

  layer = lv_event_get_layer(event);
  clip_saved = layer->_clip_area;
  count = nav_strip_count();
  for (k = 0; k < count; k++)
    {
      if (nav_strip_span(k, count, nav_progress(g_nav.reveal_value),
                         &a1, &a2))
        {
          nav_span_area(a1, a2, &area);
          if (_lv_area_intersect(&clip, &clip_saved, &area))
            {
              layer->_clip_area = clip;
              lv_obj_redraw(layer, g_nav.target->root);
            }
        }
    }

  layer->_clip_area = clip_saved;
}

static void nav_invalidate(int32_t a1, int32_t a2)
{
  lv_area_t area;

  if (a2 > a1)
    {
      nav_span_area(a1, a2, &area);
      lv_obj_invalidate_area(g_nav.reveal, &area);
    }
}

/* Invalidate only what differs between the drawn and the new value.
 * Strips share an anchored edge, so the difference is one span per strip;
 * the iris is covered with coarse bands.
 */

static void nav_apply(int32_t value)
{
  int32_t old = g_nav.reveal_value;
  int count;
  int k;

  if (value == old)
    {
      return;
    }

  g_nav.reveal_value = value;
  if (g_nav.cover_preview >= 0)
    {
      lv_obj_invalidate(g_nav.reveal);
      return;
    }

  if (g_nav.mode == VELARIDE_TRANSITION_IRIS)
    {
      int32_t r_old = iris_radius(old);
      int32_t r_new = iris_radius(value);
      int32_t y;

      for (y = 0; y < SCR_H; y += IRIS_BAND)
        {
          int32_t y2 = LV_MIN(y + IRIS_BAND, SCR_H) - 1;
          int32_t h_old = iris_half_width(r_old, y, y2);
          int32_t h_new = iris_half_width(r_new, y, y2);
          int32_t hi = LV_MAX(h_old, h_new);
          int32_t lo = LV_MIN(h_old, h_new);
          lv_area_t area;

          if (hi < 0 || hi == lo)
            {
              continue;
            }

          area.y1 = y;
          area.y2 = y2;

          /* Near the top and bottom of the circle the chord changes a lot
           * per frame; one span is then cheaper than two plus the gap.
           */

          if (lo < 0 || lo < hi - lo)
            {
              area.x1 = LV_MAX(0, SCR_W / 2 - hi);
              area.x2 = LV_MIN(SCR_W - 1, SCR_W / 2 + hi);
              lv_obj_invalidate_area(g_nav.reveal, &area);
              continue;
            }

          area.x1 = LV_MAX(0, SCR_W / 2 - hi);
          area.x2 = SCR_W / 2 - lo;
          lv_obj_invalidate_area(g_nav.reveal, &area);
          area.x1 = SCR_W / 2 + lo;
          area.x2 = LV_MIN(SCR_W - 1, SCR_W / 2 + hi);
          lv_obj_invalidate_area(g_nav.reveal, &area);
        }

      return;
    }

  count = nav_strip_count();
  for (k = 0; k < count; k++)
    {
      int32_t o1;
      int32_t o2;
      int32_t n1;
      int32_t n2;
      bool had = nav_strip_span(k, count, nav_progress(old), &o1, &o2);
      bool has = nav_strip_span(k, count, nav_progress(value), &n1, &n2);

      if (!had && !has)
        {
          continue;
        }
      else if (!had)
        {
          nav_invalidate(n1, n2);
        }
      else if (!has)
        {
          nav_invalidate(o1, o2);
        }
      else if (g_nav.axis == AXIS_V &&
               g_nav.mode == VELARIDE_TRANSITION_SLIDE)
        {
          nav_invalidate(LV_MIN(o1, n1), LV_MAX(o2, n2));
        }
      else if (g_nav.target_direction < 0)
        {
          nav_invalidate(LV_MIN(o1, n1), LV_MAX(o1, n1));
        }
      else
        {
          nav_invalidate(LV_MIN(o2, n2), LV_MAX(o2, n2));
        }
    }
}

static void update_current_scene(void)
{
  time_t now = time(NULL);
  struct tm tm_now;

  if (g_nav.dragging || g_nav.transition_active)
    {
      return;
    }

  localtime_r(&now, &tm_now);
  if (tm_now.tm_min != g_nav.preview_minute)
    {
      if (g_nav.preview_minute >= 0)
        {
          preview_refresh_all();
        }

      g_nav.preview_minute = tm_now.tm_min;
    }

  if (g_nav.current->scene <= SCENE_FACE_RINGS)
    {
      face_set_time(g_nav.current);
    }
  else if (g_nav.current->scene == SCENE_CONTROL)
    {
      cc_status(g_nav.current);
      cc_style(g_nav.current);
    }

  /* Status line and light level drift; refresh the pull-down snapshot
   * every 30 s while the face is shown.
   */

  if (g_nav.current->scene <= SCENE_FACE_RINGS &&
      lv_tick_elaps(g_nav.control_refresh) >= 30000)
    {
      g_nav.control_refresh = lv_tick_get();
      preview_invalidate(SCENE_CONTROL);
    }

  note_watch();
  update_face_detail(g_nav.current);
}

static void preload_timer_cb(lv_timer_t *timer)
{
  lv_timer_pause(timer);
  if (!g_nav.dragging && !g_nav.transition_active)
    {
      preload_neighbor_scenes();
    }
}

/* Touch samples arrive at 70-90 Hz and the spring steps every 10 ms. With a
 * slow frame several updates pile up; LVGL holds at most LV_INV_BUF_SIZE
 * (32) dirty areas and then redraws the whole screen, which makes the next
 * frame slower still. Recording the value and applying it once per refresh
 * keeps every frame at one set of strips.
 */

static void place_slots(int32_t value)
{
  g_nav.pending_value = value;
}

static void nav_refr_start_cb(lv_event_t *event)
{
  LV_UNUSED(event);
  if (g_nav.axis != AXIS_NONE && g_nav.pending_value != g_nav.reveal_value)
    {
      nav_apply(g_nav.pending_value);
    }
}

static void nav_finish(void)
{
  struct scene_slot_s *old_current;

  /* Cancelled transitions must clear what was revealed. */

  nav_apply(g_nav.pending_value);

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
      else if (g_nav.function_index >= 0)
        {
          g_nav.function_index = -1;
        }
      else
        {
          g_nav.function_index = g_nav.target_direction > 0 ? 0 : 1;
        }

      lv_obj_set_pos(g_nav.current->root, 0, 0);
      lv_obj_remove_flag(g_nav.current->root, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_nav.current->root);
      lv_obj_move_foreground(g_nav.reveal);
    }
  else
    {
      lv_obj_set_pos(g_nav.current->root, 0, 0);
      lv_obj_remove_flag(g_nav.current->root, LV_OBJ_FLAG_HIDDEN);
    }

  g_nav.cover_preview = -1;
  lv_obj_add_flag(g_nav.target->root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_nav.spare->root, LV_OBJ_FLAG_HIDDEN);
  g_nav.reveal_value = 0;
  g_nav.pending_value = 0;
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
      struct nav_stats_s *stats = &g_nav_stats[g_nav.mode %
                                               VELARIDE_TRANSITION_COUNT];

      stats->swipes++;
      stats->frames += g_nav.perf_frames - 1;
      stats->span_us += g_nav.perf_last_us - g_nav.perf_first_us;
      stats->max_gap_us = LV_MAX(stats->max_gap_us, g_nav.perf_max_gap_us);
      stats->slow_frames += g_nav.perf_slow_frames;
      printf("VELARIDE_PERF: nav=%" PRIu32 ".%" PRIu32
             "Hz max_gap=%" PRIu32 "ms frames=%" PRIu32 " mode=%u\n",
             rate_x10 / 10, rate_x10 % 10,
             g_nav.perf_max_gap_us / 1000, g_nav.perf_frames, g_nav.mode);
    }
  else
    {
      printf("VELARIDE_PERF: nav=0.0Hz max_gap=0ms frames=%" PRIu32
             "\n", g_nav.perf_frames);
    }

  /* Rebuilding the neighbours takes tens of milliseconds; doing it in the
   * landing frame shows up as a hitch, so wait until the page is at rest.
   */

  lv_timer_set_period(g_nav.preload_timer, PRELOAD_DELAY_MS);
  lv_timer_reset(g_nav.preload_timer);
  lv_timer_resume(g_nav.preload_timer);
}

/* Damped spring on the reveal value. The finger's release speed becomes
 * the initial velocity, so a flick keeps its momentum into the landing.
 */

static void spring_timer_cb(lv_timer_t *timer)
{
  float lo = (float)LV_MIN(0, g_nav.spring_target);
  float hi = (float)LV_MAX(0, g_nav.spring_target);
  float target = (float)g_nav.spring_target;
  float dt = lv_tick_elaps(g_nav.spring_tick) / 1000.0f;
  bool done = false;
  int steps;
  int i;

  if (!g_nav.transition_active)
    {
      lv_timer_pause(timer);
      return;
    }

  if (g_nav.target_origin != 0)
    {
      lo = (float)LV_MIN(0, -g_nav.target_origin);
      hi = (float)LV_MAX(0, -g_nav.target_origin);
    }

  g_nav.spring_tick = lv_tick_get();
  dt = LV_CLAMP(0.004f, dt, 0.040f);
  steps = dt > 0.012f ? 3 : 1;
  for (i = 0; i < steps; i++)
    {
      float h = dt / steps;
      float a = -SPRING_K * (g_nav.spring_x - target) -
                SPRING_C * g_nav.spring_v;

      g_nav.spring_v += a * h;
      g_nav.spring_x += g_nav.spring_v * h;
    }

  /* The page cannot be revealed past its edges: arriving at the target
   * edge ends the motion instead of bouncing through it.
   */

  if (g_nav.spring_x <= lo || g_nav.spring_x >= hi)
    {
      g_nav.spring_x = LV_CLAMP(lo, g_nav.spring_x, hi);
      if (fabsf(g_nav.spring_x - target) < 0.5f)
        {
          done = true;
        }
      else
        {
          g_nav.spring_v = 0;
        }
    }

  if (fabsf(g_nav.spring_x - target) < 0.6f &&
      fabsf(g_nav.spring_v) < SPRING_REST_SPEED)
    {
      done = true;
    }

  if (done)
    {
      place_slots(g_nav.spring_target);
      lv_timer_pause(timer);
      nav_finish();
      return;
    }

  place_slots((int32_t)lroundf(g_nav.spring_x));
}

static void start_settle(bool commit, float velocity)
{
  int32_t start = g_nav.axis == AXIS_H ? g_nav.drag_x : g_nav.drag_y;

  start = LV_CLAMP(-nav_length(), start, nav_length());
  g_nav.commit_transition = commit;
  g_nav.transition_active = true;
  g_nav.spring_target = commit ? -g_nav.target_origin : 0;
  g_nav.spring_x = (float)start;
  g_nav.spring_v = LV_CLAMP(-SPRING_MAX_SPEED, velocity, SPRING_MAX_SPEED);
  g_nav.spring_tick = lv_tick_get();
  lv_timer_set_period(g_nav.spring_timer, SPRING_PERIOD_MS);
  lv_timer_reset(g_nav.spring_timer);
  lv_timer_resume(g_nav.spring_timer);
}

static int find_preview(enum scene_id_e scene)
{
  int i;

  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      if (g_nav.preview_ready[i] && g_nav.preview_scene[i] == scene)
        {
          return i;
        }
    }

  return -1;
}

static void begin_axis(enum axis_e axis, int direction)
{
  struct velaride_ui_prefs_s prefs;
  enum scene_id_e scene;
  struct scene_slot_s *tmp;

  if (!choose_target(axis, direction, &scene))
    {
      return;
    }

  /* Switching direction mid-drag: erase the old reveal first. */

  if (g_nav.axis != AXIS_NONE && g_nav.reveal_value != 0)
    {
      nav_apply(0);
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

  velaride_ui_prefs_get(&prefs);
  g_nav.axis = axis;
  g_nav.reveal_scene = scene;
  g_nav.reveal_preview = find_preview(scene);

  /* The transition setting is for switching faces; the centres always
   * slide like a sheet over the face.
   */

  g_nav.mode = axis == AXIS_V ? VELARIDE_TRANSITION_SLIDE : prefs.transition;
  if (g_nav.mode == VELARIDE_TRANSITION_IRIS && g_nav.reveal_preview < 0)
    {
      /* Drawing the live page through dozens of row clips is too slow. */

      g_nav.mode = VELARIDE_TRANSITION_SLIDE;
    }

  {
    struct tm local;

    g_nav.reveal_second = face_local_time(&local) ? local.tm_sec : 0;
  }

  g_nav.cover_preview = -1;
  if (axis == AXIS_V && g_nav.function_index >= 0 &&
      g_nav.reveal_preview >= 0)
    {
      g_nav.cover_preview = find_preview(g_nav.current->scene);
    }

  if (g_nav.cover_preview >= 0)
    {
      lv_obj_add_flag(g_nav.current->root, LV_OBJ_FLAG_HIDDEN);
    }

  g_nav.reveal_value = 0;
  g_nav.pending_value = 0;
  g_nav.target_direction = direction;
  g_nav.target_origin = axis == AXIS_H ?
                        (direction < 0 ? SCR_W : -SCR_W) :
                        (direction < 0 ? SCR_H : -SCR_H);
  lv_obj_add_flag(g_nav.target->root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_nav.reveal);
  place_slots(axis == AXIS_H ? g_nav.drag_x : g_nav.drag_y);
}

static void drag_sample(int32_t value)
{
  uint8_t i = g_nav.sample_count % DRAG_SAMPLES;

  g_nav.sample_value[i] = value;
  g_nav.sample_tick[i] = lv_tick_get();
  g_nav.sample_count++;
}

/* Release speed in px/s from samples within the last DRAG_VELOCITY_MS. */

static float drag_velocity(void)
{
  uint8_t n = LV_MIN(g_nav.sample_count, DRAG_SAMPLES);
  uint8_t last = (g_nav.sample_count + DRAG_SAMPLES - 1) % DRAG_SAMPLES;
  uint32_t now = lv_tick_get();
  int oldest = -1;
  uint8_t k;

  if (n < 2 || now - g_nav.sample_tick[last] > DRAG_VELOCITY_MS)
    {
      return 0;
    }

  for (k = 1; k < n; k++)
    {
      uint8_t i = (last + DRAG_SAMPLES - k) % DRAG_SAMPLES;

      if (now - g_nav.sample_tick[i] > DRAG_VELOCITY_MS)
        {
          break;
        }

      oldest = i;
    }

  if (oldest < 0 || g_nav.sample_tick[last] == g_nav.sample_tick[oldest])
    {
      return 0;
    }

  return (g_nav.sample_value[last] - g_nav.sample_value[oldest]) * 1000.0f /
         (float)(g_nav.sample_tick[last] - g_nav.sample_tick[oldest]);
}

static void drag_update(void)
{
  int32_t length = nav_length();
  int32_t *drag = g_nav.axis == AXIS_H ? &g_nav.drag_x : &g_nav.drag_y;
  int32_t value = *drag;

  /* Dragging back past the start: faces switch to the other neighbour,
   * other pages simply stop at the start.
   */

  if ((value > 0) != (g_nav.target_direction > 0) && value != 0)
    {
      if (g_nav.axis == AXIS_H && g_nav.function_index < 0 &&
          LV_ABS(value) > AXIS_LOCK_PX / 2)
        {
          begin_axis(AXIS_H, value < 0 ? -1 : 1);
          return;
        }

      value = 0;
    }

  value = LV_CLAMP(-length, value, length);
  drag_sample(value);
  place_slots(value);
}

static void gesture_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_PRESSED)
    {
      g_nav.dragging = true;
      g_nav.sample_count = 0;
      g_nav.perf_first_us = 0;
      g_nav.perf_last_us = 0;
      g_nav.perf_frames = 0;
      g_nav.perf_max_gap_us = 0;
      g_nav.perf_slow_frames = 0;
      if (g_nav.transition_active && g_nav.axis != AXIS_NONE)
        {
          /* Catch the page mid-flight and keep dragging from there. */

          lv_timer_pause(g_nav.spring_timer);
          g_nav.transition_active = false;
          g_nav.drag_x = g_nav.axis == AXIS_H ? g_nav.pending_value : 0;
          g_nav.drag_y = g_nav.axis == AXIS_V ? g_nav.pending_value : 0;
          g_nav.caught = true;
          return;
        }

      g_nav.caught = false;
      g_nav.axis = AXIS_NONE;
      g_nav.drag_x = 0;
      g_nav.drag_y = 0;
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

      if (g_nav.axis != AXIS_NONE)
        {
          drag_update();
        }
    }
  else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
           g_nav.dragging)
    {
      if (g_nav.axis != AXIS_NONE)
        {
          int32_t length = nav_length();
          int32_t value = LV_CLAMP(-length, g_nav.axis == AXIS_H ?
                                   g_nav.drag_x : g_nav.drag_y, length);
          float velocity = drag_velocity();
          float target = (float)-g_nav.target_origin;
          float projected = (value + velocity * FLING_PROJECT_S) / target;
          bool commit = projected >= COMMIT_FRACTION ||
                        (velocity / target * length >= FLING_SPEED &&
                         LV_ABS(value) >= AXIS_LOCK_PX);

          /* A flick against the page cancels even when past halfway. */

          if (velocity / target * length <= -FLING_SPEED)
            {
              commit = false;
            }

          g_nav.drag_x = g_nav.axis == AXIS_H ? value : 0;
          g_nav.drag_y = g_nav.axis == AXIS_V ? value : 0;
          start_settle(commit, velocity);
        }
      else
        {
          g_nav.dragging = false;
          if (code == LV_EVENT_RELEASED && !g_nav.caught &&
              LV_ABS(g_nav.drag_x) + LV_ABS(g_nav.drag_y) < AXIS_LOCK_PX)
            {
              lv_indev_t *indev = lv_indev_active();
              lv_point_t point = {0, 0};

              if (indev != NULL)
                {
                  lv_indev_get_point(indev, &point);
                }

              if (g_nav.current->scene == SCENE_CONTROL)
                {
                  cc_tap(point);
                }
              else if (g_nav.current->scene == SCENE_NOTES)
                {
                  note_tap(point);
                }
              else if (g_nav.current->scene == SCENE_FACE_MODULAR &&
                       point.x >= FACE_PILL_X &&
                       point.x < FACE_PILL_X + FACE_PILL_W &&
                       point.y >= FACE_PILL_Y &&
                       point.y < FACE_PILL_Y + FACE_PILL_H)
                {
                  launch_app(APP_RIDE);
                }
            }
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

              if (gap > NAV_SLOW_GAP_US)
                {
                  g_menu.perf_slow_frames++;
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

      if (gap > NAV_SLOW_GAP_US)
        {
          g_nav.perf_slow_frames++;
        }
    }

  g_nav.perf_last_us = now;
  g_nav.perf_frames++;
}

static void data_timer_cb(lv_timer_t *timer)
{
  LV_UNUSED(timer);
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  velaride_alarm_tick(time(NULL));
  if (!g_nav.dragging && !g_nav.transition_active &&
      velaride_alarm_take_due())
    {
      note_post(NOTE_ALARM);
      printf("VELARIDE_ALARM: completed notification=1\n");
    }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  /* Keep running outside the page and while the screen is dark. Defer the
   * banner during a drag/transition so it cannot replace a moving scene.
   */

  if (!g_nav.dragging && !g_nav.transition_active &&
      velaride_countdown_take_done(lv_tick_get()))
    {
      note_post(NOTE_COUNTDOWN);
      printf("VELARIDE_COUNTDOWN: completed notification=1\n");
    }
#endif
  update_current_scene();
}

#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
static void night_cache_invalidate(void)
{
  for (int i = 0; i < PREVIEW_COUNT; i++) g_nav.preview_ready[i] = false;
  if (!velaride_sport_ui_active()) preview_schedule();
}

static void night_watch_cb(lv_timer_t *timer)
{
  struct velaride_device_snapshot_s device;
  LV_UNUSED(timer);
  velaride_device_get(&device);
  velaride_night_poll(lv_tick_get(), device.light_samples,
                      device.light_valid, device.light_lux);
  if (!g_nav.dragging && !g_nav.transition_active)
    {
      velaride_night_ui_apply(velaride_sport_ui_night_exempt());
    }
}
#endif

/* Launcher. The same apps appear as a honeycomb (Apple Watch style: icons
 * shrink and draw inward near the screen's rounded edges, the grid drifts
 * with inertia) or as a list whose rows fade near the top and bottom. The
 * wearer picks one in Settings > Appearance.
 */

static void menu_perf_begin(void)
{
  g_menu.perf_active = true;
  g_menu.perf_first_us = 0;
  g_menu.perf_last_us = 0;
  g_menu.perf_frames = 0;
  g_menu.perf_max_gap_us = 0;
  g_menu.perf_slow_frames = 0;
}

static void menu_perf_end(int style)
{
  struct nav_stats_s *stats = &g_menu_stats[style];

  if (!g_menu.perf_active)
    {
      return;
    }

  g_menu.perf_active = false;
  if (g_menu.perf_frames > 1 && g_menu.perf_last_us > g_menu.perf_first_us)
    {
      uint32_t rate_x10 =
        (uint32_t)((uint64_t)(g_menu.perf_frames - 1) * 10000000 /
                   (g_menu.perf_last_us - g_menu.perf_first_us));

      stats->swipes++;
      stats->frames += g_menu.perf_frames - 1;
      stats->span_us += g_menu.perf_last_us - g_menu.perf_first_us;
      stats->max_gap_us = LV_MAX(stats->max_gap_us, g_menu.perf_max_gap_us);
      stats->slow_frames += g_menu.perf_slow_frames;
      printf("VELARIDE_MENU_PERF: style=%d move=%" PRIu32 ".%" PRIu32
             "Hz max_gap=%" PRIu32 "ms frames=%" PRIu32 "\n", style,
             rate_x10 / 10, rate_x10 % 10,
             g_menu.perf_max_gap_us / 1000, g_menu.perf_frames);
    }
}

static lv_color_t app_color(int app)
{
  return g_apps[app].color == 0 ? vr_accent() :
                                  lv_color_hex(g_apps[app].color);
}

static lv_color_t app_ink(int app)
{
  return g_apps[app].color == 0 ? vr_accent_ink() : lv_color_white();
}

/* Screen position and size of every honeycomb icon for the current
 * offset; computed once per move, used by drawing and hit testing.
 */

static void honey_layout(void)
{
  float best_d2 = 1e9f;
  int best = 0;
  int i;

  for (i = 0; i < APP_COUNT; i++)
    {
      struct honey_cell_s *cell = &g_menu.cells[i];
      float x = SCR_W / 2 + HONEY_STEP * (g_apps[i].q + g_apps[i].r / 2.0f) +
                g_menu.offset_x;
      float y = SCR_H / 2 + HONEY_STEP * 0.866f * g_apps[i].r +
                g_menu.offset_y;
      float ex = fminf(x, SCR_W - x);
      float ey = fminf(y, SCR_H - y);
      float edge = fminf(ex, ey) - 20.0f;
      float corner = hypotf(fmaxf(0, 110.0f - ex), fmaxf(0, 110.0f - ey));
      float scale = LV_CLAMP(0.22f, (edge + 34.0f) / 84.0f, 1.0f) *
                    LV_CLAMP(0.35f, 1.0f - corner / 300.0f, 1.0f);
      float dx;
      float dy;

      x += (SCR_W / 2 - x) * (1.0f - scale) * 0.35f;
      y += (SCR_H / 2 - y) * (1.0f - scale) * 0.35f;
      cell->x = (int16_t)lroundf(x);
      cell->y = (int16_t)lroundf(y);
      cell->diameter = (int16_t)lroundf(HONEY_DIAMETER * scale);
      dx = x - SCR_W / 2;
      dy = y - SCR_H / 2;
      if (dx * dx + dy * dy < best_d2)
        {
          best_d2 = dx * dx + dy * dy;
          best = i;
        }
    }

  if (best != g_menu.selected)
    {
      g_menu.selected = best;
      set_text(g_menu.title, g_apps[best].name);
    }
}

static void honey_move(float x, float y)
{
  g_menu.offset_x = x;
  g_menu.offset_y = y;
  honey_layout();
  lv_obj_invalidate(g_menu.root);
}

static void honey_draw_cb(lv_event_t *event)
{
  lv_layer_t *layer = lv_event_get_layer(event);
  int i;

  for (i = 0; i < APP_COUNT; i++)
    {
      struct honey_cell_s *cell = &g_menu.cells[i];
      int radius = cell->diameter / 2;
      int icon = cell->diameter * 2 / 5;
      lv_draw_rect_dsc_t rect;
      lv_area_t area;

      if (radius < 4 || cell->x + radius < 0 || cell->x - radius >= SCR_W ||
          cell->y + radius < 0 || cell->y - radius >= SCR_H)
        {
          continue;
        }

      area.x1 = cell->x - radius;
      area.y1 = cell->y - radius;
      area.x2 = cell->x + radius;
      area.y2 = cell->y + radius;
      lv_draw_rect_dsc_init(&rect);
      rect.radius = LV_RADIUS_CIRCLE;
      rect.bg_opa = LV_OPA_COVER;
      rect.bg_color = velaride_night_color(app_color(i));
      if (i == APP_RIDE && velaride_sport_ui_active() == false &&
          g_menu.riding)
        {
          /* A ride in progress: ring the ride icon. */

          rect.outline_width = 3;
          rect.outline_pad = 4;
          rect.outline_color = velaride_night_color(vr_accent());
          rect.outline_opa = LV_OPA_COVER;
        }

      lv_draw_rect(layer, &rect, &area);
      if (icon >= 10)
        {
          velaride_icon_draw(layer, g_apps[i].icon, cell->x - icon / 2,
                             cell->y - icon / 2, icon, app_ink(i));
        }
    }
}

static int honey_hit_test(lv_point_t point)
{
  int i;

  for (i = 0; i < APP_COUNT; i++)
    {
      struct honey_cell_s *cell = &g_menu.cells[i];
      int dx = point.x - cell->x;
      int dy = point.y - cell->y;
      int radius = cell->diameter / 2 + 4;

      if (cell->diameter >= HONEY_DIAMETER / 3 &&
          dx * dx + dy * dy <= radius * radius)
        {
          return i;
        }
    }

  return -1;
}

static void honey_gesture_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);
  lv_indev_t *indev = lv_indev_active();
  lv_point_t point;

  if (indev == NULL)
    {
      return;
    }

  lv_indev_get_point(indev, &point);
  if (code == LV_EVENT_PRESSED)
    {
      g_menu.dragging = true;
      g_menu.moved = false;
      g_menu.velocity_x = 0;
      g_menu.velocity_y = 0;
      g_menu.press_point = point;
      g_menu.last_point = point;
      g_menu.last_tick = lv_tick_get();
      menu_perf_begin();
    }
  else if (code == LV_EVENT_PRESSING && g_menu.dragging)
    {
      float dx = point.x - g_menu.last_point.x;
      float dy = point.y - g_menu.last_point.y;
      uint32_t elapsed = lv_tick_elaps(g_menu.last_tick);

      if (LV_ABS(point.x - g_menu.press_point.x) +
          LV_ABS(point.y - g_menu.press_point.y) > 8)
        {
          g_menu.moved = true;
        }

      if (dx == 0 && dy == 0)
        {
          return;
        }

      /* Past the edge of the grid the finger meets resistance. */

      if ((g_menu.offset_x > HONEY_LIMIT_X && dx > 0) ||
          (g_menu.offset_x < -HONEY_LIMIT_X && dx < 0))
        {
          dx *= 0.35f;
        }

      if ((g_menu.offset_y > HONEY_LIMIT_Y && dy > 0) ||
          (g_menu.offset_y < -HONEY_LIMIT_Y && dy < 0))
        {
          dy *= 0.35f;
        }

      if (elapsed > 0)
        {
          float k = elapsed >= 40 ? 1.0f : 0.5f;

          g_menu.velocity_x += (dx * 1000.0f / elapsed - g_menu.velocity_x) * k;
          g_menu.velocity_y += (dy * 1000.0f / elapsed - g_menu.velocity_y) * k;
        }

      g_menu.last_point = point;
      g_menu.last_tick = lv_tick_get();
      honey_move(g_menu.offset_x + dx, g_menu.offset_y + dy);
    }
  else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) &&
           g_menu.dragging)
    {
      g_menu.dragging = false;
      if (lv_tick_elaps(g_menu.last_tick) > 80)
        {
          g_menu.velocity_x = 0;
          g_menu.velocity_y = 0;
        }

      g_menu.inertia_tick = lv_tick_get();
      if (!g_menu.moved && code == LV_EVENT_RELEASED)
        {
          int app = honey_hit_test(point);

          g_menu.velocity_x = 0;
          g_menu.velocity_y = 0;
          g_menu.perf_active = false;
          if (app >= 0)
            {
              launch_app(app);
            }
        }
    }
}

/* Inertia after release, then a spring back inside the grid limits. */

static void honey_inertia_timer_cb(lv_timer_t *timer)
{
  float dt = lv_tick_elaps(g_menu.inertia_tick) / 1000.0f;
  float x = g_menu.offset_x;
  float y = g_menu.offset_y;
  float pull_x;
  float pull_y;

  LV_UNUSED(timer);
  g_menu.inertia_tick = lv_tick_get();
  if (!g_menu.active || g_menu.dragging || g_menu.list_mode)
    {
      return;
    }

  dt = LV_CLAMP(0.004f, dt, 0.040f);
  pull_x = x > HONEY_LIMIT_X ? HONEY_LIMIT_X - x :
           x < -HONEY_LIMIT_X ? -HONEY_LIMIT_X - x : 0;
  pull_y = y > HONEY_LIMIT_Y ? HONEY_LIMIT_Y - y :
           y < -HONEY_LIMIT_Y ? -HONEY_LIMIT_Y - y : 0;
  if (fabsf(g_menu.velocity_x) + fabsf(g_menu.velocity_y) < 12.0f &&
      fabsf(pull_x) + fabsf(pull_y) < 0.5f)
    {
      if (g_menu.velocity_x != 0 || g_menu.velocity_y != 0)
        {
          g_menu.velocity_x = 0;
          g_menu.velocity_y = 0;
          honey_move(lroundf(x), lroundf(y));
        }

      menu_perf_end(VELARIDE_MENU_HONEY);
      return;
    }

  /* Friction keeps 2% of the speed per second; a stiff spring with heavy
   * damping returns the grid without wobbling.
   */

  g_menu.velocity_x = g_menu.velocity_x * powf(0.02f, dt) +
                      pull_x * HONEY_SPRING * dt;
  g_menu.velocity_y = g_menu.velocity_y * powf(0.02f, dt) +
                      pull_y * HONEY_SPRING * dt;
  if (pull_x != 0)
    {
      g_menu.velocity_x *= 0.86f;
    }

  if (pull_y != 0)
    {
      g_menu.velocity_y *= 0.86f;
    }

  /* The spring only acts outside the limits; once the grid is pulled back
   * across an edge it stops there instead of coasting on inwards.
   */

  float nx = x + g_menu.velocity_x * dt;
  float ny = y + g_menu.velocity_y * dt;

  if (pull_x != 0 && fabsf(nx) <= HONEY_LIMIT_X)
    {
      nx = x > 0 ? HONEY_LIMIT_X : -HONEY_LIMIT_X;
      g_menu.velocity_x = 0;
    }

  if (pull_y != 0 && fabsf(ny) <= HONEY_LIMIT_Y)
    {
      ny = y > 0 ? HONEY_LIMIT_Y : -HONEY_LIMIT_Y;
      g_menu.velocity_y = 0;
    }

  honey_move(nx, ny);
}

static void build_honey_menu(lv_obj_t *screen)
{
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
  g_menu.title = make_label(g_menu.root, "", vr_accent(),
                            &velaride_font_cn_20, 0, 22, SCR_W);
  g_menu.selected = -1;
  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
  honey_move(0, 0);
}

static void list_row_cb(lv_event_t *event)
{
  launch_app((int)(intptr_t)lv_event_get_user_data(event));
}

/* Rows dim towards the top and bottom edges by mixing their colors with
 * black; LVGL opacity or scaling would force an extra layer per row.
 */

static void list_fade(void)
{
  int32_t view = lv_obj_get_height(g_menu.list);
  int i;

  for (i = 0; i < APP_COUNT; i++)
    {
      lv_obj_t *row = g_menu.rows[i];
      int32_t y = lv_obj_get_y(row) - lv_obj_get_scroll_y(g_menu.list) +
                  LIST_ROW_H / 2;
      int32_t edge = LV_MIN(y, view - y);
      lv_opa_t mix = (lv_opa_t)LV_CLAMP(64, edge * 255 / LIST_FADE_PX, 255);

      if (mix == g_menu.row_mix[i])
        {
          continue;
        }

      g_menu.row_mix[i] = mix;
      lv_obj_set_style_bg_color(lv_obj_get_child(row, 0),
        lv_color_mix(app_color(i), lv_color_black(), mix), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(row, 1),
        lv_color_mix(lv_color_hex(VR_INK), lv_color_black(), mix), 0);
    }
}

static void list_scroll_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_SCROLL_BEGIN)
    {
      menu_perf_begin();
    }
  else if (code == LV_EVENT_SCROLL_END)
    {
      menu_perf_end(VELARIDE_MENU_LIST);
    }
  else
    {
      list_fade();
    }
}

static void list_icon_draw_cb(lv_event_t *event)
{
  lv_obj_t *dot = lv_event_get_target(event);
  int app = (int)(intptr_t)lv_event_get_user_data(event);
  lv_area_t area;

  lv_obj_get_coords(dot, &area);
  velaride_icon_draw(lv_event_get_layer(event), g_apps[app].icon,
                     area.x1 + 11, area.y1 + 11, 26,
                     lv_color_mix(app_ink(app), lv_color_black(),
                                  g_menu.row_mix[app]));
}

static void build_list_menu(lv_obj_t *screen)
{
  int i;

  g_menu.list = lv_obj_create(screen);
  lv_obj_remove_style_all(g_menu.list);
  lv_obj_set_size(g_menu.list, SCR_W, SCR_H);
  lv_obj_set_pos(g_menu.list, 0, 0);
  lv_obj_set_style_bg_color(g_menu.list, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_menu.list, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_top(g_menu.list, 40, 0);
  lv_obj_set_style_pad_bottom(g_menu.list, 60, 0);
  lv_obj_set_scroll_dir(g_menu.list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_menu.list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_menu.list, LV_OBJ_FLAG_SCROLL_MOMENTUM |
                               LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_event_cb(g_menu.list, list_scroll_cb, LV_EVENT_SCROLL, NULL);
  lv_obj_add_event_cb(g_menu.list, list_scroll_cb, LV_EVENT_SCROLL_BEGIN,
                      NULL);
  lv_obj_add_event_cb(g_menu.list, list_scroll_cb, LV_EVENT_SCROLL_END,
                      NULL);
  for (i = 0; i < APP_COUNT; i++)
    {
      lv_obj_t *row = lv_obj_create(g_menu.list);
      lv_obj_t *dot;
      lv_obj_t *name;

      lv_obj_remove_style_all(row);
      lv_obj_set_pos(row, 24, i * LIST_ROW_H);
      lv_obj_set_size(row, SCR_W - 48, LIST_ROW_H);
      lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_transform_scale(row, 245, LV_STATE_PRESSED);
      lv_obj_set_style_transform_pivot_x(row, (SCR_W - 48) / 2, 0);
      lv_obj_set_style_transform_pivot_y(row, LIST_ROW_H / 2, 0);
      lv_obj_add_event_cb(row, list_row_cb, LV_EVENT_CLICKED,
                          (void *)(intptr_t)i);

      dot = lv_obj_create(row);
      lv_obj_remove_style_all(dot);
      lv_obj_set_size(dot, 48, 48);
      lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(dot, app_color(i), 0);
      lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_event_cb(dot, list_icon_draw_cb, LV_EVENT_DRAW_MAIN_END,
                          (void *)(intptr_t)i);

      name = lv_label_create(row);
      lv_label_set_text(name, g_apps[i].name);
      lv_obj_set_style_text_font(name, &velaride_font_cn_28, 0);
      lv_obj_set_style_text_color(name, lv_color_hex(VR_INK), 0);
      lv_obj_align(name, LV_ALIGN_LEFT_MID, 66, 0);
      g_menu.rows[i] = row;
      g_menu.row_mix[i] = 255;
    }

  lv_obj_add_flag(g_menu.list, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *menu_root(void)
{
  return g_menu.list_mode ? g_menu.list : g_menu.root;
}

static void menu_show(void)
{
  struct velaride_ui_prefs_s prefs;
  struct velaride_ride_snapshot_s ride;

  velaride_ui_prefs_get(&prefs);
  velaride_ride_session_get(&ride);
  g_menu.list_mode = prefs.menu == VELARIDE_MENU_LIST;
  g_menu.riding = ride.active;
  g_menu.active = true;
  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_menu.list, LV_OBJ_FLAG_HIDDEN);
  if (g_menu.list_mode)
    {
      lv_obj_scroll_to_y(g_menu.list, 0, LV_ANIM_OFF);
      list_fade();
    }
  else
    {
      g_menu.velocity_x = 0;
      g_menu.velocity_y = 0;
      honey_move(0, 0);
    }

  lv_obj_remove_flag(menu_root(), LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(menu_root());
}

static void menu_hide(void)
{
  g_menu.active = false;
  g_menu.velocity_x = 0;
  g_menu.velocity_y = 0;
  lv_obj_add_flag(g_menu.root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_menu.list, LV_OBJ_FLAG_HIDDEN);
}

static void switch_shell_mode(void)
{
  /* From either centre the key closes it, like the opposite swipe. */

  if (!g_menu.active && g_nav.function_index >= 0)
    {
      if (!g_nav.dragging && !g_nav.transition_active)
        {
          begin_axis(AXIS_V, g_nav.function_index == 0 ? -1 : 1);
          start_settle(true, 0);
        }

      return;
    }

  if (!g_menu.active)
    {
      menu_show();
    }
  else
    {
      menu_hide();
      lv_obj_move_foreground(g_nav.reveal);
      lv_obj_invalidate(g_nav.current->root);
    }
}

/* Control centre (pulled down from a face) and notification centre (pushed
 * up). Both are ordinary scenes in the swipe navigator, so they reuse its
 * spring, catch and preview drawing. Taps are routed by position because the
 * whole scene tree is non-clickable while the navigator owns the pointer.
 */

static bool cc_value(int id)
{
  struct velaride_display_s display;

  switch (id)
    {
      case CC_AUTO:
        velaride_display_get(&display);
        return display.automatic;
      case CC_DND:
        return g_notes.dnd;
      default:
        return false;
    }
}

static void cc_button_draw_cb(lv_event_t *event)
{
  lv_obj_t *obj = lv_event_get_target(event);
  int id = (int)(intptr_t)lv_event_get_user_data(event);
  lv_area_t area;

  lv_obj_get_coords(obj, &area);
  velaride_icon_draw(lv_event_get_layer(event), g_cc_items[id].icon,
                     area.x1 + CC_SIZE / 2 - 16, area.y1 + 14, 32,
                     cc_value(id) && g_cc_items[id].toggle ? vr_accent_ink() :
                                                             lv_color_white());
}

static void cc_style(struct scene_slot_s *slot)
{
  int i;

  for (i = 0; i < CC_COUNT; i++)
    {
      bool on = g_cc_items[i].toggle && cc_value(i);
      lv_obj_t *obj = slot->value[0] != NULL ?
                      lv_obj_get_child(slot->value[0], i) : NULL;

      if (obj == NULL)
        {
          continue;
        }

      lv_obj_set_style_bg_color(obj, on ? vr_accent() :
                                lv_color_hex(VR_TILE2), 0);
      lv_obj_set_style_text_color(lv_obj_get_child(obj, 0), on ?
                                  vr_accent_ink() : lv_color_hex(VR_INK), 0);
      lv_obj_invalidate(obj);
    }
}

static void cc_status(struct scene_slot_s *slot)
{
  struct velaride_device_snapshot_s device;
  struct velaride_display_s display;
  char text[96];
  char lux[24];

  velaride_device_get(&device);
  velaride_display_get(&display);
  if (device.light_valid)
    {
      snprintf(lux, sizeof(lux), "%lu lux",
               (unsigned long)(device.light_lux + 0.5f));
    }
  else
    {
      snprintf(lux, sizeof(lux), "光线 --");
    }

  snprintf(text, sizeof(text), "%s   %s   %s",
           device.usb_valid && device.usb_connected ? "USB 供电" : "电池",
           velaride_ride_session_connected() ? "蓝牙已连" : "蓝牙未连", lux);
  set_text(slot->secondary, text);
  snprintf(text, sizeof(text), "亮度 %u%%", display.applied);
  set_text(slot->primary, text);
}

static void create_control_center(struct scene_slot_s *slot)
{
  lv_obj_t *grid;
  int i;

  slot->secondary = make_label(slot->detail, "", lv_color_hex(VR_INK2),
                               &velaride_font_cn_20, 0, 36, SCR_W);
  grid = lv_obj_create(slot->detail);
  lv_obj_remove_style_all(grid);
  lv_obj_set_pos(grid, 0, 0);
  lv_obj_set_size(grid, SCR_W, SCR_H);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  slot->value[0] = grid;
  for (i = 0; i < CC_COUNT; i++)
    {
      lv_obj_t *obj = lv_obj_create(grid);
      lv_obj_t *name;

      lv_obj_remove_style_all(obj);
      lv_obj_set_pos(obj, CC_X0 + (i % 3) * CC_STEP_X,
                     CC_Y0 + (i / 3) * CC_STEP_Y);
      lv_obj_set_size(obj, CC_SIZE, CC_SIZE);
      lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_event_cb(obj, cc_button_draw_cb, LV_EVENT_DRAW_MAIN_END,
                          (void *)(intptr_t)i);
      name = lv_label_create(obj);
      lv_label_set_text(name, g_cc_items[i].name);
      lv_obj_set_style_text_font(name, &velaride_font_cn_20, 0);
      lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -12);
    }

  slot->primary = make_label(slot->detail, "", lv_color_hex(VR_INK2),
                             &velaride_font_cn_20, 0, 380, SCR_W);
  cc_style(slot);
  cc_status(slot);
}

static int cc_hit(lv_point_t point)
{
  int i;

  for (i = 0; i < CC_COUNT; i++)
    {
      int x = CC_X0 + (i % 3) * CC_STEP_X;
      int y = CC_Y0 + (i / 3) * CC_STEP_Y;

      if (point.x >= x && point.x < x + CC_SIZE &&
          point.y >= y && point.y < y + CC_SIZE)
        {
          return i;
        }
    }

  return -1;
}

static void cc_tap(lv_point_t point)
{
  struct velaride_display_s display;
  int id = cc_hit(point);

  switch (id)
    {
      case CC_LIGHT:
        launch_app(APP_LIGHT);
        break;
      case CC_DND:
        g_notes.dnd = !g_notes.dnd;
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
        velaride_sound_set_dnd(g_notes.dnd);
#endif
        break;
      case CC_AUTO:
        velaride_display_get(&display);
        velaride_display_set(display.manual, !display.automatic);
        break;
      case CC_BRIGHT:
        velaride_display_get(&display);
        velaride_display_set(display.manual >= 80 ? 20 :
                             display.manual >= 50 ? 80 : 50, false);
        break;
      case CC_CONNECT:
        launch_app(APP_CONNECT);
        break;
      case CC_SETTINGS:
        launch_app(APP_SETTINGS);
        break;
      default:
        return;
    }

  cc_style(g_nav.current);
  cc_status(g_nav.current);
  preview_invalidate(SCENE_CONTROL);
}

/* --- Notifications ---------------------------------------------------------- */

static const struct
{
  const char *title;
  const char *body;
  enum velaride_icon_e icon;
  uint32_t color;
  int app;               /* opened when the note is tapped, -1 for none */
} g_note_kinds[NOTE_KIND_COUNT] =
{
  [NOTE_REVIEW] = {"AI 复盘已同步", "最近一次骑行的建议已保存",
                   VR_ICON_SPARK, 0xb06ad9, APP_REVIEW},
  [NOTE_REST] = {"该休息了", "已到骑前设置的休息时间",
                 VR_ICON_MOON, 0xe99a2c, -1},
  [NOTE_LINK_UP] = {"已连接网关", "已自动校准时间",
                    VR_ICON_BLUETOOTH, 0x3d6fe0, APP_CONNECT},
  [NOTE_LINK_DOWN] = {"蓝牙已断开", "记录会先保存在手表里",
                      VR_ICON_BLUETOOTH, 0x6e6e73, APP_CONNECT},
  [NOTE_ACTIVITY] = {"起来活动一下", "手腕活动较少，起身动一动",
                     VR_ICON_PULSE, 0x6ec6ff, -1},
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  [NOTE_COUNTDOWN] = {"倒计时结束", "时间到，点击查看",
                      VR_ICON_TIMER, 0xc98536, APP_COUNTDOWN},
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  [NOTE_ALARM] = {"闹钟提醒", "已到设定时间，点击查看",
                  VR_ICON_TIMER, 0xc95c54, APP_ALARM},
#endif
};

static void note_time(time_t when, char *text, size_t size)
{
  struct tm local;
  time_t now = time(NULL);

  if (when < 1704067200 || localtime_r(&when, &local) == NULL)
    {
      snprintf(text, size, "刚才");
      return;
    }

  if (now - when < 60)
    {
      snprintf(text, size, "刚才");
    }
  else if (now - when < 3600)
    {
      snprintf(text, size, "%ld 分钟前", (long)((now - when) / 60));
    }
  else
    {
      snprintf(text, size, "%02d:%02d", local.tm_hour, local.tm_min);
    }
}

static void note_row(lv_obj_t *parent, int y, const struct note_s *note)
{
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_t *dot;
  lv_obj_t *text;
  char when[24];

  lv_obj_remove_style_all(row);
  lv_obj_set_pos(row, 18, y);
  lv_obj_set_size(row, SCR_W - 36, NOTE_ROW_H - 8);
  lv_obj_set_style_radius(row, 22, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(VR_TILE), 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  dot = lv_obj_create(row);
  lv_obj_remove_style_all(dot);
  lv_obj_set_pos(dot, 14, 16);
  lv_obj_set_size(dot, 40, 40);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(g_note_kinds[note->kind].color),
                            0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  velaride_icon_create(dot, g_note_kinds[note->kind].icon, 9, 9, 22, VR_INK);
  text = make_label(row, g_note_kinds[note->kind].title, lv_color_hex(VR_INK),
                    &velaride_font_cn_20, 68, 12, 0);
  LV_UNUSED(text);
  make_label(row, g_note_kinds[note->kind].body, lv_color_hex(VR_INK2),
             &velaride_font_cn_20, 68, 40, 0);
  note_time(note->when, when, sizeof(when));
  text = make_label(row, when, lv_color_hex(VR_INK3), &velaride_font_cn_20,
                    196, 12, 140);
  lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_RIGHT, 0);
}

static void create_note_center(struct scene_slot_s *slot)
{
  int i;

  make_label(slot->detail, "通知", lv_color_hex(VR_INK), &velaride_font_cn_28,
             36, 32, 0);
  if (g_notes.count == 0)
    {
      make_label(slot->detail, "没有新通知", lv_color_hex(VR_INK3),
                 &velaride_font_cn_20, 0, 200, SCR_W);
      return;
    }

  for (i = 0; i < g_notes.count && i < NOTE_VISIBLE; i++)
    {
      note_row(slot->detail, NOTE_Y0 + i * NOTE_ROW_H, &g_notes.items[i]);
    }

  make_label(slot->detail, "清除全部", vr_accent(), &velaride_font_cn_20,
             0, NOTE_CLEAR_Y, SCR_W);
}

static void note_tap(lv_point_t point)
{
  int index;
  int app;

  if (g_notes.count > 0 && point.y >= NOTE_CLEAR_Y - 10 &&
      point.y < NOTE_CLEAR_Y + 34)
    {
      g_notes.count = 0;
      build_scene(g_nav.current, SCENE_NOTES, false);
      preview_invalidate(SCENE_NOTES);
      return;
    }

  if (point.y < NOTE_Y0)
    {
      return;
    }

  index = (point.y - NOTE_Y0) / NOTE_ROW_H;
  if (index >= g_notes.count || index >= NOTE_VISIBLE)
    {
      return;
    }

  app = g_note_kinds[g_notes.items[index].kind].app;
  if(g_notes.items[index].kind==NOTE_ACTIVITY&&!velaride_sport_ui_active())
    {
      if(g_menu.active)menu_hide();
      velaride_sport_ui_show_info(VELARIDE_INFO_ACTIVITY);
      return;
    }
  if (app >= 0)
    {
      launch_app(app);
    }
}

static void banner_hide_cb(lv_timer_t *timer)
{
  lv_timer_pause(timer);
  lv_obj_add_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN);
}

static void banner_y_cb(void *obj, int32_t y)
{
  lv_obj_set_y(obj, y);
}

/* Banner over whatever is on screen; slides in with a short overshoot. */

static void banner_show(enum note_kind_e kind)
{
  lv_anim_t anim;

  lv_obj_set_style_bg_color(lv_obj_get_child(g_notes.banner, 0),
                            lv_color_hex(g_note_kinds[kind].color), 0);
  lv_obj_set_user_data(g_notes.banner, (void *)(intptr_t)kind);
  lv_label_set_text(g_notes.banner_title, g_note_kinds[kind].title);
  lv_label_set_text(g_notes.banner_body, g_note_kinds[kind].body);
  lv_obj_invalidate(lv_obj_get_child(g_notes.banner, 0));
  lv_obj_remove_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_notes.banner);
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, g_notes.banner);
  lv_anim_set_exec_cb(&anim, banner_y_cb);
  lv_anim_set_values(&anim, -BANNER_H, BANNER_Y);
  lv_anim_set_duration(&anim, 420);
  lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
  lv_anim_start(&anim);
  lv_timer_set_period(g_notes.banner_timer, BANNER_SHOW_MS);
  lv_timer_reset(g_notes.banner_timer);
  lv_timer_resume(g_notes.banner_timer);
}

static void banner_tap_cb(lv_event_t *event)
{
  int kind = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(event));

#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop_alerts();
#endif
  lv_timer_pause(g_notes.banner_timer);
  lv_obj_add_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN);
  if(kind==NOTE_ACTIVITY&&!velaride_sport_ui_active())
    {
      if(g_menu.active)menu_hide();
      velaride_sport_ui_show_info(VELARIDE_INFO_ACTIVITY);
      return;
    }
  if (g_note_kinds[kind].app >= 0 && !velaride_sport_ui_active())
    {
      launch_app(g_note_kinds[kind].app);
    }
}

static void banner_icon_draw_cb(lv_event_t *event)
{
  lv_obj_t *dot = lv_event_get_target(event);
  int kind = (int)(intptr_t)lv_obj_get_user_data(g_notes.banner);
  lv_area_t area;

  lv_obj_get_coords(dot, &area);
  velaride_icon_draw(lv_event_get_layer(event), g_note_kinds[kind].icon,
                     area.x1 + 9, area.y1 + 9, 22, lv_color_white());
}

static void build_banner(lv_obj_t *screen)
{
  lv_obj_t *dot;

  g_notes.banner = lv_obj_create(screen);
  lv_obj_remove_style_all(g_notes.banner);
  lv_obj_set_pos(g_notes.banner, 18, -BANNER_H);
  lv_obj_set_size(g_notes.banner, SCR_W - 36, BANNER_H);
  lv_obj_set_style_radius(g_notes.banner, 26, 0);
  lv_obj_set_style_bg_color(g_notes.banner, lv_color_hex(0x26262a), 0);
  lv_obj_set_style_bg_opa(g_notes.banner, LV_OPA_COVER, 0);
  lv_obj_add_flag(g_notes.banner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(g_notes.banner, LV_OBJ_FLAG_SCROLLABLE |
                                     LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_notes.banner, banner_tap_cb, LV_EVENT_CLICKED, NULL);
  dot = lv_obj_create(g_notes.banner);
  lv_obj_remove_style_all(dot);
  lv_obj_set_pos(dot, 14, 14);
  lv_obj_set_size(dot, 40, 40);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(dot, banner_icon_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
  g_notes.banner_title = make_label(g_notes.banner, "", lv_color_hex(VR_INK),
                                    &velaride_font_cn_20, 68, 10, 0);
  g_notes.banner_body = make_label(g_notes.banner, "", lv_color_hex(VR_INK2),
                                   &velaride_font_cn_20, 68, 38, 0);
  lv_obj_add_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN);
  g_notes.banner_timer = lv_timer_create(banner_hide_cb, BANNER_SHOW_MS, NULL);
  lv_timer_pause(g_notes.banner_timer);
}

/* Newest first. Do-not-disturb keeps the note but skips the banner. */

static void note_post(enum note_kind_e kind)
{
  int i;

  for (i = 0; i < g_notes.count; i++)
    {
      if (g_notes.items[i].kind == kind)
        {
          break;
        }
    }

  if (i == g_notes.count && g_notes.count < NOTE_MAX)
    {
      g_notes.count++;
    }

  for (i = LV_MIN(i, NOTE_MAX - 1); i > 0; i--)
    {
      g_notes.items[i] = g_notes.items[i - 1];
    }

  g_notes.items[0].kind = kind;
  g_notes.items[0].when = time(NULL);
  preview_invalidate(SCENE_NOTES);
  if (g_nav.current->scene == SCENE_NOTES && !g_nav.transition_active)
    {
      build_scene(g_nav.current, SCENE_NOTES, false);
    }

  if (!g_notes.dnd)
    {
      screen_wake(false);
      banner_show(kind);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
      if (kind == NOTE_ALARM)
        {
          velaride_sound_request(VR_SOUND_ALARM);
        }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
      if (kind == NOTE_COUNTDOWN)
        {
          velaride_sound_request(VR_SOUND_COUNTDOWN);
        }
#endif
#endif
    }
}

/* A review arrives from the gateway's BLE task. The ride or page on
 * screen stays; the banner leads to the review.
 */

static void review_watch(void)
{
  if (velaride_ride_session_take_review_view_request())
    {
      g_face_record_refresh = true;
      note_post(NOTE_REVIEW);
    }
}

/* Called once per second: turns state changes into notifications. */

static void note_watch(void)
{
  struct velaride_ride_snapshot_s ride;
  bool connected = velaride_ride_session_connected();

  if (connected != g_notes.connected)
    {
      g_notes.connected = connected;
      if (g_notes.watch_started)
        {
          note_post(connected ? NOTE_LINK_UP : NOTE_LINK_DOWN);
        }
    }

  velaride_ride_session_get(&ride);
  if(!ride.active&&!g_nav.dragging&&!g_nav.transition_active&&velaride_activity_take_due())
    note_post(NOTE_ACTIVITY);
  if (ride.alert == VELARIDE_ALERT_REST && !g_notes.rest_posted)
    {
      g_notes.rest_posted = true;
      note_post(NOTE_REST);
    }
  else if (ride.alert != VELARIDE_ALERT_REST)
    {
      g_notes.rest_posted = false;
    }

  g_notes.watch_started = true;
}

static uint8_t g_applied_accent;

static void apply_theme_change(void)
{
  struct velaride_ui_prefs_s prefs;
  int i;

  velaride_ui_prefs_get(&prefs);
  if (prefs.accent == g_applied_accent)
    {
      return;
    }

  g_applied_accent = prefs.accent;
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      g_nav.preview_ready[i] = false;
    }

  build_scene(g_nav.current, current_scene(), false);
  preload_neighbor_scenes();

  /* The list menu bakes the accent into the ride row. */

  g_menu.row_mix[APP_RIDE] = 0;
  lv_obj_set_style_bg_color(lv_obj_get_child(g_menu.rows[APP_RIDE], 0),
                            app_color(APP_RIDE), 0);
  lv_obj_set_style_text_color(g_menu.title, vr_accent(), 0);
  lv_obj_invalidate(g_menu.root);
}

static void sport_exit_cb(void *arg)
{
  LV_UNUSED(arg);
  apply_theme_change();
  g_face_record_refresh = true;
  menu_hide();
  lv_obj_move_foreground(g_nav.current->root);
  lv_obj_move_foreground(g_nav.reveal);
  lv_obj_invalidate(g_nav.current->root);
  update_current_scene();
  preview_schedule();

  /* Apps opened from the launcher return to it, like on a phone. */

  if (g_menu.return_to_menu)
    {
      g_menu.return_to_menu = false;
      menu_show();
    }
}

static void enter_sport_mode(void)
{
  if (velaride_sport_ui_active())
    {
      return;
    }

  menu_hide();
  if (g_nav.preview_timer != NULL)
    {
      lv_timer_pause(g_nav.preview_timer);
    }
  velaride_sport_ui_enter();
}

static void launch_app(int app)
{
  static const enum velaride_info_e info[APP_COUNT] =
  {
    [APP_COMPASS] = VELARIDE_INFO_COMPASS,
    [APP_SAFETY] = VELARIDE_INFO_SAFETY,
    [APP_CONNECT] = VELARIDE_INFO_CONNECTION,
    [APP_DISPLAY] = VELARIDE_INFO_DISPLAY,
    [APP_SETTINGS] = VELARIDE_INFO_DEVICE,
    [APP_HELP] = VELARIDE_INFO_HELP,
    [APP_ABOUT] = VELARIDE_INFO_HARDWARE,
    [APP_LIGHT] = VELARIDE_INFO_LIGHT,
    [APP_STOPWATCH] = VELARIDE_INFO_STOPWATCH,
    [APP_LEVEL] = VELARIDE_INFO_LEVEL,
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
    [APP_COUNTDOWN] = VELARIDE_INFO_COUNTDOWN,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
    [APP_ALARM] = VELARIDE_INFO_ALARM,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
    [APP_SOUND] = VELARIDE_INFO_SOUND,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
    [APP_NIGHT] = VELARIDE_INFO_NIGHT,
#endif
  };
  bool from_menu = g_menu.active;

  if (app < 0 || app >= APP_COUNT ||
      g_nav.transition_active || velaride_sport_ui_active())
    {
      return;
    }

  menu_hide();
  lv_timer_pause(g_nav.preview_timer);

  /* A finished ride ends on the watch face, not back in the launcher. */

  g_menu.return_to_menu = from_menu && app != APP_RIDE;
  if (app == APP_RIDE)
    {
      enter_sport_mode();
    }
  else if (app == APP_RECORDS)
    {
      velaride_sport_ui_show_records();
    }
  else if (app == APP_REVIEW)
    {
      velaride_sport_ui_show_latest();
    }
  else
    {
      velaride_sport_ui_show_info(info[app]);
    }
}

/* --- Screen wake ----------------------------------------------------------- */

/* The panel is switched with DISPLAY_OFF/ON only; its supply and the
 * shared rails stay on (see the black-screen notes). While it is off a
 * transparent shield on the top layer takes the first touch, so waking
 * never presses whatever is under the finger.
 */

static struct
{
  bool off;
  bool by_raise;         /* woke on a raise and not touched since */
  bool facing;
  lv_obj_t *shield;
  lv_timer_t *timer;
  uint32_t facing_since;
  uint32_t away_tick;    /* last sample that faced away */
  uint32_t away_since;
  bool away_valid;
  uint32_t woke_tick;
  float up;              /* last screen-up component, for diagnostics */
  int16_t accel[3];
  int accel_error;
  uint32_t sleeps;
  uint32_t raises;
} g_wake;

static void screen_power(bool on)
{
  g_screen_power_request = on ? 1 : 0;
}

static uint32_t wake_timeout_ms(void)
{
  struct velaride_wake_prefs_s prefs;

  velaride_wake_prefs_get(&prefs);
  return prefs.sleep == VELARIDE_SLEEP_10S ? 10000 :
         prefs.sleep == VELARIDE_SLEEP_20S ? 20000 : 0;
}

static void screen_sleep(void)
{
  if (g_wake.off)
    {
      return;
    }

  g_wake.off = true;
  g_wake.by_raise = false;
  g_wake.facing = false;
  g_wake.away_valid = false;
  g_wake.sleeps++;
  lv_obj_remove_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_wake.shield);
  screen_power(false);
}

static void screen_wake(bool raise)
{
  lv_display_trigger_activity(NULL);
  if (!g_wake.off)
    {
      return;
    }

  g_wake.off = false;
  g_wake.by_raise = raise;
  g_wake.woke_tick = lv_tick_get();
  g_wake.away_valid = false;
  if (raise)
    {
      g_wake.raises++;

      /* Nothing is pressed: drop the shield right away. */

      lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
    }

  /* The clock kept running while dark; bring the page up to date
   * before the first frame is drawn.
   */

  update_current_scene();
  screen_power(true);
}

static void shield_cb(lv_event_t *event)
{
  lv_event_code_t code = lv_event_get_code(event);

  if (code == LV_EVENT_PRESSED)
    {
      screen_wake(false);
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      if (!g_wake.off)
        {
          lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* One accelerometer sample: returns the screen-up component of gravity
 * (1 face up, 0 on edge, -1 face down) or a value below -1 on failure.
 */

static float wake_sample(void)
{
  int16_t x;
  int16_t y;
  int16_t z;
  float g;

  g_wake.accel_error = velaride_ride_session_read_accel(&x, &y, &z);
  if (g_wake.accel_error < 0)
    {
      return -2.0f;
    }

  g_wake.accel[0] = x;
  g_wake.accel[1] = y;
  g_wake.accel[2] = z;
  g = sqrtf((float)x * x + (float)y * y + (float)z * z);
  if (g < 1.0f)
    {
      return -2.0f;
    }

  g_wake.up = WAKE_SCREEN_UP_SIGN * z / g;
  return g_wake.up;
}

/* Pose rules, separate from the sensor so tests can feed samples. A raise
 * is a turn from facing away to facing up, held briefly; a watch left
 * face up on a table never wakes itself again.
 */

static void wake_pose(float up, uint32_t now, bool raise_enabled)
{
  if (up < -1.5f)
    {
      return;
    }

  if (up <= WAKE_AWAY)
    {
      if (!g_wake.away_valid)
        {
          g_wake.away_since = now;
        }

      g_wake.away_valid = true;
      g_wake.away_tick = now;
      g_wake.facing = false;
    }
  else
    {
      g_wake.away_valid = false;
    }

  if (up >= WAKE_FACING)
    {
      if (!g_wake.facing)
        {
          g_wake.facing = true;
          g_wake.facing_since = now;
        }
    }
  else
    {
      g_wake.facing = false;
    }

  if (!raise_enabled)
    {
      return;
    }

  if (g_wake.off && g_wake.facing && g_wake.away_tick != 0 &&
      now - g_wake.facing_since >= WAKE_HOLD_MS &&
      g_wake.facing_since - g_wake.away_tick <= WAKE_FROM_AWAY_MS)
    {
      g_wake.away_tick = 0;
      screen_wake(true);
    }
  else if (!g_wake.off && g_wake.by_raise && g_wake.away_valid &&
           now - g_wake.away_since >= WAKE_DOWN_MS &&
           !velaride_sport_ui_keep_awake())
    {
      screen_sleep();
    }
}

static void wake_timer_cb(lv_timer_t *timer)
{
  struct velaride_wake_prefs_s prefs;
  uint32_t timeout = wake_timeout_ms();
  bool busy = g_nav.dragging || g_nav.transition_active;

  LV_UNUSED(timer);
  velaride_wake_prefs_get(&prefs);

  /* A touch after a raise turns it into an ordinary wake. */

  if (g_wake.by_raise &&
      lv_display_get_inactive_time(NULL) < lv_tick_elaps(g_wake.woke_tick))
    {
      g_wake.by_raise = false;
    }

  if (prefs.raise && (g_wake.off || g_wake.by_raise))
    {
      wake_pose(wake_sample(), lv_tick_get(), true);
    }

  if (!g_wake.off && !busy && timeout != 0 &&
      lv_display_get_inactive_time(NULL) >= timeout)
    {
      if (velaride_sport_ui_keep_awake())
        {
          lv_display_trigger_activity(NULL);
        }
      else
        {
          screen_sleep();
        }
    }
}

static void wake_init(void)
{
  memset(&g_wake, 0, sizeof(g_wake));
  g_wake.shield = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_wake.shield);
  lv_obj_set_size(g_wake.shield, SCR_W, SCR_H);
  lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_CLICKABLE |
                                 LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(g_wake.shield, LV_OBJ_FLAG_SCROLLABLE |
                                    LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(g_wake.shield, shield_cb, LV_EVENT_ALL, NULL);
  g_wake.timer = lv_timer_create(wake_timer_cb, WAKE_POLL_MS, NULL);
}

static void button_handle_state(btn_buttonset_t state, uint64_t now)
{
  bool pressed = (state & 1) != 0 && (g_menu.button_last & 1) == 0;

  g_menu.button_last = state;
  if (!pressed || (g_menu.button_has_action &&
      now - g_menu.button_action_us < BUTTON_GUARD_MS * 1000))
    {
      return;
    }

  /* One navigation action per press. Rapid repeat presses are ignored;
   * ride entry is available only through the on-screen controls.
   */

  g_menu.button_has_action = true;
  g_menu.button_action_us = now;
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop_alerts();
#endif
  if (g_wake.off)
    {
      screen_wake(false);
      lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
      return;
    }

  lv_display_trigger_activity(NULL);
  if (!velaride_sport_ui_active())
    {
      switch_shell_mode();
    }
  else
    {
      velaride_sport_ui_back();
    }
}

static void button_timer_cb(lv_timer_t *timer)
{
  btn_buttonset_t state = 0;
  ssize_t nread;

  LV_UNUSED(timer);
  if (g_menu.button_fd < 0)
    {
      return;
    }

  nread = read(g_menu.button_fd, &state, sizeof(state));
  if (nread == sizeof(state))
    {
      button_handle_state(state, monotonic_us());
    }
}

static void build_shell(void)
{
  lv_obj_t *screen = lv_screen_active();
  int i;

  struct velaride_ui_prefs_s prefs;

  memset(&g_nav, 0, sizeof(g_nav));
  g_nav.face_index = 0;
  g_nav.function_index = -1;
  g_nav.preview_minute = -1;
  g_nav.reveal_preview = -1;

  /* Load saved appearance before the first face is built. */

  velaride_device_init();
  velaride_ui_prefs_get(&prefs);
  g_applied_accent = prefs.accent;

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
  g_preview_handlers = *lv_draw_buf_get_handlers();
  g_preview_handlers.invalidate_cache_cb = g_preview_handlers.flush_cache_cb;
  lv_obj_set_size(g_nav.preview_slot.root, SCR_W, SCR_H);
  lv_obj_set_pos(g_nav.preview_slot.root, 0, 0);
  lv_obj_add_flag(g_nav.preview_slot.root, LV_OBJ_FLAG_HIDDEN);

  g_nav.cover_preview = -1;
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
  memset(&g_menu, 0, sizeof(g_menu));
  g_menu.button_fd = -1;
  build_honey_menu(screen);
  build_list_menu(screen);
  velaride_sport_ui_init(screen, sport_exit_cb, NULL);
  build_banner(screen);
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
  lv_display_add_event_cb(lv_display_get_default(), nav_refr_start_cb,
                          LV_EVENT_REFR_START, NULL);
  lv_display_add_event_cb(lv_display_get_default(), nav_flush_start_cb,
                          LV_EVENT_FLUSH_START, NULL);
  g_nav.spring_timer = lv_timer_create(spring_timer_cb, SPRING_PERIOD_MS,
                                       NULL);
  lv_timer_pause(g_nav.spring_timer);
  g_nav.preload_timer = lv_timer_create(preload_timer_cb, PRELOAD_DELAY_MS,
                                        NULL);
  lv_timer_pause(g_nav.preload_timer);
  g_nav.data_timer = lv_timer_create(data_timer_cb, 1000, NULL);
  g_nav.preview_timer = lv_timer_create(preview_timer_cb,
                                         PREVIEW_DELAY_MS, NULL);
  preview_schedule();
  lv_timer_create(button_timer_cb, BUTTON_POLL_MS, NULL);
  wake_init();
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  velaride_night_ui_on_change(night_cache_invalidate);
  night_watch_cb(NULL);
  lv_timer_create(night_watch_cb, 250, NULL);
#endif
  lv_timer_create(honey_inertia_timer_cb, 16, NULL);
  update_current_scene();
}

#ifdef CONFIG_LV_USE_NUTTX_LIBUV
static void lcd_test_pattern(void)
{
  struct lcddev_area_s area;
  uint16_t pixels[SCR_W * 2];
  int lcd_fd;
  int ret = 0;
  int i;
  int y;

  lcd_fd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
  if (lcd_fd < 0)
    {
      return;
    }

  ioctl(lcd_fd, LCDDEVIO_WAITAREA, 200);
  for (i = 0; i < SCR_W * 2; i++)
    {
      int x = i % SCR_W;
      pixels[i] = x < SCR_W / 3 ? 0xf800 :
                  x < SCR_W * 2 / 3 ? 0x07e0 : 0x001f;
    }

  memset(&area, 0, sizeof(area));
  area.col_end = SCR_W - 1;
  area.stride = SCR_W * 2;
  area.data = (uint8_t *)pixels;
  for (y = 0; y < SCR_H; y += 2)
    {
      area.row_start = y;
      area.row_end = y + 1;
      ret = ioctl(lcd_fd, LCDDEVIO_PUTAREA, (unsigned long)&area);
      if (ret < 0)
        {
          break;
        }
    }

  ioctl(lcd_fd, LCDDEVIO_SETPOWER, CONFIG_LCD_MAXPOWER);
  printf("VELARIDE_LCD: color bars rows=%d ret=%d\n", y, ret);
  close(lcd_fd);
}

static bool g_screen_dark;

static void frame_timer_cb(uv_timer_t *timer)
{
#ifdef CONFIG_LCD_USING_CO5300
  static uint32_t last_check;
  static uint32_t last_brightness;
  static int health_fd = -1;
  static int health_ret = -1;
  int ret_power;

  if (!g_lcd_test_active && lv_tick_elaps(last_check) >= 1000)
    {
      last_check = lv_tick_get();
      if (health_fd < 0)
        {
          health_fd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
        }

      health_ret = health_fd < 0 ? -1 :
                   ioctl(health_fd, LCDDEVIO_CHECKSTATE, 0);
      if (health_ret == 1)
        {
          printf("VELARIDE_LCD: recovering lost panel state at %" PRIu32
                 "ms\n", last_check);
          g_lcd_control_requested = LCDDEVIO_REINITIALIZE;
        }
    }

  if (g_screen_power_request == 0)
    {
      g_screen_power_request = -1;
      g_screen_dark = true;
      ret_power = health_fd < 0 ? -1 :
                  ioctl(health_fd, LCDDEVIO_SETPOWER, 0);
      printf("VELARIDE_WAKE: display off ret=%d\n", ret_power);
    }
  else if (g_screen_power_request == 1)
    {
      /* Draw the up-to-date frame into the panel memory first, so the
       * panel never shows the frame from before it went dark.
       */

      g_screen_power_request = -1;
      lv_obj_invalidate(lv_screen_active());
      review_watch();
      _lv_display_refr_timer(NULL);
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
      {
        struct velaride_night_s night;
        velaride_night_get(&night);
        if (night.active && !night.exempt)
          {
            uint8_t target = velaride_night_brightness(velaride_display_tick());
            int brightness_ret = health_fd < 0 ? -1 :
              ioctl(health_fd, LCDDEVIO_SETBRIGHTNESS, target);
            velaride_display_applied(target, brightness_ret < 0 ? -errno : 0);
          }
      }
#endif
      ret_power = health_fd < 0 ? -1 :
                  ioctl(health_fd, LCDDEVIO_SETPOWER, CONFIG_LCD_MAXPOWER);
      g_screen_dark = false;
      printf("VELARIDE_WAKE: display on ret=%d\n", ret_power);
      return;
    }

  if (g_screen_dark && !g_lcd_test_active)
    {
      /* Keep LVGL timers and gateway messages going; skip drawing. */

      review_watch();
      return;
    }

  if (!g_lcd_test_active && health_ret == 0 &&
      lv_tick_elaps(last_brightness) >= 250)
    {
      struct velaride_display_s display;
      uint8_t target = velaride_display_tick();
      int ret;

#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
      target = velaride_night_brightness(target);
#endif
      last_brightness = lv_tick_get();
      velaride_display_get(&display);
      if (display.applied != target || display.apply_error != 0)
        {
          ret = ioctl(health_fd, LCDDEVIO_SETBRIGHTNESS, target);
          velaride_display_applied(target, ret < 0 ? -errno : 0);
        }
    }
#endif

  if (g_lcd_control_requested != 0)
    {
      int command = g_lcd_control_requested;
      int lcd_fd;
      int control_ret;

      g_lcd_control_requested = 0;
      lcd_fd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
      control_ret = lcd_fd < 0 ? -1 :
                    ioctl(lcd_fd, command, command == LCDDEVIO_SETPOWER ?
                          CONFIG_LCD_MAXPOWER : 0);
      printf("VELARIDE_LCD: control=%d ret=%d errno=%d\n",
             command, control_ret, control_ret < 0 ? errno : 0);
      if (lcd_fd >= 0)
        {
          close(lcd_fd);
        }

      if ((command == LCDDEVIO_REINITIALIZE ||
           command == LCDDEVIO_SETPOWER) && control_ret == 0)
        {
          g_refresh_requested = true;
        }
    }

  if (g_lcd_test_requested)
    {
      g_lcd_test_requested = false;
      g_lcd_test_active = true;
      lcd_test_pattern();
    }

  if (g_refresh_requested)
    {
      g_refresh_requested = false;
      g_lcd_test_active = false;
      lv_obj_invalidate(lv_screen_active());
    }

  if (g_lcd_test_active)
    {
      return;
    }

  review_watch();
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

#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  if (argc > 1 && strcmp(argv[1], "countdown") == 0)
    {
      struct velaride_countdown_s countdown;
      int status = 0;

      if (!lv_is_initialized())
        {
          printf("VELARIDE_COUNTDOWN: UI not initialized\n");
          return EXIT_FAILURE;
        }

      if (argc == 4 && strcmp(argv[2], "start") == 0)
        {
          char *end;
          unsigned long seconds;

          errno = 0;
          seconds = strtoul(argv[3], &end, 10);
          if (errno != 0 || end == argv[3] || *end != '\0' ||
              seconds == 0 || seconds > 180 * 60)
            {
              return EXIT_FAILURE;
            }

          status = velaride_countdown_set((uint32_t)seconds);
          if (status == 0)
            {
              velaride_countdown_start(lv_tick_get());
            }
        }
      else if (argc == 3 && strcmp(argv[2], "pause") == 0)
        {
          velaride_countdown_pause(lv_tick_get());
        }
      else if (argc == 3 && strcmp(argv[2], "resume") == 0)
        {
          velaride_countdown_get(lv_tick_get(), &countdown);
          if (countdown.state != VELARIDE_COUNTDOWN_PAUSED)
            {
              return EXIT_FAILURE;
            }

          velaride_countdown_start(lv_tick_get());
        }
      else if (argc == 3 && strcmp(argv[2], "cancel") == 0)
        {
          velaride_countdown_cancel();
        }
      else if (argc != 2 && !(argc == 3 && strcmp(argv[2], "status") == 0))
        {
          printf("usage: velaride countdown [status|start SECONDS|pause|resume|cancel]\n");
          return EXIT_FAILURE;
        }

      velaride_countdown_get(lv_tick_get(), &countdown);
      printf("VELARIDE_COUNTDOWN: state=%d duration=%" PRIu32
             " remaining_ms=%" PRIu32 " completions=%" PRIu32 " ret=%d\n",
             countdown.state, countdown.duration_s, countdown.remaining_ms,
             countdown.completions, status);
      return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  if (argc > 1 && strcmp(argv[1], "alarm") == 0)
    {
      struct velaride_alarm_s alarm;
      int status = 0;
      velaride_alarm_get(&alarm);
      if (argc == 5 && strcmp(argv[2], "set") == 0)
        {
          char *end;
          unsigned long hour;
          unsigned long minute;
          errno = 0;
          hour = strtoul(argv[3], &end, 10);
          if (errno || end == argv[3] || *end || hour > 23)
            {
              return EXIT_FAILURE;
            }
          minute = strtoul(argv[4], &end, 10);
          if (errno || end == argv[4] || *end || minute > 59)
            {
              return EXIT_FAILURE;
            }
          status = velaride_alarm_set(hour, minute, true, time(NULL));
        }
      else if (argc == 3 && strcmp(argv[2], "off") == 0)
        {
          status = velaride_alarm_set(alarm.hour, alarm.minute, false,
                                      time(NULL));
        }
      else if (argc == 3 && strcmp(argv[2], "confirm") == 0)
        {
          status = velaride_alarm_set(alarm.hour, alarm.minute, true,
                                      time(NULL));
        }
      else if (argc == 3 && strcmp(argv[2], "ack") == 0)
        {
          velaride_alarm_ack();
        }
      else if (argc != 2 && !(argc == 3 && strcmp(argv[2], "status") == 0))
        {
          printf("usage: velaride alarm [status|set HH MM|off|confirm|ack]\n");
          return EXIT_FAILURE;
        }
      velaride_alarm_get(&alarm);
      printf("VELARIDE_ALARM: hour=%u minute=%u enabled=%d confirm=%d "
             "saving=%d error=%d ringing=%d completions=%" PRIu32 " ret=%d\n",
             alarm.hour, alarm.minute, alarm.enabled, alarm.needs_confirmation,
             alarm.saving, alarm.save_error, alarm.ringing, alarm.completions,
             status);
      return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  if (argc > 1 && strcmp(argv[1], "sound") == 0)
    {
      struct velaride_sound_s sound;
      int status = 0;
      velaride_sound_get(&sound);
      if (argc == 3 && strcmp(argv[2], "mute") == 0)
        {
          status = velaride_sound_set(false, sound.voice, sound.rings,
                                      sound.volume);
        }
      else if (argc == 3 && strcmp(argv[2], "on") == 0)
        {
          status = velaride_sound_set(true, sound.voice, sound.rings,
                                      sound.volume);
        }
      else if (argc == 3 && strcmp(argv[2], "stop") == 0)
        {
          velaride_sound_stop(VR_SOUND_NONE);
        }
      else if (argc == 4 && strcmp(argv[2], "volume") == 0)
        {
          unsigned long percent;
          char *end;
          if (strcmp(argv[3], "soft") == 0)
            {
              percent = 35;
            }
          else if (strcmp(argv[3], "normal") == 0)
            {
              percent = 100;
            }
          else
            {
              errno = 0;
              percent = strtoul(argv[3], &end, 10);
              if (errno || end == argv[3] || *end || percent > 100)
                {
                  printf("volume must be 0..100\n");
                  return EXIT_FAILURE;
                }
            }
          status = velaride_sound_set_volume((uint8_t)percent);
        }
      else if (argc == 4 && strcmp(argv[2], "play") == 0)
        {
          static const char *names[] = {"none", "ride", "3", "2", "1",
            "go", "alarm", "countdown", "alarm-mode", "countdown-mode",
            "stopwatch"};
          int cue;
          status = -EINVAL;
          for (cue = 1; cue < VR_SOUND_CUE_COUNT; cue++)
            {
              if (strcmp(argv[3], names[cue]) == 0)
                {
                  status = velaride_sound_request(cue);
                  break;
                }
            }
        }
      else if (argc != 2 && !(argc == 3 && strcmp(argv[2], "status") == 0))
        {
          printf("usage: velaride sound [status|mute|on|stop|"
                 "volume 0..100|play ride/3/2/1/go/alarm/countdown]\n");
          return EXIT_FAILURE;
        }
      velaride_sound_get(&sound);
      printf("VELARIDE_SOUND_STATUS: enabled=%d voice=%d rings=%d "
             "volume=%u dnd=%d active=%d pending=%d saving=%d save_error=%d "
             "error=%d completed=%lu cancelled=%lu skipped=%lu hw=%lu ret=%d\n",
             sound.enabled, sound.voice, sound.rings, sound.volume, sound.dnd,
             sound.active, sound.pending, sound.saving, sound.save_error,
             sound.last_error, (unsigned long)sound.completed,
             (unsigned long)sound.cancelled, (unsigned long)sound.skipped,
             (unsigned long)velaride_sound_hardware_state(),
             status);
      return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  if (argc > 1 && strcmp(argv[1], "night") == 0)
    {
      struct velaride_night_s night;
      int ret = 0;
      velaride_night_get(&night);
      if (argc == 3 && strcmp(argv[2], "on") == 0)
        ret = velaride_night_set(VR_NIGHT_ON, night.limit);
      else if (argc == 3 && strcmp(argv[2], "off") == 0)
        ret = velaride_night_set(VR_NIGHT_OFF, night.limit);
      else if (argc == 3 && strcmp(argv[2], "auto") == 0)
        ret = velaride_night_set(VR_NIGHT_AUTO, night.limit);
      else if (argc == 4 && strcmp(argv[2], "limit") == 0)
        {
          char *end;
          unsigned long limit;
          errno = 0;
          limit = strtoul(argv[3], &end, 10);
          if (errno || end == argv[3] || *end || limit < 20 || limit > 40)
            return EXIT_FAILURE;
          ret = velaride_night_set(night.mode, limit);
        }
      else if (argc != 2 && !(argc == 3 && strcmp(argv[2], "status") == 0))
        {
          printf("usage: velaride night [status|off|on|auto|limit 20..40]\n");
          return EXIT_FAILURE;
        }
      velaride_night_get(&night);
      printf("VELARIDE_NIGHT: mode=%u limit=%u active=%d exempt=%d "
             "light_valid=%d lux=%.2f saving=%d error=%d ret=%d\n",
             night.mode, night.limit, night.active, night.exempt,
             night.light_valid, (double)night.lux, night.saving,
             night.save_error, ret);
      return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
#endif

  if (argc > 1 && strcmp(argv[1], "wake-status") == 0)
    {
      struct velaride_wake_prefs_s prefs;

      velaride_wake_prefs_get(&prefs);
      printf("VELARIDE_WAKE: off=%d raise_pref=%d sleep=%u by_raise=%d "
             "accel=%d,%d,%d up=%.2f err=%d sleeps=%" PRIu32
             " raises=%" PRIu32 " idle=%" PRIu32 "ms\n",
             g_wake.off, prefs.raise, prefs.sleep, g_wake.by_raise,
             g_wake.accel[0], g_wake.accel[1], g_wake.accel[2],
             (double)g_wake.up, g_wake.accel_error, g_wake.sleeps,
             g_wake.raises, lv_display_get_inactive_time(NULL));
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "nav-stats") == 0)
    {
      static const char *names[VELARIDE_TRANSITION_COUNT] =
        {"slide", "blinds", "iris"};
      int i;

      for (i = 0; i < VELARIDE_TRANSITION_COUNT; i++)
        {
          struct nav_stats_s *st = &g_nav_stats[i];
          uint32_t rate_x10 = st->span_us == 0 ? 0 :
            (uint32_t)((uint64_t)st->frames * 10000000 / st->span_us);

          printf("VELARIDE_NAV_STATS: mode=%s swipes=%" PRIu32
                 " frames=%" PRIu32 " avg=%" PRIu32 ".%" PRIu32
                 "Hz max_gap=%" PRIu32 "ms slow=%" PRIu32 "\n",
                 names[i], st->swipes, st->frames, rate_x10 / 10,
                 rate_x10 % 10, st->max_gap_us / 1000, st->slow_frames);
          if (argc > 2 && strcmp(argv[2], "reset") == 0)
            {
              memset(st, 0, sizeof(*st));
            }
        }

      for (i = 0; i < VELARIDE_MENU_COUNT; i++)
        {
          struct nav_stats_s *st = &g_menu_stats[i];
          uint32_t rate_x10 = st->span_us == 0 ? 0 :
            (uint32_t)((uint64_t)st->frames * 10000000 / st->span_us);

          printf("VELARIDE_MENU_STATS: style=%s moves=%" PRIu32
                 " frames=%" PRIu32 " avg=%" PRIu32 ".%" PRIu32
                 "Hz max_gap=%" PRIu32 "ms slow=%" PRIu32 "\n",
                 i == VELARIDE_MENU_HONEY ? "honey" : "list", st->swipes,
                 st->frames, rate_x10 / 10, rate_x10 % 10,
                 st->max_gap_us / 1000, st->slow_frames);
          if (argc > 2 && strcmp(argv[2], "reset") == 0)
            {
              memset(st, 0, sizeof(*st));
            }
        }

      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "compass") == 0)
    {
      struct velaride_compass_s compass;
      if (argc == 3 && strcmp(argv[2], "on") == 0)
        velaride_compass_enable(true);
      else if (argc == 3 && strcmp(argv[2], "off") == 0)
        velaride_compass_enable(false);
      else if (argc != 2)
        {
          printf("usage: velaride compass [on|off]\n");
          return EXIT_FAILURE;
        }
      velaride_compass_get(&compass);
      printf("VELARIDE_COMPASS: enabled=%d valid=%d calibrated=%d "
             "x=%.2f y=%.2f z=%.2f field=%.2f samples=%" PRIu32
             " failures=%" PRIu32 " error=%d\n", compass.enabled,
             compass.valid, compass.calibrated, (double)compass.x,
             (double)compass.y, (double)compass.z, (double)compass.field_ut,
             compass.samples, compass.failures, compass.error);
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "display-set") == 0)
    {
      struct velaride_display_s display;
      int level;

      if (argc != 3 || (strcmp(argv[2], "20") != 0 &&
          strcmp(argv[2], "50") != 0 && strcmp(argv[2], "80") != 0 &&
          strcmp(argv[2], "auto") != 0))
        {
          printf("usage: velaride display-set 20|50|80|auto\n");
          return EXIT_FAILURE;
        }

      velaride_display_get(&display);
      level = strcmp(argv[2], "auto") == 0 ? display.manual : atoi(argv[2]);
      velaride_display_set(level, strcmp(argv[2], "auto") == 0);
      printf("VELARIDE_DISPLAY: queued; inspect device-status for result\n");
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "device-status") == 0)
    {
      struct velaride_device_snapshot_s device;
      struct velaride_display_s display;

      velaride_device_get(&device);
      velaride_display_get(&display);
      printf("{\"usb_valid\":%s,\"usb_connected\":%s,"
             "\"adc_valid\":%s,\"reference_mv\":%" PRId32 ","
             "\"adc_error\":%d,\"calibrated\":false,\"battery_present\":null,"
             "\"light_detected\":%s,\"magnetometer_detected\":%s,"
             "\"adc_samples\":%" PRIu32 ",\"adc_failures\":%" PRIu32 ","
             "\"light_valid\":%s,\"light_lux\":%.2f,\"light_error\":%d,"
             "\"light_samples\":%" PRIu32 ",\"light_failures\":%" PRIu32 ","
             "\"brightness\":%u,\"brightness_target\":%u,\"auto\":%s,"
             "\"brightness_error\":%d,\"settings_error\":%d,"
             "\"settings_pending\":%s}\n",
             device.usb_valid ? "true" : "false",
             device.usb_connected ? "true" : "false",
             device.adc_valid ? "true" : "false", device.reference_mv,
             device.adc_error, device.light_detected ? "true" : "false",
             device.magnetometer_detected ? "true" : "false",
             device.adc_samples, device.adc_failures,
             device.light_valid ? "true" : "false", (double)device.light_lux,
             device.light_error, device.light_samples, device.light_failures,
             display.applied, display.target,
             display.automatic ? "true" : "false", display.apply_error,
             display.save_error, display.save_pending ? "true" : "false");
      return EXIT_SUCCESS;
    }

  if (argc > 1 && (strcmp(argv[1], "lcd-recover") == 0 ||
                  strcmp(argv[1], "lcd-diagnose") == 0))
    {
      g_lcd_control_requested = strcmp(argv[1], "lcd-recover") == 0 ?
                                LCDDEVIO_REINITIALIZE : LCDDEVIO_DUMPSTATE;
      return EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "lcd-test") == 0)
    {
      g_lcd_test_requested = true;
      return EXIT_SUCCESS;
    }

  if (argc > 1 && (strcmp(argv[1], "lcd-status") == 0 ||
                  strcmp(argv[1], "lcd-wake") == 0))
    {
      int lcd_fd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
      int power = -1;
      int lcd_ret;

      if (lcd_fd < 0)
        {
          return EXIT_FAILURE;
        }

      lcd_ret = ioctl(lcd_fd, LCDDEVIO_GETPOWER,
                      (unsigned long)&power);
      if (strcmp(argv[1], "lcd-wake") == 0)
        {
          g_lcd_control_requested = LCDDEVIO_SETPOWER;
        }

      printf("VELARIDE_LCD: power=%d ret=%d\n", power, lcd_ret);
      close(lcd_fd);
      return lcd_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

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
  if (!wait_for_device("/dev/lcd0", 10000))
    {
      return EXIT_FAILURE;
    }

#if defined(CONFIG_ARCH_CHIP_SF32LB52) && defined(CONFIG_INPUT_FT6146)
  {
    extern int huangshan_touch_retry(void) __attribute__((weak));
    int touch_ret = huangshan_touch_retry != NULL ?
                    huangshan_touch_retry() : -ENOSYS;

    if (touch_ret < 0)
      {
        printf("velaride: touch recovery failed: %d; starting display\n",
               touch_ret);
      }
  }
#endif

  /* Missing touch input is reported below, but must not suppress the
   * entire watch face and hardware-button interface.
   */

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
