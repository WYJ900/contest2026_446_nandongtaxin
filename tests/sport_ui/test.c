/* Real LVGL layout/events and actual UI source; device state is a fixture. */
#include "../../app/velaride/velaride_sport_ui.c"
#include <assert.h>
#include <stdlib.h>
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
static void night_wait_saved(void)
{
  struct velaride_night_s night;
  struct timespec delay = {.tv_nsec = 1000000};
  for (int i = 0; i < 3000; i++)
    {
      velaride_night_get(&night);
      if (!night.saving) return;
      nanosleep(&delay, NULL);
    }
  assert(!"night save timeout");
}
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
extern int sound_test_events[64];
extern int sound_test_count;
extern int sound_test_voice_stops;
void sound_test_reset(void);
#endif

static struct velaride_ride_snapshot_s state;
static struct velaride_ride_record_s saved;
static bool has_saved = true;
static bool ai_available;
static bool phone_valid;
bool velaride_ride_session_get_phone_data(double *speed, double *distance)
{
  *speed = phone_valid ? 12.5 : 0;
  *distance = phone_valid ? 1.25 : 0;
  return phone_valid;
}
static bool save_fails;
static int starts;
static int exits;
static uint16_t pixels[390 * 450];
static const char *output_dir;
static struct velaride_display_s screen_settings = {.manual = 50, .applied = 50};
static struct velaride_compass_s compass_state;
void velaride_compass_enable(bool enable) { compass_state.enabled = enable; }
void velaride_compass_get(struct velaride_compass_s *out) { *out = compass_state; }
void velaride_compass_calibrate(void) { compass_state.calibrating = true; }
int velaride_compass_finish(void) { compass_state.calibrating = false; return 0; }
void velaride_compass_cancel(void) { compass_state.calibrating = false; }

void velaride_display_get(struct velaride_display_s *out) { *out = screen_settings; }
int velaride_display_set(uint8_t manual, bool automatic)
{
  screen_settings.manual = screen_settings.applied = manual;
  screen_settings.automatic = automatic;
  return 0;
}

int velaride_device_init(void) { return 0; }

/* Lying face up, bottom edge raised 10 degrees (sensor +X = screen bottom). */

static int16_t accel_xyz[3] = {2845, 0, 16135};
static bool accel_fails;
int velaride_ride_session_read_accel(int16_t *x, int16_t *y, int16_t *z)
{
  if (accel_fails) return -5;
  *x = accel_xyz[0];
  *y = accel_xyz[1];
  *z = accel_xyz[2];
  return 0;
}

static struct velaride_ui_prefs_s ui_prefs;
static struct velaride_wake_prefs_s wake_prefs = {.raise = true,
                                                  .sleep = VELARIDE_SLEEP_20S};
void velaride_wake_prefs_get(struct velaride_wake_prefs_s *out)
{
  *out = wake_prefs;
}

int velaride_wake_prefs_set(bool raise, uint8_t sleep)
{
  wake_prefs.raise = raise;
  wake_prefs.sleep = sleep;
  return 0;
}
void velaride_ui_prefs_get(struct velaride_ui_prefs_s *out) { *out = ui_prefs; }
int velaride_ui_prefs_set(uint8_t accent, uint8_t menu, uint8_t transition)
{
  if (accent >= VELARIDE_ACCENT_COUNT) return -1;
  ui_prefs.accent = accent;
  ui_prefs.menu = menu;
  ui_prefs.transition = transition;
  return 0;
}

void velaride_device_get(struct velaride_device_snapshot_s *out)
{
  memset(out, 0, sizeof(*out));
  out->usb_valid = true;
  out->usb_connected = true;
  out->adc_valid = true;
  out->reference_mv = 4200;
  out->light_valid = true;
  out->light_lux = 123;
}

int velaride_ride_session_init(void)
{
  state.sensor_ready = true;
  return 0;
}

int velaride_ride_session_probe_sensor(void)
{
  return state.sensor_ready ? 0 : -1;
}

void velaride_ride_session_start_with_goal(uint32_t goal)
{
  memset(&state, 0, sizeof(state));
  state.active = true;
  state.sensor_ready = true;
  state.goal_s = goal;
  ai_available = false;
  starts++;
}

void velaride_ride_session_get(struct velaride_ride_snapshot_s *out)
{
  *out = state;
}

void velaride_ride_session_set_paused(bool paused)
{
  if (state.paused != paused && paused)
    {
      state.pause_count++;
    }
  state.paused = paused;
}

void velaride_ride_session_ack_alert(void)
{
  if (state.alert == VELARIDE_ALERT_IMPACT) return;
  state.alert = VELARIDE_ALERT_NONE;
}

int velaride_ride_session_respond_safety(enum velaride_safety_action_e action)
{
  assert(state.alert == VELARIDE_ALERT_IMPACT);
  if (action == VELARIDE_SAFETY_OK) state.safety.confirmed++;
  else if (action == VELARIDE_SAFETY_PAUSE) state.safety.pauses++;
  else if (action == VELARIDE_SAFETY_HELP) state.safety.help++;
  else return -EINVAL;
  state.safety.last_action = action;
  state.safety.last_elapsed_s = state.elapsed_s;
  state.safety_remaining_s = 0;
  state.alert = VELARIDE_ALERT_NONE;
  if (action != VELARIDE_SAFETY_OK) velaride_ride_session_set_paused(true);
  return 0;
}

void velaride_ride_session_sensor_tick(void)
{
}

void velaride_ride_session_second_tick(void)
{
  if (state.alert == VELARIDE_ALERT_IMPACT && state.safety_remaining_s > 0)
    {
      if (--state.safety_remaining_s == 0) state.safety.unanswered++;
    }
  if (state.active && !state.paused)
    {
      state.elapsed_s++;
      state.goal_reached = state.goal_s && state.elapsed_s >= state.goal_s;
    }
}

int velaride_ride_session_save_record(const struct velaride_ride_record_s *in)
{
  if (save_fails)
    {
      return -1;
    }
  saved = *in;
  has_saved = true;
  return 0;
}

int velaride_ride_session_finish(struct velaride_ride_record_s *out)
{
  state.active = false;
  *out = saved;
  out->sequence++;
  out->elapsed_s = state.elapsed_s;
  out->moving_s = state.moving_s;
  out->still_s = state.still_s;
  out->pause_count = state.pause_count;
  out->impact_count = state.impact_count;
  out->safety = state.safety;
  return velaride_ride_session_save_record(out);
}

int velaride_ride_session_load_latest(struct velaride_ride_record_s *out)
{
  *out = saved;
  return has_saved ? 0 : -1;
}

int velaride_ride_session_load_review(char *out, size_t size)
{
  if (!ai_available)
    {
      return -1;
    }
  snprintf(out, size, "建议保持平稳。\n建议保持平稳。\n建议保持平稳。\n"
           "建议保持平稳。\n建议保持平稳。\n建议保持平稳。\n"
           "建议保持平稳。\n建议保持平稳。\n建议保持平稳。\n建议保持平稳。");
  return 0;
}

bool velaride_ride_session_connected(void)
{
  return true;
}

static void exit_cb(void *arg)
{
  (void)arg;
  exits++;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  (void)area;
  (void)data;
  lv_display_flush_ready(display);
}

static lv_indev_t *input;
static lv_point_t pointer;
static bool pressing;

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  (void)indev;
  data->point = pointer;
  data->state = pressing ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Finger swipe across the lower half; dx < 0 swipes left. */

static void swipe(int dx)
{
  int i;

  pointer.x = dx < 0 ? 330 : 60;
  pointer.y = 380;
  pressing = true;
  lv_indev_read(input);
  for (i = 0; i < 6; i++)
    {
      pointer.x += dx / 6;
      lv_tick_inc(12);
      lv_indev_read(input);
    }
  pressing = false;
  lv_indev_read(input);
}

static void advance(unsigned ms)
{
  lv_tick_inc(ms);
  lv_timer_handler();
  lv_obj_update_layout(lv_screen_active());
}

/* Buttons may sit directly on the page or inside a scrolling list. */

static lv_obj_t *find_button_in(lv_obj_t *parent, const char *text)
{
  uint32_t i;
  for (i = 0; i < lv_obj_get_child_count(parent); i++)
    {
      lv_obj_t *obj = lv_obj_get_child(parent, i);
      lv_obj_t *found;
      if (lv_obj_check_type(obj, &lv_button_class) &&
          lv_obj_get_child_count(obj) > 0 &&
          lv_obj_check_type(lv_obj_get_child(obj, 0), &lv_label_class) &&
          strcmp(lv_label_get_text(lv_obj_get_child(obj, 0)), text) == 0)
        {
          return obj;
        }
      if (!lv_obj_check_type(obj, &lv_button_class) &&
          (found = find_button_in(obj, text)) != NULL)
        {
          return found;
        }
    }
  return NULL;
}

static lv_obj_t *find_button(const char *text)
{
  lv_obj_t *obj = find_button_in(g_sport.root, text);
  if (obj == NULL)
    {
      fprintf(stderr, "Missing button: %s\n", text);
      abort();
    }
  return obj;
}

static bool has_label(lv_obj_t *parent, const char *text)
{
  if (lv_obj_check_type(parent, &lv_label_class) &&
      strcmp(lv_label_get_text(parent), text) == 0) return true;
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); i++)
    if (has_label(lv_obj_get_child(parent, i), text)) return true;
  return false;
}

static void click(const char *text)
{
  lv_obj_send_event(find_button(text), LV_EVENT_CLICKED, NULL);
  lv_obj_update_layout(lv_screen_active());
}

static void screenshot(const char *name)
{
  char path[512];
  size_t i;
  FILE *file;
  lv_refr_now(NULL);
  snprintf(path, sizeof(path), "%s/%s.ppm", output_dir, name);
  file = fopen(path, "wb");
  assert(file != NULL);
  fprintf(file, "P6\n390 450\n255\n");
  for (i = 0; i < sizeof(pixels) / sizeof(pixels[0]); i++)
    {
      unsigned char rgb[] =
      {
        ((pixels[i] >> 11) & 31) * 255 / 31,
        ((pixels[i] >> 5) & 63) * 255 / 63,
        (pixels[i] & 31) * 255 / 31
      };
      assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
  fclose(file);
}

int main(int argc, char **argv)
{
  lv_display_t *display;
  lv_area_t card_area;
  lv_area_t button_area;
  uint32_t paused_elapsed;
  assert(argc == 2);
  output_dir = argv[1];
  saved.sequence = 7;
  saved.elapsed_s = 25;
  lv_init();
  display = lv_display_create(390, 450);
  lv_display_set_buffers(display, pixels, NULL, sizeof(pixels),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush);
  input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, pointer_read);
  velaride_sport_ui_init(lv_screen_active(), exit_cb, NULL);
  velaride_sport_ui_show_info(VELARIDE_INFO_DEVICE);
  click("连接与校时");
  assert(g_sport.info == VELARIDE_INFO_CONNECTION);
  assert(strstr(lv_label_get_text(g_sport.body), "GPS  -- km/h"));
  screenshot("00-gps-waiting");
  phone_valid = true;
  advance(1500);
  assert(strstr(lv_label_get_text(g_sport.body), "GPS  12.5 km/h"));
  assert(strstr(lv_label_get_text(g_sport.body), "1.25 km"));
  screenshot("00-gps-live");
  phone_valid = false;
  advance(1500);
  assert(!strstr(lv_label_get_text(g_sport.body), "12.5"));
  assert(strstr(lv_label_get_text(g_sport.body), "GPS  -- km/h"));
  velaride_sport_ui_back();
  velaride_sport_ui_back();
  saved.gps.distance_m = 1250;
  saved.elapsed_s = 300;
  saved.gps.time_ms = 250000;
  saved.gps.avg_speed_centi = 1800;
  saved.gps.max_speed_centi = 2250;
  velaride_sport_ui_show_latest();
  assert(has_label(lv_screen_active(), "GPS  1.25 km"));
  assert(has_label(lv_screen_active(), "平均 18.00 / 最高 22.50 km/h"));
  lv_obj_scroll_to_y(g_sport.scroll, 340, LV_ANIM_OFF);
  screenshot("00-ride-gps-summary");
  velaride_sport_ui_back();
  memset(&saved.gps, 0, sizeof(saved.gps));
  saved.elapsed_s = 25;
  exits = 0;
  velaride_sport_ui_enter();
  screenshot("01-select");
  click("户外骑行");
  assert(g_sport.page == SPORT_PAGE_GOAL && g_sport.goal_s == 30 * 60);
  screenshot("02-goal");
  click("+");
  assert(g_sport.goal_s == 35 * 60);
  click("15");
  assert(g_sport.goal_s == 15 * 60);
  click("开始");
  advance(1000);
  assert(!state.active && starts == 0);
  click("取消");
  assert(g_sport.page == SPORT_PAGE_GOAL);
  assert(starts == 0 && saved.sequence == 7 && saved.elapsed_s == 25);
  click("开始");
  screenshot("04-countdown");
  advance(3100);
  assert(starts == 1 && state.active && state.goal_s == 900);
  assert(g_sport.page == SPORT_PAGE_ACTIVE &&
         g_sport.ride_view == RIDE_VIEW_DATA);
  screenshot("05-active");

  /* Three views; the heading view asks for calibration first. */

  swipe(-200);
  assert(g_sport.ride_view == RIDE_VIEW_HEADING && compass_state.enabled);
  screenshot("05c-heading");
  swipe(-200);
  assert(g_sport.ride_view == RIDE_VIEW_HEADING);
  swipe(200);
  assert(g_sport.ride_view == RIDE_VIEW_DATA);
  swipe(200);
  assert(g_sport.ride_view == RIDE_VIEW_CONTROL && !compass_state.enabled);
  screenshot("05b-control");
  click(LV_SYMBOL_PAUSE);
  paused_elapsed = state.elapsed_s;
  advance(2000);
  assert(state.elapsed_s == paused_elapsed && state.paused);
  screenshot("06-paused");

  /* Hold to end: letting go early does nothing. */

  lv_obj_send_event(g_sport.hold_btn, LV_EVENT_PRESSED, NULL);
  advance(400);
  lv_obj_send_event(g_sport.hold_btn, LV_EVENT_RELEASED, NULL);
  assert(state.active && g_sport.page == SPORT_PAGE_ACTIVE);
  assert(lv_arc_get_value(g_sport.hold_arc) == 0);

  /* Lock blocks touches until the key is pressed. */

  lock_cb(NULL);
  assert(g_sport.locked && g_sport.ride_view == RIDE_VIEW_DATA);
  swipe(200);
  assert(g_sport.locked && g_sport.ride_view == RIDE_VIEW_DATA);
  screenshot("05d-locked");
  velaride_sport_ui_back();
  assert(!g_sport.locked && g_sport.page == SPORT_PAGE_ACTIVE);

  /* The key during a ride asks before ending. */

  velaride_sport_ui_back();
  assert(g_sport.page == SPORT_PAGE_CONFIRM && state.paused);
  click("继续骑行");
  assert(g_sport.page == SPORT_PAGE_ACTIVE && state.paused);
  ride_view_set(RIDE_VIEW_CONTROL);
  click(LV_SYMBOL_PLAY);
  assert(!state.paused);
  state.elapsed_s = 899;
  advance(1000);
  assert(g_sport.page == SPORT_PAGE_GOAL_DONE);
  screenshot("07-goal-done");
  click("继续骑行");
  advance(1000);
  assert(g_sport.page == SPORT_PAGE_ACTIVE);
  state.alert = VELARIDE_ALERT_IMPACT;
  state.safety.version = 1;
  state.safety.checks = 1;
  state.safety_remaining_s = 30;
  advance(1000);
  assert(g_sport.page == SPORT_PAGE_ALERT);
  velaride_sport_ui_back();
  assert(g_sport.page == SPORT_PAGE_ALERT && state.safety.confirmed == 0);
  screenshot("08-safety-waiting");
  for (const char **caption = (const char *[]){"我没事", "暂停休息", "需要帮助", NULL};
       *caption != NULL; caption++)
    {
      lv_area_t area;
      lv_obj_get_coords(find_button(*caption), &area);
      assert(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 390 && area.y2 < 450);
    }
  for (int i = 0; i < 29; i++) advance(1000);
  assert(has_label(g_sport.root, "尚未确认，不会自动求助"));
  screenshot("08-alert");
  click("我没事");
  assert(state.safety.confirmed == 1 && state.safety.unanswered == 1);
  state.alert = VELARIDE_ALERT_IMPACT;
  state.safety.checks++;
  state.safety_remaining_s = 30;
  advance(1000);
  click("需要帮助");
  assert(g_sport.page == SPORT_PAGE_HELP_CONFIRM && state.safety.help == 0);
  screenshot("08a-help-confirm");
  velaride_sport_ui_back();
  assert(g_sport.page == SPORT_PAGE_ALERT && state.safety.help == 0);
  click("需要帮助");
  click("确认并暂停");
  assert(g_sport.page == SPORT_PAGE_HELP_RESULT && state.safety.help == 1 && state.paused);
  assert(has_label(g_sport.root, "尚未发送求助消息\n请使用手机联系他人"));
  screenshot("08a-help-result");
  click("返回骑行");
  velaride_ride_session_set_paused(false);
  state.alert = VELARIDE_ALERT_REST;
  advance(1000);
  assert(g_sport.page == SPORT_PAGE_ALERT);
  screenshot("08b-rest");
  click("继续骑行");

  /* Holding the end control for the full time finishes the ride. */

  assert(g_sport.ride_view == RIDE_VIEW_DATA);
  swipe(200);
  assert(g_sport.ride_view == RIDE_VIEW_CONTROL);
  {
    lv_area_t end_area;

    lv_obj_update_layout(lv_screen_active());
    lv_obj_get_coords(g_sport.hold_btn, &end_area);
    pointer.x = (end_area.x1 + end_area.x2) / 2;
    pointer.y = (end_area.y1 + end_area.y2) / 2;
    pressing = true;
    lv_indev_read(input);
    advance(500);
    assert(state.active && lv_arc_get_value(g_sport.hold_arc) > 0);
    advance(500);
    pressing = false;
    lv_indev_read(input);
  }
  assert(!state.active && g_sport.page == SPORT_PAGE_REVIEW);
  assert(strstr(lv_label_get_text(g_sport.body), "尚未同步") != NULL);
  screenshot("09-record");
  lv_obj_scroll_by(g_sport.scroll, 0, -330, LV_ANIM_OFF);
  screenshot("09-safety-summary");
  ai_available = true;
  advance(1000);
  lv_obj_get_coords(g_sport.review_card, &card_area);
  lv_obj_get_coords(g_sport.review_button, &button_area);
  assert(card_area.y2 < button_area.y1);
  assert(lv_obj_get_scroll_bottom(g_sport.scroll) > 0);
  lv_obj_scroll_to_view(g_sport.review_button, LV_ANIM_OFF);
  screenshot("11-ai-long");
  click("完成");
  assert(!velaride_sport_ui_active() && exits == 1);
  has_saved = false;
  velaride_sport_ui_show_records();
  assert(strstr(lv_label_get_text(g_sport.body), "还没有") != NULL);
  screenshot("12-no-record");
  velaride_sport_ui_back();
  velaride_sport_ui_show_info(VELARIDE_INFO_DEVICE);
  screenshot("13-device");
  click("外观");
  assert(g_sport.info == VELARIDE_INFO_APPEARANCE);
  screenshot("20-appearance");
  lv_obj_send_event(g_sport.accent_dots[2], LV_EVENT_CLICKED, NULL);
  assert(ui_prefs.accent == 2 && g_sport.info == VELARIDE_INFO_APPEARANCE);
  screenshot("21-appearance-blue");
  velaride_sport_ui_back();
  assert(g_sport.info == VELARIDE_INFO_DEVICE);
  ui_prefs.accent = 0;
  velaride_sport_ui_show_info(VELARIDE_INFO_DEVICE);
  click("亮屏与熄屏");
  assert(g_sport.info == VELARIDE_INFO_WAKE);
  screenshot("22-wake");
  click("关闭");
  assert(!wake_prefs.raise && g_sport.info == VELARIDE_INFO_WAKE);
  click("10 秒");
  assert(wake_prefs.sleep == VELARIDE_SLEEP_10S && !wake_prefs.raise);
  click("开启");
  click("20 秒");
  assert(wake_prefs.raise && wake_prefs.sleep == VELARIDE_SLEEP_20S);
  assert(!velaride_sport_ui_keep_awake());
  velaride_sport_ui_back();
  assert(g_sport.info == VELARIDE_INFO_DEVICE);
  velaride_sport_ui_show_info(VELARIDE_INFO_DEVICE);
  click("屏幕设置");
  assert(g_sport.info == VELARIDE_INFO_DISPLAY);
  screenshot("15-display");
  click("柔和");
  assert(screen_settings.manual == 20 && !screen_settings.automatic);
  click("自动调光  已关闭");
  assert(screen_settings.automatic);
  click("明亮");
  assert(screen_settings.manual == 80 && !screen_settings.automatic);
  click("标准");
  velaride_sport_ui_back();
  assert(g_sport.info == VELARIDE_INFO_DEVICE);
  click("指南针");
  assert(g_sport.info == VELARIDE_INFO_COMPASS && compass_state.enabled);
  screenshot("16-compass-unavailable");
  compass_state.valid = true;
  compass_state.field_ut = 48;
  update_info();
  screenshot("17-compass-ready");
  click("校准");
  assert(compass_state.calibrating);
  screenshot("18-compass-calibrating");
  click("取消");
  assert(!compass_state.calibrating);
  compass_state.calibrated = compass_state.heading_valid = true;
  compass_state.heading = 45;
  update_info();
  screenshot("19-compass-preview");
  velaride_sport_ui_back();
  assert(!compass_state.enabled && g_sport.info == VELARIDE_INFO_DEVICE);

  /* Night light: taps cycle the modes; red flashing toggles every 250 ms. */

  velaride_sport_ui_show_info(VELARIDE_INFO_LIGHT);
  assert(g_sport.light_mode == 0);
  screenshot("22-light");
  lv_obj_send_event(g_sport.tape, LV_EVENT_CLICKED, NULL);
  assert(g_sport.light_mode == 1 && g_sport.light_on);
  advance(260);
  assert(!g_sport.light_on);
  advance(260);
  assert(g_sport.light_on);
  screenshot("22b-light-red");
  velaride_sport_ui_back();
  assert(!velaride_sport_ui_active());

  /* Stopwatch: start, lap, stop, reset. */

  velaride_sport_ui_show_info(VELARIDE_INFO_STOPWATCH);
  click("开始");
  assert(g_sport.sw_running);
  advance(1240);
  advance(10);
  assert(strcmp(lv_label_get_text(g_sport.time_label), "00:01") == 0);
  click("计圈");
  assert(g_sport.sw_lap_count == 1 && g_sport.sw_laps[0] >= 1240);
  advance(500);
  click("停止");
  assert(!g_sport.sw_running);
  {
    uint32_t stopped = sw_elapsed();
    advance(1000);
    assert(sw_elapsed() == stopped);
  }
  screenshot("23-stopwatch");
  click("复位");
  assert(sw_elapsed() == 0 && g_sport.sw_lap_count == 0);
  velaride_sport_ui_back();

#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  /* Independent timer survives closing/reopening its page and pausing. */
  velaride_countdown_cancel();
  velaride_sport_ui_show_info(VELARIDE_INFO_COUNTDOWN);
  screenshot("25-countdown-ready");
  click("1 分钟");
  click("+1");
  click("-1");
  click("开始");
  advance(1250);
  click("暂停");
  {
    struct velaride_countdown_s paused;
    struct velaride_countdown_s later;
    velaride_countdown_get(lv_tick_get(), &paused);
    assert(paused.state == VELARIDE_COUNTDOWN_PAUSED);
    assert(paused.remaining_ms <= 58750 && paused.remaining_ms > 58000);
    advance(2000);
    velaride_countdown_get(lv_tick_get(), &later);
    assert(later.remaining_ms == paused.remaining_ms);
    screenshot("25b-countdown-paused");
  }
  click("继续");
  velaride_sport_ui_back();
  advance(2000);
  velaride_sport_ui_show_info(VELARIDE_INFO_COUNTDOWN);
  assert(strcmp(lv_label_get_text(g_sport.status_label), "正在计时") == 0);
  click("取消");
  assert(velaride_countdown_set(2) == 0);
  click("开始");
  advance(2250);
  assert(strcmp(lv_label_get_text(g_sport.status_label), "时间到") == 0);
  screenshot("25c-countdown-done");
  assert(velaride_countdown_take_done(lv_tick_get()));
  assert(!velaride_countdown_take_done(lv_tick_get()));
  click("再次开始");
  click("取消");
  velaride_sport_ui_back();
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  velaride_sport_ui_show_info(VELARIDE_INFO_ALARM);
  assert(g_sport.alarm_hour == 7 && g_sport.alarm_minute == 0);
  click("时 +");
  click("分 -");
  assert(g_sport.alarm_hour == 8 && g_sport.alarm_minute == 59);
  click("分 +");
  assert(g_sport.alarm_minute == 0);
  screenshot("27-alarm-setting");
  click("保存并开启");
  {
    struct velaride_alarm_s alarm;
    struct timespec delay = {.tv_nsec = 1000000};
    for (int i = 0; i < 2000; i++)
      {
        velaride_alarm_get(&alarm);
        if (!alarm.saving) break;
        nanosleep(&delay, NULL);
      }
    assert(!alarm.saving && alarm.enabled && alarm.save_error == 0);
    assert(alarm.hour == 8 && alarm.minute == 0);
  }
  advance(300);
  advance(300); /* Finish the disabled-to-enabled style transition. */
  screenshot("27b-alarm-enabled");
  click("关闭闹钟");
  {
    struct velaride_alarm_s alarm;
    struct timespec delay = {.tv_nsec = 1000000};
    for (int i = 0; i < 2000; i++)
      {
        velaride_alarm_get(&alarm);
        if (!alarm.saving) break;
        nanosleep(&delay, NULL);
      }
    assert(!alarm.saving && !alarm.enabled);
  }
  velaride_sport_ui_back();
#endif

  /* Level: bottom edge raised 10 degrees moves the bubble down; held
   * upright (top up) it sits at the top edge; raising the right edge moves
   * it right. Sensor failure is reported.
   */

  velaride_sport_ui_show_info(VELARIDE_INFO_LEVEL);
  advance(100);
  assert(g_sport.level_valid);
  assert(g_sport.level_y > 9.5f && g_sport.level_y < 10.5f);
  assert(g_sport.level_x > -0.5f && g_sport.level_x < 0.5f);
  assert(lv_obj_get_y(g_sport.tape) > LEVEL_Y - LEVEL_BUBBLE / 2);
  assert(lv_obj_get_x(g_sport.tape) == LEVEL_X - LEVEL_BUBBLE / 2);
  assert(strstr(lv_label_get_text(g_sport.body), "Y 10.0") != NULL);
  screenshot("24-level");
  accel_xyz[0] = -16384;
  accel_xyz[2] = 0;
  for (int i = 0; i < 20; i++) advance(100);
  assert(lv_obj_get_y(g_sport.tape) == LEVEL_Y - LEVEL_R);
  assert(lv_obj_get_x(g_sport.tape) == LEVEL_X - LEVEL_BUBBLE / 2);
  screenshot("24c-level-upright");
  accel_xyz[0] = 0;
  accel_xyz[1] = -2845;  /* +Y points left, so a raised right edge reads -Y. */
  accel_xyz[2] = 16135;
  for (int i = 0; i < 20; i++) advance(100);
  assert(g_sport.level_x > 9.5f && fabsf(g_sport.level_y) < 1.0f);
  assert(lv_obj_get_x(g_sport.tape) > LEVEL_X - LEVEL_BUBBLE / 2);
  accel_xyz[1] = 0;
  for (int i = 0; i < 20; i++) advance(100);
  assert(fabsf(g_sport.level_x) < 1.0f);
  screenshot("24b-level-flat");
  accel_fails = true;
  advance(100);
  assert(strstr(lv_label_get_text(g_sport.body), "不可用") != NULL);
  accel_fails = false;
  velaride_sport_ui_back();
  assert(!velaride_sport_ui_active());
  velaride_sport_ui_show_info(VELARIDE_INFO_DEVICE);
  click("连接与校时");
  velaride_sport_ui_back();
  assert(g_sport.info == VELARIDE_INFO_DEVICE);
  velaride_sport_ui_back();
  assert(!velaride_sport_ui_active());
  velaride_sport_ui_enter();
  click("户外骑行");
  click("不限");
  assert(g_sport.goal_s == 0);
  click("开始");
  advance(3100);
  state.elapsed_s = 42;
  save_fails = true;
  velaride_sport_ui_back();
  click("结束并保存");
  assert(g_sport.persist_result < 0);
  screenshot("14-save-failed");
  velaride_sport_ui_back();
  velaride_sport_ui_show_records();
  assert(g_sport.latest_record.elapsed_s == 42 && g_sport.persist_result < 0);
  velaride_sport_ui_back();
  velaride_sport_ui_enter();
  assert(g_sport.page == SPORT_PAGE_REVIEW);
  save_fails = false;
  click("重试保存");
  assert(g_sport.persist_result == 0 && saved.elapsed_s == 42);
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  velaride_sport_ui_back();
  {
    struct velaride_display_s day = screen_settings;
    struct velaride_ui_prefs_s theme = ui_prefs;
    struct velaride_night_s night;
    velaride_night_poll(lv_tick_get(), 1, true, 4);
    velaride_sport_ui_show_info(VELARIDE_INFO_NIGHT);
    lv_color_t day_color = lv_obj_get_style_text_color_filtered(g_sport.status_label, LV_PART_MAIN);
    screenshot("29-night-off");
    click("开启");
    night_wait_saved();
    advance(300);
    lv_color_t red = lv_obj_get_style_text_color_filtered(g_sport.status_label, LV_PART_MAIN);
    assert(red.red > red.green * 2 && red.red > red.blue * 2);
    assert(lv_color_to_u32(velaride_night_color(red)) == lv_color_to_u32(red));
    assert(lv_color_to_u32(velaride_night_color(lv_color_black())) == lv_color_to_u32(lv_color_black()));
    assert(velaride_night_brightness(80) == 20);
    click("+5");
    lv_slider_set_value(g_sport.night_slider, 35, LV_ANIM_OFF);
    lv_obj_send_event(g_sport.night_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(velaride_night_brightness(80) == 35);
    night_wait_saved();
    advance(300);
    screenshot("29b-night-on");
    velaride_sport_ui_back();
    velaride_sport_ui_show_info(VELARIDE_INFO_LIGHT);
    velaride_night_get(&night);
    assert(night.active && night.exempt && velaride_night_brightness(80) == 80);
    assert(lv_color_to_u32(velaride_night_color(lv_color_white())) == lv_color_to_u32(lv_color_white()));
    screenshot("29c-night-light-exempt");
    velaride_sport_ui_back();
    assert(velaride_night_brightness(80) == 35);
    velaride_sport_ui_show_info(VELARIDE_INFO_NIGHT);
    click("自动");
    velaride_night_poll(lv_tick_get(), 2, true, 60);
    advance(3000);
    velaride_night_poll(lv_tick_get(), 3, true, 60);
    velaride_night_ui_apply(false);
    assert(velaride_night_brightness(80) == 80);
    click("关闭");
    night_wait_saved();
    advance(300);
    assert(lv_color_to_u32(lv_obj_get_style_text_color_filtered(g_sport.status_label, LV_PART_MAIN)) == lv_color_to_u32(day_color));
    assert(memcmp(&day, &screen_settings, sizeof(day)) == 0);
    assert(memcmp(&theme, &ui_prefs, sizeof(theme)) == 0);
    screenshot("29d-night-restored");
    velaride_sport_ui_back();
  }
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sport_ui_back();
  velaride_sport_ui_show_info(VELARIDE_INFO_SOUND);
  screenshot("28-sound-silent");
  click("有声");
  click("+5");
  click("-5");
  lv_slider_set_value(g_sport.sound_slider, 70, LV_ANIM_OFF);
  lv_obj_send_event(g_sport.sound_slider, LV_EVENT_VALUE_CHANGED, NULL);
  click("模式语音  已开启");
  {
    struct velaride_sound_s sound;
    velaride_sound_get(&sound);
    assert(sound.enabled && !sound.voice && sound.rings && sound.volume == 70);
  }
  {
    lv_area_t area;
    struct velaride_sound_s sound;
    lv_obj_update_layout(g_sport.sound_slider);
    lv_obj_get_coords(g_sport.sound_slider, &area);
    pointer.x = area.x1 + (area.x2 - area.x1) * 70 / 100;
    pointer.y = (area.y1 + area.y2) / 2;
    pressing = true;
    advance(30);
    for (int i = 1; i <= 6; i++)
      {
        pointer.x = area.x1 + (area.x2 - area.x1) * (70 - i * 8) / 100;
        advance(30);
      }
    pressing = false;
    advance(30);
    velaride_sound_get(&sound);
    assert(sound.volume >= 10 && sound.volume <= 35);
    assert(g_sport.info == VELARIDE_INFO_SOUND);
  }
  click("模式语音  已关闭");
  lv_slider_set_value(g_sport.sound_slider, 0, LV_ANIM_OFF);
  lv_obj_send_event(g_sport.sound_slider, LV_EVENT_VALUE_CHANGED, NULL);
  assert(lv_obj_has_state(g_sport.sound_buttons[6], LV_STATE_DISABLED));
  click("-5");
  lv_slider_set_value(g_sport.sound_slider, 100, LV_ANIM_OFF);
  lv_obj_send_event(g_sport.sound_slider, LV_EVENT_VALUE_CHANGED, NULL);
  click("+5");
  {
    struct velaride_sound_s sound;
    velaride_sound_get(&sound);
    assert(sound.volume == 100);
  }
  sound_test_reset();
  click("试听");
  assert(sound_test_count == 1 && sound_test_events[0] == VR_SOUND_RIDE_MODE);
  click("停止声音");
  advance(500);
  screenshot("28b-sound-enabled");
  click("静音");
  velaride_sport_ui_back();

  velaride_sport_ui_enter();
  click("户外骑行");
  click("不限");
  sound_test_reset();
  click("开始");
  advance(500);
  click("取消");
  advance(4000);
  assert(sound_test_voice_stops > 0 && sound_test_count == 1);
  assert(sound_test_events[0] == VR_SOUND_THREE);
  sound_test_reset();
  click("开始");
  advance(1000);
  advance(1000);
  advance(1100);
  assert(sound_test_count == 4);
  assert(sound_test_events[0] == VR_SOUND_THREE);
  assert(sound_test_events[1] == VR_SOUND_TWO);
  assert(sound_test_events[2] == VR_SOUND_ONE);
  assert(sound_test_events[3] == VR_SOUND_GO);
#endif
  puts("PASS: ride flow (goal presets, countdown cancel, real swipes between three views, lock, real hold to end), alerts, summary scroll, night light, stopwatch, level, empty state, save retry");
  lv_deinit();
  return 0;
}
