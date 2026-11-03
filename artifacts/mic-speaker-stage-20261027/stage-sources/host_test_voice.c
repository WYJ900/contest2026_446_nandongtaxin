/* Host check for voice_test_boost/voice_test_process; build with ASan/UBSan. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice_test_dsp.h"

#define MAX_SAMPLES (16000 * 5)

static int16_t g_pcm[MAX_SAMPLES];

static size_t load_wav(const char *path, size_t skip)
{
  FILE *file = fopen(path, "rb");
  size_t count;

  assert(file != NULL);
  fseek(file, 44 + (long)skip * 2, SEEK_SET);
  count = fread(g_pcm, 2, MAX_SAMPLES, file);
  fclose(file);
  return count;
}

static void report(const char *name, size_t count)
{
  int16_t raw[MAX_SAMPLES];
  double sum = 0;
  int32_t peak;
  int boost;
  size_t i;

  memcpy(raw, g_pcm, count * 2);
  boost = voice_test_boost(g_pcm, count);
  for (i = 0; i < count; i++)
    {
      assert(g_pcm[i] == raw[i] * boost);
    }

  peak = voice_test_process(g_pcm, count, 12);
  for (i = 0; i < count; i++)
    {
      assert(g_pcm[i] >= -5999 && g_pcm[i] <= 5999);
      sum += (double)g_pcm[i] * g_pcm[i];
    }

  printf("%-34s boost=%d out_peak=%5d out_rms=%7.1f\n", name, boost,
         (int)peak, sqrt(sum / count));
}

static int edge(int16_t value, size_t count)
{
  size_t i;

  for (i = 0; i < count; i++)
    {
      g_pcm[i] = (i & 1) ? value : (int16_t)-value;
    }

  return voice_test_boost(g_pcm, count);
}

int main(int argc, char *argv[])
{
  int i;

  for (i = 1; i + 1 < argc; i += 2)
    {
      size_t count = load_wav(argv[i], strtoul(argv[i + 1], NULL, 10));
      report(argv[i], count);
    }

  assert(edge(0, 1000) == VOICE_TEST_MAX_BOOST);
  assert(edge(32767, 1000) == 1);
  g_pcm[0] = -32768;
  assert(voice_test_boost(g_pcm, 1) == 1);
  assert(edge(249, 1000) == 4);
  assert(edge(250, 1000) == 4);
  assert(edge(251, 1000) == 3);
  assert(edge(500, 1000) == 2);
  assert(edge(501, 1000) == 1);
  assert(edge(1000, 1000) == 1);
  assert(voice_test_boost(g_pcm, 0) == VOICE_TEST_MAX_BOOST);
  for (i = 0; i < 1000; i++)
    {
      assert(abs(g_pcm[i]) <= 32767);
    }

  edge(249, MAX_SAMPLES);
  voice_test_boost(g_pcm, MAX_SAMPLES);
  for (i = 0; i < MAX_SAMPLES; i++)
    {
      assert(abs(g_pcm[i]) == 996);
    }

  puts("PASS edge cases");
  return 0;
}
