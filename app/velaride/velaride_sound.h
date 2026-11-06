/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_SOUND_H
#define __APPS_EXAMPLES_VELARIDE_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VELARIDE_SOUND_WORDS 16384
#define VELARIDE_SOUND_VOICE_LEAD 1920 /* 120 ms before speech samples. */

enum velaride_sound_cue_e
{
  VR_SOUND_NONE = 0,
  VR_SOUND_RIDE_MODE,
  VR_SOUND_THREE,
  VR_SOUND_TWO,
  VR_SOUND_ONE,
  VR_SOUND_GO,
  VR_SOUND_ALARM,
  VR_SOUND_COUNTDOWN,
  VR_SOUND_ALARM_MODE,
  VR_SOUND_COUNTDOWN_MODE,
  VR_SOUND_STOPWATCH_MODE,
  VR_SOUND_SAFETY,            /* Fall check: rings through DND and mute. */
  VR_SOUND_CUE_COUNT
};

struct velaride_sound_s
{
  bool enabled;
  bool voice;
  bool rings;
  bool dnd;
  bool saving;
  uint8_t volume;             /* 0..100 percent; zero is digitally muted. */
  int active;
  int pending;
  int last_error;
  int save_error;
  uint32_t completed;
  uint32_t cancelled;
  uint32_t skipped;
};

void velaride_sound_get(struct velaride_sound_s *out);
int velaride_sound_set(bool enabled, bool voice, bool rings, uint8_t volume);
int velaride_sound_set_volume(uint8_t volume);
int velaride_sound_gain(uint8_t volume, bool voice);
int velaride_sound_request(enum velaride_sound_cue_e cue);
void velaride_sound_stop(enum velaride_sound_cue_e cue);
void velaride_sound_stop_voice(void);
void velaride_sound_stop_alerts(void);
void velaride_sound_set_dnd(bool enabled);
uint32_t velaride_sound_hardware_state(void);
int velaride_sound_render(enum velaride_sound_cue_e cue,
                          uint32_t *packed, size_t capacity,
                          uint32_t *samples, uint32_t *words);

#endif
