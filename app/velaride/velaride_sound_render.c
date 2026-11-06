/****************************************************************************
 * Bounded offline PCM renderer: no allocations, I/O or hardware access.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <errno.h>
#include <string.h>
#include "velaride_sound_assets.h"

static const int16_t g_sine[64] =
{
  0, 490, 976, 1451, 1913, 2357, 2778, 3172,
  3536, 3865, 4157, 4410, 4619, 4785, 4904, 4976,
  5000, 4976, 4904, 4785, 4619, 4410, 4157, 3865,
  3536, 3172, 2778, 2357, 1913, 1451, 976, 490,
  0, -490, -976, -1451, -1913, -2357, -2778, -3172,
  -3536, -3865, -4157, -4410, -4619, -4785, -4904, -4976,
  -5000, -4976, -4904, -4785, -4619, -4410, -4157, -3865,
  -3536, -3172, -2778, -2357, -1913, -1451, -976, -490
};

int velaride_sound_render(enum velaride_sound_cue_e cue,
                          uint32_t *packed, size_t capacity,
                          uint32_t *samples, uint32_t *words)
{
  static const uint16_t melody[] = {660, 880, 784, 880};
  const int16_t *voice = NULL;
  uint32_t count = 0;
  uint32_t lead = 0;
  uint32_t length;
  uint32_t phase = 0;
  uint32_t i;
  if (packed == NULL || samples == NULL || words == NULL)
    {
      return -EINVAL;
    }

  if (cue == VR_SOUND_SAFETY)
    {
      cue = VR_SOUND_ALARM;
    }

  if (cue == VR_SOUND_ALARM || cue == VR_SOUND_COUNTDOWN)
    {
      count = cue == VR_SOUND_ALARM ? 32000 : 14400;
    }
  else
    {
      for (i = 0; i < g_velaride_voice_clip_count; i++)
        {
          if (g_velaride_voice_clips[i].cue == cue)
            {
              voice = g_velaride_voice_clips[i].pcm;
              lead = VELARIDE_SOUND_VOICE_LEAD;
              count = g_velaride_voice_clips[i].count + lead;
              break;
            }
        }
    }

  if (count == 0 || count > 32000)
    {
      return -EINVAL;
    }

  length = ((count + 512 + 15) / 16) * 8;
  if (capacity < length)
    {
      return -ENOSPC;
    }

  memset(packed, 0, length * sizeof(*packed));
  for (i = 0; i < count; i++)
    {
      int32_t sample;
      if (voice != NULL)
        {
          sample = i < lead ? 0 : voice[i - lead];
        }
      else
        {
          uint32_t within = i % 4000;
          uint32_t envelope = within < 160 ? within :
                              within >= 2880 ? 0 :
                              within > 2720 ? 2880 - within : 160;
          uint32_t hz = melody[(i / 4000) % 4];
          phase = (phase + hz * 64) % (16000 * 64);
          sample = g_sine[phase / 16000] * (int32_t)envelope / 160;
          if (count - 1 - i < 160)
            {
              sample = sample * (int32_t)(count - 1 - i) / 160;
            }
        }

      if (sample > 6000) sample = 6000;
      if (sample < -6000) sample = -6000;
      packed[i / 2] |= (uint32_t)(uint16_t)sample << ((i & 1) * 16);
    }

  *samples = count;
  *words = length;
  return 0;
}
