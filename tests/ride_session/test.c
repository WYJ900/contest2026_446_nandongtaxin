/* Exercise the actual app code with deterministic sensor/NVS adapters. */
#define open test_open
#define close test_close
#define ioctl test_ioctl
#define time test_time
#define clock_gettime test_clock_gettime
#include "../../app/velaride/velaride_ride_session.c"
#undef open
#undef close
#undef ioctl
#undef time
#undef clock_gettime

#include <assert.h>
#include <stdarg.h>

static struct lsm6dsl_sensor_data_s sample;
static bool sensor_fails;
static bool commit_fails;
static const char *write_fails_key;
static time_t wallclock = 1791541758;
static uint64_t monotonic_ms = 1000;
static bool clock_fails;

int test_clock_gettime(clockid_t id, struct timespec *out)
{
  assert(id == CLOCK_MONOTONIC);
  if (clock_fails)
    {
      return -1;
    }

  out->tv_sec = monotonic_ms / 1000;
  out->tv_nsec = (monotonic_ms % 1000) * 1000000;
  return 0;
}
static struct
{
  char key[64];
  unsigned char data[128];
  size_t size;
} store[64];

int test_open(const char *path, int flags, ...)
{
  (void)flags;
  assert(strcmp(path, "/dev/lsm6dsl0") == 0);
  return 4;
}

int test_close(int fd)
{
  assert(fd == 4);
  return 0;
}

int test_ioctl(int fd, unsigned long request, ...)
{
  va_list args;
  struct lsm6dsl_sensor_data_s *out;
  assert(fd == 4);
  if (request == SNIOC_START)
    {
      return 0;
    }
  assert(request == SNIOC_LSM6DSLSENSORREAD);
  if (sensor_fails)
    {
      errno = EIO;
      return -1;
    }
  va_start(args, request);
  out = (void *)va_arg(args, unsigned long);
  va_end(args);
  *out = sample;
  return 0;
}

time_t test_time(time_t *out)
{
  if (out != NULL)
    {
      *out = wallclock;
    }
  return wallclock;
}

ssize_t property_get_buffer(const char *key, void *data, size_t size)
{
  size_t i;
  for (i = 0; i < 64; i++)
    {
      if (strcmp(store[i].key, key) == 0)
        {
          assert(size >= store[i].size);
          memcpy(data, store[i].data, store[i].size);
          return store[i].size;
        }
    }
  return -EINVAL; /* Actual KVDB wrapper collapses missing-key errors. */
}

int property_get_with_err(const char *key, char *value)
{
  for (size_t i = 0; i < 64; i++)
    if (strcmp(store[i].key, key) == 0)
      {
        value[0] = 'x'; value[1] = '\0';
        return 1;
      }
  return -ENOENT;
}

int property_set_buffer(const char *key, const void *data, size_t size)
{
  size_t i;
  if (write_fails_key != NULL && strcmp(key, write_fails_key) == 0) return -EIO;
  assert(size <= sizeof(store[0].data));
  for (i = 0; i < 64; i++)
    {
      if (store[i].key[0] == '\0' || strcmp(store[i].key, key) == 0)
        {
          snprintf(store[i].key, sizeof(store[i].key), "%s", key);
          memcpy(store[i].data, data, size);
          store[i].size = size;
          return 0;
        }
    }
  return -ENOSPC;
}

int property_commit(void)
{
  return commit_fails ? -EIO : 0;
}

static void begin(uint32_t goal)
{
  unsigned i;
  memset(&g_ride, 0, sizeof(g_ride));
  g_ride.sensor_fd = -1;
  memset(store, 0, sizeof(store));
  memset(&sample, 0, sizeof(sample));
  sensor_fails = false;
  commit_fails = false;
  write_fails_key = NULL;
  g_review_view_pending = false;
  assert(velaride_ride_session_init() == 0);
  assert(velaride_ride_session_probe_sensor() == 0);
  velaride_ride_session_start_with_goal(goal);
  for (i = 0; i < 8; i++)
    {
      velaride_ride_session_sensor_tick();
    }
}

static void feed(unsigned seconds, bool moving)
{
  unsigned s;
  unsigned q;
  sample.g_x_data = moving ? 6000 : 0;
  for (s = 0; s < seconds; s++)
    {
      for (q = 0; q < 4; q++)
        {
          velaride_ride_session_sensor_tick();
        }
      wallclock++;
      velaride_ride_session_second_tick();
    }
}

static void impact(void)
{
  sample.x_data = 1000;
  velaride_ride_session_sensor_tick();
}

static void test_goal_and_pause(void)
{
  begin(3);
  feed(2, true);
  assert(!g_ride.state.goal_reached && g_ride.state.elapsed_s == 2);
  velaride_ride_session_set_paused(true);
  velaride_ride_session_set_paused(true);
  feed(5, true);
  assert(g_ride.state.elapsed_s == 2 && g_ride.state.pause_count == 1);
  assert(!g_ride.state.goal_reached);
  velaride_ride_session_set_paused(false);
  feed(1, true);
  assert(g_ride.state.goal_reached && g_ride.state.active);
  feed(4, true);
  assert(g_ride.state.elapsed_s == 7 && g_ride.state.active);
  begin(0);
  feed(4, true);
  assert(!g_ride.state.goal_reached);
}

static void test_consecutive_still(void)
{
  begin(0);
  feed(10, false);
  feed(2, true);
  feed(10, false);
  assert(g_ride.state.still_s == 20);
  assert(g_ride.state.alert == VELARIDE_ALERT_NONE);
  feed(5, false);
  assert(g_ride.state.alert == VELARIDE_ALERT_STILL);
}

static void test_recent_impact_and_priority(void)
{
  begin(0);
  impact();
  feed(3, false);
  assert(g_ride.state.alert == VELARIDE_ALERT_IMPACT);
  feed(20, false);
  assert(g_ride.state.alert == VELARIDE_ALERT_IMPACT);
  velaride_ride_session_ack_alert();
  assert(g_ride.state.alert == VELARIDE_ALERT_IMPACT);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_OK) == 0);
  assert(g_ride.state.alert == VELARIDE_ALERT_NONE);
}

static void test_expired_impact(void)
{
  begin(0);
  impact();
  feed(11, true);
  feed(3, false);
  assert(g_ride.state.impact_count == 1);
  assert(g_ride.state.alert == VELARIDE_ALERT_NONE);
}

static void test_sensor_loss(void)
{
  begin(0);
  feed(2, true);
  sensor_fails = true;
  feed(3, false);
  assert(!g_ride.state.sensor_ready);
  assert(g_ride.state.motion == VELARIDE_MOTION_UNKNOWN);
  assert(g_ride.state.elapsed_s == 5 && g_ride.state.moving_s == 2);
  assert(g_ride.state.still_s == 0);
  sensor_fails = false;
  feed(3, true);
  assert(g_ride.state.sensor_ready);
  assert(g_ride.state.motion == VELARIDE_MOTION_MOVING);
}

static void test_record_and_commit_failure(void)
{
  struct velaride_ride_record_s record;
  struct velaride_ride_record_s loaded;
  size_t i;
  begin(3);
  feed(4, true);
  assert(velaride_ride_session_finish(&record) == 0);
  assert(!g_ride.state.active);
  assert(velaride_ride_session_load_latest(&loaded) == 0);
  assert(record.sequence == loaded.sequence && loaded.elapsed_s == 4);
  commit_fails = true;
  assert(velaride_ride_session_save_record(&record) == -EIO);
  commit_fails = false;
  for (i = 0; i < 64; i++)
    {
      if (strcmp(store[i].key, RIDE_RECORD_KEY) == 0)
        {
          store[i].data[0] ^= 1;
        }
    }
  assert(velaride_ride_session_load_latest(&loaded) == 0); /* Slot recovers bad primary. */
  for (i = 0; i < 64; i++)
    if (strncmp(store[i].key, RIDE_HISTORY_PREFIX, strlen(RIDE_HISTORY_PREFIX)) == 0)
      store[i].data[0] ^= 1;
  assert(velaride_ride_session_load_latest(&loaded) < 0);
}

static void test_review_chunk_roundtrip(void)
{
  char input[261];
  char output[384];
  begin(0);
  memset(input, 'x', 260);
  input[260] = '\0';
  assert(velaride_ride_session_save_review(input) == 0);
  assert(velaride_ride_session_load_review(output, sizeof(output)) == 0);
  assert(strcmp(input, output) == 0);
  assert(velaride_ride_session_take_review_view_request());
  assert(!velaride_ride_session_take_review_view_request());
}

static void test_connection_and_probe(void)
{
  struct velaride_ride_record_s record;
  begin(0);
  assert(velaride_ride_session_finish(&record) == 0);
  sensor_fails = true;
  assert(velaride_ride_session_probe_sensor() == -EIO);
  assert(!g_ride.state.sensor_ready);
  sensor_fails = false;
  assert(velaride_ride_session_probe_sensor() == 0);
  velaride_ride_session_set_connected(true);
  velaride_ride_session_start();
  assert(velaride_ride_session_connected());
  velaride_ride_session_set_connected(false);
  assert(!velaride_ride_session_connected());
}

static void test_phone_data_lifecycle(void)
{
  double speed;
  double distance;

  velaride_ride_session_set_connected(true);
  velaride_ride_session_set_phone_data(22.5, 1.25);
  assert(velaride_ride_session_get_phone_data(&speed, &distance));
  assert(speed == 22.5 && distance == 1.25);
  velaride_ride_session_clear_phone_data();
  assert(!velaride_ride_session_get_phone_data(&speed, &distance));
  velaride_ride_session_set_phone_data(17.5, 3.0);
  velaride_ride_session_set_connected(false);
  velaride_ride_session_set_connected(true);
  assert(!velaride_ride_session_get_phone_data(NULL, NULL));
  velaride_ride_session_set_phone_data(INFINITY, NAN);
  assert(!velaride_ride_session_get_phone_data(&speed, &distance));
  assert(speed == 0 && distance == 0);
  velaride_ride_session_set_phone_data(20, 1);
  monotonic_ms += 14999;
  assert(velaride_ride_session_get_phone_data(NULL, NULL));
  monotonic_ms++;
  assert(!velaride_ride_session_get_phone_data(&speed, &distance));
  assert(speed == 0 && distance == 0);
  velaride_ride_session_set_phone_data(0, 1);
  assert(velaride_ride_session_get_phone_data(&speed, &distance));
  assert(speed == 0 && distance == 1);
  clock_fails = true;
  assert(!velaride_ride_session_get_phone_data(NULL, NULL));
  clock_fails = false;
  assert(!velaride_ride_session_get_phone_data(NULL, NULL));
}

static void *phone_writer(void *unused)
{
  int i;
  (void)unused;
  for (i = 0; i < 10000; i++)
    {
      velaride_ride_session_set_phone_data(i % 2 + 2, (i % 2 + 2) * 2);
    }
  return NULL;
}

static void test_phone_pair_consistency(void)
{
  pthread_t writer;
  double speed;
  double distance;
  int i;

  velaride_ride_session_set_phone_data(2, 4);
  assert(pthread_create(&writer, NULL, phone_writer, NULL) == 0);
  for (i = 0; i < 10000; i++)
    {
      assert(velaride_ride_session_get_phone_data(&speed, &distance));
      assert(distance == speed * 2);
    }
  assert(pthread_join(writer, NULL) == 0);
}

static void test_ride_gps_accounting(void)
{
  struct velaride_ride_gps_s gps;
  struct velaride_ride_record_s record;
  struct velaride_ride_record_s loaded;

  monotonic_ms = 1000;
  begin(0);
  velaride_ride_session_set_phone_data(18, 10.1);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 10.125);
  velaride_ride_session_get_gps(&gps);
  assert(gps.distance_m == 25 && gps.time_ms == 5000);
  assert(gps.avg_speed_centi == 1800 && gps.max_speed_centi == 1800);
  velaride_ride_session_set_paused(true);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(180, 15);
  velaride_ride_session_set_paused(false);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 15.1);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 15.125);
  velaride_ride_session_get_gps(&gps);
  assert(gps.distance_m == 50 && gps.time_ms == 10000);
  assert(gps.max_speed_centi == 1800);
  velaride_ride_session_set_connected(false);
  velaride_ride_session_set_connected(true);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 20);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 20.025);
  monotonic_ms += 16000;
  velaride_ride_session_set_phone_data(18, 25);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 25.025);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 0);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, .025);
  velaride_ride_session_get_gps(&gps);
  assert(gps.distance_m == 125 && gps.time_ms == 25000);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(18, 50); /* Reject a distance jump. */
  velaride_ride_session_get_gps(&gps);
  assert(gps.distance_m == 125 && gps.time_ms == 25000);
  feed(25, true);
  assert(velaride_ride_session_finish(&record) == 0);
  assert(record.gps.distance_m == 125 && record.gps.avg_speed_centi == 1800);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(90, 50.1);
  assert(velaride_ride_session_load_latest(&loaded) == 0);
  assert(memcmp(&loaded.gps, &record.gps, sizeof(record.gps)) == 0);
  velaride_ride_session_get_gps(&gps);
  assert(gps.distance_m == 125 && gps.max_speed_centi == 1800);
  velaride_ride_session_start();
  velaride_ride_session_get_gps(&gps);
  assert(gps.distance_m == 0 && gps.time_ms == 0);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(0, 50.1);
  monotonic_ms += 5000;
  velaride_ride_session_set_phone_data(0, 50.1);
  velaride_ride_session_get_gps(&gps);
  assert(gps.time_ms == 5000 && gps.distance_m == 0);
}

static void test_v1_record_migration(void)
{
  struct ride_record_disk_v1_s old;
  struct velaride_ride_record_s loaded;
  unsigned char before[sizeof(old)];
  begin(0);
  memset(&old, 0, sizeof(old));
  old.magic = RIDE_RECORD_MAGIC;
  old.version = 1;
  old.size = sizeof(old);
  old.record.sequence = 11;
  old.record.start_epoch = 1791606763;
  old.record.end_epoch = 1791606787;
  old.record.elapsed_s = 23;
  old.record.moving_s = 19;
  old.record.still_s = 1;
  old.record.pause_count = 1;
  old.record.impact_count = 2;
  old.crc32 = ride_crc32(&old, offsetof(struct ride_record_disk_v1_s, crc32));
  assert(property_set_buffer(RIDE_RECORD_KEY, &old, sizeof(old)) == 0);
  assert(velaride_ride_session_load_latest(&loaded) == 0);
  assert(loaded.sequence == 11 && loaded.elapsed_s == 23 && loaded.impact_count == 2);
  assert(loaded.gps.time_ms == 0 && loaded.gps.distance_m == 0);
  assert(property_get_buffer(RIDE_RECORD_KEY, before, sizeof(before)) == sizeof(old));
  assert(memcmp(before, &old, sizeof(old)) == 0);
  old.crc32 ^= 1;
  assert(property_set_buffer(RIDE_RECORD_KEY, &old, sizeof(old)) == 0);
  assert(velaride_ride_session_load_latest(&loaded) == -EBADMSG);
}

static void test_safety_choices_and_timeout(void)
{
  struct velaride_ride_record_s record;
  struct velaride_ride_record_s loaded;

  assert(sizeof(struct ride_record_disk_s) * 2 <= 255);
  begin(0);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_HELP) == -EALREADY);
  impact();
  feed(3, false);
  assert(g_ride.state.safety.checks == 1 && g_ride.state.safety_remaining_s == 30);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_UNANSWERED) == -EINVAL);
  feed(30, false);
  assert(g_ride.state.alert == VELARIDE_ALERT_IMPACT && g_ride.state.active);
  assert(!g_ride.state.paused && g_ride.state.safety.unanswered == 1);
  feed(30, false);
  assert(g_ride.state.safety.unanswered == 1 && g_ride.state.safety.help == 0);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_OK) == 0);
  assert(g_ride.state.safety.confirmed == 1 && !g_ride.state.paused);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_OK) == -EALREADY);

  sample.x_data = 0;
  impact(); /* Change the reading to create a fresh impact. */
  sample.x_data = 0;
  velaride_ride_session_sensor_tick();
  feed(3, false);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_PAUSE) == 0);
  assert(g_ride.state.paused && g_ride.state.safety.pauses == 1);
  velaride_ride_session_set_paused(false);
  feed(3, true);
  sample.x_data = 1000;
  velaride_ride_session_sensor_tick();
  feed(3, false);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_HELP) == 0);
  assert(g_ride.state.paused && g_ride.state.safety.help == 1);
  assert(g_ride.state.safety.last_action == VELARIDE_SAFETY_HELP);
  assert(velaride_ride_session_finish(&record) == 0);
  memset(&g_ride.state, 0, sizeof(g_ride.state));
  assert(velaride_ride_session_load_latest(&loaded) == 0);
  assert(loaded.safety.version == 1 && loaded.safety.checks == 3);
  assert(loaded.safety.confirmed == 1 && loaded.safety.pauses == 1);
  assert(loaded.safety.help == 1 && loaded.safety.unanswered == 1);
  assert(loaded.safety.last_elapsed_s <= loaded.elapsed_s);

  begin(0);
  impact(); feed(3, false);
  assert(velaride_ride_session_finish(&record) == 0);
  assert(record.safety.unanswered == 1);
  assert(record.safety.last_action == VELARIDE_SAFETY_UNANSWERED);
  velaride_ride_session_start();
  assert(g_ride.state.safety.checks == 0 && g_ride.state.safety_remaining_s == 0);

  /* No wrap or impossible sums after the 16-bit recording limit. */
  begin(0);
  g_ride.state.safety.checks = UINT16_MAX;
  g_ride.state.safety.confirmed = UINT16_MAX;
  impact(); feed(3, false);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_HELP) == 0);
  assert(g_ride.state.safety.checks == UINT16_MAX && g_ride.state.safety.help == 0);

  begin(0);
  impact(); feed(3, false);
  velaride_ride_session_set_paused(true);
  feed(30, false);
  assert(g_ride.state.safety.unanswered == 1 && g_ride.state.alert == VELARIDE_ALERT_IMPACT);
  assert(g_ride.state.elapsed_s == 3);
  assert(velaride_ride_session_respond_safety(VELARIDE_SAFETY_PAUSE) == 0);
  assert(g_ride.state.pause_count == 1);
  assert(velaride_ride_session_finish(&record) == 0);
  record.safety.version = 0;
  assert(velaride_ride_session_save_record(&record) == -EINVAL);
  record.safety.version = 1;
  record.safety.help = 2;
  assert(velaride_ride_session_save_record(&record) == -EINVAL);
}

static void test_v2_record_migration(void)
{
  struct ride_record_disk_v2_s old;
  struct velaride_ride_record_s loaded;
  unsigned char before[sizeof(old)];
  begin(0);
  memset(&old, 0, sizeof(old));
  old.magic = RIDE_RECORD_MAGIC; old.version = 2; old.size = sizeof(old);
  old.record.sequence = 12; old.record.elapsed_s = 60;
  old.record.gps.time_ms = 10000; old.record.gps.distance_m = 50;
  old.record.gps.avg_speed_centi = 1800; old.record.gps.max_speed_centi = 2000;
  old.crc32 = ride_crc32(&old, offsetof(struct ride_record_disk_v2_s, crc32));
  assert(property_set_buffer(RIDE_RECORD_KEY, &old, sizeof(old)) == 0);
  assert(velaride_ride_session_load_latest(&loaded) == 0);
  assert(loaded.sequence == 12 && loaded.gps.distance_m == 50);
  assert(loaded.safety.version == 0 && loaded.safety.checks == 0);
  assert(property_get_buffer(RIDE_RECORD_KEY, before, sizeof(before)) == sizeof(old));
  assert(memcmp(before, &old, sizeof(old)) == 0);
  old.crc32 ^= 1;
  assert(property_set_buffer(RIDE_RECORD_KEY, &old, sizeof(old)) == 0);
  assert(velaride_ride_session_load_latest(&loaded) == -EBADMSG);
}

static void test_history_ring_recovery(void)
{
  uint32_t sequences[VELARIDE_HISTORY_CAPACITY];
  struct velaride_ride_record_s record = {0};
  struct velaride_ride_record_s loaded;
  unsigned int corrupt;
  int count;
  begin(0);
  assert(velaride_ride_history_list(sequences, VELARIDE_HISTORY_CAPACITY, &corrupt) == 0);
  assert(corrupt == 0);
  record.elapsed_s = 30; record.moving_s = 20; record.safety.version = 1;
  for (unsigned int seq = 1; seq <= VELARIDE_HISTORY_CAPACITY + 3; seq++)
    {
      record.sequence = seq;
      assert(velaride_ride_session_save_record(&record) == 0);
    }
  count = velaride_ride_history_list(sequences, VELARIDE_HISTORY_CAPACITY, &corrupt);
  assert(count == VELARIDE_HISTORY_CAPACITY && corrupt == 0);
  for (int i = 0; i < count; i++)
    {
      assert(sequences[i] == (uint32_t)(VELARIDE_HISTORY_CAPACITY + 3 - i));
      assert(velaride_ride_history_get(sequences[i], &loaded) == 0);
      assert(loaded.sequence == sequences[i] && loaded.elapsed_s == 30);
    }
  assert(velaride_ride_history_get(1, &loaded) == -ENOENT);
  assert(velaride_ride_session_save_record(&record) == 0);
  assert(velaride_ride_history_list(sequences, VELARIDE_HISTORY_CAPACITY, NULL) == count);
  write_fails_key = RIDE_RECORD_KEY;
  record.sequence++;
  assert(velaride_ride_session_save_record(&record) == -EIO);
  write_fails_key = NULL;
  assert(velaride_ride_session_load_latest(&loaded) == 0);
  assert(loaded.sequence == record.sequence); /* Slot committed before primary update. */
  assert(velaride_ride_session_save_record(&record) == 0);
  memset(&g_ride, 0, sizeof(g_ride)); g_ride.sensor_fd = -1;
  assert(velaride_ride_session_init() == 0 && g_ride.sequence == record.sequence);
  for (size_t i = 0; i < 64; i++)
    {
      char key[40];
      ride_history_key(key, sizeof(key), record.sequence % VELARIDE_HISTORY_CAPACITY);
      if (strcmp(store[i].key, key) == 0 || strcmp(store[i].key, RIDE_RECORD_KEY) == 0)
        store[i].data[0] ^= 1;
    }
  assert(velaride_ride_history_list(sequences, VELARIDE_HISTORY_CAPACITY, &corrupt) == count - 1);
  assert(corrupt == 2);
  assert(velaride_ride_session_load_latest(&loaded) == 0 && loaded.sequence == record.sequence - 1);
  assert(velaride_ride_history_get(record.sequence, &loaded) == -ENOENT);
  assert(velaride_ride_history_list(sequences, 1, NULL) == -EINVAL);
}

static void configure_ride(struct velaride_ride_prefs_s prefs)
{
  begin(0);
  g_ride.state.active = false;
  assert(velaride_ride_prefs_set(&prefs) == 0);
  assert(velaride_ride_session_init() == 0);
  assert(g_ride_prefs.rest_min == prefs.rest_min && g_ride_prefs.drink_min == prefs.drink_min);
  velaride_ride_session_start();
  feed(2, true);
}

static void test_reminder_actions(void)
{
  struct velaride_ride_prefs_s prefs = {.rest_min=5,.drink_min=5};
  struct velaride_ride_record_s saved;
  configure_ride(prefs);
  assert(velaride_ride_prefs_set(&prefs) == -EBUSY);
  g_ride.next_drink_s = g_ride.state.elapsed_s + 600;
  feed(300, true);
  assert(g_ride.state.alert == VELARIDE_ALERT_REST);
  assert(velaride_ride_session_respond_reminder(VELARIDE_REMINDER_SNOOZE) == 0);
  assert(g_ride.state.extras.rest_snoozed == 1 && !g_ride.state.paused);
  g_ride.next_drink_s = g_ride.state.elapsed_s + 1;
  feed(1, true);
  assert(g_ride.state.alert == VELARIDE_ALERT_DRINK);
  assert(velaride_ride_session_respond_reminder(VELARIDE_REMINDER_TAKE) == 0);
  assert(g_ride.state.extras.drink_done == 1 && !g_ride.state.paused);
  g_ride.next_rest_s = g_ride.state.moving_s + 1;
  feed(1, true);
  velaride_ride_session_ack_alert();
  assert(g_ride.state.extras.rest_ignored == 1);
  g_ride.next_rest_s = g_ride.state.moving_s + 1;
  feed(1, true);
  assert(velaride_ride_session_respond_reminder(VELARIDE_REMINDER_TAKE) == 0);
  assert(g_ride.state.paused && g_ride.state.extras.rest_taken == 1);
  uint32_t elapsed = g_ride.state.elapsed_s;
  feed(400, false);
  assert(g_ride.state.elapsed_s == elapsed && g_ride.state.alert == VELARIDE_ALERT_NONE);
  assert(velaride_ride_session_finish(&saved) == 0);
  assert(saved.extras.version == 1 && saved.extras.rest_taken == 1 && saved.extras.drink_done == 1);
  assert(velaride_ride_session_load_latest(&saved) == 0 && saved.extras.rest_snoozed == 1);
  prefs.rest_min = 1;
  assert(velaride_ride_prefs_set(&prefs) == -EINVAL);
}

static void test_pause_advice_and_distance_goal(void)
{
  struct velaride_ride_prefs_s prefs = {.pause_advice=true,.distance_goal_m=1000,.lap_m=500};
  configure_ride(prefs);
  monotonic_ms = 1000;
  velaride_ride_session_set_phone_data(180,0);
  monotonic_ms += 10000;
  velaride_ride_session_set_phone_data(180,0.5);
  feed(1,true);
  assert(g_ride.state.extras.lap_count == 1 && !g_ride.state.distance_goal_reached);
  monotonic_ms += 10000;
  velaride_ride_session_set_phone_data(180,1.0);
  feed(1,true);
  assert(g_ride.state.distance_goal_reached && g_ride.state.extras.lap_count == 2);
  assert(velaride_ride_session_lap() == -EAGAIN);
  feed(1,true);
  assert(velaride_ride_session_lap() == 0 && g_ride.state.extras.lap_count == 3);
  velaride_ride_session_set_phone_data(0,1.0);
  feed(6,false);
  assert(g_ride.state.alert == VELARIDE_ALERT_STILL && !g_ride.state.paused);
  velaride_ride_session_ack_alert();
  velaride_ride_session_set_paused(true);
  uint32_t elapsed = g_ride.state.elapsed_s;
  velaride_ride_session_set_phone_data(10,1.0);
  feed(4,true);
  assert(g_ride.state.alert == VELARIDE_ALERT_RESUME && g_ride.state.paused);
  assert(g_ride.state.elapsed_s == elapsed);
  velaride_ride_session_ack_alert();
  feed(3,true);
  assert(g_ride.state.alert == VELARIDE_ALERT_NONE); /* Ignoring does not spam. */
  assert(velaride_ride_session_lap() == -EBUSY);
}

static void test_v3_record_migration(void)
{
  struct ride_record_disk_v3_s old = {0};
  struct velaride_ride_record_s record;
  begin(0);
  old.magic = RIDE_RECORD_MAGIC; old.version = 3; old.size = sizeof(old);
  old.record.sequence = 100; old.record.elapsed_s = 60;
  old.record.safety.version = 1; old.record.safety.checks = 1;
  old.record.safety.confirmed = 1; old.record.safety.last_action = VELARIDE_SAFETY_OK;
  old.crc32 = ride_crc32(&old, offsetof(struct ride_record_disk_v3_s, crc32));
  assert(property_set_buffer(RIDE_RECORD_KEY, &old, sizeof(old)) == 0);
  assert(velaride_ride_session_load_latest(&record) == 0 && record.safety.confirmed == 1);
  assert(record.extras.version == 0 && record.extras.lap_count == 0);
  assert(sizeof(struct ride_record_disk_s) * 2 <= 255);
}

static void test_navigation_expiry(void)
{
  struct velaride_nav_s nav;
  begin(0); monotonic_ms=1000; clock_fails=false;
  velaride_nav_clear();assert(!velaride_nav_get(&nav));
  assert(velaride_nav_set(1,80,false)==0);
  assert(velaride_nav_get(&nav) && nav.direction==1 && nav.distance_m==80);
  assert(velaride_nav_set(6,80,false)==-EINVAL);
  assert(velaride_nav_set(1,1000001,false)==-EINVAL);
  monotonic_ms+=14999;assert(velaride_nav_get(&nav));
  monotonic_ms++;assert(!velaride_nav_get(&nav));
  assert(velaride_nav_set(2,50,true)==0);
  velaride_ride_session_clear_phone_data();assert(!velaride_nav_get(&nav));
  assert(velaride_nav_set(0,30,false)==0);
  velaride_ride_session_set_connected(false);assert(!velaride_nav_get(&nav));
  assert(velaride_nav_set(0,30,false)==0);
  clock_fails=true;assert(!velaride_nav_get(&nav));
  assert(velaride_nav_set(0,30,false)==-EIO);clock_fails=false;
}

int main(void)
{
  test_goal_and_pause();
  test_consecutive_still();
  test_recent_impact_and_priority();
  test_expired_impact();
  test_sensor_loss();
  test_record_and_commit_failure();
  test_review_chunk_roundtrip();
  test_connection_and_probe();
  test_phone_data_lifecycle();
  test_phone_pair_consistency();
  test_ride_gps_accounting();
  test_v1_record_migration();
  test_safety_choices_and_timeout();
  test_v2_record_migration();
  test_history_ring_recovery();
  test_reminder_actions();
  test_pause_advice_and_distance_goal();
  test_v3_record_migration();
  test_navigation_expiry();
  puts("PASS: 19 ride-session scenarios: navigation bounds/expiry/clearing, reminders, stop/resume advice, distance/laps, V1-V4 persistence, history and GPS");
  return 0;
}
