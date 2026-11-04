/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_ACTIVITY_H
#define __APPS_EXAMPLES_VELARIDE_ACTIVITY_H
#include <stdbool.h>
#include <stdint.h>
struct velaride_activity_s
{
  bool enabled;
  bool sensor_ready;
  bool quiet;
  bool riding;
  bool pending;
  uint8_t interval_min;
  uint8_t hours; /* 0:09-21, 1:06-22, 2:all day */
  uint32_t low_activity_s;
  uint32_t active_s;
  uint32_t reminders;
  uint32_t manual_acknowledgements;
  uint32_t snooze_remaining_s;
  int save_error;
};
void velaride_activity_get(struct velaride_activity_s *out);
int velaride_activity_set(bool enabled, uint8_t interval_min, uint8_t hours);
void velaride_activity_sample(uint32_t now, unsigned int local_minute,
                              bool time_valid, bool riding, bool valid,
                              int16_t x, int16_t y, int16_t z);
bool velaride_activity_take_due(void);
int velaride_activity_ack(bool snooze);
#endif
