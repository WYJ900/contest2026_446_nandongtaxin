/****************************************************************************
 * Non-blocking application sound service. One bounded request at a time;
 * priority/preemption and cancellation prevent stale countdown speech.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifdef __NuttX__
#include <sched.h>
#endif
#include <kvdb.h>
#include <arch/board/watch_audio.h>
#include "velaride_sound.h"

#define SOUND_KEY "persist.velaride.sound"

struct sound_play_s
{
  uint32_t epoch;
  uint64_t deadline;
  bool voice;
};

static pthread_mutex_t g_sound_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_sound_cond = PTHREAD_COND_INITIALIZER;
static struct velaride_sound_s g_sound;
static bool g_sound_loaded;
static bool g_sound_worker;
static bool g_sound_dirty;
static uint32_t g_sound_epoch;
static uint32_t g_sound_config;
static uint64_t g_sound_expires;
static uint64_t g_sound_save_after;

/* 0.5-dB steps approximating amplitude percentage; no runtime logarithms. */
static const int8_t g_sound_attenuation[101] =
{
  -72, -72, -68, -61, -56, -52, -49, -46, -44, -42, -40, -38,
  -37, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -26,
  -25, -24, -23, -23, -22, -22, -21, -20, -20, -19, -19, -18,
  -18, -17, -17, -16, -16, -15, -15, -15, -14, -14, -13, -13,
  -13, -12, -12, -12, -11, -11, -11, -10, -10, -10, -9, -9,
  -9, -9, -8, -8, -8, -7, -7, -7, -7, -6, -6, -6,
  -6, -5, -5, -5, -5, -5, -4, -4, -4, -4, -3, -3,
  -3, -3, -3, -2, -2, -2, -2, -2, -1, -1, -1, -1,
  -1, -1, 0, 0, 0,
};

int velaride_sound_gain(uint8_t volume, bool voice)
{
  int gain;
  if (volume == 0 || volume > 100) return HUANGSHAN_AUDIO_MUTED;
  gain = g_sound_attenuation[volume] + (voice ? 6 : 0);
  return gain < -72 ? -72 : gain;
}

static uint64_t sound_now(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool sound_voice(int cue)
{
  return cue > VR_SOUND_NONE && cue < VR_SOUND_CUE_COUNT &&
         cue != VR_SOUND_ALARM && cue != VR_SOUND_COUNTDOWN;
}

static int sound_priority(int cue)
{
  if (cue == VR_SOUND_ALARM) return 100;
  if (cue == VR_SOUND_COUNTDOWN) return 80;
  if (cue >= VR_SOUND_THREE && cue <= VR_SOUND_GO) return 50;
  return sound_voice(cue) ? 10 : 0;
}

static uint32_t sound_crc(const uint8_t *data)
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

static void sound_load_locked(void)
{
  uint8_t disk[12];
  uint32_t crc;
  if (g_sound_loaded) return;
  g_sound_loaded = true;
  g_sound.enabled = false;
  g_sound.voice = true;
  g_sound.rings = true;
  g_sound.volume = 35;
  if (property_get_buffer(SOUND_KEY, disk, sizeof(disk)) != sizeof(disk))
    {
      return;
    }
  crc = (uint32_t)disk[8] | (uint32_t)disk[9] << 8 |
        (uint32_t)disk[10] << 16 | (uint32_t)disk[11] << 24;
  if (((memcmp(disk, "VRS1", 4) == 0 && disk[7] <= 1) ||
       (memcmp(disk, "VRS2", 4) == 0 && disk[7] <= 100)) &&
      disk[4] <= 1 && disk[5] <= 1 && disk[6] <= 1 &&
      crc == sound_crc(disk))
    {
      g_sound.enabled = disk[4];
      g_sound.voice = disk[5];
      g_sound.rings = disk[6];
      g_sound.volume = memcmp(disk, "VRS1", 4) == 0 ?
                       (disk[7] ? 100 : 35) : disk[7];
    }
}

static bool sound_cancelled(void *arg)
{
  struct sound_play_s *play = arg;
  bool cancelled;
  pthread_mutex_lock(&g_sound_lock);
  cancelled = play->epoch != g_sound_epoch || !g_sound.enabled ||
              g_sound.dnd;
  pthread_mutex_unlock(&g_sound_lock);
  return cancelled || sound_now() >= play->deadline;
}

static int sound_live_gain(void *arg)
{
  struct sound_play_s *play = arg;
  uint8_t volume;
  pthread_mutex_lock(&g_sound_lock);
  volume = g_sound.volume;
  pthread_mutex_unlock(&g_sound_lock);
  return velaride_sound_gain(volume, play->voice);
}

static void sound_save_locked(void)
{
  uint8_t disk[12] = {'V', 'R', 'S', '2', 0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t generation = g_sound_config;
  uint32_t crc;
  int i;
  int ret;
  disk[4] = g_sound.enabled;
  disk[5] = g_sound.voice;
  disk[6] = g_sound.rings;
  disk[7] = g_sound.volume;
  crc = sound_crc(disk);
  for (i = 0; i < 4; i++) disk[8 + i] = (uint8_t)(crc >> (i * 8));
  g_sound_dirty = false;
  pthread_mutex_unlock(&g_sound_lock);
  ret = property_set_buffer(SOUND_KEY, disk, sizeof(disk));
  if (ret == 0) ret = property_commit();
  pthread_mutex_lock(&g_sound_lock);
  if (generation == g_sound_config)
    {
      g_sound.save_error = ret;
      g_sound.saving = false;
    }
}

static void *sound_worker(void *arg)
{
  uint32_t *packed = NULL;
  (void)arg;
  pthread_mutex_lock(&g_sound_lock);
  for (; ; )
    {
      struct sound_play_s play;
      uint32_t samples = 0;
      uint32_t words = 0;
      int cue;
      int repeat;
      int i;
      int ret;
      while (!g_sound_dirty && g_sound.pending == VR_SOUND_NONE)
        {
          pthread_cond_wait(&g_sound_cond, &g_sound_lock);
        }
      if (g_sound_dirty && sound_now() >= g_sound_save_after)
        {
          sound_save_locked();
          continue;
        }
      if (g_sound.pending == VR_SOUND_NONE)
        {
          struct timespec delay = {.tv_sec = 0, .tv_nsec = 20000000};
          pthread_mutex_unlock(&g_sound_lock);
          nanosleep(&delay, NULL);
          pthread_mutex_lock(&g_sound_lock);
          continue;
        }
      cue = g_sound.pending;
      g_sound.pending = VR_SOUND_NONE;
      if (!g_sound.enabled || g_sound.volume == 0 || g_sound.dnd ||
          sound_now() > g_sound_expires)
        {
          g_sound.skipped++;
          continue;
        }
      play.epoch = g_sound_epoch;
      play.deadline = sound_now() + (cue == VR_SOUND_ALARM ? 10000 : 3000);
      play.voice = sound_voice(cue);
      g_sound.active = cue;
      pthread_mutex_unlock(&g_sound_lock);
      if (packed == NULL)
        {
          packed = memalign(32, VELARIDE_SOUND_WORDS * sizeof(*packed));
        }
      ret = packed == NULL ? -ENOMEM :
            velaride_sound_render(cue, packed, VELARIDE_SOUND_WORDS,
                                  &samples, &words);
      repeat = cue == VR_SOUND_ALARM ? 4 : 1;
      for (i = 0; ret == 0 && i < repeat; i++)
        {
          if (sound_cancelled(&play))
            {
              ret = -ECANCELED;
              break;
            }
          ret = huangshan_watch_audio_play(packed, words, samples,
                                           sound_live_gain,
                                           sound_cancelled, &play);
        }
      pthread_mutex_lock(&g_sound_lock);
      g_sound.active = VR_SOUND_NONE;
      g_sound.last_error = ret;
      if (ret == 0) g_sound.completed++;
      else if (ret == -ECANCELED) g_sound.cancelled++;
      printf("VELARIDE_SOUND: cue=%d result=%d completed=%lu cancelled=%lu\n",
             cue, ret, (unsigned long)g_sound.completed,
             (unsigned long)g_sound.cancelled);
    }
  return NULL;
}

#ifdef __NuttX__
static int sound_task(int argc, char *argv[])
{
  (void)argc;
  (void)argv;
  sound_worker(NULL);
  return 0;
}
#endif

static int sound_start_locked(void)
{
  int ret;
  if (g_sound_worker) return 0;
#ifdef __NuttX__
  ret = task_create("vr_sound", 90, 32768, sound_task, NULL);
  ret = ret < 0 ? errno : 0;
#else
  {
    pthread_t worker;
    ret = pthread_create(&worker, NULL, sound_worker, NULL);
    if (ret == 0) pthread_detach(worker);
  }
#endif
  if (ret == 0) g_sound_worker = true;
  return -ret;
}

void velaride_sound_get(struct velaride_sound_s *out)
{
  pthread_mutex_lock(&g_sound_lock);
  sound_load_locked();
  *out = g_sound;
  pthread_mutex_unlock(&g_sound_lock);
}

static int sound_set_locked(bool enabled, bool voice, bool rings,
                            uint8_t volume)
{
  int ret;
  if (enabled == g_sound.enabled && voice == g_sound.voice &&
      rings == g_sound.rings && volume == g_sound.volume &&
      g_sound.save_error == 0) return 0;
  if (!enabled || (!voice && g_sound.voice) ||
      (!rings && g_sound.rings))
    {
      g_sound_epoch++;
      g_sound.pending = VR_SOUND_NONE;
    }
  g_sound.enabled = enabled;
  g_sound.voice = voice;
  g_sound.rings = rings;
  g_sound.volume = volume;
  g_sound_config++;
  g_sound_dirty = true;
  g_sound_save_after = sound_now() + 400;
  g_sound.saving = true;
  g_sound.save_error = 0;
  ret = sound_start_locked();
  if (ret != 0)
    {
      g_sound.save_error = ret;
      g_sound.saving = false;
    }
  pthread_cond_signal(&g_sound_cond);
  return ret;
}

int velaride_sound_set(bool enabled, bool voice, bool rings, uint8_t volume)
{
  int ret;
  if (volume > 100) return -EINVAL;
  pthread_mutex_lock(&g_sound_lock);
  sound_load_locked();
  ret = sound_set_locked(enabled, voice, rings, volume);
  pthread_mutex_unlock(&g_sound_lock);
  return ret;
}

int velaride_sound_set_volume(uint8_t volume)
{
  int ret;
  if (volume > 100) return -EINVAL;
  pthread_mutex_lock(&g_sound_lock);
  sound_load_locked();
  ret = sound_set_locked(g_sound.enabled, g_sound.voice, g_sound.rings,
                         volume);
  pthread_mutex_unlock(&g_sound_lock);
  return ret;
}

int velaride_sound_request(enum velaride_sound_cue_e cue)
{
  int ret = 0;
  int priority = sound_priority(cue);
  if (priority == 0) return -EINVAL;
  pthread_mutex_lock(&g_sound_lock);
  sound_load_locked();
  if (!g_sound.enabled || g_sound.volume == 0 || g_sound.dnd ||
      (sound_voice(cue) ? !g_sound.voice : !g_sound.rings))
    {
      g_sound.skipped++;
      goto out;
    }
  if (priority < sound_priority(g_sound.active) ||
      priority < sound_priority(g_sound.pending))
    {
      g_sound.skipped++;
      ret = -EBUSY;
      goto out;
    }
  ret = sound_start_locked();
  if (ret == 0)
    {
      g_sound_epoch++;
      g_sound.pending = cue;
      g_sound.last_error = 0;
      g_sound_expires = sound_now() + (priority == 50 ? 750 : 2000);
      pthread_cond_signal(&g_sound_cond);
    }
  else
    {
      g_sound.last_error = ret;
    }
out:
  pthread_mutex_unlock(&g_sound_lock);
  return ret;
}

void velaride_sound_stop(enum velaride_sound_cue_e cue)
{
  pthread_mutex_lock(&g_sound_lock);
  if (cue == VR_SOUND_NONE || g_sound.active == (int)cue) g_sound_epoch++;
  if (cue == VR_SOUND_NONE || g_sound.pending == (int)cue)
    {
      g_sound.pending = VR_SOUND_NONE;
    }
  pthread_mutex_unlock(&g_sound_lock);
}

void velaride_sound_stop_voice(void)
{
  pthread_mutex_lock(&g_sound_lock);
  if (sound_voice(g_sound.active)) g_sound_epoch++;
  if (sound_voice(g_sound.pending)) g_sound.pending = VR_SOUND_NONE;
  pthread_mutex_unlock(&g_sound_lock);
}

void velaride_sound_stop_alerts(void)
{
  pthread_mutex_lock(&g_sound_lock);
  if (sound_priority(g_sound.active) >= 80) g_sound_epoch++;
  if (sound_priority(g_sound.pending) >= 80) g_sound.pending = VR_SOUND_NONE;
  pthread_mutex_unlock(&g_sound_lock);
}

void velaride_sound_set_dnd(bool enabled)
{
  pthread_mutex_lock(&g_sound_lock);
  g_sound.dnd = enabled;
  if (enabled)
    {
      g_sound_epoch++;
      g_sound.pending = VR_SOUND_NONE;
    }
  pthread_mutex_unlock(&g_sound_lock);
}

uint32_t velaride_sound_hardware_state(void)
{
  return huangshan_watch_audio_state();
}
