#define _POSIX_C_SOURCE 200809L
#include "../../app/velaride/velaride_sound.c"
#include <assert.h>
#include <stdlib.h>

static uint8_t saved[12];
static bool have_saved;
static bool fail_save;
static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int backend_calls;
static bool backend_busy;
static unsigned int backend_ms = 100;
static unsigned int cleanup_ms;
static int last_gain;
static unsigned int saves;

ssize_t property_get_buffer(const char *key, void *data, size_t size)
{
  assert(strcmp(key, SOUND_KEY) == 0 && size == sizeof(saved));
  if (!have_saved) return -ENOENT;
  memcpy(data, saved, size);
  return size;
}
int property_set_buffer(const char *key, const void *data, size_t size)
{
  assert(strcmp(key, SOUND_KEY) == 0 && size == sizeof(saved));
  if (fail_save) return -EIO;
  saves++;
  memcpy(saved, data, size);
  have_saved = true;
  return 0;
}
int property_commit(void) { return 0; }
uint32_t huangshan_watch_audio_state(void) { return 0; }

static void delay(unsigned int ms)
{
  struct timespec t = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L};
  nanosleep(&t, NULL);
}

int huangshan_watch_audio_play(const uint32_t *packed, uint32_t words,
                              uint32_t samples, huangshan_audio_gain_t gain,
                              huangshan_audio_cancel_t cancelled, void *arg)
{
  unsigned int duration;
  unsigned int cleanup;
  bool busy;
  assert(((uintptr_t)packed & 31) == 0 && samples > 0 && samples <= 32000);
  assert(words * 2 >= samples + 512);
  pthread_mutex_lock(&fake_lock);
  backend_calls++;
  duration = backend_ms;
  cleanup = cleanup_ms;
  busy = backend_busy;
  pthread_mutex_unlock(&fake_lock);
  if (busy) return -EBUSY;
  for (unsigned int i = 0; i < duration; i += 5)
    {
      if (cancelled(arg))
        {
          delay(cleanup);
          return -ECANCELED;
        }
      int value = gain(arg);
      assert(value == HUANGSHAN_AUDIO_MUTED || (value >= -72 && value <= 6));
      pthread_mutex_lock(&fake_lock); last_gain = value; pthread_mutex_unlock(&fake_lock);
      delay(5);
    }
  return 0;
}

static unsigned int calls(void)
{
  unsigned int n;
  pthread_mutex_lock(&fake_lock); n = backend_calls; pthread_mutex_unlock(&fake_lock);
  return n;
}

static struct velaride_sound_s settled(bool idle)
{
  struct velaride_sound_s state;
  for (int i = 0; i < 4000; i++)
    {
      velaride_sound_get(&state);
      if (!state.saving && (!idle || (!state.active && !state.pending))) return state;
      delay(1);
    }
  assert(!"sound worker timed out");
  return state;
}

static void active(int cue)
{
  struct velaride_sound_s state;
  for (int i = 0; i < 2000; i++)
    {
      velaride_sound_get(&state);
      if (state.active == cue) return;
      delay(1);
    }
  assert(!"cue did not start");
}

static void renderer_test(void)
{
  uint32_t *packed = memalign(32, VELARIDE_SOUND_WORDS * 4);
  uint32_t count;
  uint32_t words;
  assert(packed);
  for (int cue = 1; cue < VR_SOUND_CUE_COUNT; cue++)
    {
      for (int pass = 0; pass < 1; pass++)
        {
          assert(velaride_sound_render(cue, packed, VELARIDE_SOUND_WORDS, &count, &words) == 0);
          assert(count <= 32000 && words <= VELARIDE_SOUND_WORDS && words * 2 >= count + 512);
          unsigned int nonzero = 0;
          for (unsigned int i = 0; i < words * 2; i++)
            {
              int16_t sample = (int16_t)(packed[i / 2] >> ((i & 1) * 16));
              assert(abs(sample) <= 6000);
              if (i >= count) assert(sample == 0);
              if (sound_voice(cue) && i < VELARIDE_SOUND_VOICE_LEAD)
                assert(sample == 0);
              nonzero += sample != 0;
            }
          assert(nonzero > 100);
          assert((int16_t)packed[0] == 0);
          assert((int16_t)(packed[(count - 1) / 2] >> (((count - 1) & 1) * 16)) == 0);
        }
    }
  assert(velaride_sound_render(VR_SOUND_NONE, packed, VELARIDE_SOUND_WORDS, &count, &words) == -EINVAL);
  assert(velaride_sound_set_volume(101) == -EINVAL);
  assert(velaride_sound_render(VR_SOUND_ALARM, packed, 1, &count, &words) == -ENOSPC);
  free(packed);
}

static void wait_gain(int expected)
{
  for (int i = 0; i < 1000; i++)
    {
      pthread_mutex_lock(&fake_lock);
      int value = last_gain;
      pthread_mutex_unlock(&fake_lock);
      if (value == expected) return;
      delay(1);
    }
  assert(!"live gain did not change");
}

int main(void)
{
  struct velaride_sound_s state;
  unsigned int n;
  renderer_test();
  assert(velaride_sound_gain(0, true) == HUANGSHAN_AUDIO_MUTED);
  assert(velaride_sound_gain(101, true) == HUANGSHAN_AUDIO_MUTED);
  assert(velaride_sound_gain(100, true) == 6);
  assert(velaride_sound_gain(100, false) == 0);
  for (int percent = 2; percent <= 100; percent++)
    {
      assert(velaride_sound_gain(percent, true) >=
             velaride_sound_gain(percent - 1, true));
      assert(velaride_sound_gain(percent, true) <= 6);
    }
  velaride_sound_get(&state);
  assert(!state.enabled && state.voice && state.rings && state.volume == 35);
  assert(velaride_sound_request(VR_SOUND_ALARM) == 0);
  delay(20); assert(calls() == 0);

  assert(velaride_sound_set(true, false, true, 100) == 0);
  settled(true);
  velaride_sound_request(VR_SOUND_RIDE_MODE);
  delay(20); assert(calls() == 0);
  velaride_sound_request(VR_SOUND_ALARM);
  active(VR_SOUND_ALARM);
  assert(velaride_sound_request(VR_SOUND_THREE) == 0); /* Voice disabled. */
  state = settled(true);
  assert(calls() == 4 && state.completed == 1); /* Finite ring, four buffers. */

  assert(velaride_sound_set(true, true, true, 100) == 0);
  settled(true);
  velaride_sound_request(VR_SOUND_ALARM);
  active(VR_SOUND_ALARM);
  assert(velaride_sound_request(VR_SOUND_THREE) == -EBUSY);
  velaride_sound_set(false, true, true, 100);
  state = settled(true);
  assert(state.cancelled == 1 && !state.enabled);

  velaride_sound_set(true, true, true, 100);
  settled(true);
  velaride_sound_request(VR_SOUND_THREE);
  active(VR_SOUND_THREE);
  velaride_sound_stop_voice();
  state = settled(true);
  assert(state.cancelled == 2);
  velaride_sound_request(VR_SOUND_GO);
  active(VR_SOUND_GO);
  velaride_sound_set_dnd(true);
  state = settled(true);
  assert(state.cancelled == 3);
  n = calls();
  velaride_sound_request(VR_SOUND_ALARM);
  delay(20); assert(calls() == n);
  velaride_sound_set_dnd(false);

  /* A delayed old clip must not allow stale countdown speech to catch up. */
  pthread_mutex_lock(&fake_lock); cleanup_ms = 1000; pthread_mutex_unlock(&fake_lock);
  velaride_sound_request(VR_SOUND_RIDE_MODE);
  active(VR_SOUND_RIDE_MODE);
  n = calls();
  velaride_sound_request(VR_SOUND_TWO);
  state = settled(true);
  assert(calls() == n && state.skipped > 0);
  pthread_mutex_lock(&fake_lock); cleanup_ms = 0; backend_busy = true; pthread_mutex_unlock(&fake_lock);
  velaride_sound_request(VR_SOUND_GO);
  state = settled(true);
  assert(state.last_error == -EBUSY);
  pthread_mutex_lock(&fake_lock); backend_busy = false; pthread_mutex_unlock(&fake_lock);

  velaride_sound_set(true, true, true, 35);
  settled(true);
  velaride_sound_request(VR_SOUND_RIDE_MODE);
  assert(settled(true).last_error == 0); /* Lower volume remains a valid playback request. */

  fail_save = true;
  velaride_sound_set(false, true, true, 35);
  state = settled(true);
  assert(!state.enabled && state.save_error == -EIO);
  fail_save = false;
  velaride_sound_set(false, false, true, 35);
  assert(settled(true).save_error == 0);
  pthread_mutex_lock(&g_sound_lock); g_sound_loaded = false; pthread_mutex_unlock(&g_sound_lock);
  velaride_sound_get(&state);
  assert(!state.enabled && !state.voice && state.rings && state.volume == 35);
  saved[7] ^= 1;
  pthread_mutex_lock(&g_sound_lock); g_sound_loaded = false; pthread_mutex_unlock(&g_sound_lock);
  velaride_sound_get(&state);
  assert(!state.enabled && state.voice && state.rings);
  /* Migrate a saved VRS1 normal preset without losing mute/sub-options. */
  memcpy(saved, "VRS1", 4);
  saved[4] = 0; saved[5] = 1; saved[6] = 1; saved[7] = 1;
  uint32_t crc = sound_crc(saved);
  for (int i = 0; i < 4; i++) saved[8 + i] = (uint8_t)(crc >> (8 * i));
  pthread_mutex_lock(&g_sound_lock); g_sound_loaded = false; pthread_mutex_unlock(&g_sound_lock);
  velaride_sound_get(&state);
  assert(!state.enabled && state.volume == 100);

  velaride_sound_set(true, true, true, 100);
  settled(true);
  pthread_mutex_lock(&fake_lock); backend_ms = 1000; pthread_mutex_unlock(&fake_lock);
  n = calls();
  velaride_sound_request(VR_SOUND_RIDE_MODE);
  active(VR_SOUND_RIDE_MODE);
  wait_gain(6);
  velaride_sound_set_volume(30);
  wait_gain(velaride_sound_gain(30, true));
  velaride_sound_set_volume(0);
  wait_gain(HUANGSHAN_AUDIO_MUTED);
  velaride_sound_get(&state);
  assert(state.active == VR_SOUND_RIDE_MODE); /* Zero does not restart it. */
  velaride_sound_set_volume(80);
  wait_gain(velaride_sound_gain(80, true));
  state = settled(true);
  assert(calls() == n + 1 && state.last_error == 0);
  unsigned int writes = saves;
  velaride_sound_set_volume(40);
  velaride_sound_set_volume(45);
  velaride_sound_set_volume(50);
  settled(true);
  assert(saves == writes + 1 && memcmp(saved, "VRS2", 4) == 0);
  pthread_mutex_lock(&g_sound_lock); g_sound_loaded = false; pthread_mutex_unlock(&g_sound_lock);
  velaride_sound_get(&state);
  assert(state.volume == 50);
  velaride_sound_set_volume(0);
  settled(true);
  n = calls();
  velaride_sound_request(VR_SOUND_GO);
  delay(50);
  assert(calls() == n);
  puts("PASS: PCM bounds, percentage gain, live volume/mute/resume without restart, debounce, VRS1 migration/VRS2 reload, priority and cancellation");
  return 0;
}
