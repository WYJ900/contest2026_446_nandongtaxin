/****************************************************************************
 * vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/mic_test.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded onboard analog microphone capture; no cloud transmission.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/cache.h>

#include "bf0_hal.h"
#include "audio_test.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MIC_RATE 16000
#define MIC_PATH "/data/mic_test.pcm"
#define MIC_WARMUP_SAMPLES (MIC_RATE / 5)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static AUDCODE_ADC_CLK_CONFIG_TYPE g_mic_clock =
{
  .samplerate = MIC_RATE,
  .clk_src_sel = 0,
  .clk_div = 10,
  .osr_sel = 1,
  .sel_clk_adc_source = 0,
  .sel_clk_adc = 0,
  .diva_clk_adc = 5,
  .fsp = 2
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint64_t mic_time(void)
{
  struct timespec now;

  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static uint32_t mic_checksum(FAR const uint8_t *data, size_t length)
{
  uint32_t value = 2166136261u;
  size_t i;

  for (i = 0; i < length; i++)
    {
      value = (value ^ data[i]) * 16777619u;
    }

  return value;
}

static int mic_dump(void)
{
  static const char hex[] = "0123456789abcdef";
  FAR FILE *file;
  FAR uint8_t *data;
  char line[160];
  long length;
  size_t offset;
  size_t count;
  size_t i;
  int used;

  file = fopen(MIC_PATH, "rb");
  if (file == NULL)
    {
      return -errno;
    }

  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
      length > MIC_RATE * 5 * 2 || (length & 1) != 0 ||
      fseek(file, 0, SEEK_SET) != 0)
    {
      fclose(file);
      return -EINVAL;
    }

  data = malloc(length);
  if (data == NULL)
    {
      fclose(file);
      return -ENOMEM;
    }

  count = fread(data, 1, length, file);
  fclose(file);
  if (count != (size_t)length)
    {
      free(data);
      return -EIO;
    }

  printf("MIC_BEGIN bytes=%ld rate=16000 bits=16 checksum=%08" PRIx32 "\n",
         length, mic_checksum(data, length));
  for (offset = 0; offset < (size_t)length; offset += count)
    {
      count = length - offset;
      if (count > 64)
        {
          count = 64;
        }

      used = snprintf(line, sizeof(line), "MICDATA %08lx ",
                      (unsigned long)offset);
      for (i = 0; i < count; i++)
        {
          line[used++] = hex[data[offset + i] >> 4];
          line[used++] = hex[data[offset + i] & 15];
        }

      line[used++] = '\n';
      if (fwrite(line, 1, used, stdout) != (size_t)used)
        {
          free(data);
          return -EIO;
        }
    }

  printf("MIC_END\n");
  fflush(stdout);
  free(data);
  return 0;
}

static int mic_capture(unsigned int seconds)
{
  AUDCODEC_HandleTypeDef codec =
    {
      0
    };

  DMA_HandleTypeDef dma =
    {
      0
    };

  FAR int16_t *samples;
  FAR FILE *file;
  uint32_t words = (seconds * MIC_RATE + MIC_WARMUP_SAMPLES) / 2;
  size_t bytes = words * sizeof(uint32_t);
  uint64_t start;
  uint32_t flags;
  uint32_t remaining;
  unsigned int zeros = 0;
  unsigned int clipped = 0;
  unsigned int i;
  int minimum = INT16_MAX;
  int maximum = INT16_MIN;
  int64_t sum = 0;
  uint64_t energy = 0;
  int ret;

  ret = huangshan_audio_test_acquire();
  if (ret != 0)
    {
      return -EBUSY;
    }

  if (hwp_audcodec->CFG &
      (AUDCODEC_CFG_DAC_ENABLE | AUDCODEC_CFG_ADC_ENABLE))
    {
      ret = -EBUSY;
      goto out_unlock;
    }

  samples = memalign(32, bytes);
  if (samples == NULL)
    {
      ret = -ENOMEM;
      goto out_unlock;
    }

  memset(samples, 0, bytes);
  up_clean_dcache((uintptr_t)samples, (uintptr_t)samples + bytes);
  up_invalidate_dcache((uintptr_t)samples, (uintptr_t)samples + bytes);
  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  codec.Instance = hwp_audcodec;
  codec.Init.adc_cfg.opmode = 1;
  codec.Init.adc_cfg.adc_clk = &g_mic_clock;
  ret = -EIO;
  if (HAL_AUDCODEC_Init(&codec) != HAL_OK)
    {
      goto out_power;
    }

  HAL_TURN_ON_PLL();
  if (HAL_AUDCODEC_Config_RChanel(&codec, 0,
                                &codec.Init.adc_cfg) != HAL_OK)
    {
      goto out_codec;
    }

  HAL_AUDCODEC_Config_ADCPath_Volume(&codec, 0, 0);
  HAL_AUDCODEC_Config_Analog_ADCPath(&g_mic_clock);
  dma.Instance = DMA1_Channel1;
  dma.Init.Request = DMA_REQUEST_39;
  dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
  dma.Init.PeriphInc = DMA_PINC_DISABLE;
  dma.Init.MemInc = DMA_MINC_ENABLE;
  dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  dma.Init.Mode = DMA_NORMAL;
  dma.Init.Priority = DMA_PRIORITY_HIGH;
  dma.Init.IrqPrio = 6;
  if (HAL_DMA_Init(&dma) != HAL_OK)
    {
      goto out_analog;
    }

  hwp_audcodec->IRQ = AUDCODEC_IRQ_ADC_CH0_APB_OF |
                     AUDCODEC_IRQ_ADC_CH0_APB_UF |
                     AUDCODEC_IRQ_ADC_CH0_SAT;
  if (HAL_DMA_Start(&dma, (uint32_t)&hwp_audcodec->ADC_CH0_ENTRY,
                    (uint32_t)samples, words) != HAL_OK)
    {
      ret = -EBUSY;
      goto out_analog;
    }

  __HAL_DMA_DISABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->ADC_CH0_CFG |= AUDCODEC_ADC_CH0_CFG_DMA_EN;
  printf("MIC_RECORD seconds=%u rate=16000 bits=16 warmup_ms=200\n",
         seconds);
  start = mic_time();
  __HAL_AUDCODEC_ADC_ENABLE(&codec);
  ret = 0;
  for (; ; )
    {
      remaining = dma.Instance->CNDTR;
      if (remaining == 0)
        {
          break;
        }

      flags = dma.DmaBaseAddress->ISR >> (dma.ChannelIndex & 0x1c);
      if ((flags & DMA_ISR_TEIF1) ||
          (hwp_audcodec->IRQ & AUDCODEC_IRQ_ADC_CH0_APB_OF))
        {
          ret = -EIO;
          break;
        }

      if (mic_time() - start > (seconds + 2) * 1000)
        {
          ret = -ETIMEDOUT;
          break;
        }

      usleep(10000);
    }

  __HAL_AUDCODEC_ADC_DISABLE(&codec);
  hwp_audcodec->ADC_CH0_CFG &= ~AUDCODEC_ADC_CH0_CFG_DMA_EN;
  printf("MIC_DMA remaining=%lu elapsed=%lu irq=%08lx result=%d\n",
         (unsigned long)remaining, (unsigned long)(mic_time() - start),
         (unsigned long)hwp_audcodec->IRQ, ret);
  HAL_DMA_Abort(&dma);
  up_invalidate_dcache((uintptr_t)samples, (uintptr_t)samples + bytes);

out_analog:
  HAL_AUDCODEC_Close_Analog_ADCPath();
  hwp_audcodec->ADC_ANA_CFG &= ~AUDCODEC_ADC_ANA_CFG_MICBIAS_EN;
out_codec:
  HAL_AUDCODEC_Clear_All_Channel(&codec, 2);
  HAL_AUDCODEC_DeInit(&codec);
  HAL_TURN_OFF_PLL();
out_power:
  HAL_RCC_DisableModule(RCC_MOD_AUDCODEC);
  HAL_PMU_EnableAudio(0);
  if (ret == 0)
    {
      for (i = 0; i < seconds * MIC_RATE; i++)
        {
          int value = samples[i + MIC_WARMUP_SAMPLES];
          if (value < minimum)
            {
              minimum = value;
            }

          if (value > maximum)
            {
              maximum = value;
            }

          zeros += value == 0;
          clipped += value == INT16_MIN || value == INT16_MAX;
          sum += value;
          energy += (int64_t)value * value;
        }

      file = fopen(MIC_PATH, "wb");
      if (file == NULL)
        {
          ret = -errno;
        }
      else
        {
          bytes = seconds * MIC_RATE * sizeof(int16_t);
          if (fwrite(samples + MIC_WARMUP_SAMPLES, 1, bytes, file) != bytes)
            {
              ret = -EIO;
            }

          if (fclose(file) != 0)
            {
              ret = -EIO;
            }
        }

      printf("MIC_STATS min=%d max=%d mean=%ld mean_square=%" PRIu64
             " zeros=%u clipped=%u result=%d\n",
             minimum, maximum, (long)(sum / (seconds * MIC_RATE)),
             energy / (seconds * MIC_RATE), zeros, clipped, ret);
    }

  free(samples);
out_unlock:
  huangshan_audio_test_release();
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int mic_test_main(int argc, FAR char *argv[])
{
  char *end;
  long seconds = 3;
  int ret;

  if (argc == 2 && strcmp(argv[1], "dump") == 0)
    {
      ret = huangshan_audio_test_acquire();
      if (ret == 0)
        {
          ret = mic_dump();
          huangshan_audio_test_release();
        }
      else
        {
          ret = -EBUSY;
        }
    }
  else
    {
      if (argc > 2)
        {
          fprintf(stderr, "Usage: mic_test [1..5|dump]\n");
          return EXIT_FAILURE;
        }

      if (argc == 2)
        {
          errno = 0;
          seconds = strtol(argv[1], &end, 10);
          if (errno != 0 || end == argv[1] || *end != '\0' ||
              seconds < 1 || seconds > 5)
            {
              fprintf(stderr, "Usage: mic_test [1..5|dump]\n");
              return EXIT_FAILURE;
            }
        }

      ret = mic_capture(seconds);
    }

  printf("mic_test: done result=%d\n", ret);
  return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
