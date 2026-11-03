/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/speaker_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Huangshan Pi CN2 speaker diagnostic. DAC1 -> NS4150B; PA42 = CTRL.
 * Uses the vendor DAC HAL and its shared DMA allocator. This is a bounded
 * board test, not a general-purpose NuttX audio lower-half driver.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/cache.h>

#include "bf0_hal.h"
#include "audio_test.h"
#include "voice_test_dsp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPEAKER_RATE       16000
#define SPEAKER_SAMPLES    512
#define SPEAKER_PA_PIN     42
#define SPEAKER_VOLUME     (-24) /* HAL units are 0.5 dB: -12 dB. */
#define MUSIC_WORDS        2048
#define MUSIC_HALF_WORDS   (MUSIC_WORDS / 2)
#define MUSIC_BEAT_SAMPLES (SPEAKER_RATE / 2)
#define MUSIC_SAMPLES      (32 * MUSIC_BEAT_SAMPLES)
#define STEADY_SAMPLES     (SPEAKER_RATE * 4)
#define SPEAKER_DAC_ERRORS (AUDCODEC_IRQ_DAC_CH0_OUT_UF | \
                            AUDCODEC_IRQ_DAC_CH0_APB_OF | \
                            AUDCODEC_IRQ_DAC_CH0_STB_OF)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Fixed SRAM storage: DMA must not depend on the caller's PSRAM stack.
 * The mutex owns the single on-chip DAC and this buffer for the whole test.
 */

static pthread_mutex_t g_speaker_lock = PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_speaker_pcm[SPEAKER_SAMPLES]
  __attribute__((aligned(32)));
static uint32_t g_music_pcm[MUSIC_WORDS] __attribute__((aligned(32)));

static const int16_t g_sine[64] =
{
  0, 588, 1171, 1742, 2296, 2828, 3333, 3806,
  4243, 4638, 4989, 5292, 5543, 5742, 5885, 5971,
  6000, 5971, 5885, 5742, 5543, 5292, 4989, 4638,
  4243, 3806, 3333, 2828, 2296, 1742, 1171, 588,
  0, -588, -1171, -1742, -2296, -2828, -3333, -3806,
  -4243, -4638, -4989, -5292, -5543, -5742, -5885, -5971,
  -6000, -5971, -5885, -5742, -5543, -5292, -4989, -4638,
  -4243, -3806, -3333, -2828, -2296, -1742, -1171,  -588
};

/* 16 kHz XTAL configuration from SiFli drv_audcodec_m.c.
 * The board uses the HAL's default 3.3 V analog supply configuration.
 */

static AUDCODE_DAC_CLK_CONFIG_TYPE g_dac_clock =
{
  .samplerate         = SPEAKER_RATE,
  .clk_src_sel        = 0,
  .clk_div            = 1,
  .osr_sel            = 4,
  .sinc_gain          = 0x14d,
  .sel_clk_dac_source = 0,
  .diva_clk_dac       = 5,
  .diva_clk_chop_dac  = 4,
  .divb_clk_chop_dac  = 2,
  .diva_clk_chop_bg   = 20,
  .diva_clk_chop_refgen = 20,
  .sel_clk_dac        = 0
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void speaker_delay(unsigned int milliseconds)
{
  struct timespec delay;

  delay.tv_sec  = milliseconds / 1000;
  delay.tv_nsec = (milliseconds % 1000) * 1000000L;
  while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
    {
    }
}

static void speaker_pa(bool enable)
{
  HAL_GPIO_WritePin(hwp_gpio1, SPEAKER_PA_PIN,
                    enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void speaker_fill(unsigned int hz, bool pcm24)
{
  unsigned int step = hz / 250;
  unsigned int words = pcm24 ? SPEAKER_SAMPLES : SPEAKER_SAMPLES / 2;
  unsigned int i;
  uint16_t first;
  uint16_t second;

  for (i = 0; i < words; i++)
    {
      if (pcm24)
        {
          g_speaker_pcm[i] =
            (uint32_t)((int32_t)g_sine[(i * step) % 64] * 256) & 0xffffff;
          continue;
        }

      first  = (uint16_t)g_sine[(2 * i * step) % 64];
      second = (uint16_t)g_sine[((2 * i + 1) * step) % 64];
      g_speaker_pcm[i] = (uint32_t)first | ((uint32_t)second << 16);
    }
}

static int speaker_tone(FAR AUDCODEC_HandleTypeDef *codec,
                        FAR DMA_HandleTypeDef *dma,
                        unsigned int hz, unsigned int duration, int gain,
                        bool pcm24)
{
  uint32_t remaining;
  uint32_t next;
  uint32_t words = 0;
  uint32_t flags;
  unsigned int updates = 0;
  unsigned int stalls = 0;
  unsigned int elapsed;
  unsigned int samples_per_word = pcm24 ? 1 : 2;
  unsigned int count = SPEAKER_SAMPLES / samples_per_word;
  bool internal = (hwp_audcodec->CFG & AUDCODEC_CFG_DAC_1K_MODE) != 0;
  struct timespec start;
  struct timespec finish;
  long milliseconds;
  int volume;
  int ret = 0;

  if (internal)
    {
      /* Keep the APB DMA path supplied with zeros while selecting the
       * hardware sine source. This separates PCM contents from the DAC
       * generator without relying on an empty APB FIFO for timing.
       */

      memset(g_speaker_pcm, 0, sizeof(g_speaker_pcm));
    }
  else
    {
      speaker_fill(hz, pcm24);
    }

  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CFG |= AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CFG &= ~AUDCODEC_DAC_CFG_PATH_RESET;
  if (pcm24)
    {
      hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
    }
  else
    {
      hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
    }

  hwp_audcodec->IRQ = SPEAKER_DAC_ERRORS;
  HAL_AUDCODEC_Config_DACPath(codec, 1);
  HAL_AUDCODEC_Config_DACPath_Volume(codec, 0, -72);
  printf("speaker_test: pcm=%08lx,%08lx,%08lx,%08lx\n",
         (unsigned long)g_speaker_pcm[0],
         (unsigned long)g_speaker_pcm[1],
         (unsigned long)g_speaker_pcm[2],
         (unsigned long)g_speaker_pcm[3]);

  /* The SDK cache helper only recognizes PSRAM. Use NuttX's cache API for
   * the source range, including SRAM with bootloader-dependent attributes.
   * Polling avoids changing another peripheral's interrupt handler.
   */

  up_clean_dcache((uintptr_t)g_speaker_pcm,
                  (uintptr_t)g_speaker_pcm + count * sizeof(uint32_t));
  if (HAL_DMA_Start(dma, (uint32_t)g_speaker_pcm,
                    (uint32_t)&hwp_audcodec->DAC_CH0_ENTRY,
                    count) != HAL_OK)
    {
      return -EBUSY;
    }

  /* A recycled channel can retain its previous owner's interrupt bits.
   * Requests are still disabled here, so clear them before feeding the DAC.
   */

  __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
  __HAL_AUDCODEC_DAC_ENABLE(codec);
  HAL_AUDCODEC_Config_DACPath(codec, 0);

  remaining = dma->Instance->CNDTR;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (elapsed = 0; elapsed < duration; elapsed += 5)
    {
      speaker_delay(5);
      next = dma->Instance->CNDTR;
      if (next != remaining)
        {
          words += (remaining + count - next) % count;
          updates++;
          stalls = 0;
        }
      else if (++stalls >= 10 && !internal)
        {
          ret = -ETIMEDOUT;
          break;
        }

      remaining = next;

      flags = dma->DmaBaseAddress->ISR >> (dma->ChannelIndex & 0x1c);
      if ((flags & DMA_ISR_TEIF1) ||
          (!internal && (hwp_audcodec->IRQ & SPEAKER_DAC_ERRORS)))
        {
          ret = -EIO;
          break;
        }

      if (elapsed <= 40)
        {
          volume = -72 + (gain + 72) * (int)elapsed / 40;
          HAL_AUDCODEC_Config_DACPath_Volume(codec, 0, volume);
        }
    }

  clock_gettime(CLOCK_MONOTONIC, &finish);
  milliseconds = (finish.tv_sec - start.tv_sec) * 1000 +
                 (finish.tv_nsec - start.tv_nsec) / 1000000;
  printf("speaker_test: cfg=%08lx dac=%08lx ch0=%08lx ext=%08lx\n",
         (unsigned long)hwp_audcodec->CFG,
         (unsigned long)hwp_audcodec->DAC_CFG,
         (unsigned long)hwp_audcodec->DAC_CH0_CFG,
         (unsigned long)hwp_audcodec->DAC_CH0_CFG_EXT);
  printf("speaker_test: fifo=%08lx irq=%08lx analog=%08lx pll4=%08lx\n",
         (unsigned long)hwp_audcodec->APB_STAT,
         (unsigned long)hwp_audcodec->IRQ,
         (unsigned long)hwp_audcodec->DAC1_CFG,
         (unsigned long)hwp_audcodec->PLL_CFG4);
  printf("speaker_test: dma=%p ccr=%08lx count=%lu updates=%u\n",
         dma->Instance, (unsigned long)dma->Instance->CCR,
         (unsigned long)remaining, updates);
  printf("speaker_test: source=%08lx destination=%08lx debug=%08lx\n",
         (unsigned long)dma->Instance->CM0AR,
         (unsigned long)dma->Instance->CPAR,
         (unsigned long)hwp_audcodec->DAC_CH0_DEBUG);
  printf("speaker_test: words=%lu elapsed=%ld ms rate=%lu samples/s\n",
         (unsigned long)words, milliseconds,
         milliseconds > 0 ? (unsigned long)words * 1000 *
                            samples_per_word / milliseconds : 0);

  for (volume = gain; volume >= -72; volume -= 12)
    {
      HAL_AUDCODEC_Config_DACPath_Volume(codec, 0, volume);
      speaker_delay(10);
    }

  HAL_AUDCODEC_Config_DACPath(codec, 1);
  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_DMA_Abort(dma);

  if (updates < 10 && ret == 0 && !internal)
    {
      ret = -ETIMEDOUT;
    }

  printf("speaker_test: %u Hz, DMA updates=%u, result=%d\n",
         hz, updates, ret);
  return ret;
}

static int speaker_internal(FAR AUDCODEC_HandleTypeDef *codec,
                             FAR DMA_HandleTypeDef *dma)
{
  int ret;

  /* The hardware generator has its own full-scale source. Keep its gain
   * below the PCM test, regardless of the requested command-line gain.
   */

  hwp_audcodec->CFG |= AUDCODEC_CFG_DAC_1K_MODE;
  printf("speaker_test: INTERNAL 1 kHz, -24 dB, zero DMA, 1500 ms\n");
  ret = speaker_tone(codec, dma, 1000, 1500, -48, true);
  hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;
  return ret;
}

/* Original 32-beat tune: interpolated sine partials with a plucked envelope.
 * Phase increments are round(Hz * 2^32 / 16000), for equal-tempered notes.
 */

static const uint32_t g_music_notes[] =
{
  140459156, 157660072, 176967447, 210450947, 236223201, 265151578
};

static const uint8_t g_music_melody[32] =
{
  2, 3, 4, 3, 2, 1, 0, 1,
  2, 3, 5, 4, 3, 2, 1, 0,
  1, 2, 3, 2, 1, 0, 1, 2,
  0, 2, 3, 4, 3, 2, 1, 0
};

static const uint32_t g_music_chords[4][4] =
{
    {
      70229578, 88483724, 105225474, 88483724
    },

    {
      93745240, 118111601, 140459156, 118111601
    },

    {
      105225474, 132575789, 157660072, 132575789
    },

    {
      70229578, 88483724, 105225474, 140459156
    }
};

static int32_t music_sine(uint32_t phase)
{
  unsigned int index = phase >> 26;
  int32_t fraction = (phase >> 10) & 65535;
  int32_t first = g_sine[index];
  int32_t second = g_sine[(index + 1) & 63];

  return first + (second - first) * fraction / 65536;
}

static int32_t music_voice(uint32_t step, uint32_t age, uint32_t length)
{
  uint32_t phase = step * age;
  int32_t envelope = (length - age) * 32767 / (length + 2 * age);
  int32_t wave;

  if (age < 128)
    {
      envelope = envelope * age / 128;
    }

  if (length - age < 320)
    {
      envelope = envelope * (length - age) / 320;
    }

  wave = music_sine(phase) + music_sine(phase * 2) * 7 / 20 +
         music_sine(phase * 3) / 10;
  return wave * envelope / 32767;
}

static int16_t music_sample(uint32_t index)
{
  uint32_t beat;
  uint32_t age;
  uint32_t chord;
  uint32_t arp;
  int32_t sample;

  if (index >= MUSIC_SAMPLES)
    {
      return 0;
    }

  beat = index / MUSIC_BEAT_SAMPLES;
  age = index % MUSIC_BEAT_SAMPLES;
  chord = (beat / 4) % 4;
  arp = (index / (MUSIC_BEAT_SAMPLES / 2)) % 4;
  sample = music_voice(g_music_notes[g_music_melody[beat]],
                       age, MUSIC_BEAT_SAMPLES);
  sample += music_voice(g_music_chords[chord][arp],
                        index % (MUSIC_BEAT_SAMPLES / 2),
                        MUSIC_BEAT_SAMPLES / 2) / 3;
  return (int16_t)sample;
}

static FAR int16_t *music_prepare(int gain, bool clean, bool usb)
{
  /* Q12 gains for -24 through +6 dB. Clean mode applies gain before its
   * soft limiter and keeps the DAC at unity, so later gain cannot undo
   * the limiter. Leave raw modes available for listening comparisons.
   */

  static const uint16_t gains[31] =
  {
    258, 290, 325, 365, 410, 460, 516, 579,
    649, 728, 817, 917, 1029, 1154, 1295, 1453,
    1631, 1830, 2053, 2303, 2584, 2900, 3254, 3651,
    4096, 4596, 5157, 5786, 6492, 7284, 8173
  };

  FAR int16_t *samples;
  uint32_t index;
  uint32_t fade;
  uint32_t limited = 0;
  int32_t previous_in = 0;
  int32_t previous_out = 0;
  int32_t limiter_in = 0;
  int32_t limiter_out = 0;
  int32_t sample;
  int32_t magnitude;
  int32_t excess;
  int32_t peak = 0;

  samples = malloc(MUSIC_SAMPLES * sizeof(*samples));
  if (samples == NULL)
    {
      return NULL;
    }

  for (index = 0; index < MUSIC_SAMPLES; index++)
    {
      sample = music_sample(index);
      if (clean)
        {
          /* First-order DC blocker/high-pass, approximately 180 Hz at
           * 16 kHz. Avoid driving the small speaker with sub-bass.
           */

          magnitude = sample;
          sample = (int64_t)(previous_out + sample - previous_in) *
                   30532 / 32768;
          previous_in = magnitude;
          previous_out = sample;
          /* USB-only listening needs a lower crest factor, not simply a
           * lower average level. Extra drive before the soft limiter
           * raises quiet samples while preserving the same peak ceiling.
           */

          sample = (int64_t)sample * gains[gain / 2 + 24] *
                   (usb ? 3 : 1) / 4096;
          magnitude = sample < 0 ? -sample : sample;
          if (magnitude > 3000)
            {
              excess = magnitude - 3000;
              magnitude = 3000 + 3000 * excess / (3000 + excess);
              sample = sample < 0 ? -magnitude : magnitude;
              limited++;
            }

          /* Nonlinear limiting can introduce DC with asymmetric musical
           * waveforms. Remove it with a 20 Hz blocker after the limiter.
           */

          magnitude = sample;
          sample = (int64_t)(limiter_out + sample - limiter_in) *
                   32512 / 32768;
          limiter_in = magnitude;
          limiter_out = sample;
          if (sample > 5999)
            {
              sample = 5999;
            }
          else if (sample < -5999)
            {
              sample = -5999;
            }
        }

      /* Twenty-millisecond fades, independent of DMA block boundaries. */

      fade = index;
      if (MUSIC_SAMPLES - 1 - index < fade)
        {
          fade = MUSIC_SAMPLES - 1 - index;
        }

      if (fade < SPEAKER_RATE / 50)
        {
          sample = sample * (int32_t)fade / (SPEAKER_RATE / 50);
        }

      if (sample > INT16_MAX)
        {
          sample = INT16_MAX;
        }
      else if (sample < INT16_MIN)
        {
          sample = INT16_MIN;
        }

      samples[index] = sample;
      magnitude = sample < 0 ? -sample : sample;
      if (magnitude > peak)
        {
          peak = magnitude;
        }
    }

  printf("speaker_test: prepared=%u clean=%u peak=%ld limited=%lu\n",
         (unsigned int)MUSIC_SAMPLES, (unsigned int)clean,
         (long)peak, (unsigned long)limited);
  return samples;
}

static int16_t music_next(FAR const int16_t *samples, FAR uint32_t *index)
{
  uint32_t current = (*index)++;

  return current < MUSIC_SAMPLES ? samples[current] : 0;
}

static void music_fill(unsigned int half, FAR uint32_t *index, bool pcm24,
                       FAR const int16_t *samples)
{
  unsigned int i;
  unsigned int offset = half * MUSIC_HALF_WORDS;
  uint16_t first;
  uint16_t second;

  for (i = 0; i < MUSIC_HALF_WORDS; i++)
    {
      if (pcm24)
        {
          /* One signed 24-bit sample per word; the top byte must be zero.
           * Multiplication avoids shifting a negative signed value.
           */

          g_music_pcm[offset + i] =
            (uint32_t)((int32_t)music_next(samples, index) * 256) & 0xffffff;
          continue;
        }

      first = (uint16_t)music_next(samples, index);
      second = (uint16_t)music_next(samples, index);
      g_music_pcm[offset + i] = (uint32_t)first |
                                ((uint32_t)second << 16);
    }

  up_clean_dcache((uintptr_t)&g_music_pcm[offset],
                  (uintptr_t)&g_music_pcm[offset + MUSIC_HALF_WORDS]);
  __DSB();
}

static uint64_t music_milliseconds(void)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int speaker_music(FAR AUDCODEC_HandleTypeDef *codec,
                         FAR DMA_HandleTypeDef *dma, int gain, bool silence,
                         bool pcm24, bool clean)
{
  FAR int16_t *samples = NULL;
  uint32_t generated = 0;
  uint32_t played = 0;
  uint32_t previous;
  uint32_t remaining;
  uint32_t flags;
  unsigned int active = 0;
  unsigned int next_half;
  unsigned int refills = 0;
  unsigned int samples_per_word = pcm24 ? 1 : 2;
  uint64_t last;
  uint64_t now;
  uint64_t started;
  int ret = 0;

  if (silence)
    {
      memset(g_music_pcm, 0, sizeof(g_music_pcm));
    }
  else
    {
      /* Synthesis, filtering and allocation must finish before DMA starts.
       * During playback only copy already prepared samples into SRAM.
       */

      samples = music_prepare(gain, clean, false);
      if (samples == NULL)
        {
          return -ENOMEM;
        }

      music_fill(0, &generated, pcm24, samples);
      music_fill(1, &generated, pcm24, samples);
    }

  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CFG |= AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CFG &= ~AUDCODEC_DAC_CFG_PATH_RESET;
  if (pcm24)
    {
      hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
    }
  else
    {
      hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
    }

  hwp_audcodec->IRQ = SPEAKER_DAC_ERRORS;
  HAL_AUDCODEC_Config_DACPath(codec, 1);
  HAL_AUDCODEC_Config_DACPath_Volume(codec, 0, clean ? 0 : gain);
  if (HAL_DMA_Start(dma, (uint32_t)g_music_pcm,
                    (uint32_t)&hwp_audcodec->DAC_CH0_ENTRY,
                    MUSIC_WORDS) != HAL_OK)
    {
      free(samples);
      return -EBUSY;
    }

  __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_AUDCODEC_Config_DACPath(codec, 0);
  __HAL_AUDCODEC_DAC_ENABLE(codec);
  previous = dma->Instance->CNDTR;
  last = music_milliseconds();
  started = last;
  printf(silence ? "speaker_test: ZERO PCM, listen for hiss, 5 seconds\n" :
         "speaker_test: MUSIC, piano melody + accompaniment, 16 seconds\n");
  printf("speaker_test: format=%u-bit, samples/word=%u\n",
         pcm24 ? 24 : 16, samples_per_word);

  /* Each half lasts 128/64 ms in 16/24-bit mode. Poll every 5 ms.
   * Abort if scheduling falls behind instead of replaying stale audio.
   */

  while (played < ((silence ? SPEAKER_RATE * 5 : MUSIC_SAMPLES) +
                   SPEAKER_RATE / 10) / samples_per_word)
    {
      speaker_delay(5);
      now = music_milliseconds();
      if (now - last > (pcm24 ? 50 : 100))
        {
          ret = -ETIMEDOUT;
          break;
        }

      last = now;
      remaining = dma->Instance->CNDTR;
      if (remaining == previous)
        {
          ret = -EIO;
          break;
        }

      played += (previous + MUSIC_WORDS - remaining) % MUSIC_WORDS;
      previous = remaining;
      flags = dma->DmaBaseAddress->ISR >> (dma->ChannelIndex & 0x1c);
      if ((flags & DMA_ISR_TEIF1) ||
          (hwp_audcodec->IRQ & SPEAKER_DAC_ERRORS))
        {
          ret = -EIO;
          break;
        }

      next_half = remaining > MUSIC_HALF_WORDS ? 0 : 1;
      if (next_half != active)
        {
          if (!silence)
            {
              music_fill(active, &generated, pcm24, samples);
              remaining = dma->Instance->CNDTR;
              if ((remaining > MUSIC_HALF_WORDS ? 0 : 1) != next_half)
                {
                  ret = -ETIMEDOUT;
                  break;
                }
            }

          active = next_half;
          refills++;
        }
    }

  printf("speaker_test: elapsed=%lu irq=%08lx fifo=%08lx ch0=%08lx\n",
         (unsigned long)(music_milliseconds() - started),
         (unsigned long)hwp_audcodec->IRQ,
         (unsigned long)hwp_audcodec->APB_STAT,
         (unsigned long)hwp_audcodec->DAC_CH0_CFG);
  HAL_AUDCODEC_Config_DACPath(codec, 1);
  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_DMA_Abort(dma);
  free(samples);
  printf("speaker_test: music samples=%lu refills=%u result=%d\n",
         (unsigned long)played * samples_per_word, refills, ret);
  return ret;
}

/* A pre-rendered four-second loop fits one 16-bit DMA transfer count.
 * Unlike streaming music, this mode never rewrites a live DMA buffer and
 * does not poll every 5 ms. Keep its gain and sample format identical to
 * raw music24, so a noise comparison does not merely turn the sound down.
 */

static void steady_fill(FAR uint32_t *samples,
                         FAR const int16_t *prepared, bool usb)
{
  unsigned int i;
  unsigned int fade;
  int32_t sample;

  for (i = 0; i < STEADY_SAMPLES; i++)
    {
      sample = usb ? prepared[i] : music_sample(i);
      if (usb)
        {
          fade = i < STEADY_SAMPLES - 1 - i ? i : STEADY_SAMPLES - 1 - i;
          if (fade < SPEAKER_RATE / 50)
            {
              sample = sample * (int32_t)fade / (SPEAKER_RATE / 50);
            }
        }

      samples[i] = (uint32_t)(sample * 256) & 0xffffff;
    }
}

static int speaker_steady(FAR AUDCODEC_HandleTypeDef *codec,
                          FAR DMA_HandleTypeDef *dma, int gain, bool usb)
{
  FAR uint32_t *samples;
  FAR int16_t *prepared = NULL;
  unsigned int i;
  uint32_t dmaflags;
  int ret = 0;

  if (usb)
    {
      prepared = music_prepare(gain, true, true);
      if (prepared == NULL)
        {
          return -ENOMEM;
        }
    }

  samples = malloc(STEADY_SAMPLES * sizeof(*samples));
  if (samples == NULL)
    {
      free(prepared);
      return -ENOMEM;
    }

  steady_fill(samples, prepared, usb);
  free(prepared);
  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CFG |= AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CFG &= ~AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
  hwp_audcodec->IRQ = SPEAKER_DAC_ERRORS;
  HAL_AUDCODEC_Config_DACPath(codec, 1);
  HAL_AUDCODEC_Config_DACPath_Volume(codec, 0, usb ? 0 : gain);
  up_clean_dcache((uintptr_t)samples,
                  (uintptr_t)samples + STEADY_SAMPLES * sizeof(*samples));
  if (HAL_DMA_Start(dma, (uint32_t)samples,
                    (uint32_t)&hwp_audcodec->DAC_CH0_ENTRY,
                    STEADY_SAMPLES) != HAL_OK)
    {
      free(samples);
      return -EBUSY;
    }

  __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_AUDCODEC_Config_DACPath(codec, 0);
  __HAL_AUDCODEC_DAC_ENABLE(codec);
  printf("speaker_test: STEADY 24-bit, 4-second loop, no refills, 16s\n");
  printf("speaker_test: steady buffer=%p words=%u gain=%d dB usb=%d\n",
         samples, (unsigned int)STEADY_SAMPLES, gain / 2, usb);

  for (i = 0; i < 4; i++)
    {
      speaker_delay(4000);
      dmaflags = dma->DmaBaseAddress->ISR >> (dma->ChannelIndex & 0x1c);
      if ((dmaflags & DMA_ISR_TEIF1) ||
          (hwp_audcodec->IRQ & SPEAKER_DAC_ERRORS))
        {
          ret = -EIO;
          break;
        }
    }

  printf("speaker_test: steady irq=%08lx count=%lu result=%d\n",
         (unsigned long)hwp_audcodec->IRQ,
         (unsigned long)dma->Instance->CNDTR, ret);
  HAL_AUDCODEC_Config_DACPath(codec, 1);
  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_DMA_Abort(dma);
  free(samples);
  return ret;
}

/* Pre-render the complete recording in packed 16-bit format. Five seconds
 * fit one DMA count (40,256 words including the zero tail), with no writes
 * to a running buffer. Stop inside the 32 ms zero tail before it repeats.
 */

static int speaker_voice(FAR AUDCODEC_HandleTypeDef *codec,
                         FAR DMA_HandleTypeDef *dma, int gain)
{
  FAR FILE *file;
  FAR int16_t *pcm;
  FAR uint32_t *packed;
  uint64_t started;
  uint32_t words;
  uint32_t remaining;
  uint32_t previous;
  uint32_t flags;
  size_t count;
  size_t i;
  long length;
  int32_t peak;
  int ret = 0;

  file = fopen("/data/mic_test.pcm", "rb");
  if (file == NULL)
    {
      return -errno;
    }

  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
      length > SPEAKER_RATE * 5 * 2 || (length & 1) != 0 ||
      fseek(file, 0, SEEK_SET) != 0)
    {
      fclose(file);
      return -EINVAL;
    }

  count = length / sizeof(int16_t);
  words = ((count + 512 + 63) / 64) * 32;
  pcm = malloc(length);
  packed = memalign(32, words * sizeof(*packed));
  if (pcm == NULL || packed == NULL)
    {
      fclose(file);
      free(pcm);
      free(packed);
      return -ENOMEM;
    }

  if (fread(pcm, 1, length, file) != (size_t)length)
    {
      fclose(file);
      free(pcm);
      free(packed);
      return -EIO;
    }

  fclose(file);
  peak = voice_test_process(pcm, count, gain);
  memset(packed, 0, words * sizeof(*packed));
  for (i = 0; i < count; i++)
    {
      packed[i / 2] |= (uint32_t)(uint16_t)pcm[i] << ((i & 1) * 16);
    }

  free(pcm);
  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CFG |= AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CFG &= ~AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
  hwp_audcodec->IRQ = SPEAKER_DAC_ERRORS;
  HAL_AUDCODEC_Config_DACPath(codec, 1);
  HAL_AUDCODEC_Config_DACPath_Volume(codec, 0, 0);
  up_clean_dcache((uintptr_t)packed,
                  (uintptr_t)packed + words * sizeof(*packed));
  if (HAL_DMA_Start(dma, (uint32_t)packed,
                    (uint32_t)&hwp_audcodec->DAC_CH0_ENTRY,
                    words) != HAL_OK)
    {
      free(packed);
      return -EBUSY;
    }

  __HAL_DMA_DISABLE_IT(dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_AUDCODEC_Config_DACPath(codec, 0);
  __HAL_AUDCODEC_DAC_ENABLE(codec);
  started = music_milliseconds();
  previous = words;
  printf("speaker_test: voice samples=%lu words=%lu peak=%ld gain=%d dB\n",
         (unsigned long)count, (unsigned long)words, (long)peak, gain / 2);
  for (; ; )
    {
      remaining = dma->Instance->CNDTR;
      flags = dma->DmaBaseAddress->ISR >> (dma->ChannelIndex & 0x1c);
      if ((flags & DMA_ISR_TEIF1) ||
          (hwp_audcodec->IRQ & SPEAKER_DAC_ERRORS))
        {
          ret = -EIO;
          break;
        }

      if (remaining > previous ||
          music_milliseconds() - started >
          count * 1000 / SPEAKER_RATE + 1000)
        {
          ret = -ETIMEDOUT;
          break;
        }

      if (remaining <= 256)
        {
          break;
        }

      previous = remaining;
      speaker_delay(5);
    }

  HAL_AUDCODEC_Config_DACPath(codec, 1);
  __HAL_AUDCODEC_DAC_DISABLE(codec);
  hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_DMA_Abort(dma);
  free(packed);
  printf("speaker_test: voice elapsed=%lu remaining=%lu result=%d\n",
         (unsigned long)(music_milliseconds() - started),
         (unsigned long)remaining, ret);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int huangshan_audio_test_acquire(void)
{
  return pthread_mutex_trylock(&g_speaker_lock);
}

void huangshan_audio_test_release(void)
{
  pthread_mutex_unlock(&g_speaker_lock);
}

int speaker_test_main(int argc, FAR char *argv[])
{
  AUDCODEC_HandleTypeDef codec =
    {
      0
    };

  DMA_HandleTypeDef dma =
    {
      0
    };

  GPIO_InitTypeDef gpio =
    {
      0
    };

  unsigned int repetitions = 1;
  bool internal = false;
  bool music = false;
  bool silence = false;
  bool pa_only = false;
  bool pcm24 = false;
  bool clean = false;
  bool steady = false;
  bool usb = false;
  bool voice = false;
  int gain = SPEAKER_VOLUME;
  unsigned int i;
  unsigned int note;
  char *end;
  long value;
  int ret;

  if (argc > 4)
    {
      fprintf(stderr,
              "Usage: speaker_test [1..5] [-24..6 dB] "
              "[pcm|pcm24|internal|music|music24|musicclean|"
              "musicsteady|musicusb|mic|silence|silence24|pa-only]\n");
      return EXIT_FAILURE;
    }

  if (argc >= 2)
    {
      errno = 0;
      value = strtol(argv[1], &end, 10);
      if (errno != 0 || end == argv[1] || *end != '\0' ||
          value < 1 || value > 5)
        {
          fprintf(stderr, "Usage: speaker_test [1..5]\n");
          return EXIT_FAILURE;
        }

      repetitions = value;
    }

  if (argc >= 3)
    {
      errno = 0;
      value = strtol(argv[2], &end, 10);
      if (errno != 0 || end == argv[2] || *end != '\0' ||
          value < -24 || value > 6)
        {
          fprintf(stderr, "Gain must be -24..6 dB\n");
          return EXIT_FAILURE;
        }

      gain = value * 2;
    }

  if (argc == 4)
    {
      if (strcmp(argv[3], "internal") == 0)
        {
          internal = true;
        }
      else if (strcmp(argv[3], "music") == 0)
        {
          music = true;
        }
      else if (strcmp(argv[3], "music24") == 0)
        {
          music = true;
          pcm24 = true;
        }
      else if (strcmp(argv[3], "musicclean") == 0)
        {
          music = true;
          pcm24 = true;
          clean = true;
        }
      else if (strcmp(argv[3], "musicsteady") == 0)
        {
          steady = true;
        }
      else if (strcmp(argv[3], "musicusb") == 0)
        {
          steady = true;
          usb = true;
        }
      else if (strcmp(argv[3], "mic") == 0)
        {
          voice = true;
        }
      else if (strcmp(argv[3], "pcm24") == 0)
        {
          pcm24 = true;
        }
      else if (strcmp(argv[3], "silence") == 0)
        {
          music = true;
          silence = true;
        }
      else if (strcmp(argv[3], "silence24") == 0)
        {
          music = true;
          silence = true;
          pcm24 = true;
        }
      else if (strcmp(argv[3], "pa-only") == 0)
        {
          pa_only = true;
        }
      else if (strcmp(argv[3], "pcm") != 0)
        {
          fprintf(stderr, "Unknown playback mode\n");
          return EXIT_FAILURE;
        }
    }

  ret = pthread_mutex_trylock(&g_speaker_lock);
  if (ret != 0)
    {
      fprintf(stderr, "speaker_test: already running (%d)\n", ret);
      return EXIT_FAILURE;
    }

  /* Refuse to disturb a future audio driver that owns the codec. */

  if (hwp_audcodec->CFG &
      (AUDCODEC_CFG_DAC_ENABLE | AUDCODEC_CFG_ADC_ENABLE))
    {
      fprintf(stderr, "speaker_test: audio codec is already active\n");
      pthread_mutex_unlock(&g_speaker_lock);
      return EXIT_FAILURE;
    }

  HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_PULLDOWN, 1);
  gpio.Pin  = SPEAKER_PA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT;
  gpio.Pull = GPIO_NOPULL;
  speaker_pa(false);
  HAL_GPIO_Init(hwp_gpio1, &gpio);

  if (pa_only)
    {
      printf("speaker_test: PA ONLY, DAC off, listen for hiss, 5 seconds\n");
      speaker_pa(true);
      speaker_delay(5000);
      speaker_pa(false);
      pthread_mutex_unlock(&g_speaker_lock);
      printf("speaker_test: done, amplifier off, result=0\n");
      return EXIT_SUCCESS;
    }

  ret = -EIO;
  if (HAL_PMU_EnableAudio(1) != HAL_OK)
    {
      goto out_unlock;
    }

  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  hwp_pmuc->HXT_CR1 |= PMUC_HXT_CR1_BUF_AUD_EN;
  codec.Instance = hwp_audcodec;
  codec.Init.dac_cfg.opmode = 1;
  codec.Init.dac_cfg.dac_clk = &g_dac_clock;
  if (HAL_AUDCODEC_Init(&codec) != HAL_OK)
    {
      goto out_power;
    }

  HAL_TURN_ON_PLL();
  if (HAL_AUDCODEC_Config_TChanel(&codec, 0,
                                &codec.Init.dac_cfg) != HAL_OK)
    {
      goto out_codec;
    }

  HAL_AUDCODEC_Config_DACPath(&codec, 1);
  hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;

  /* Keep the HAL's slow volume ramp and zero-crossing adjustment. */

  HAL_AUDCODEC_Config_DACPath_Volume(&codec, 0, -72);
  if (HAL_AUDCODEC_Config_Analog_DACPath(&g_dac_clock) != HAL_OK)
    {
      goto out_codec;
    }

  dma.Instance                 = DMA1_Channel1;
  dma.Init.Request             = DMA_REQUEST_41; /* AUDCODEC DAC0 */
  dma.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  dma.Init.PeriphInc           = DMA_PINC_DISABLE;
  dma.Init.MemInc              = DMA_MINC_ENABLE;
  dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  dma.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;
  dma.Init.Mode                = DMA_CIRCULAR;
  dma.Init.Priority            = DMA_PRIORITY_HIGH;
  dma.Init.IrqPrio             = 6;
  if (HAL_DMA_Init(&dma) != HAL_OK)
    {
      goto out_codec;
    }

  speaker_pa(true);
  speaker_delay(150); /* NS4150B startup is typically 120 ms. */
  printf("speaker_test: CN2, DAC1, PA42, 16000 Hz, gain=%d dB\n",
         gain / 2);

  ret = 0;
  for (i = 0; i < repetitions && ret == 0; i++)
    {
      if (voice)
        {
          ret = speaker_voice(&codec, &dma, gain);
          speaker_delay(150);
          continue;
        }

      if (steady)
        {
          ret = speaker_steady(&codec, &dma, gain, usb);
          speaker_delay(150);
          continue;
        }

      if (internal)
        {
          ret = speaker_internal(&codec, &dma);
          speaker_delay(150);
          continue;
        }

      if (music)
        {
          ret = speaker_music(&codec, &dma, gain, silence, pcm24, clean);
          speaker_delay(150);
          continue;
        }

      for (note = 0; note < 3 && ret == 0; note++)
        {
          ret = speaker_tone(&codec, &dma, 500 + note * 250, 600, gain,
                             pcm24);
          speaker_delay(150);
        }
    }

  speaker_pa(false);

  /* speaker_tone already aborts and releases the dynamically owned channel.
   * Do not DeInit it here: another peripheral may now own that channel.
   */

out_codec:
  speaker_pa(false);
  speaker_delay(100); /* NS4150B shutdown is typically 80 ms. */
  hwp_audcodec->CFG &= ~AUDCODEC_CFG_DAC_1K_MODE;
  HAL_AUDCODEC_Config_DACPath(&codec, 1);
  HAL_AUDCODEC_Clear_All_Channel(&codec, 1);
  HAL_AUDCODEC_Close_Analog_DACPath();
  HAL_AUDCODEC_DeInit(&codec);
  HAL_TURN_OFF_PLL();
out_power:
  HAL_RCC_DisableModule(RCC_MOD_AUDCODEC);
  HAL_PMU_EnableAudio(0);
out_unlock:
  speaker_pa(false);
  pthread_mutex_unlock(&g_speaker_lock);
  printf("speaker_test: done, amplifier off, result=%d\n", ret);
  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
