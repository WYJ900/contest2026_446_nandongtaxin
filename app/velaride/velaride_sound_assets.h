/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_SOUND_ASSETS_H
#define __APPS_EXAMPLES_VELARIDE_SOUND_ASSETS_H
#include "velaride_sound.h"
struct velaride_voice_clip_s
{
  enum velaride_sound_cue_e cue;
  const int16_t *pcm;
  uint32_t count;
};
extern const struct velaride_voice_clip_s g_velaride_voice_clips[];
extern const unsigned int g_velaride_voice_clip_count;
#endif
