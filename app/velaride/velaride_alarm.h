/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_ALARM_H
#define __APPS_EXAMPLES_VELARIDE_ALARM_H
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct velaride_alarm_s
{
  uint8_t hour;
  uint8_t minute;
  bool enabled;
  bool needs_confirmation;
  bool saving;
  bool ringing;
  int save_error;
  uint32_t completions;
};

void velaride_alarm_get(struct velaride_alarm_s *out);
int velaride_alarm_set(unsigned int hour, unsigned int minute, bool enabled,
                       time_t now);
void velaride_alarm_tick(time_t now);
bool velaride_alarm_take_due(void);
void velaride_alarm_ack(void);
#endif
