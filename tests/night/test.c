#define _POSIX_C_SOURCE 200809L
#include "../../app/velaride/velaride_night.c"
#include <assert.h>
#include <stdio.h>

static uint8_t saved[12];
static bool present;
static bool fail_save;
static unsigned int writes;
ssize_t property_get_buffer(const char *key, void *data, size_t size)
{
  assert(strcmp(key, NIGHT_KEY) == 0 && size == sizeof(saved));
  if (!present) return -ENOENT;
  memcpy(data, saved, size);
  return size;
}
int property_set_buffer(const char *key, const void *data, size_t size)
{
  assert(strcmp(key, NIGHT_KEY) == 0 && size == sizeof(saved));
  if (fail_save) return -EIO;
  memcpy(saved, data, size); present = true; writes++;
  return 0;
}
int property_commit(void) { return 0; }

static struct velaride_night_s state(void)
{
  struct velaride_night_s night;
  velaride_night_get(&night);
  return night;
}
static void wait_saved(void)
{
  struct timespec t = {.tv_nsec = 1000000};
  for (int i = 0; i < 3000; i++)
    {
      if (!state().saving) return;
      nanosleep(&t, NULL);
    }
  assert(!"night save timeout");
}
int main(void)
{
  assert(state().mode == VR_NIGHT_OFF && state().limit == 20);
  assert(velaride_night_set(3, 20) == -EINVAL);
  assert(velaride_night_set(1, 19) == -EINVAL);
  assert(velaride_night_set(1, 41) == -EINVAL);
  assert(velaride_night_set(VR_NIGHT_ON, 25) == 0);
  assert(state().active && velaride_night_brightness(80) == 25);
  assert(velaride_night_brightness(20) == 20);
  velaride_night_exempt(true);
  assert(velaride_night_brightness(80) == 80);
  velaride_night_exempt(false);
  assert(velaride_night_set(VR_NIGHT_OFF, 25) == 0);
  assert(!state().active && velaride_night_brightness(80) == 80);
  velaride_night_set(VR_NIGHT_AUTO, 20);
  velaride_night_poll(100, 1, true, 5);
  velaride_night_poll(3100, 1, true, 5); /* Cached samples cannot trigger. */
  assert(!state().active);
  velaride_night_poll(3100, 2, true, 5);
  assert(state().active);
  velaride_night_poll(4000, 3, true, 50);
  velaride_night_poll(5000, 4, true, 20); /* Band cancels pending change. */
  velaride_night_poll(7100, 5, true, 50);
  assert(state().active);
  velaride_night_poll(10100, 6, true, 50);
  assert(!state().active);
  velaride_night_poll(11000, 7, true, 0);
  velaride_night_poll(15000, 8, false, 0);
  assert(!state().active && !state().light_valid);
  velaride_night_poll(16000, 9, true, 0);
  velaride_night_poll(19000, 10, true, 0);
  assert(state().active);
  velaride_night_poll(20000, 11, true, NAN);
  assert(state().active && !state().light_valid);
  velaride_night_poll(21000, 12, true, -1);
  assert(!state().light_valid);
  velaride_night_set(VR_NIGHT_OFF, 20);
  velaride_night_set(VR_NIGHT_AUTO, 20);
  velaride_night_poll(UINT32_MAX - 1499, 13, true, 1);
  velaride_night_poll(1500, 14, true, 1);
  assert(state().active); /* Millisecond wrap. */
  velaride_night_exempt(true);
  velaride_night_poll(2000, 15, true, 200);
  velaride_night_poll(6000, 16, true, 200);
  assert(state().active);
  velaride_night_exempt(false);
  velaride_night_poll(7000, 16, true, 200);
  assert(state().active);
  wait_saved();
  unsigned int before = writes;
  velaride_night_set(VR_NIGHT_ON, 25);
  velaride_night_set(VR_NIGHT_ON, 30);
  velaride_night_set(VR_NIGHT_ON, 35);
  wait_saved();
  assert(writes == before + 1 && state().save_error == 0);
  pthread_mutex_lock(&g_night_lock); g_loaded = false; pthread_mutex_unlock(&g_night_lock);
  assert(state().mode == VR_NIGHT_ON && state().limit == 35 && state().active);
  fail_save = true;
  velaride_night_set(VR_NIGHT_OFF, 35); wait_saved();
  assert(state().save_error == -EIO && !state().active);
  fail_save = false;
  velaride_night_set(VR_NIGHT_OFF, 35); wait_saved();
  assert(state().save_error == 0);
  saved[5] ^= 1;
  pthread_mutex_lock(&g_night_lock); g_loaded = false; pthread_mutex_unlock(&g_night_lock);
  assert(state().mode == VR_NIGHT_OFF && state().limit == 20 && !state().active);
  puts("PASS: manual/auto, fresh samples, hysteresis, dwell, invalid/stale light, wrap, brightness/exemption/restore, debounce and persistence");
}
