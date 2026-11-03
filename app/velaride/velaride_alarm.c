/****************************************************************************
 * Daily visual alarm; settings are saved outside the LVGL thread.
 * After a reboot the user must check the clock and explicitly re-arm it:
 * this board can restore an old saved time after losing USB power.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#ifdef __NuttX__
#include <sched.h>
#endif
#include <string.h>
#include <kvdb.h>
#include "velaride_alarm.h"
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
#include "velaride_sound.h"
#endif

#define ALARM_KEY "persist.velaride.alarm"
#define ALARM_MIN_EPOCH 1704067200
static pthread_mutex_t g_alarm_lock = PTHREAD_MUTEX_INITIALIZER;
static struct velaride_alarm_s g_alarm;
static bool g_alarm_loaded;
static bool g_alarm_pending;
static int g_alarm_last_day = -1;

static uint32_t alarm_crc(const uint8_t *data)
{
  uint32_t crc = UINT32_MAX;
  int i;
  int bit;
  for (i = 0; i < 8; i++)
    {
      crc ^= data[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
  return ~crc;
}

static void alarm_load_locked(void)
{
  uint8_t disk[12];
  uint32_t crc;
  if (g_alarm_loaded)
    {
      return;
    }

  g_alarm_loaded = true;
  memset(&g_alarm, 0, sizeof(g_alarm));
  g_alarm.hour = 7;
  g_alarm_pending = false;
  g_alarm_last_day = -1;
  if (property_get_buffer(ALARM_KEY, disk, sizeof(disk)) != sizeof(disk))
    {
      return;
    }

  crc = (uint32_t)disk[8] | (uint32_t)disk[9] << 8 |
        (uint32_t)disk[10] << 16 | (uint32_t)disk[11] << 24;
  if (memcmp(disk, "VRA1", 4) == 0 && disk[4] < 24 && disk[5] < 60 &&
      disk[6] <= 1 && disk[7] == 0 && crc == alarm_crc(disk))
    {
      g_alarm.hour = disk[4];
      g_alarm.minute = disk[5];
      g_alarm.enabled = disk[6] != 0;
      g_alarm.needs_confirmation = g_alarm.enabled;
    }
}

static void *alarm_save_worker(void *arg)
{
  uint8_t disk[12] = {'V', 'R', 'A', '1', 0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t crc;
  int ret;
  int i;
  (void)arg;
  pthread_mutex_lock(&g_alarm_lock);
  disk[4] = g_alarm.hour;
  disk[5] = g_alarm.minute;
  disk[6] = g_alarm.enabled;
  pthread_mutex_unlock(&g_alarm_lock);
  crc = alarm_crc(disk);
  for (i = 0; i < 4; i++)
    {
      disk[8 + i] = (uint8_t)(crc >> (i * 8));
    }

  ret = property_set_buffer(ALARM_KEY, disk, sizeof(disk));
  if (ret == 0)
    {
      ret = property_commit();
    }

  pthread_mutex_lock(&g_alarm_lock);
  g_alarm.save_error = ret;
  g_alarm.saving = false;
  pthread_mutex_unlock(&g_alarm_lock);
  return NULL;
}

static bool alarm_local_time(time_t now, struct tm *local)
{
  return now >= ALARM_MIN_EPOCH && localtime_r(&now, local) != NULL;
}

#ifdef __NuttX__
static int alarm_save_task(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  alarm_save_worker(NULL);
  return 0;
}
#endif

void velaride_alarm_get(struct velaride_alarm_s *out)
{
  pthread_mutex_lock(&g_alarm_lock);
  alarm_load_locked();
  *out = g_alarm;
  pthread_mutex_unlock(&g_alarm_lock);
}

int velaride_alarm_set(unsigned int hour, unsigned int minute, bool enabled,
                       time_t now)
{
#ifdef __NuttX__
  int pid;
#else
  pthread_t worker;
  pthread_attr_t attr;
#endif
  struct tm local;
  bool valid = alarm_local_time(now, &local);
  int ret;
  if (hour > 23 || minute > 59 || (enabled && !valid))
    {
      return -ERANGE;
    }

  pthread_mutex_lock(&g_alarm_lock);
  alarm_load_locked();
  if (g_alarm.saving)
    {
      pthread_mutex_unlock(&g_alarm_lock);
      return -EBUSY;
    }

  g_alarm.hour = hour;
  g_alarm.minute = minute;
  g_alarm.enabled = enabled;
  g_alarm.needs_confirmation = false;
  g_alarm.ringing = false;
  g_alarm_pending = false;
  /* Enabling within the chosen minute means the next day, not immediately. */
  g_alarm_last_day = valid && local.tm_hour == (int)hour &&
                    local.tm_min == (int)minute ?
                    local.tm_year * 400 + local.tm_yday : -1;
  g_alarm.saving = true;
  g_alarm.save_error = 0;
#ifdef __NuttX__
  /* A pthread belongs to its caller's task group and is killed if an NSH
   * command returns. Use an independent task for both UI and NSH callers.
   */
  pid = task_create("vr_alarm_save", 100, 32768, alarm_save_task, NULL);
  ret = pid < 0 ? errno : 0;
#else
  ret = pthread_attr_init(&attr);
  if (ret == 0)
    {
      ret = pthread_attr_setstacksize(&attr, 32768);
      if (ret == 0)
        {
          ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        }
      if (ret == 0)
        {
          ret = pthread_create(&worker, &attr, alarm_save_worker, NULL);
        }
      pthread_attr_destroy(&attr);
    }
#endif

  if (ret != 0)
    {
      g_alarm.save_error = -ret;
      g_alarm.saving = false;
    }

  pthread_mutex_unlock(&g_alarm_lock);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop(VR_SOUND_ALARM);
#endif
  return -ret;
}

void velaride_alarm_tick(time_t now)
{
  struct tm local;
  int day;
  if (!alarm_local_time(now, &local))
    {
      return;
    }

  day = local.tm_year * 400 + local.tm_yday;
  pthread_mutex_lock(&g_alarm_lock);
  alarm_load_locked();
  if (g_alarm.enabled && !g_alarm.needs_confirmation && !g_alarm.saving &&
      g_alarm.save_error == 0 && day > g_alarm_last_day &&
      local.tm_hour == g_alarm.hour && local.tm_min == g_alarm.minute)
    {
      g_alarm_last_day = day;
      g_alarm.ringing = true;
      g_alarm_pending = true;
      g_alarm.completions++;
    }
  pthread_mutex_unlock(&g_alarm_lock);
}

bool velaride_alarm_take_due(void)
{
  bool pending;
  pthread_mutex_lock(&g_alarm_lock);
  pending = g_alarm_pending;
  g_alarm_pending = false;
  pthread_mutex_unlock(&g_alarm_lock);
  return pending;
}

void velaride_alarm_ack(void)
{
  pthread_mutex_lock(&g_alarm_lock);
  g_alarm.ringing = false;
  g_alarm_pending = false;
  pthread_mutex_unlock(&g_alarm_lock);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop(VR_SOUND_ALARM);
#endif
}
