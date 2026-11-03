/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELARIDE_COUNTDOWN_H
#define __APPS_EXAMPLES_VELARIDE_COUNTDOWN_H

#include <stdbool.h>
#include <stdint.h>

enum velaride_countdown_state_e
{
  VELARIDE_COUNTDOWN_IDLE = 0,
  VELARIDE_COUNTDOWN_RUNNING,
  VELARIDE_COUNTDOWN_PAUSED,
  VELARIDE_COUNTDOWN_DONE
};

struct velaride_countdown_s
{
  enum velaride_countdown_state_e state;
  uint32_t duration_s;
  uint32_t remaining_ms;
  uint32_t completions;
};

/* All timestamps use the same monotonic millisecond clock (lv_tick_get).
 * No wall clock, I/O, sound, persistence or LVGL calls occur in this module.
 * The lock also protects explicit NSH diagnostic commands from the UI task.
 */

int velaride_countdown_set(uint32_t seconds);
void velaride_countdown_start(uint32_t now);
void velaride_countdown_pause(uint32_t now);
void velaride_countdown_cancel(void);
void velaride_countdown_get(uint32_t now, struct velaride_countdown_s *out);
bool velaride_countdown_take_done(uint32_t now);

#endif
