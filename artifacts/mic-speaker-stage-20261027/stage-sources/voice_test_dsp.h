/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/voice_test_dsp.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Offline 16 kHz voice conditioning for bounded speaker diagnostics.
 ****************************************************************************/

#ifndef __BOARDS_SF32LB52_HUANGSHAN_VOICE_TEST_DSP_H
#define __BOARDS_SF32LB52_HUANGSHAN_VOICE_TEST_DSP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Quiet board captures leave the limiter idle. Raise them in whole steps,
 * at most 12 dB, until the raw peak approaches the target. Louder input,
 * such as the accepted v16 recording, keeps a boost of 1.
 */

#define VOICE_TEST_TARGET_PEAK 1000
#define VOICE_TEST_MAX_BOOST   4

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline int voice_test_boost(int16_t *pcm, size_t count)
{
  int32_t peak = 0;
  int32_t magnitude;
  int boost;
  size_t i;

  for (i = 0; i < count; i++)
    {
      magnitude = pcm[i] < 0 ? -pcm[i] : pcm[i];
      if (magnitude > peak)
        {
          peak = magnitude;
        }
    }

  boost = peak > 0 ? VOICE_TEST_TARGET_PEAK / peak : VOICE_TEST_MAX_BOOST;
  if (boost < 1)
    {
      boost = 1;
    }
  else if (boost > VOICE_TEST_MAX_BOOST)
    {
      boost = VOICE_TEST_MAX_BOOST;
    }

  /* A boost above 1 keeps boost * peak <= 1000, far from the int16 limit. */

  for (i = 0; boost > 1 && i < count; i++)
    {
      pcm[i] = pcm[i] * boost;
    }

  return boost;
}

static inline int32_t voice_test_process(int16_t *pcm, size_t count,
                                         int gain)
{
  static const uint16_t gains[31] =
  {
    258, 290, 325, 365, 410, 460, 516, 579,
    649, 728, 817, 917, 1029, 1154, 1295, 1453,
    1631, 1830, 2053, 2303, 2584, 2900, 3254, 3651,
    4096, 4596, 5157, 5786, 6492, 7284, 8173
  };

  int32_t previous_in = 0;
  int32_t previous_out = 0;
  int32_t limiter_in = 0;
  int32_t limiter_out = 0;
  int32_t peak = 0;
  int32_t input;
  int32_t sample;
  int32_t magnitude;
  int32_t excess;
  size_t fade;
  size_t i;

  /* Same high-pass, soft knee and final peak ceiling as musicusb. The
   * extra fixed drive compensates the quiet microphone without automatic
   * gain chasing silence. Gain is in the HAL's half-dB units, -48..12.
   */

  for (i = 0; i < count; i++)
    {
      input = pcm[i];
      sample = (int64_t)(previous_out + input - previous_in) *
               30532 / 32768;
      previous_in = input;
      previous_out = sample;
      sample = (int64_t)sample * gains[gain / 2 + 24] * 8 / 4096;
      magnitude = sample < 0 ? -sample : sample;
      if (magnitude > 3000)
        {
          excess = magnitude - 3000;
          magnitude = 3000 + (int64_t)3000 * excess / (3000 + excess);
          sample = sample < 0 ? -magnitude : magnitude;
        }

      input = sample;
      sample = (int64_t)(limiter_out + input - limiter_in) *
               32512 / 32768;
      limiter_in = input;
      limiter_out = sample;
      if (sample > 5999)
        {
          sample = 5999;
        }
      else if (sample < -5999)
        {
          sample = -5999;
        }

      fade = i < count - 1 - i ? i : count - 1 - i;
      if (fade < 320)
        {
          sample = sample * (int32_t)fade / 320;
        }

      pcm[i] = sample;
      magnitude = sample < 0 ? -sample : sample;
      if (magnitude > peak)
        {
          peak = magnitude;
        }
    }

  return peak;
}

#endif /* __BOARDS_SF32LB52_HUANGSHAN_VOICE_TEST_DSP_H */
