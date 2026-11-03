/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>
#include <errno.h>
#include <pthread.h>
#include "velaride_countdown.h"
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
#include "velaride_sound.h"
#endif

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static enum velaride_countdown_state_e g_state;
static uint32_t g_duration_s = 5 * 60;
static uint32_t g_remaining_ms = 5 * 60 * 1000;
static uint32_t g_started;
static uint32_t g_completions;
static bool g_pending;

static uint32_t update_locked(uint32_t now)
{
  uint32_t elapsed;

  if (g_state != VELARIDE_COUNTDOWN_RUNNING)
    {
      return g_remaining_ms;
    }

  elapsed = now - g_started;
  if (elapsed >= g_remaining_ms)
    {
      g_remaining_ms = 0;
      g_state = VELARIDE_COUNTDOWN_DONE;
      g_pending = true;
      g_completions++;
      return 0;
    }

  return g_remaining_ms - elapsed;
}

int velaride_countdown_set(uint32_t seconds)
{
  int ret = 0;

  if (seconds == 0 || seconds > 180 * 60)
    {
      return -ERANGE;
    }

  pthread_mutex_lock(&g_lock);
  if (g_state == VELARIDE_COUNTDOWN_RUNNING ||
      g_state == VELARIDE_COUNTDOWN_PAUSED)
    {
      ret = -EBUSY;
    }
  else
    {
      g_duration_s = seconds;
      g_remaining_ms = seconds * 1000;
      g_state = VELARIDE_COUNTDOWN_IDLE;
      g_pending = false;
    }

  pthread_mutex_unlock(&g_lock);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  if (ret == 0) velaride_sound_stop(VR_SOUND_COUNTDOWN);
#endif
  return ret;
}

void velaride_countdown_start(uint32_t now)
{
  pthread_mutex_lock(&g_lock);
  if (g_state != VELARIDE_COUNTDOWN_RUNNING)
    {
      if (g_state != VELARIDE_COUNTDOWN_PAUSED)
        {
          g_remaining_ms = g_duration_s * 1000;
        }

      g_started = now;
      g_pending = false;
      g_state = VELARIDE_COUNTDOWN_RUNNING;
    }

  pthread_mutex_unlock(&g_lock);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop(VR_SOUND_COUNTDOWN);
#endif
}

void velaride_countdown_pause(uint32_t now)
{
  uint32_t remaining;

  pthread_mutex_lock(&g_lock);
  remaining = update_locked(now);
  if (g_state == VELARIDE_COUNTDOWN_RUNNING)
    {
      g_remaining_ms = remaining;
      g_state = VELARIDE_COUNTDOWN_PAUSED;
    }

  pthread_mutex_unlock(&g_lock);
}

void velaride_countdown_cancel(void)
{
  pthread_mutex_lock(&g_lock);
  g_state = VELARIDE_COUNTDOWN_IDLE;
  g_remaining_ms = g_duration_s * 1000;
  g_pending = false;
  pthread_mutex_unlock(&g_lock);
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  velaride_sound_stop(VR_SOUND_COUNTDOWN);
#endif
}

void velaride_countdown_get(uint32_t now, struct velaride_countdown_s *out)
{
  pthread_mutex_lock(&g_lock);
  out->remaining_ms = update_locked(now);
  out->state = g_state;
  out->duration_s = g_duration_s;
  out->completions = g_completions;
  pthread_mutex_unlock(&g_lock);
}

bool velaride_countdown_take_done(uint32_t now)
{
  bool pending;

  pthread_mutex_lock(&g_lock);
  update_locked(now);
  pending = g_pending;
  g_pending = false;
  pthread_mutex_unlock(&g_lock);
  return pending;
}
