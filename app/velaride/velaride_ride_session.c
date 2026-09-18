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
#define RIDE_RECORD_VERSION 1
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
#define STILL_ALERT_SECONDS 15
#define REST_ALERT_SECONDS (45 * 60)

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
  bool impact_pending;
  bool rest_alerted;
  bool still_alerted;
  int64_t start_epoch;
  uint32_t sequence;
};

static struct ride_session_s g_ride =
{
  .sensor_fd = -1,
};

static struct
{
  double speed_kmh;
  double distance_km;
  bool valid;
} g_phone_data;

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
  bool sensor_ready = g_ride.state.sensor_ready;

  memset(&g_ride.state, 0, sizeof(g_ride.state));
  g_ride.state.active = true;
  g_ride.state.sensor_ready = sensor_ready;
  g_ride.state.motion = VELARIDE_MOTION_UNKNOWN;
  g_ride.warmup_samples = 0;
  g_ride.moving_score = 0;
  g_ride.still_score = 0;
  g_ride.sensor_ticks = 0;
  g_ride.post_impact_still_s = 0;
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
      return;
    }

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
  if (!g_ride.state.active || g_ride.state.paused)
    {
      return;
    }

  g_ride.state.elapsed_s++;
  if (g_ride.state.motion == VELARIDE_MOTION_MOVING)
    {
      g_ride.state.moving_s++;
      g_ride.post_impact_still_s = 0;
    }
  else if (g_ride.state.motion == VELARIDE_MOTION_STILL)
    {
      g_ride.state.still_s++;
      if (g_ride.impact_pending && g_ride.post_impact_still_s < UINT8_MAX)
        {
          g_ride.post_impact_still_s++;
        }
    }

  if (g_ride.impact_pending &&
      g_ride.post_impact_still_s >= IMPACT_STILL_SECONDS)
    {
      g_ride.state.alert = VELARIDE_ALERT_IMPACT;
      g_ride.impact_pending = false;
    }
  else if (!g_ride.still_alerted &&
           g_ride.state.motion == VELARIDE_MOTION_STILL &&
           g_ride.state.still_s >= STILL_ALERT_SECONDS)
    {
      g_ride.state.alert = VELARIDE_ALERT_STILL;
      g_ride.still_alerted = true;
    }
  else if (!g_ride.rest_alerted &&
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
  g_ride.state.alert = VELARIDE_ALERT_NONE;
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

  g_ride.state.active = false;
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

  if (record == NULL)
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
  g_phone_data.speed_kmh = speed_kmh >= 0.0 ? speed_kmh : 0.0;
  g_phone_data.distance_km = distance_km >= 0.0 ? distance_km : 0.0;
  g_phone_data.valid = true;
}

void velaride_ride_session_clear_phone_data(void)
{
  memset(&g_phone_data, 0, sizeof(g_phone_data));
}

bool velaride_ride_session_get_phone_data(double *speed_kmh,
                                          double *distance_km)
{
  if (!g_phone_data.valid)
    {
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

  return property_commit();
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
