/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_NIGHT_H
#define __APPS_EXAMPLES_VELARIDE_NIGHT_H

#include <stdbool.h>
#include <stdint.h>

enum velaride_night_mode_e
{
  VR_NIGHT_OFF = 0,
  VR_NIGHT_ON,
  VR_NIGHT_AUTO
};

struct velaride_night_s
{
  uint8_t mode;
  uint8_t limit;
  bool active;
  bool exempt;
  bool light_valid;
  bool saving;
  float lux;
  int save_error;
};

void velaride_night_get(struct velaride_night_s *out);
int velaride_night_set(uint8_t mode, uint8_t limit);
void velaride_night_poll(uint32_t now, uint32_t sample_id,
                         bool valid, float lux);
void velaride_night_exempt(bool exempt);
uint8_t velaride_night_brightness(uint8_t normal);

#endif
