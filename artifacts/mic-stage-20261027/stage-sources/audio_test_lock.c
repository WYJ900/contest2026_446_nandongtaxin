/****************************************************************************
 * boards/sf32lb52/lckfb_huangshan_pi/src/audio_test_lock.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <pthread.h>
#include "audio_test.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* The board has one codec. Recording is explicit and never starts at boot. */

static pthread_mutex_t g_audio_test_lock = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int huangshan_audio_test_acquire(void)
{
  return pthread_mutex_trylock(&g_audio_test_lock);
}

void huangshan_audio_test_release(void)
{
  pthread_mutex_unlock(&g_audio_test_lock);
}
