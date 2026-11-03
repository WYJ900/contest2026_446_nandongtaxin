/****************************************************************************
 * Application night preference and ambient-light hysteresis.
 * No sensor/driver writes and no changes to saved day brightness/theme.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#ifdef __NuttX__
#include <sched.h>
#endif
#include <kvdb.h>
#include "velaride_night.h"

#define NIGHT_KEY "persist.velaride.night"
#define NIGHT_DWELL_MS 3000
static pthread_mutex_t g_night_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_night_cond = PTHREAD_COND_INITIALIZER;
static struct velaride_night_s g_night;
static bool g_loaded;
static bool g_worker;
static bool g_dirty;
static uint32_t g_generation;
static uint64_t g_save_after;
static bool g_have_sample;
static uint32_t g_last_sample;
static int g_candidate = -1;
static uint32_t g_candidate_since;

static uint64_t night_now(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static uint32_t night_crc(const uint8_t *data)
{
  uint32_t crc = UINT32_MAX;
  for (int i = 0; i < 8; i++)
    {
      crc ^= data[i];
      for (int bit = 0; bit < 8; bit++)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
  return ~crc;
}

static void night_load_locked(void)
{
  uint8_t disk[12];
  uint32_t crc;
  if (g_loaded) return;
  g_loaded = true;
  g_night.mode = VR_NIGHT_OFF;
  g_night.limit = 20;
  g_night.active = false;
  g_night.light_valid = false;
  g_night.save_error = 0;
  g_candidate = -1;
  g_have_sample = false;
  if (property_get_buffer(NIGHT_KEY, disk, sizeof(disk)) != sizeof(disk)) return;
  crc = (uint32_t)disk[8] | (uint32_t)disk[9] << 8 |
        (uint32_t)disk[10] << 16 | (uint32_t)disk[11] << 24;
  if (memcmp(disk, "VRN1", 4) == 0 && disk[4] <= VR_NIGHT_AUTO &&
      disk[5] >= 20 && disk[5] <= 40 && disk[6] == 0 && disk[7] == 0 &&
      crc == night_crc(disk))
    {
      g_night.mode = disk[4];
      g_night.limit = disk[5];
    }
  g_night.active = g_night.mode == VR_NIGHT_ON;
}

static void *night_save_worker(void *arg)
{
  (void)arg;
  pthread_mutex_lock(&g_night_lock);
  for (; ; )
    {
      uint8_t disk[12] = {'V', 'R', 'N', '1', 0, 0, 0, 0, 0, 0, 0, 0};
      uint32_t generation;
      uint32_t crc;
      int ret;
      while (!g_dirty) pthread_cond_wait(&g_night_cond, &g_night_lock);
      if (night_now() < g_save_after)
        {
          struct timespec delay = {.tv_nsec = 20000000};
          pthread_mutex_unlock(&g_night_lock);
          nanosleep(&delay, NULL);
          pthread_mutex_lock(&g_night_lock);
          continue;
        }
      generation = g_generation;
      disk[4] = g_night.mode;
      disk[5] = g_night.limit;
      crc = night_crc(disk);
      for (int i = 0; i < 4; i++) disk[8 + i] = (uint8_t)(crc >> (i * 8));
      g_dirty = false;
      pthread_mutex_unlock(&g_night_lock);
      ret = property_set_buffer(NIGHT_KEY, disk, sizeof(disk));
      if (ret == 0) ret = property_commit();
      pthread_mutex_lock(&g_night_lock);
      if (generation == g_generation)
        {
          g_night.save_error = ret;
          g_night.saving = false;
        }
    }
  return NULL;
}

#ifdef __NuttX__
static int night_save_task(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  night_save_worker(NULL);
  return 0;
}
#endif

void velaride_night_get(struct velaride_night_s *out)
{
  pthread_mutex_lock(&g_night_lock);
  night_load_locked();
  *out = g_night;
  pthread_mutex_unlock(&g_night_lock);
}

int velaride_night_set(uint8_t mode, uint8_t limit)
{
  int ret = 0;
  if (mode > VR_NIGHT_AUTO || limit < 20 || limit > 40) return -EINVAL;
  pthread_mutex_lock(&g_night_lock);
  night_load_locked();
  if (g_night.mode == mode && g_night.limit == limit && g_night.save_error == 0)
    {
      pthread_mutex_unlock(&g_night_lock);
      return 0;
    }
  if (g_night.mode != mode)
    {
      g_candidate = -1;
      g_have_sample = false;
    }
  g_night.mode = mode;
  g_night.limit = limit;
  if (mode != VR_NIGHT_AUTO) g_night.active = mode == VR_NIGHT_ON;
  g_generation++;
  g_dirty = true;
  g_night.saving = true;
  g_night.save_error = 0;
  g_save_after = night_now() + 400;
  if (!g_worker)
    {
#ifdef __NuttX__
      ret = task_create("vr_night_save", 80, 32768, night_save_task, NULL);
      ret = ret < 0 ? errno : 0;
#else
      pthread_t worker;
      ret = pthread_create(&worker, NULL, night_save_worker, NULL);
      if (ret == 0) pthread_detach(worker);
#endif
      if (ret == 0) g_worker = true;
      else
        {
          g_night.saving = false;
          g_night.save_error = -ret;
        }
    }
  pthread_cond_signal(&g_night_cond);
  pthread_mutex_unlock(&g_night_lock);
  return -ret;
}

void velaride_night_poll(uint32_t now, uint32_t sample_id, bool valid, float lux)
{
  int desired;
  pthread_mutex_lock(&g_night_lock);
  night_load_locked();
  g_night.light_valid = valid && isfinite(lux) && lux >= 0;
  g_night.lux = g_night.light_valid ? lux : 0;
  if (!g_night.light_valid)
    {
      g_candidate = -1;
      g_have_sample = false;
      goto out; /* Keep the last display state on stale/unavailable input. */
    }
  if (g_night.exempt)
    {
      /* The white night-light must not classify its own light as daylight. */
      g_candidate = -1;
      g_have_sample = true;
      g_last_sample = sample_id;
      goto out;
    }
  if (g_night.mode != VR_NIGHT_AUTO ||
      (g_have_sample && sample_id == g_last_sample)) goto out;
  g_have_sample = true;
  g_last_sample = sample_id;
  desired = lux <= 10 ? 1 : lux >= 30 ? 0 : -1;
  if (desired < 0 || desired == (int)g_night.active)
    {
      g_candidate = -1;
      goto out;
    }
  if (desired != g_candidate)
    {
      g_candidate = desired;
      g_candidate_since = now;
    }
  else if ((uint32_t)(now - g_candidate_since) >= NIGHT_DWELL_MS)
    {
      g_night.active = desired != 0;
      g_candidate = -1;
    }
out:
  pthread_mutex_unlock(&g_night_lock);
}

void velaride_night_exempt(bool exempt)
{
  pthread_mutex_lock(&g_night_lock);
  g_night.exempt = exempt;
  pthread_mutex_unlock(&g_night_lock);
}

uint8_t velaride_night_brightness(uint8_t normal)
{
  struct velaride_night_s night;
  velaride_night_get(&night);
  return night.active && !night.exempt && normal > night.limit ?
         night.limit : normal;
}
