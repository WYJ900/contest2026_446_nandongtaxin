/* Actual shell rendering and pointer routing, with device/service fixtures. */
#include "shell-ui.c"
#include <assert.h>

static uint16_t pixels[390 * 450];
static int opened = -1;
static lv_point_t pointer;
static bool pressing;
static bool page_active;
static int back_count;
static struct velaride_ui_prefs_s ui_prefs;

int velaride_device_init(void) { return 0; }

/* Screen wake fixtures: prefs, a settable gravity direction and a page
 * that may hold the screen on.
 */

static struct velaride_wake_prefs_s wake_prefs = {.raise = true,
                                                  .sleep = VELARIDE_SLEEP_20S};
static float accel_up = 0.0f;
static bool keep_awake;
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
static bool night_exempt;
bool velaride_sport_ui_night_exempt(void) { return night_exempt; }
#endif

void velaride_wake_prefs_get(struct velaride_wake_prefs_s *out)
{
  *out = wake_prefs;
}

int velaride_ride_session_read_accel(int16_t *x, int16_t *y, int16_t *z)
{
  /* up = -z / g with the board's axes; the rest of gravity along +X. */

  *x = (int16_t)(16384.0f * sqrtf(1.0f - accel_up * accel_up));
  *y = 0;
  *z = (int16_t)(-16384.0f * accel_up);
  return 0;
}

bool velaride_sport_ui_keep_awake(void) { return keep_awake; }

/* Advance the clock in poll steps, running the wake timer each time. */

static void wake_run(uint32_t ms)
{
  uint32_t t;

  for (t = 0; t < ms; t += WAKE_POLL_MS)
    {
      lv_tick_inc(WAKE_POLL_MS);
      wake_timer_cb(g_wake.timer);
    }
}
void velaride_ui_prefs_get(struct velaride_ui_prefs_s *out) { *out = ui_prefs; }
static bool linked = true;
static bool review_request;
static struct velaride_display_s display_state = {.manual = 50,
                                                  .applied = 50};

bool velaride_ride_session_connected(void) { return linked; }
bool velaride_ride_session_take_review_view_request(void)
{
  bool r = review_request;
  review_request = false;
  return r;
}

void velaride_display_get(struct velaride_display_s *out)
{
  *out = display_state;
}

int velaride_display_set(uint8_t manual, bool automatic)
{
  display_state.manual = display_state.applied = manual;
  display_state.automatic = automatic;
  return 0;
}

static float test_lux = 123;
static bool test_light_valid = true;
static uint32_t test_light_samples;
void velaride_device_get(struct velaride_device_snapshot_s *out)
{
  memset(out, 0, sizeof(*out));
  out->usb_valid = out->usb_connected = true;
  out->light_valid = test_light_valid;
  out->light_lux = test_lux;
  out->light_samples = ++test_light_samples;
}

void velaride_sport_ui_init(lv_obj_t *screen,
                          velaride_sport_exit_cb_t exit_cb, void *arg)
{
  (void)screen;
  (void)exit_cb;
  (void)arg;
}

/* opened: 0 ride, 1 records, 2 latest review, 100 + info for info pages. */

void velaride_sport_ui_enter(void) { opened = 0; }
void velaride_sport_ui_show_records(void) { opened = 1; }
void velaride_sport_ui_show_latest(void) { opened = 2; }
void velaride_sport_ui_back(void) { back_count++; }
bool velaride_sport_ui_active(void) { return page_active; }
/* The frozen device shell also includes a diagnostic scroller hook. */
lv_obj_t *velaride_sport_ui_bench_scroller(void) { return NULL; }

void velaride_sport_ui_show_info(enum velaride_info_e info)
{
  opened = 100 + (int)info;
}

void velaride_ride_session_get(struct velaride_ride_snapshot_s *out)
{
  memset(out, 0, sizeof(*out));
  out->sensor_ready = true;
}

int velaride_ride_session_load_latest(struct velaride_ride_record_s *out)
{
  memset(out, 0, sizeof(*out));
  out->sequence = 7;
  out->elapsed_s = 25;
  out->moving_s = 22;
  return 0;
}

int velaride_ride_session_load_review(char *out, size_t size)
{
  snprintf(out, size, "本次骑行仅25秒，实际移动22秒。距离未记录。");
  return 0;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  (void)area;
  (void)data;
  lv_display_flush_ready(display);
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  (void)indev;
  data->point = pointer;
  data->state = pressing ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void snapshot(const char *folder, const char *name)
{
  char path[512];
  size_t i;
  FILE *file;
  lv_obj_update_layout(lv_screen_active());
  lv_refr_now(NULL);
  snprintf(path, sizeof(path), "%s/%s.ppm", folder, name);
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
      assert(fwrite(rgb, 1, 3, file) == 3);
    }
  fclose(file);
}

static void run_spring(void)
{
  int guard = 0;

  while (g_nav.transition_active && guard++ < 400)
    {
      lv_tick_inc(10);
      spring_timer_cb(g_nav.spring_timer);
    }

  assert(!g_nav.transition_active);
}

int main(int argc, char **argv)
{
  lv_display_t *display;
  lv_indev_t *input;
  int i;
  char name[32];
  assert(argc == 2);
  lv_init();
  display = lv_display_create(390, 450);
  lv_display_set_buffers(display, pixels, NULL, sizeof(pixels),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush);
  input = lv_indev_create();
  lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(input, pointer_read);
  build_shell();
  lv_timer_pause(g_nav.preview_timer);
  for (i = 0; i < FACE_COUNT; i++)
    {
      g_nav.face_index = i;
      build_scene(g_nav.current, current_scene(), false);
      update_current_scene();
      snprintf(name, sizeof(name), "face-%d", i);
      snapshot(argv[1], name);
    }

  /* Every theme color renders on the modular face; the last one stays. */

  for (i = 0; i < VELARIDE_ACCENT_COUNT; i++)
    {
      ui_prefs.accent = i;
      g_nav.face_index = 0;
      build_scene(g_nav.current, current_scene(), false);
      update_current_scene();
      snprintf(name, sizeof(name), "accent-%d", i);
      snapshot(argv[1], name);
    }

  ui_prefs.accent = 0;
  g_nav.face_index = 0;
  build_scene(g_nav.current, current_scene(), false);

  /* Tapping the start pill on the modular face opens the ride flow. */

  opened = -1;
  pointer.x = FACE_PILL_X + FACE_PILL_W / 2;
  pointer.y = FACE_PILL_Y + FACE_PILL_H / 2;
  pressing = true;
  lv_indev_read(input);
  pressing = false;
  lv_indev_read(input);
  assert(opened == 0);
  {
    static const int expect[APP_COUNT] =
    {
      [APP_RIDE] = 0,
      [APP_RECORDS] = 1,
      [APP_REVIEW] = 2,
      [APP_COMPASS] = 100 + VELARIDE_INFO_COMPASS,
      [APP_SAFETY] = 100 + VELARIDE_INFO_SAFETY,
      [APP_CONNECT] = 100 + VELARIDE_INFO_CONNECTION,
      [APP_DISPLAY] = 100 + VELARIDE_INFO_DISPLAY,
      [APP_SETTINGS] = 100 + VELARIDE_INFO_DEVICE,
      [APP_HELP] = 100 + VELARIDE_INFO_HELP,
      [APP_ABOUT] = 100 + VELARIDE_INFO_HARDWARE,
      [APP_LIGHT] = 100 + VELARIDE_INFO_LIGHT,
      [APP_STOPWATCH] = 100 + VELARIDE_INFO_STOPWATCH,
      [APP_LEVEL] = 100 + VELARIDE_INFO_LEVEL,
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
      [APP_COUNTDOWN] = 100 + VELARIDE_INFO_COUNTDOWN,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
      [APP_ALARM] = 100 + VELARIDE_INFO_ALARM,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
      [APP_SOUND] = 100 + VELARIDE_INFO_SOUND,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
      [APP_NIGHT] = 100 + VELARIDE_INFO_NIGHT,
#endif
    };
    int step;

    /* Honeycomb: bring each icon to the centre, then tap it. */

    for (i = 0; i < APP_COUNT; i++)
      {
        opened = -1;
        assert(!g_menu.active);
        switch_shell_mode();
        assert(g_menu.active && !g_menu.list_mode);
        if (i == 0)
          {
            snapshot(argv[1], "menu-honey");
          }

        honey_move(-HONEY_STEP * (g_apps[i].q + g_apps[i].r / 2.0f),
                   -HONEY_STEP * 0.866f * g_apps[i].r);
        assert(g_menu.selected == i);
        pointer.x = g_menu.cells[i].x;
        pointer.y = g_menu.cells[i].y;
        pressing = true;
        lv_indev_read(input);
        pressing = false;
        lv_indev_read(input);
        assert(!g_menu.active && opened == expect[i]);
        assert(g_menu.return_to_menu == (i != APP_RIDE));

        /* Leaving the app returns to the launcher, except after a ride. */

        sport_exit_cb(NULL);
        assert(g_menu.active == (i != APP_RIDE));
        if (g_menu.active)
          {
            switch_shell_mode();
          }
      }

    /* Edge icons shrink: ring-two icons are smaller than the centre one. */

    switch_shell_mode();
    assert(g_menu.cells[APP_HELP].diameter < g_menu.cells[APP_RIDE].diameter);

    /* A flick keeps moving after release and springs back inside limits. */

    pointer.x = 195;
    pointer.y = 225;
    pressing = true;
    lv_indev_read(input);
    for (step = 0; step < 4; step++)
      {
        pointer.x -= 30;
        lv_tick_inc(10);
        lv_indev_read(input);
      }
    pressing = false;
    lv_indev_read(input);
    {
      float released = g_menu.offset_x;

      for (step = 0; step < 300 && (g_menu.velocity_x != 0 ||
           g_menu.offset_x < -HONEY_LIMIT_X - 1); step++)
        {
          lv_tick_inc(16);
          honey_inertia_timer_cb(NULL);
        }

      assert(g_menu.offset_x < released - 10);
      assert(g_menu.offset_x >= -HONEY_LIMIT_X - 1);
      assert(g_menu.velocity_x == 0);
    }
    snapshot(argv[1], "menu-honey-moved");
    switch_shell_mode();

    /* List: every row opens the same app. */

    ui_prefs.menu = VELARIDE_MENU_LIST;
    for (i = 0; i < APP_COUNT; i++)
      {
        opened = -1;
        switch_shell_mode();
        assert(g_menu.active && g_menu.list_mode);
        if (i == 0)
          {
            snapshot(argv[1], "menu-list");
          }

        lv_obj_send_event(g_menu.rows[i], LV_EVENT_CLICKED, NULL);
        assert(!g_menu.active && opened == expect[i]);
        g_menu.return_to_menu = false;
      }

    lv_obj_scroll_to_y(g_menu.list, 200, LV_ANIM_OFF);
    switch_shell_mode();
    lv_obj_scroll_to_y(g_menu.list, 200, LV_ANIM_OFF);
    list_fade();
    snapshot(argv[1], "menu-list-scrolled");
    assert(g_menu.row_mix[0] < 255);
    switch_shell_mode();
    ui_prefs.menu = VELARIDE_MENU_HONEY;
  }
  opened = -1;
  button_handle_state(1, 1000000);
  assert(g_menu.active && opened == -1);
  button_handle_state(0, 1050000);
  button_handle_state(1, 1150000); /* Old double-click shortcut. */
  assert(g_menu.active && opened == -1);
  button_handle_state(1, 2000000); /* Held key must not repeat. */
  assert(g_menu.active && opened == -1);
  button_handle_state(0, 2100000);
  button_handle_state(1, 2500000);
  assert(!g_menu.active && opened == -1);
  for (uint64_t t = 3000000; t < 6000000; t += 100000)
    {
      button_handle_state(0, t);
      button_handle_state(1, t + 10000);
      assert(opened == -1);
    }
  if (g_menu.active) switch_shell_mode();
  page_active = true;
  button_handle_state(0, 7000000);
  button_handle_state(1, 7100000);
  button_handle_state(0, 7150000);
  button_handle_state(1, 7200000);
  assert(back_count == 1 && opened == -1 && !g_menu.active);
  page_active = false;

  /* Transitions: build face previews as the device does, then drive the
   * spring with a test clock.
   */

  g_nav.preview_count = PREVIEW_COUNT;
  g_nav.preview_next = 0;
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      preview_timer_cb(g_nav.preview_timer);
    }
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      assert(g_nav.preview_ready[i]);
    }
  build_scene(g_nav.current, current_scene(), false);
  for (i = 0; i < VELARIDE_TRANSITION_COUNT; i++)
    {
      int before = g_nav.face_index;

      ui_prefs.transition = i;
      g_nav.dragging = true;
      g_nav.drag_x = -150;
      g_nav.drag_y = 0;
      begin_axis(AXIS_H, -1);
      assert(g_nav.mode == i && g_nav.reveal_preview >= 0);
      if (i == VELARIDE_TRANSITION_SLIDE)
        {
          /* The analog snapshot has no second hand; it is painted at
           * the second the swipe began (here 15 s, pointing right).
           */

          g_nav.reveal_second = 15;
        }

      snprintf(name, sizeof(name), "transition-%d", i);
      snapshot(argv[1], name);
      start_settle(true, 0);
      run_spring();
      assert(g_nav.face_index == (before + 1) % FACE_COUNT);
      assert(g_nav.reveal_value == 0 && g_nav.axis == AXIS_NONE);
    }

  /* A short, fast flick turns the page even below the distance threshold. */

  ui_prefs.transition = VELARIDE_TRANSITION_SLIDE;
  {
    int before = g_nav.face_index;
    int step;

    pointer.x = 300;
    pointer.y = 225;
    pressing = true;
    lv_indev_read(input);
    for (step = 0; step < 4; step++)
      {
        pointer.x -= 12;
        lv_tick_inc(8);
        lv_indev_read(input);
      }
    pressing = false;
    lv_indev_read(input);
    assert(g_nav.transition_active && g_nav.commit_transition);
    run_spring();
    assert(g_nav.face_index == (before + 1) % FACE_COUNT);
  }

  /* Pressing during the landing catches the page; releasing lands it. */

  {
    int before = g_nav.face_index;
    int step;

    g_nav.dragging = true;
    g_nav.drag_x = -200;
    begin_axis(AXIS_H, -1);
    start_settle(true, 0);
    for (step = 0; step < 3; step++)
      {
        lv_tick_inc(10);
        spring_timer_cb(g_nav.spring_timer);
      }
    assert(g_nav.transition_active);
    pointer.x = 195;
    pointer.y = 225;
    pressing = true;
    lv_indev_read(input);
    assert(!g_nav.transition_active && g_nav.axis == AXIS_H && g_nav.caught);
    lv_tick_inc(200);
    pressing = false;
    lv_indev_read(input);
    run_spring();
    assert(g_nav.face_index == (before + 1) % FACE_COUNT);
  }

  /* Dragging back past the start switches to the other neighbour face;
   * letting go there cancels and keeps the current face.
   */

  {
    int before = g_nav.face_index;

    g_nav.dragging = true;
    g_nav.drag_x = -50;
    begin_axis(AXIS_H, -1);
    g_nav.drag_x = 40;
    drag_update();
    assert(g_nav.target_direction == 1 && g_nav.pending_value == 40);
    start_settle(false, 0);
    run_spring();
    assert(g_nav.face_index == before);
  }

  /* Control centre: pull down from a face, toggle, then push back up. */

  g_nav.function_index = -1;
  g_nav.face_index = 0;
  build_scene(g_nav.current, current_scene(), false);
  g_nav.preview_count = PREVIEW_COUNT;
  g_nav.preview_next = 0;
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      preview_timer_cb(g_nav.preview_timer);
    }
  assert(g_nav.preview_ready[SCENE_CONTROL] && g_nav.preview_ready[SCENE_NOTES]);
  ui_prefs.transition = VELARIDE_TRANSITION_SLIDE;
  g_nav.dragging = true;
  g_nav.drag_x = 0;
  g_nav.drag_y = 200;
  begin_axis(AXIS_V, 1);
  assert(g_nav.reveal_scene == SCENE_CONTROL && g_nav.reveal_preview >= 0);
  snapshot(argv[1], "cc-pulling");
  start_settle(true, 0);
  run_spring();
  assert(g_nav.current->scene == SCENE_CONTROL && g_nav.function_index == 0);
  snapshot(argv[1], "cc");
  {
    lv_point_t p = {CC_X0 + CC_STEP_X + CC_SIZE / 2, CC_Y0 + CC_SIZE / 2};

    assert(cc_hit(p) == CC_DND);
    cc_tap(p);
    assert(g_notes.dnd && !g_nav.preview_ready[SCENE_CONTROL]);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
    {
      struct velaride_sound_s sound;
      velaride_sound_get(&sound);
      assert(sound.dnd);
    }
#endif
    cc_tap(p);
    assert(!g_notes.dnd);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
    {
      struct velaride_sound_s sound;
      velaride_sound_get(&sound);
      assert(!sound.dnd);
    }
#endif
    p.x = CC_X0 + 2 * CC_STEP_X + CC_SIZE / 2;
    cc_tap(p);
    assert(display_state.automatic);
    cc_tap((lv_point_t){CC_X0 + CC_STEP_X + CC_SIZE / 2,
                        CC_Y0 + CC_SIZE / 2});
    assert(g_notes.dnd);
    snapshot(argv[1], "cc-on");
    cc_tap((lv_point_t){CC_X0 + CC_STEP_X + CC_SIZE / 2,
                        CC_Y0 + CC_SIZE / 2});
    p.x = CC_X0 + CC_SIZE / 2;
    p.y = CC_Y0 + CC_STEP_Y + CC_SIZE / 2;
    cc_tap(p);
    assert(display_state.manual == 80 && !display_state.automatic);
    snapshot(argv[1], "cc-toggled");
    p.y = CC_Y0 + CC_SIZE / 2;
    opened = -1;
    cc_tap(p);
    assert(opened == 100 + VELARIDE_INFO_LIGHT);
    sport_exit_cb(NULL);
    if (g_menu.active) switch_shell_mode();
  }
  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      preview_schedule();
      preview_timer_cb(g_nav.preview_timer);
    }

  g_nav.dragging = true;
  g_nav.drag_y = -200;
  begin_axis(AXIS_V, -1);
  assert(g_nav.reveal_scene == SCENE_FACE_MODULAR);
  assert(g_nav.cover_preview == SCENE_CONTROL &&
         lv_obj_has_flag(g_nav.current->root, LV_OBJ_FLAG_HIDDEN));
  snapshot(argv[1], "cc-closing");
  start_settle(true, 0);
  run_spring();
  assert(g_nav.function_index == -1 && g_nav.current->scene == SCENE_FACE_MODULAR);

  /* Notifications: link loss and an arriving review each post once, show a
   * banner and appear newest first; do-not-disturb keeps the note only.
   */

  update_current_scene();
  linked = false;
  update_current_scene();
  assert(g_notes.count == 1 && g_notes.items[0].kind == NOTE_LINK_DOWN);
  assert(!lv_obj_has_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN));
  lv_tick_inc(500);
  lv_timer_handler();
  snapshot(argv[1], "banner");
  review_request = true;
  review_watch();
  assert(g_notes.count == 2 && g_notes.items[0].kind == NOTE_REVIEW);
  update_current_scene();
  assert(g_notes.count == 2);
  lv_tick_inc(BANNER_SHOW_MS + 100);
  lv_timer_handler();
  assert(lv_obj_has_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN));
  g_notes.dnd = true;
  linked = true;
  update_current_scene();
  assert(g_notes.count == 3 && g_notes.items[0].kind == NOTE_LINK_UP);
  assert(lv_obj_has_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN));
  g_notes.dnd = false;

  /* Push up into the notification centre; tap the review; clear all. */

  g_nav.dragging = true;
  g_nav.drag_y = -200;
  begin_axis(AXIS_V, -1);
  assert(g_nav.reveal_scene == SCENE_NOTES);
  start_settle(true, 0);
  run_spring();
  assert(g_nav.current->scene == SCENE_NOTES && g_nav.function_index == 1);
  snapshot(argv[1], "notes");
  {
    lv_point_t p = {195, NOTE_Y0 + NOTE_ROW_H + NOTE_ROW_H / 2};

    opened = -1;
    note_tap(p);
    assert(opened == 2);
    sport_exit_cb(NULL);
    p.y = NOTE_CLEAR_Y + 10;
    note_tap(p);
    assert(g_notes.count == 0);
    snapshot(argv[1], "notes-empty");
  }

  for (i = 0; i < PREVIEW_COUNT; i++)
    {
      preview_schedule();
      preview_timer_cb(g_nav.preview_timer);
    }

  g_nav.dragging = true;
  g_nav.drag_y = 200;
  begin_axis(AXIS_V, 1);
  assert(g_nav.cover_preview == SCENE_NOTES);
  snapshot(argv[1], "notes-closing");
  start_settle(false, 0);
  run_spring();
  assert(g_nav.function_index == 1 && g_nav.cover_preview < 0 &&
         !lv_obj_has_flag(g_nav.current->root, LV_OBJ_FLAG_HIDDEN));

  /* Leaving the centre with a real finger: swipe down from the middle. */

  {
    int step;

    pointer.x = 195;
    pointer.y = 150;
    pressing = true;
    lv_indev_read(input);
    for (step = 0; step < 6; step++)
      {
        pointer.y += 25;
        lv_tick_inc(10);
        lv_indev_read(input);
      }
    pressing = false;
    lv_indev_read(input);
    run_spring();
    assert(g_nav.function_index == -1);
  }

  /* The key closes either centre instead of opening the launcher. */

  for (i = 1; i >= -1; i -= 2)
    {
      g_nav.dragging = true;
      g_nav.drag_y = 200 * i;
      begin_axis(AXIS_V, i);
      start_settle(true, 0);
      run_spring();
      assert(g_nav.function_index == (i > 0 ? 0 : 1));
      switch_shell_mode();
      run_spring();
      assert(g_nav.function_index == -1 && !g_menu.active);
    }

  /* Screen wake. Idle for the chosen time turns the panel off. */

  lv_display_trigger_activity(NULL);
  g_screen_power_request = -1;
  wake_run(19000);
  assert(!g_wake.off && g_screen_power_request == -1);
  wake_run(1100);
  assert(g_wake.off && g_screen_power_request == 0);
  assert(!lv_obj_has_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN));

  /* The first touch only wakes: tapping the start pill opens nothing. */

  opened = -1;
  pointer.x = FACE_PILL_X + FACE_PILL_W / 2;
  pointer.y = FACE_PILL_Y + FACE_PILL_H / 2;
  pressing = true;
  lv_indev_read(input);
  assert(!g_wake.off && g_screen_power_request == 1);
  pressing = false;
  lv_indev_read(input);
  assert(opened == -1 && lv_obj_has_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN));
  pressing = true;
  lv_indev_read(input);
  pressing = false;
  lv_indev_read(input);
  assert(opened == 0);
  sport_exit_cb(NULL);
  if (g_menu.active) switch_shell_mode();

  /* The key also only wakes; it does not open the launcher. */

  screen_sleep();
  button_handle_state(0, 50000000);
  button_handle_state(1, 50000000);
  assert(!g_wake.off && !g_menu.active);
  assert(lv_obj_has_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN));
  button_handle_state(0, 50100000);

  /* Raise: arm down (screen on edge), then turned face up and held. */

  screen_sleep();
  accel_up = 0.0f;
  wake_run(500);
  accel_up = 0.9f;
  wake_run(100);
  assert(g_wake.off);
  wake_run(150);
  assert(!g_wake.off && g_wake.by_raise && g_screen_power_request == 1);
  assert(lv_obj_has_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN));

  /* Lowering the arm again turns it off without waiting for the timeout. */

  accel_up = 0.1f;
  wake_run(500);
  assert(!g_wake.off);
  wake_run(400);
  assert(g_wake.off && g_screen_power_request == 0);

  /* Face up on a table, timed out there: no turn, so it never wakes by
   * itself.
   */

  accel_up = 0.95f;
  screen_wake(false);
  lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
  wake_run(20100);
  assert(g_wake.off);
  wake_run(3000);
  assert(g_wake.off);

  /* A raise followed by a touch stays on until the normal timeout. */

  accel_up = 0.0f;
  wake_run(300);
  accel_up = 0.9f;
  wake_run(300);
  assert(!g_wake.off && g_wake.by_raise);
  lv_display_trigger_activity(NULL);
  wake_run(WAKE_POLL_MS);
  assert(!g_wake.by_raise);
  accel_up = 0.0f;
  wake_run(2000);
  assert(!g_wake.off);

  /* Raise switched off in settings: turning the wrist does nothing. */

  screen_sleep();
  wake_prefs.raise = false;
  accel_up = 0.0f;
  wake_run(300);
  accel_up = 0.9f;
  wake_run(500);
  assert(g_wake.off);
  wake_prefs.raise = true;

  /* A banner wakes the screen; with do-not-disturb it does not. */

  g_notes.dnd = true;
  note_post(NOTE_LINK_DOWN);
  assert(g_wake.off);
  g_notes.dnd = false;
  note_post(NOTE_LINK_UP);
  assert(!g_wake.off && !lv_obj_has_flag(g_notes.banner, LV_OBJ_FLAG_HIDDEN));

  /* During a ride (or the night light) the screen stays on; "always on"
   * never sleeps.
   */

  keep_awake = true;
  wake_run(30000);
  assert(!g_wake.off);
  keep_awake = false;
  wake_prefs.sleep = VELARIDE_SLEEP_NEVER;
  wake_run(60000);
  assert(!g_wake.off);
  wake_prefs.sleep = VELARIDE_SLEEP_10S;
  wake_run(10100);
  assert(g_wake.off);
  screen_wake(false);
  lv_obj_add_flag(g_wake.shield, LV_OBJ_FLAG_HIDDEN);
  wake_prefs.sleep = VELARIDE_SLEEP_20S;

#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  /* Countdown notifications wake a dark screen once, defer during a swipe,
   * and respect DND without losing the completion in the notification list.
   */
  velaride_countdown_cancel();
  assert(velaride_countdown_set(1) == 0);
  velaride_countdown_start(lv_tick_get());
  screen_sleep();
  g_nav.dragging = true;
  lv_tick_inc(1000);
  data_timer_cb(NULL);
  assert(g_wake.off);
  g_nav.dragging = false;
  data_timer_cb(NULL);
  assert(!g_wake.off && g_notes.items[0].kind == NOTE_COUNTDOWN);
  assert(!velaride_countdown_take_done(lv_tick_get()));
  lv_tick_inc(500);
  lv_timer_handler();
  snapshot(argv[1], "26-countdown-banner");
  velaride_countdown_start(lv_tick_get());
  g_notes.dnd = true;
  screen_sleep();
  lv_tick_inc(1000);
  data_timer_cb(NULL);
  assert(g_wake.off && g_notes.items[0].kind == NOTE_COUNTDOWN);
  g_notes.dnd = false;
  velaride_countdown_cancel();
#endif

#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  page_active = false;
  g_nav.transition_active = false;
  velaride_night_set(VR_NIGHT_ON, 20);
  for (int i = 0; i < PREVIEW_COUNT; i++) g_nav.preview_ready[i] = true;
  g_nav.dragging = true;
  night_watch_cb(NULL);
  assert(lv_color_to_u32(velaride_night_color(lv_color_white())) == lv_color_to_u32(lv_color_white()));
  g_nav.dragging = false;
  night_watch_cb(NULL);
  for (int i = 0; i < PREVIEW_COUNT; i++) assert(!g_nav.preview_ready[i]);
  snapshot(argv[1], "30-night-watch");
  menu_show();
  snapshot(argv[1], "30b-night-menu");
  menu_hide();
  night_exempt = true;
  night_watch_cb(NULL);
  assert(velaride_night_brightness(80) == 80);
  night_exempt = false;
  night_watch_cb(NULL);
  assert(velaride_night_brightness(80) == 20);
  velaride_night_set(VR_NIGHT_OFF, 20);
  night_watch_cb(NULL);
  velaride_night_set(VR_NIGHT_AUTO, 20);
  test_lux = 0;
  for (int i = 0; i < 4; i++) { lv_tick_inc(1000); night_watch_cb(NULL); }
  assert(velaride_night_brightness(80) == 20);
  test_lux = 60;
  for (int i = 0; i < 4; i++) { lv_tick_inc(1000); night_watch_cb(NULL); }
  assert(velaride_night_brightness(80) == 80);
  velaride_night_set(VR_NIGHT_OFF, 20);
  night_watch_cb(NULL);
  snapshot(argv[1], "30c-night-watch-restored");
#endif

  g_notes.count=0;g_notes.dnd=true;g_nav.dragging=false;g_nav.transition_active=false;page_active=false;
  lv_obj_add_flag(g_notes.banner,LV_OBJ_FLAG_HIDDEN);
  velaride_activity_set(true,20,2);
  velaride_activity_sample(1000,600,true,false,true,0,0,16384);
  for(unsigned int i=1;i<=1200;i++)velaride_activity_sample(1000+i*1000,600,true,false,true,0,0,16384);
  note_watch();
  assert(g_notes.count==1&&g_notes.items[0].kind==NOTE_ACTIVITY);
  assert(lv_obj_has_flag(g_notes.banner,LV_OBJ_FLAG_HIDDEN));
  velaride_activity_ack(false);
  g_notes.dnd=false;
  for(unsigned int i=1201;i<=2400;i++)velaride_activity_sample(1000+i*1000,600,true,false,true,0,0,16384);
  note_watch();assert(!lv_obj_has_flag(g_notes.banner,LV_OBJ_FLAG_HIDDEN));
  lv_tick_inc(500);lv_timer_handler();
  snapshot(argv[1],"31-activity-banner");
  lv_obj_send_event(g_notes.banner,LV_EVENT_CLICKED,NULL);
  assert(opened==100+VELARIDE_INFO_ACTIVITY);
  velaride_activity_ack(false);velaride_activity_set(true,50,0);
  puts("PASS: screen wake, sedentary notification/DND/banner routing and original controls");
  puts("PASS: control centre (pull, toggles, back), notifications (banner, dnd, centre, tap, clear, real swipe out), key closes centres; honeycomb and list launchers (all enabled apps, inertia, return), three transitions, fling, catch, reverse; three native faces, four accents, start pill, seven touch routes, key menu/back, rapid/held presses never enter ride");
  lv_deinit();
  return 0;
}
