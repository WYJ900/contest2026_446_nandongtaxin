/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARDS_SF32LB52_HUANGSHAN_WATCH_AUDIO_H
#define __BOARDS_SF32LB52_HUANGSHAN_WATCH_AUDIO_H

#include <nuttx/compiler.h>
#include <stdbool.h>
#include <stdint.h>

typedef bool (*huangshan_audio_cancel_t)(FAR void *arg);
typedef int (*huangshan_audio_gain_t)(FAR void *arg);
#define HUANGSHAN_AUDIO_MUTED (-32768)

/* Bounded board playback service, not an audio lower-half. The caller owns
 * a 32-byte aligned packed 16-bit/16-kHz mono buffer, including >=512 zero
 * tail samples. It must stay immutable until return. PCM limit: +/-6000.
 * Gain callback is polled during playback: -72..6 half-dB units, or MUTED.
 * Sleeps in task context; no ISR use. No hardware is enabled at boot.
 */

int huangshan_watch_audio_play(FAR const uint32_t *packed, uint32_t words,
                              uint32_t samples, huangshan_audio_gain_t gain,
                              huangshan_audio_cancel_t cancelled,
                              FAR void *arg);

/* Diagnostics: bit 0 PA, bit 1 DAC, bit 2 ADC, bit 3 live DAC output muted. */

uint32_t huangshan_watch_audio_state(void);

#endif
