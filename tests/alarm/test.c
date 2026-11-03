#define _POSIX_C_SOURCE 200809L
#include "../../app/velaride/velaride_alarm.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t saved[12];
static bool have_saved;
static bool fail_save;
ssize_t property_get_buffer(const char *key, void *data, size_t size)
{
  assert(strcmp(key, ALARM_KEY) == 0 && size == sizeof(saved));
  if (!have_saved) return -ENOENT;
  memcpy(data, saved, size);
  return size;
}
int property_set_buffer(const char *key, const void *data, size_t size)
{
  assert(strcmp(key, ALARM_KEY) == 0 && size == sizeof(saved));
  if (fail_save) return -EIO;
  memcpy(saved, data, size);
  have_saved = true;
  return 0;
}
int property_commit(void) { return 0; }

static struct velaride_alarm_s wait_saved(void)
{
  struct velaride_alarm_s alarm;
  struct timespec delay = {.tv_nsec = 1000000};
  for (int i = 0; i < 2000; i++)
    {
      velaride_alarm_get(&alarm);
      if (!alarm.saving) return alarm;
      nanosleep(&delay, NULL);
    }
  assert(!"save worker timed out");
  return alarm;
}

static time_t at(int day, int hour, int minute, int second)
{
  struct tm local = {.tm_year = 126, .tm_mon = 0, .tm_mday = day,
                     .tm_hour = hour, .tm_min = minute, .tm_sec = second};
  return mktime(&local);
}

int main(void)
{
  struct velaride_alarm_s alarm;
  setenv("TZ", "UTC0", 1);
  tzset();
  velaride_alarm_get(&alarm);
  assert(!alarm.enabled && alarm.hour == 7 && alarm.minute == 0);
  assert(velaride_alarm_set(24, 0, true, at(1, 6, 0, 0)) == -ERANGE);
  assert(velaride_alarm_set(7, 60, true, at(1, 6, 0, 0)) == -ERANGE);
  assert(velaride_alarm_set(7, 0, true, 0) == -ERANGE);
  assert(velaride_alarm_set(7, 0, true, at(1, 6, 59, 50)) == 0);
  assert(wait_saved().save_error == 0);
  velaride_alarm_tick(at(1, 6, 59, 59));
  assert(!velaride_alarm_take_due());
  velaride_alarm_tick(at(1, 7, 0, 0));
  assert(velaride_alarm_take_due());
  velaride_alarm_tick(at(1, 7, 0, 30));
  assert(!velaride_alarm_take_due());
  velaride_alarm_ack();
  velaride_alarm_tick(at(1, 6, 0, 0)); /* Clock goes backward. */
  velaride_alarm_tick(at(1, 7, 0, 0));
  assert(!velaride_alarm_take_due());
  velaride_alarm_tick(at(2, 7, 0, 0));
  assert(velaride_alarm_take_due());
  velaride_alarm_tick(at(3, 8, 0, 0)); /* Do not replay a missed alarm. */
  assert(!velaride_alarm_take_due());

  /* A restored clock may be an old saved floor: no automatic re-arming. */
  g_alarm_loaded = false;
  velaride_alarm_get(&alarm);
  assert(alarm.enabled && alarm.needs_confirmation);
  velaride_alarm_tick(at(4, 7, 0, 0));
  assert(!velaride_alarm_take_due());
  assert(velaride_alarm_set(7, 0, true, at(4, 7, 0, 10)) == 0);
  wait_saved();
  velaride_alarm_tick(at(4, 7, 0, 20));
  assert(!velaride_alarm_take_due()); /* Enabling now schedules tomorrow. */
  velaride_alarm_tick(at(5, 7, 0, 0));
  assert(velaride_alarm_take_due());
  assert(velaride_alarm_set(7, 0, false, at(5, 7, 1, 0)) == 0);
  wait_saved();
  velaride_alarm_tick(at(6, 7, 0, 0));
  assert(!velaride_alarm_take_due());

  fail_save = true;
  assert(velaride_alarm_set(8, 0, true, at(6, 7, 0, 0)) == 0);
  assert(wait_saved().save_error == -EIO);
  velaride_alarm_tick(at(6, 8, 0, 0));
  assert(!velaride_alarm_take_due());
  fail_save = false;
  assert(velaride_alarm_set(8, 0, true, at(6, 7, 0, 0)) == 0);
  assert(wait_saved().save_error == 0);
  saved[4] ^= 1;
  g_alarm_loaded = false;
  velaride_alarm_get(&alarm);
  assert(!alarm.enabled && alarm.hour == 7);
  puts("PASS: daily alarm, no duplicate/backward-clock replay, missed alarm, boot confirmation, bounds, save failure/retry and CRC");
  return 0;
}
