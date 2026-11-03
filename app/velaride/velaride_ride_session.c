/****************************************************************************
 * apps/examples/velaride/velaride_ride_session.c
 *
 * Ride state, LSM6DSL motion classification and NVS-backed latest record.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <kvdb.h>
#include <nuttx/sensors/lsm6dsl.h>

#include "velaride_ride_session.h"

#define RIDE_RECORD_MAGIC 0x564c5244u
#define RIDE_RECORD_VERSION 3
#define RIDE_RECORD_KEY "persist.velaride.latest"
#define RIDE_REVIEW_MAGIC 0x56525256u
#define RIDE_REVIEW_VERSION 1
#define RIDE_REVIEW_KEY_PREFIX "persist.velaride.review."
#define RIDE_REVIEW_CHUNK_DATA 52
#define RIDE_REVIEW_CHUNK_COUNT 5
#define RIDE_REVIEW_MAX (RIDE_REVIEW_CHUNK_DATA * RIDE_REVIEW_CHUNK_COUNT)

#define SENSOR_WARMUP_SAMPLES 8
#define SENSOR_TICKS_PER_SECOND 4
#define MOVING_ENTER_SCORE 2
#define STILL_ENTER_SCORE 4
#define ACCEL_MOVE_DELTA 45
#define GYRO_MOVE_SUM 5000
#define ACCEL_IMPACT_DELTA 650
#define IMPACT_STILL_SECONDS 3
#define IMPACT_WINDOW_SECONDS 10
#define STILL_ALERT_SECONDS 15
#define REST_ALERT_SECONDS (45 * 60)
#define SAFETY_CONFIRM_SECONDS 30

struct ride_session_s
{
  int sensor_fd;
  struct velaride_ride_snapshot_s state;
  int16_t last_x;
  int16_t last_y;
  int16_t last_z;
  uint8_t warmup_samples;
  uint8_t moving_score;
  uint8_t still_score;
  uint8_t sensor_ticks;
  uint8_t post_impact_still_s;
  uint8_t impact_age_s;
  uint32_t consecutive_still_s;
  bool impact_pending;
  bool rest_alerted;
  bool still_alerted;
  bool safety_timed_out;
  bool safety_counted;
  int64_t start_epoch;
  uint32_t sequence;
};

static struct ride_session_s g_ride =
{
  .sensor_fd = -1,
};

static bool g_review_view_pending;
static volatile bool g_gateway_connected;

static struct
{
  double speed_kmh;
  double distance_km;
  uint64_t updated_ms;
  bool valid;
} g_phone_data;

static pthread_mutex_t g_phone_lock = PTHREAD_MUTEX_INITIALIZER;

/* BLE only touches this accumulator while holding g_phone_lock. Lifecycle
 * changes invalidate the anchor so pre-start, paused and missing segments
 * cannot be attached to a later ride. */

static struct
{
  bool running;
  bool anchor;
  uint64_t last_ms;
  double last_km;
  double distance_km;
  double max_speed;
  uint32_t time_ms;
} g_ride_gps;

/* Exact original record layout, including ABI padding, for read-only V1
 * migration. Do not reinterpret an enlarged V2 record as V1. */

struct ride_record_v1_s
{
  uint32_t sequence;
  int64_t start_epoch;
  int64_t end_epoch;
  uint32_t elapsed_s;
  uint32_t moving_s;
  uint32_t still_s;
  uint16_t pause_count;
  uint16_t impact_count;
  uint16_t rest_count;
};

struct ride_record_disk_v1_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  struct ride_record_v1_s record;
  uint32_t crc32;
};

/* Frozen V2 layout: never use the enlarged live record for old CRC offsets. */

struct ride_record_v2_s
{
  uint32_t sequence;
  int64_t start_epoch;
  int64_t end_epoch;
  uint32_t elapsed_s;
  uint32_t moving_s;
  uint32_t still_s;
  uint16_t pause_count;
  uint16_t impact_count;
  uint16_t rest_count;
  struct velaride_ride_gps_s gps;
};

struct ride_record_disk_v2_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  struct ride_record_v2_s record;
  uint32_t crc32;
};

static void ride_gps_snapshot(struct velaride_ride_gps_s *gps)
{
  memset(gps, 0, sizeof(*gps));
  if (g_ride_gps.time_ms > 0)
    {
      gps->distance_m = (uint32_t)(g_ride_gps.distance_km * 1000.0 + 0.5);
      gps->time_ms = g_ride_gps.time_ms;
      gps->avg_speed_centi = (uint32_t)(g_ride_gps.distance_km *
                                      360000000.0 / gps->time_ms + 0.5);
      gps->max_speed_centi = (uint32_t)(g_ride_gps.max_speed * 100.0 + 0.5);
    }
}

void velaride_ride_session_get_gps(struct velaride_ride_gps_s *gps)
{
  pthread_mutex_lock(&g_phone_lock);
  ride_gps_snapshot(gps);
  pthread_mutex_unlock(&g_phone_lock);
}

struct ride_record_disk_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  struct velaride_ride_record_s record;
  uint32_t crc32;
};

struct ride_review_chunk_s
{
  uint32_t magic;
  uint8_t version;
  uint8_t index;
  uint8_t count;
  uint8_t data_len;
  char data[RIDE_REVIEW_CHUNK_DATA];
  uint32_t crc32;
};

static uint32_t ride_crc32(const void *data, size_t size)
{
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  size_t i;
  int bit;

  for (i = 0; i < size; i++)
    {
      crc ^= bytes[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

  return ~crc;
}

static bool ride_safety_valid(const struct velaride_ride_safety_s *safety,
                              uint32_t elapsed_s)
{
  if (safety->version > 1 ||
      safety->last_action > VELARIDE_SAFETY_UNANSWERED ||
      (uint32_t)safety->confirmed + safety->pauses + safety->help >
        safety->checks || safety->unanswered > safety->checks ||
      safety->last_elapsed_s > elapsed_s)
    {
      return false;
    }

  if ((safety->version == 0 || safety->checks == 0) &&
      (safety->checks != 0 || safety->confirmed != 0 || safety->pauses != 0 ||
       safety->help != 0 || safety->unanswered != 0 ||
       safety->last_action != 0 || safety->last_elapsed_s != 0))
    {
      return false;
    }

  return true;
}

static int ride_open_sensor(void)
{
  int ret;

  if (g_ride.sensor_fd >= 0)
    {
      return 0;
    }

  g_ride.sensor_fd = open("/dev/lsm6dsl0", O_RDONLY | O_CLOEXEC);
  if (g_ride.sensor_fd < 0)
    {
      printf("VELARIDE_RIDE: sensor open failed errno=%d\n", errno);
      return -errno;
    }

  ret = ioctl(g_ride.sensor_fd, SNIOC_START, 0);
  if (ret < 0)
    {
      printf("VELARIDE_RIDE: sensor start failed errno=%d\n", errno);
      close(g_ride.sensor_fd);
      g_ride.sensor_fd = -1;
      return -errno;
    }

  return 0;
}

int velaride_ride_session_init(void)
{
  struct velaride_ride_record_s latest;
  int ret;

  ret = ride_open_sensor();
  g_ride.state.sensor_ready = ret == 0;
  if (velaride_ride_session_load_latest(&latest) == 0)
    {
      g_ride.sequence = latest.sequence;
      printf("VELARIDE_RIDE: latest seq=%" PRIu32 " duration=%" PRIu32
             " moving=%" PRIu32 " impacts=%u\n",
             latest.sequence, latest.elapsed_s, latest.moving_s,
             latest.impact_count);
    }

  return ret;
}

void velaride_ride_session_start(void)
{
  velaride_ride_session_start_with_goal(0);
}

int velaride_ride_session_probe_sensor(void)
{
  struct lsm6dsl_sensor_data_s data;
  int ret;

  if (g_ride.state.active)
    {
      return g_ride.state.sensor_ready ? 0 : -EIO;
    }

  ret = ride_open_sensor();
  if (ret == 0 && ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
                        (unsigned long)&data) < 0)
    {
      ret = -errno;
    }

  g_ride.state.sensor_ready = ret == 0;
  return ret;
}

int velaride_ride_session_read_accel(int16_t *x, int16_t *y, int16_t *z)
{
  struct lsm6dsl_sensor_data_s data;
  int ret = ride_open_sensor();

  if (ret < 0)
    {
      return ret;
    }

  if (ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
            (unsigned long)&data) < 0)
    {
      return -errno;
    }

  *x = data.x_data;
  *y = data.y_data;
  *z = data.z_data;
  return 0;
}

void velaride_ride_session_set_connected(bool connected)
{
  g_gateway_connected = connected;
  if (!connected)
    {
      velaride_ride_session_clear_phone_data();
    }
}

bool velaride_ride_session_connected(void)
{
  return g_gateway_connected;
}

void velaride_ride_session_start_with_goal(uint32_t goal_s)
{
  bool sensor_ready = g_ride.state.sensor_ready;

  pthread_mutex_lock(&g_phone_lock);
  memset(&g_ride_gps, 0, sizeof(g_ride_gps));
  g_ride_gps.running = true;
  pthread_mutex_unlock(&g_phone_lock);

  memset(&g_ride.state, 0, sizeof(g_ride.state));
  g_ride.state.active = true;
  g_ride.state.sensor_ready = sensor_ready;
  g_ride.state.motion = VELARIDE_MOTION_UNKNOWN;
  g_ride.state.goal_s = goal_s <= 24 * 60 * 60 ? goal_s : 0;
  g_ride.state.safety.version = 1;
  g_ride.safety_timed_out = false;
  g_ride.safety_counted = false;
  g_ride.warmup_samples = 0;
  g_ride.moving_score = 0;
  g_ride.still_score = 0;
  g_ride.sensor_ticks = 0;
  g_ride.post_impact_still_s = 0;
  g_ride.impact_age_s = 0;
  g_ride.consecutive_still_s = 0;
  g_ride.impact_pending = false;
  g_ride.rest_alerted = false;
  g_ride.still_alerted = false;
  g_ride.start_epoch = (int64_t)time(NULL);
  velaride_ride_session_clear_review();
  g_ride.state.sensor_ready = ride_open_sensor() == 0;
  printf("VELARIDE_RIDE: start epoch=%" PRId64 "\n", g_ride.start_epoch);
}

void velaride_ride_session_set_paused(bool paused)
{
  if (!g_ride.state.active || g_ride.state.paused == paused)
    {
      return;
    }

  g_ride.state.paused = paused;
  pthread_mutex_lock(&g_phone_lock);
  g_ride_gps.running = !paused;
  g_ride_gps.anchor = false;
  pthread_mutex_unlock(&g_phone_lock);
  g_ride.consecutive_still_s = 0;
  g_ride.post_impact_still_s = 0;
  g_ride.impact_pending = false;
  if (!paused)
    {
      g_ride.warmup_samples = 0;
      g_ride.moving_score = 0;
      g_ride.still_score = 0;
      g_ride.still_alerted = false;
      g_ride.state.motion = VELARIDE_MOTION_UNKNOWN;
    }
  if (paused)
    {
      g_ride.state.pause_count++;
    }

  printf("VELARIDE_RIDE: %s pauses=%u\n",
         paused ? "pause" : "resume", g_ride.state.pause_count);
}

void velaride_ride_session_sensor_tick(void)
{
  struct lsm6dsl_sensor_data_s data;
  int accel_delta;
  int gyro_sum;
  bool moving_sample;
  bool impact_sample;
  int ret;

  if (!g_ride.state.active || g_ride.state.paused || g_ride.sensor_fd < 0)
    {
      return;
    }

  ret = ioctl(g_ride.sensor_fd, SNIOC_LSM6DSLSENSORREAD,
              (unsigned long)&data);
  if (ret < 0)
    {
      g_ride.state.sensor_ready = false;
      g_ride.state.motion = VELARIDE_MOTION_UNKNOWN;
      g_ride.warmup_samples = 0;
      g_ride.moving_score = 0;
      g_ride.still_score = 0;
      return;
    }

  g_ride.state.sensor_ready = true;

  if (g_ride.warmup_samples < SENSOR_WARMUP_SAMPLES)
    {
      g_ride.last_x = data.x_data;
      g_ride.last_y = data.y_data;
      g_ride.last_z = data.z_data;
      g_ride.warmup_samples++;
      if (g_ride.warmup_samples == SENSOR_WARMUP_SAMPLES)
        {
          g_ride.state.motion = VELARIDE_MOTION_STILL;
        }
      return;
    }

  accel_delta = abs(data.x_data - g_ride.last_x) +
                abs(data.y_data - g_ride.last_y) +
                abs(data.z_data - g_ride.last_z);
  gyro_sum = abs(data.g_x_data) + abs(data.g_y_data) + abs(data.g_z_data);
  impact_sample = accel_delta >= ACCEL_IMPACT_DELTA;
  moving_sample = accel_delta >= ACCEL_MOVE_DELTA || gyro_sum >= GYRO_MOVE_SUM;

  g_ride.last_x = data.x_data;
  g_ride.last_y = data.y_data;
  g_ride.last_z = data.z_data;

  if (impact_sample && !g_ride.impact_pending)
    {
      g_ride.impact_pending = true;
      g_ride.impact_age_s = 0;
      g_ride.post_impact_still_s = 0;
      g_ride.state.impact_count++;
      printf("VELARIDE_RIDE: impact delta=%d gyro=%d count=%u\n",
             accel_delta, gyro_sum, g_ride.state.impact_count);
    }

  if (moving_sample)
    {
      g_ride.moving_score = g_ride.moving_score < MOVING_ENTER_SCORE ?
                            g_ride.moving_score + 1 : g_ride.moving_score;
      g_ride.still_score = 0;
      if (g_ride.moving_score >= MOVING_ENTER_SCORE)
        {
          g_ride.state.motion = VELARIDE_MOTION_MOVING;
          g_ride.still_alerted = false;
        }
    }
  else
    {
      g_ride.still_score = g_ride.still_score < STILL_ENTER_SCORE ?
                           g_ride.still_score + 1 : g_ride.still_score;
      g_ride.moving_score = 0;
      if (g_ride.still_score >= STILL_ENTER_SCORE)
        {
          g_ride.state.motion = VELARIDE_MOTION_STILL;
        }
    }
}

void velaride_ride_session_second_tick(void)
{
  if (g_ride.state.active && g_ride.state.alert == VELARIDE_ALERT_IMPACT &&
      g_ride.state.safety_remaining_s > 0 &&
      --g_ride.state.safety_remaining_s == 0)
    {
      g_ride.safety_timed_out = true;
      if (g_ride.safety_counted)
        {
          g_ride.state.safety.unanswered++;
          g_ride.state.safety.last_action = VELARIDE_SAFETY_UNANSWERED;
          g_ride.state.safety.last_elapsed_s = g_ride.state.elapsed_s;
        }
    }

  if (!g_ride.state.active || g_ride.state.paused)
    {
      return;
    }

  g_ride.state.elapsed_s++;
  if (g_ride.state.goal_s > 0 &&
      g_ride.state.elapsed_s >= g_ride.state.goal_s)
    {
      g_ride.state.goal_reached = true;
    }

  if (g_ride.impact_pending &&
      ++g_ride.impact_age_s > IMPACT_WINDOW_SECONDS)
    {
      g_ride.impact_pending = false;
    }

  if (g_ride.state.motion == VELARIDE_MOTION_MOVING)
    {
      g_ride.state.moving_s++;
      g_ride.post_impact_still_s = 0;
      g_ride.consecutive_still_s = 0;
    }
  else if (g_ride.state.motion == VELARIDE_MOTION_STILL)
    {
      g_ride.state.still_s++;
      g_ride.consecutive_still_s++;
      if (g_ride.impact_pending && g_ride.post_impact_still_s < UINT8_MAX)
        {
          g_ride.post_impact_still_s++;
        }
    }
  else
    {
      g_ride.consecutive_still_s = 0;
      g_ride.post_impact_still_s = 0;
    }

  if (g_ride.state.alert == VELARIDE_ALERT_IMPACT)
    {
      return;
    }

  if (g_ride.impact_pending &&
      g_ride.post_impact_still_s >= IMPACT_STILL_SECONDS)
    {
      g_ride.state.alert = VELARIDE_ALERT_IMPACT;
      g_ride.impact_pending = false;
      g_ride.state.safety_remaining_s = SAFETY_CONFIRM_SECONDS;
      g_ride.safety_timed_out = false;
      g_ride.safety_counted = g_ride.state.safety.checks < UINT16_MAX;
      if (g_ride.safety_counted)
        {
          g_ride.state.safety.checks++;
          g_ride.state.safety.last_action = VELARIDE_SAFETY_NONE;
          g_ride.state.safety.last_elapsed_s = g_ride.state.elapsed_s;
        }
    }
  else if (g_ride.state.alert == VELARIDE_ALERT_NONE &&
           !g_ride.still_alerted &&
           g_ride.state.motion == VELARIDE_MOTION_STILL &&
           g_ride.consecutive_still_s >= STILL_ALERT_SECONDS)
    {
      g_ride.state.alert = VELARIDE_ALERT_STILL;
      g_ride.still_alerted = true;
    }
  else if (g_ride.state.alert == VELARIDE_ALERT_NONE &&
           !g_ride.rest_alerted &&
           g_ride.state.moving_s >= REST_ALERT_SECONDS)
    {
      g_ride.state.alert = VELARIDE_ALERT_REST;
      g_ride.rest_alerted = true;
      g_ride.state.rest_count++;
    }
}

void velaride_ride_session_get(struct velaride_ride_snapshot_s *snapshot)
{
  if (snapshot != NULL)
    {
      *snapshot = g_ride.state;
    }
}

void velaride_ride_session_ack_alert(void)
{
  /* A generic back/ack must never silently claim the user is safe. */

  if (g_ride.state.alert == VELARIDE_ALERT_IMPACT)
    {
      return;
    }

  g_ride.state.alert = VELARIDE_ALERT_NONE;
}

int velaride_ride_session_respond_safety(enum velaride_safety_action_e action)
{
  struct velaride_ride_safety_s *safety = &g_ride.state.safety;

  if (action < VELARIDE_SAFETY_OK || action > VELARIDE_SAFETY_HELP)
    {
      return -EINVAL;
    }

  if (!g_ride.state.active || g_ride.state.alert != VELARIDE_ALERT_IMPACT)
    {
      return -EALREADY;
    }

  if (g_ride.safety_counted)
    {
      if (action == VELARIDE_SAFETY_OK)
        {
          safety->confirmed++;
        }
      else if (action == VELARIDE_SAFETY_PAUSE)
        {
          safety->pauses++;
        }
      else
        {
          safety->help++;
        }
      safety->last_action = action;
      safety->last_elapsed_s = g_ride.state.elapsed_s;
    }

  g_ride.state.alert = VELARIDE_ALERT_NONE;
  g_ride.state.safety_remaining_s = 0;
  g_ride.impact_pending = false;
  g_ride.post_impact_still_s = 0;
  g_ride.consecutive_still_s = 0;
  g_ride.still_alerted = true;
  if (action != VELARIDE_SAFETY_OK)
    {
      velaride_ride_session_set_paused(true);
    }

  return 0;
}

int velaride_ride_session_finish(struct velaride_ride_record_s *record)
{
  struct velaride_ride_record_s latest;
  int ret;

  memset(&latest, 0, sizeof(latest));
  latest.sequence = ++g_ride.sequence;
  latest.start_epoch = g_ride.start_epoch;
  latest.end_epoch = (int64_t)time(NULL);
  latest.elapsed_s = g_ride.state.elapsed_s;
  latest.moving_s = g_ride.state.moving_s;
  latest.still_s = g_ride.state.still_s;
  latest.pause_count = g_ride.state.pause_count;
  latest.impact_count = g_ride.state.impact_count;
  latest.rest_count = g_ride.state.rest_count;
  if (g_ride.state.alert == VELARIDE_ALERT_IMPACT &&
      !g_ride.safety_timed_out && g_ride.safety_counted)
    {
      g_ride.state.safety.unanswered++;
      g_ride.state.safety.last_action = VELARIDE_SAFETY_UNANSWERED;
      g_ride.state.safety.last_elapsed_s = g_ride.state.elapsed_s;
      g_ride.safety_timed_out = true;
    }
  latest.safety = g_ride.state.safety;

  pthread_mutex_lock(&g_phone_lock);
  g_ride_gps.running = false;
  g_ride_gps.anchor = false;
  ride_gps_snapshot(&latest.gps);
  pthread_mutex_unlock(&g_phone_lock);

  g_ride.state.active = false;
  g_ride.state.safety_remaining_s = 0;
  ret = velaride_ride_session_save_record(&latest);

  printf("VELARIDE_RIDE: finish seq=%" PRIu32 " duration=%" PRIu32
         " moving=%" PRIu32 " still=%" PRIu32 " persist=%d\n",
         latest.sequence, latest.elapsed_s, latest.moving_s,
         latest.still_s, ret);
  if (record != NULL)
    {
      *record = latest;
    }

  return ret;
}

int velaride_ride_session_load_latest(struct velaride_ride_record_s *record)
{
  struct ride_record_disk_s disk;
  uint32_t expected;
  ssize_t ret;

  memset(&disk, 0, sizeof(disk));
  ret = property_get_buffer(RIDE_RECORD_KEY, &disk, sizeof(disk));
  if (ret == sizeof(struct ride_record_disk_v2_s) &&
      disk.magic == RIDE_RECORD_MAGIC && disk.version == 2 &&
      disk.size == sizeof(struct ride_record_disk_v2_s))
    {
      struct ride_record_disk_v2_s old;

      memcpy(&old, &disk, sizeof(old));
      if (old.crc32 != ride_crc32(&old,
                                 offsetof(struct ride_record_disk_v2_s, crc32)))
        {
          return -EBADMSG;
        }

      memset(&disk, 0, sizeof(disk));
      memcpy(&disk.record, &old.record,
             offsetof(struct ride_record_v2_s, gps) + sizeof(old.record.gps));
      /* Validate GPS below without rewriting the stored V2 record. */
      disk.magic = RIDE_RECORD_MAGIC;
      disk.version = RIDE_RECORD_VERSION;
      disk.size = sizeof(disk);
      disk.crc32 = ride_crc32(&disk, offsetof(struct ride_record_disk_s, crc32));
      ret = sizeof(disk);
    }
  if (ret == sizeof(struct ride_record_disk_v1_s) &&
      disk.magic == RIDE_RECORD_MAGIC && disk.version == 1 &&
      disk.size == sizeof(struct ride_record_disk_v1_s))
    {
      struct ride_record_disk_v1_s old;
      memcpy(&old, &disk, sizeof(old));
      if (old.crc32 != ride_crc32(&old,
                                 offsetof(struct ride_record_disk_v1_s, crc32)))
        {
          return -EBADMSG;
        }

      if (record != NULL)
        {
          memset(record, 0, sizeof(*record));
          /* Copy the unchanged prefix only, never V1's trailing padding. */
          memcpy(record, &old.record,
                 offsetof(struct ride_record_v1_s, rest_count) +
                 sizeof(old.record.rest_count));
        }

      return 0;
    }

  if (ret != sizeof(disk) || disk.magic != RIDE_RECORD_MAGIC ||
      disk.version != RIDE_RECORD_VERSION || disk.size != sizeof(disk))
    {
      return ret < 0 ? (int)ret : -EINVAL;
    }

  expected = ride_crc32(&disk,
                        offsetof(struct ride_record_disk_s, crc32));
  if (expected != disk.crc32)
    {
      return -EBADMSG;
    }

  if (disk.record.gps.avg_speed_centi > 20000 ||
      disk.record.gps.max_speed_centi > 20000 ||
      (disk.record.gps.time_ms == 0 &&
       (disk.record.gps.distance_m != 0 ||
        disk.record.gps.avg_speed_centi != 0 ||
        disk.record.gps.max_speed_centi != 0)))
    {
      return -EBADMSG;
    }

  if (!ride_safety_valid(&disk.record.safety, disk.record.elapsed_s))
    {
      return -EBADMSG;
    }

  if (record != NULL)
    {
      *record = disk.record;
    }

  return 0;
}

int velaride_ride_session_save_record(
  const struct velaride_ride_record_s *record)
{
  struct ride_record_disk_s disk;
  int ret;

  if (record == NULL || !ride_safety_valid(&record->safety, record->elapsed_s))
    {
      return -EINVAL;
    }

  memset(&disk, 0, sizeof(disk));
  disk.magic = RIDE_RECORD_MAGIC;
  disk.version = RIDE_RECORD_VERSION;
  disk.size = sizeof(disk);
  disk.record = *record;
  disk.crc32 = ride_crc32(&disk,
                          offsetof(struct ride_record_disk_s, crc32));
  ret = property_set_buffer(RIDE_RECORD_KEY, &disk, sizeof(disk));
  if (ret == 0)
    {
      ret = property_commit();
    }

  return ret;
}

void velaride_ride_session_set_phone_data(double speed_kmh,
                                          double distance_km)
{
  struct timespec now;

  pthread_mutex_lock(&g_phone_lock);
  if (!isfinite(speed_kmh) || speed_kmh < 0.0 || speed_kmh > 200.0 ||
      !isfinite(distance_km) || distance_km < 0.0 || distance_km > 100000.0 ||
      clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      memset(&g_phone_data, 0, sizeof(g_phone_data));
      g_ride_gps.anchor = false;
      pthread_mutex_unlock(&g_phone_lock);
      return;
    }

  g_phone_data.speed_kmh = speed_kmh;
  g_phone_data.distance_km = distance_km;
  g_phone_data.updated_ms = (uint64_t)now.tv_sec * 1000 +
                            now.tv_nsec / 1000000;
  g_phone_data.valid = true;
  if (g_ride_gps.running)
    {
      uint64_t ms = g_phone_data.updated_ms;
      double delta = distance_km - g_ride_gps.last_km;
      uint64_t dt = ms >= g_ride_gps.last_ms ?
                    ms - g_ride_gps.last_ms : 0;

      if (g_ride_gps.anchor && dt > 0 && dt < 15000 && delta >= 0 &&
          delta * 3600000.0 / dt <= 200.0 &&
          dt <= UINT32_MAX - g_ride_gps.time_ms &&
          g_ride_gps.distance_km + delta <= UINT32_MAX / 1000.0)
        {
          g_ride_gps.distance_km += delta;
          g_ride_gps.time_ms += dt;
        }

      if (speed_kmh > g_ride_gps.max_speed)
        {
          g_ride_gps.max_speed = speed_kmh;
        }

      g_ride_gps.last_ms = ms;
      g_ride_gps.last_km = distance_km;
      g_ride_gps.anchor = true;
    }
  pthread_mutex_unlock(&g_phone_lock);
}

void velaride_ride_session_clear_phone_data(void)
{
  pthread_mutex_lock(&g_phone_lock);
  memset(&g_phone_data, 0, sizeof(g_phone_data));
  g_ride_gps.anchor = false;
  pthread_mutex_unlock(&g_phone_lock);
}

bool velaride_ride_session_get_phone_data(double *speed_kmh,
                                          double *distance_km)
{
  struct timespec now;
  uint64_t now_ms;

  if (speed_kmh != NULL)
    {
      *speed_kmh = 0;
    }

  if (distance_km != NULL)
    {
      *distance_km = 0;
    }

  pthread_mutex_lock(&g_phone_lock);
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      g_phone_data.valid = false;
    }
  else
    {
      now_ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
      if (now_ms < g_phone_data.updated_ms ||
          now_ms - g_phone_data.updated_ms >= 15000)
        {
          g_phone_data.valid = false;
        }
    }

  if (!g_phone_data.valid)
    {
      pthread_mutex_unlock(&g_phone_lock);
      return false;
    }

  if (speed_kmh != NULL)
    {
      *speed_kmh = g_phone_data.speed_kmh;
    }

  if (distance_km != NULL)
    {
      *distance_km = g_phone_data.distance_km;
    }

  pthread_mutex_unlock(&g_phone_lock);
  return true;
}

int velaride_ride_session_save_review(const char *review)
{
  struct ride_review_chunk_s chunk;
  char key[48];
  size_t text_len;
  size_t offset;
  uint8_t count;
  uint8_t index;
  int ret;

  if (review == NULL || review[0] == '\0')
    {
      return -EINVAL;
    }

  text_len = strlen(review);
  if (text_len > RIDE_REVIEW_MAX)
    {
      text_len = RIDE_REVIEW_MAX;
      while (text_len > 0 &&
             (((const uint8_t *)review)[text_len] & 0xc0) == 0x80)
        {
          text_len--;
        }
    }

  count = (text_len + RIDE_REVIEW_CHUNK_DATA - 1) /
          RIDE_REVIEW_CHUNK_DATA;
  for (index = 0; index < RIDE_REVIEW_CHUNK_COUNT; index++)
    {
      memset(&chunk, 0, sizeof(chunk));
      snprintf(key, sizeof(key), "%s%u", RIDE_REVIEW_KEY_PREFIX, index);
      if (index < count)
        {
          offset = index * RIDE_REVIEW_CHUNK_DATA;
          chunk.magic = RIDE_REVIEW_MAGIC;
          chunk.version = RIDE_REVIEW_VERSION;
          chunk.index = index;
          chunk.count = count;
          chunk.data_len = text_len - offset > RIDE_REVIEW_CHUNK_DATA ?
                           RIDE_REVIEW_CHUNK_DATA : text_len - offset;
          memcpy(chunk.data, review + offset, chunk.data_len);
          chunk.crc32 = ride_crc32(
            &chunk, offsetof(struct ride_review_chunk_s, crc32));
        }

      ret = property_set_buffer(key, &chunk, sizeof(chunk));
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = property_commit();
  if (ret == 0)
    {
      __atomic_store_n(&g_review_view_pending, true, __ATOMIC_RELEASE);
    }

  return ret;
}

int velaride_ride_session_load_review(char *review, size_t size)
{
  struct ride_review_chunk_s chunk;
  char key[48];
  uint32_t expected;
  size_t total = 0;
  ssize_t ret;
  uint8_t count = 0;
  uint8_t index;

  if (review == NULL || size == 0)
    {
      return -EINVAL;
    }

  review[0] = '\0';
  for (index = 0; index < RIDE_REVIEW_CHUNK_COUNT; index++)
    {
      memset(&chunk, 0, sizeof(chunk));
      snprintf(key, sizeof(key), "%s%u", RIDE_REVIEW_KEY_PREFIX, index);
      ret = property_get_buffer(key, &chunk, sizeof(chunk));
      if (ret != sizeof(chunk) || chunk.magic != RIDE_REVIEW_MAGIC ||
          chunk.version != RIDE_REVIEW_VERSION || chunk.index != index ||
          chunk.count == 0 || chunk.count > RIDE_REVIEW_CHUNK_COUNT ||
          chunk.data_len == 0 || chunk.data_len > RIDE_REVIEW_CHUNK_DATA)
        {
          return ret < 0 ? (int)ret : -EINVAL;
        }

      if (index == 0)
        {
          count = chunk.count;
        }
      else if (chunk.count != count)
        {
          return -EBADMSG;
        }

      expected = ride_crc32(
        &chunk, offsetof(struct ride_review_chunk_s, crc32));
      if (expected != chunk.crc32 || total + chunk.data_len >= size)
        {
          return -EBADMSG;
        }

      memcpy(review + total, chunk.data, chunk.data_len);
      total += chunk.data_len;
      if (index + 1 == count)
        {
          review[total] = '\0';
          return total > 0 ? 0 : -EINVAL;
        }
    }

  return -EBADMSG;
}

int velaride_ride_session_clear_review(void)
{
  struct ride_review_chunk_s chunk;
  char key[48];
  uint8_t index;
  int ret;

  __atomic_store_n(&g_review_view_pending, false, __ATOMIC_RELEASE);

  memset(&chunk, 0, sizeof(chunk));
  for (index = 0; index < RIDE_REVIEW_CHUNK_COUNT; index++)
    {
      snprintf(key, sizeof(key), "%s%u", RIDE_REVIEW_KEY_PREFIX, index);
      ret = property_set_buffer(key, &chunk, sizeof(chunk));
      if (ret < 0)
        {
          return ret;
        }
    }

  return property_commit();
}

bool velaride_ride_session_take_review_view_request(void)
{
  return __atomic_exchange_n(&g_review_view_pending, false, __ATOMIC_ACQ_REL);
}
