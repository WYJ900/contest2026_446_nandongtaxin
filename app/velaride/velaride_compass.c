/****************************************************************************
 * Magnetic field, user calibration and level-only bearing preview.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <kvdb.h>
#include <nuttx/uorb.h>
#include "velaride_compass.h"

#define COMPASS_KEY "persist.velaride.compass"
#define CAL_SAMPLES 240

struct compass_cal_s
{
  uint32_t magic;
  float bias[3];
  float scale[3];
  uint32_t checksum;
};

static pthread_mutex_t g_compass_lock = PTHREAD_MUTEX_INITIALIZER;
static struct velaride_compass_s g_compass;
static struct compass_cal_s g_cal;
static float g_cal_samples[CAL_SAMPLES][3];
static uint32_t g_cal_generation;
static uint32_t g_cal_saved;
static uint64_t g_mag_time;

static uint64_t compass_time(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static uint32_t calibration_checksum(const struct compass_cal_s *cal)
{
  const uint8_t *bytes = (const uint8_t *)cal;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < offsetof(struct compass_cal_s, checksum); i++)
    hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}

void velaride_compass_init(void)
{
  struct compass_cal_s cal;
  bool valid = property_get_buffer(COMPASS_KEY, &cal, sizeof(cal)) == sizeof(cal);
  valid = valid && cal.magic == 0x564d4301 &&
          cal.checksum == calibration_checksum(&cal);
  for (int i = 0; valid && i < 3; i++)
    valid = isfinite(cal.bias[i]) && fabsf(cal.bias[i]) < 500 &&
            isfinite(cal.scale[i]) && cal.scale[i] >= 0.5f && cal.scale[i] <= 2;
  pthread_mutex_lock(&g_compass_lock);
  if (valid) { g_cal = cal; g_compass.calibrated = true; }
  pthread_mutex_unlock(&g_compass_lock);
}

void velaride_compass_enable(bool enable)
{
  pthread_mutex_lock(&g_compass_lock);
  g_compass.enabled = enable;
  if (!enable)
    {
      g_compass.calibrating = false;
      g_compass.valid = false;
      g_compass.heading_valid = false;
    }
  pthread_mutex_unlock(&g_compass_lock);
}

void velaride_compass_get(struct velaride_compass_s *snapshot)
{
  uint64_t now = compass_time();
  pthread_mutex_lock(&g_compass_lock);
  *snapshot = g_compass;
  if (now - g_mag_time > 1500000)
    {
      snapshot->valid = snapshot->heading_valid = false;
      if (snapshot->enabled) snapshot->error = -ETIMEDOUT;
    }
  pthread_mutex_unlock(&g_compass_lock);
}

static void compass_accept(const struct sensor_mag *mag)
{
  float values[3] = {mag->x, mag->y, mag->z};
  float norm;
  g_compass.x = mag->x; g_compass.y = mag->y; g_compass.z = mag->z;
  g_mag_time = compass_time();
  g_compass.samples++;
  g_compass.valid = true;
  g_compass.error = 0;
  g_compass.heading_valid = false;
  if (g_compass.calibrating && g_compass.collected < CAL_SAMPLES)
    {
      memcpy(g_cal_samples[g_compass.collected++], values, sizeof(values));
    }
  if (g_compass.calibrated)
    for (int i = 0; i < 3; i++)
      values[i] = (values[i] - g_cal.bias[i]) * g_cal.scale[i];
  norm = sqrtf(values[0]*values[0] + values[1]*values[1] + values[2]*values[2]);
  g_compass.field_ut = norm;
  if (g_compass.calibrated && !g_compass.calibrating && norm >= 15 && norm <= 100 &&
      values[0]*values[0] + values[1]*values[1] >= 25)
    {
      /* Sensor-axis bearing. Physical watch orientation is validated in S4;
       * until then the UI explicitly calls this a direction preview.
       */
      float angle = -atan2f(values[1], values[0]) * 57.2957795f;
      g_compass.heading = angle < 0 ? angle + 360 : angle;
      g_compass.heading_valid = true;
    }
}

void velaride_compass_sample(void)
{
  static int fd = -1;
  struct sensor_mag mag;
  struct compass_cal_s cal;
  uint32_t generation;
  bool enabled;
  ssize_t ret;

  pthread_mutex_lock(&g_compass_lock);
  enabled = g_compass.enabled;
  generation = g_cal_generation;
  cal = g_cal;
  pthread_mutex_unlock(&g_compass_lock);

  if (generation != g_cal_saved)
    {
      ret = property_set_buffer(COMPASS_KEY, &cal, sizeof(cal));
      if (ret >= 0) ret = property_commit();
      pthread_mutex_lock(&g_compass_lock);
      g_cal_saved = generation;
      if (generation == g_cal_generation)
        {
          g_compass.save_pending = false;
          g_compass.save_error = ret < 0 ? ret : 0;
        }
      pthread_mutex_unlock(&g_compass_lock);
    }

  if (!enabled)
    {
      if (fd >= 0) close(fd);
      fd = -1;
      return;
    }
  if (fd < 0) fd = open("/dev/uorb/sensor_mag0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  ret = fd < 0 ? -1 : read(fd, &mag, sizeof(mag));
  int error = ret < 0 ? -errno : -EIO;
  pthread_mutex_lock(&g_compass_lock);
  if (g_compass.enabled)
    {
      if (ret == sizeof(mag) && isfinite(mag.x) && isfinite(mag.y) &&
          isfinite(mag.z) && fabsf(mag.x) < 3276 && fabsf(mag.y) < 3276 &&
          fabsf(mag.z) < 3276)
        compass_accept(&mag);
      else
        {
          g_compass.valid = g_compass.heading_valid = false;
          g_compass.error = error;
          g_compass.failures++;
        }
    }
  pthread_mutex_unlock(&g_compass_lock);
  if (ret != sizeof(mag) && fd >= 0) { close(fd); fd = -1; }
}

void velaride_compass_calibrate(void)
{
  pthread_mutex_lock(&g_compass_lock);
  g_compass.enabled = true;
  g_compass.calibrating = true;
  g_compass.heading_valid = false;
  g_compass.collected = 0;
  g_compass.calibration_error = 0;
  pthread_mutex_unlock(&g_compass_lock);
}

void velaride_compass_cancel(void)
{
  pthread_mutex_lock(&g_compass_lock);
  g_compass.calibrating = false;
  g_compass.calibration_error = 0;
  pthread_mutex_unlock(&g_compass_lock);
}

int velaride_compass_finish(void)
{
  struct compass_cal_s cal = {.magic = 0x564d4301};
  float low[3] = {10000,10000,10000};
  float high[3] = {-10000,-10000,-10000};
  float radius[3];
  float average = 0;
  unsigned int octants = 0;
  int ret = -EAGAIN;

  pthread_mutex_lock(&g_compass_lock);
  if (!g_compass.calibrating || g_compass.collected < 80) goto done;
  for (int n = 0; n < g_compass.collected; n++)
    for (int i = 0; i < 3; i++)
      {
        float v = g_cal_samples[n][i];
        if (v < low[i]) low[i] = v;
        if (v > high[i]) high[i] = v;
      }
  for (int i = 0; i < 3; i++)
    {
      radius[i] = (high[i] - low[i]) / 2;
      if (radius[i] < 15 || radius[i] > 100) goto done;
      cal.bias[i] = (high[i] + low[i]) / 2;
      if (fabsf(cal.bias[i]) >= 500) goto done;
      average += radius[i] / 3;
    }
  for (int i = 0; i < 3; i++)
    {
      cal.scale[i] = average / radius[i];
      if (cal.scale[i] < 0.5f || cal.scale[i] > 2) goto done;
    }
  for (int n = 0; n < g_compass.collected; n++)
    {
      unsigned int octant = 0;
      for (int i = 0; i < 3; i++)
        if (g_cal_samples[n][i] >= cal.bias[i]) octant |= 1u << i;
      octants |= 1u << octant;
    }
  int covered = 0;
  for (int i = 0; i < 8; i++) if (octants & (1u << i)) covered++;
  if (covered < 6) goto done;
  cal.checksum = calibration_checksum(&cal);
  g_cal = cal;
  g_cal_generation++;
  g_compass.save_pending = true;
  g_compass.save_error = 0;
  g_compass.calibrated = true;
  g_compass.calibrating = false;
  ret = 0;
done:
  g_compass.calibration_error = ret;
  pthread_mutex_unlock(&g_compass_lock);
  return ret;
}
