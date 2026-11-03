/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/watch_audio.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SF32LB52 DAC1 -> NS4150B, PA42. Derived from the accepted speaker_test
 * setup. This helper is called only by the application's sound task.
 * It uses the same codec lock as mic_test/speaker_test, never their CLI.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <nuttx/cache.h>
#include <arch/board/watch_audio.h>

#include "bf0_hal.h"
#include "audio_test.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define WATCH_AUDIO_RATE       16000
#define WATCH_AUDIO_MAX        32000
#define WATCH_AUDIO_PA         42
#define WATCH_AUDIO_TAIL       512
#define WATCH_AUDIO_ERRORS     (AUDCODEC_IRQ_DAC_CH0_OUT_UF | \
                                AUDCODEC_IRQ_DAC_CH0_APB_OF | \
                                AUDCODEC_IRQ_DAC_CH0_STB_OF)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void audio_delay(unsigned int ms)
{
  struct timespec delay;

  delay.tv_sec = ms / 1000;
  delay.tv_nsec = (ms % 1000) * 1000000L;
  nanosleep(&delay, NULL);
}

static void audio_pa(bool enabled)
{
  HAL_GPIO_WritePin(hwp_gpio1, WATCH_AUDIO_PA,
                   enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint64_t audio_now(void)
{
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    {
      return 0;
    }

  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint32_t huangshan_watch_audio_state(void)
{
  return (HAL_GPIO_ReadPin(hwp_gpio1, WATCH_AUDIO_PA) == GPIO_PIN_SET ?
          1u : 0u) |
         (hwp_audcodec->CFG & AUDCODEC_CFG_DAC_ENABLE ? 2u : 0u) |
         (hwp_audcodec->CFG & AUDCODEC_CFG_ADC_ENABLE ? 4u : 0u) |
         ((hwp_audcodec->CFG & AUDCODEC_CFG_DAC_ENABLE) &&
          (hwp_audcodec->DAC_CH0_CFG & AUDCODEC_DAC_CH0_CFG_DOUT_MUTE_Msk) ?
          8u : 0u);
}

int huangshan_watch_audio_play(FAR const uint32_t *packed, uint32_t words,
                              uint32_t samples, huangshan_audio_gain_t volume,
                              huangshan_audio_cancel_t cancelled,
                              FAR void *arg)
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

  AUDCODE_DAC_CLK_CONFIG_TYPE clock =
    {
      .samplerate = WATCH_AUDIO_RATE,
      .clk_src_sel = 0,
      .clk_div = 1,
      .osr_sel = 4,
      .sinc_gain = 0x14d,
      .sel_clk_dac_source = 0,
      .diva_clk_dac = 5,
      .diva_clk_chop_dac = 4,
      .divb_clk_chop_dac = 2,
      .diva_clk_chop_bg = 20,
      .diva_clk_chop_refgen = 20,
      .sel_clk_dac = 0
    };

  uint64_t started;
  uint32_t remaining;
  uint32_t flags;
  uint32_t i;
  unsigned int polls;
  bool dma_started = false;
  int gain;
  int next_gain;
  int ret;

  if (packed == NULL || ((uintptr_t)packed & 31) != 0 ||
      samples == 0 || samples > WATCH_AUDIO_MAX ||
      words > 16384 || words * 2 < samples + WATCH_AUDIO_TAIL ||
      cancelled == NULL || volume == NULL)
    {
      return -EINVAL;
    }

  gain = volume(arg);
  if (gain != HUANGSHAN_AUDIO_MUTED && (gain < -72 || gain > 6))
    {
      return -EINVAL;
    }

  for (i = 0; i < words * 2; i++)
    {
      int16_t sample = (int16_t)(packed[i / 2] >> ((i & 1) * 16));

      if (sample < -6000 || sample > 6000 || (i >= samples && sample != 0))
        {
          return -ERANGE;
        }
    }

  if (cancelled(arg))
    {
      return -ECANCELED;
    }

  ret = huangshan_audio_test_acquire();
  if (ret != 0)
    {
      return -ret;
    }

  if (hwp_audcodec->CFG &
      (AUDCODEC_CFG_DAC_ENABLE | AUDCODEC_CFG_ADC_ENABLE))
    {
      huangshan_audio_test_release();
      return -EBUSY;
    }

  HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_PULLDOWN, 1);
  gpio.Pin = WATCH_AUDIO_PA;
  gpio.Mode = GPIO_MODE_OUTPUT;
  gpio.Pull = GPIO_NOPULL;
  audio_pa(false);
  HAL_GPIO_Init(hwp_gpio1, &gpio);
  ret = -EIO;
  if (HAL_PMU_EnableAudio(1) != HAL_OK)
    {
      HAL_PMU_EnableAudio(0);
      goto out_unlock;
    }

  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  hwp_pmuc->HXT_CR1 |= PMUC_HXT_CR1_BUF_AUD_EN;
  codec.Instance = hwp_audcodec;
  codec.Init.dac_cfg.opmode = 1;
  codec.Init.dac_cfg.dac_clk = &clock;
  if (HAL_AUDCODEC_Init(&codec) != HAL_OK)
    {
      HAL_AUDCODEC_DeInit(&codec);
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
  HAL_AUDCODEC_Config_DACPath_Volume(&codec, 0, -72);
  if (HAL_AUDCODEC_Config_Analog_DACPath(&clock) != HAL_OK)
    {
      goto out_codec;
    }

  dma.Instance = DMA1_Channel1;
  dma.Init.Request = DMA_REQUEST_41;
  dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
  dma.Init.PeriphInc = DMA_PINC_DISABLE;
  dma.Init.MemInc = DMA_MINC_ENABLE;
  dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  dma.Init.Mode = DMA_NORMAL;
  dma.Init.Priority = DMA_PRIORITY_HIGH;
  dma.Init.IrqPrio = 6;
  if (HAL_DMA_Init(&dma) != HAL_OK)
    {
      goto out_codec;
    }

  audio_pa(true);
  for (i = 0; i < 30; i++)
    {
      if (cancelled(arg))
        {
          ret = -ECANCELED;
          goto out_codec;
        }

      audio_delay(5);
    }

  __HAL_AUDCODEC_DAC_DISABLE(&codec);
  hwp_audcodec->DAC_CFG |= AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CFG &= ~AUDCODEC_DAC_CFG_PATH_RESET;
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DATA_FORMAT;
  hwp_audcodec->IRQ = WATCH_AUDIO_ERRORS;
  HAL_AUDCODEC_Config_DACPath_Volume(&codec, 0,
                                   gain == HUANGSHAN_AUDIO_MUTED ? -72 : gain);
  up_clean_dcache((uintptr_t)packed,
                 (uintptr_t)packed + words * sizeof(*packed));
  if (HAL_DMA_Start(&dma, (uint32_t)packed,
                    (uint32_t)&hwp_audcodec->DAC_CH0_ENTRY, words) != HAL_OK)
    {
      ret = -EBUSY;
      goto out_codec;
    }

  dma_started = true;
  __HAL_DMA_DISABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_AUDCODEC_Config_DACPath(&codec, gain == HUANGSHAN_AUDIO_MUTED);
  __HAL_AUDCODEC_DAC_ENABLE(&codec);
  started = audio_now();
  ret = -ETIMEDOUT;
  for (polls = 0; polls < samples / 80 + 220; polls++)
    {
      if (cancelled(arg))
        {
          ret = -ECANCELED;
          break;
        }

      next_gain = volume(arg);
      if (next_gain != HUANGSHAN_AUDIO_MUTED &&
          (next_gain < -72 || next_gain > 6))
        {
          ret = -EINVAL;
          break;
        }

      if (next_gain != gain)
        {
          if (next_gain == HUANGSHAN_AUDIO_MUTED)
            {
              HAL_AUDCODEC_Config_DACPath(&codec, 1);
            }
          else
            {
              HAL_AUDCODEC_Config_DACPath_Volume(&codec, 0, next_gain);
              HAL_AUDCODEC_Config_DACPath(&codec, 0);
            }

          gain = next_gain;
        }

      remaining = dma.Instance->CNDTR;
      flags = dma.DmaBaseAddress->ISR >> (dma.ChannelIndex & 0x1c);
      if (flags & DMA_ISR_TEIF1)
        {
          ret = -EIO;
          break;
        }

      if (remaining <= WATCH_AUDIO_TAIL / 2)
        {
          ret = 0;
          break;
        }

      if (hwp_audcodec->IRQ & WATCH_AUDIO_ERRORS)
        {
          ret = -EIO;
          break;
        }

      if (audio_now() - started > samples / 16 + 1000)
        {
          break;
        }

      audio_delay(5);
    }

out_codec:
  audio_pa(false);
  HAL_AUDCODEC_Config_DACPath(&codec, 1);
  __HAL_AUDCODEC_DAC_DISABLE(&codec);
  hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
  if (dma_started)
    {
      HAL_DMA_Abort(&dma);
    }

  audio_delay(100);
  HAL_AUDCODEC_Clear_All_Channel(&codec, 1);
  HAL_AUDCODEC_Close_Analog_DACPath();
  HAL_AUDCODEC_DeInit(&codec);
  HAL_TURN_OFF_PLL();
out_power:
  HAL_RCC_DisableModule(RCC_MOD_AUDCODEC);
  HAL_PMU_EnableAudio(0);
out_unlock:
  audio_pa(false);
  huangshan_audio_test_release();
  return ret;
}
