/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __APPS_EXAMPLES_VELARIDE_COMPASS_H
#define __APPS_EXAMPLES_VELARIDE_COMPASS_H
#include <stdbool.h>
#include <stdint.h>
struct velaride_compass_s
{
  bool enabled;
  bool valid;
  bool calibrated;
  bool calibrating;
  bool heading_valid;
  bool save_pending;
  float x, y, z;
  float field_ut;
  float heading;
  uint16_t collected;
  int error;
  int calibration_error;
  int save_error;
  uint32_t samples;
  uint32_t failures;
};
void velaride_compass_init(void);
void velaride_compass_enable(bool enable);
void velaride_compass_sample(void);
void velaride_compass_get(struct velaride_compass_s *snapshot);
void velaride_compass_calibrate(void);
int velaride_compass_finish(void);
void velaride_compass_cancel(void);
#endif
