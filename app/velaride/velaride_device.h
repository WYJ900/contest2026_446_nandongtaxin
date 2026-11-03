/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELARIDE_VELARIDE_DEVICE_H
#define __APPS_EXAMPLES_VELARIDE_VELARIDE_DEVICE_H

#include <stdbool.h>
#include <stdint.h>

struct velaride_device_snapshot_s
{
  bool usb_valid;
  bool usb_connected;
  bool adc_valid;
  bool light_detected;
  bool magnetometer_detected;
  int32_t reference_mv;
  int adc_error;
  uint32_t adc_samples;
  uint32_t adc_failures;
  uint32_t usb_sample_ms;
  uint32_t adc_sample_ms;
  bool light_valid;
  float light_lux;
  int light_error;
  uint32_t light_sample_ms;
  uint32_t light_samples;
  uint32_t light_failures;
};

struct velaride_display_s
{
  bool automatic;
  bool save_pending;
  uint8_t manual;
  uint8_t target;
  uint8_t applied;
  int apply_error;
  int save_error;
};

#define VELARIDE_ACCENT_COUNT 4
#define VELARIDE_MENU_COUNT 2
#define VELARIDE_TRANSITION_COUNT 3

enum velaride_menu_e
{
  VELARIDE_MENU_HONEY = 0,
  VELARIDE_MENU_LIST
};

enum velaride_transition_e
{
  VELARIDE_TRANSITION_SLIDE = 0,
  VELARIDE_TRANSITION_BLINDS,
  VELARIDE_TRANSITION_IRIS
};

/* Appearance choices made by the wearer; saved by the device thread. */

struct velaride_ui_prefs_s
{
  uint8_t accent;
  uint8_t menu;
  uint8_t transition;
  bool save_pending;
  int save_error;
};

int velaride_device_init(void);
void velaride_device_get(struct velaride_device_snapshot_s *snapshot);
void velaride_display_get(struct velaride_display_s *snapshot);
int velaride_display_set(uint8_t manual, bool automatic);
/* Called every 250 ms by the display thread, then acknowledged after
 * the LCD ioctl completes. No hardware I/O is done by these functions.
 */
uint8_t velaride_display_tick(void);
void velaride_display_applied(uint8_t brightness, int error);
void velaride_ui_prefs_get(struct velaride_ui_prefs_s *snapshot);
int velaride_ui_prefs_set(uint8_t accent, uint8_t menu, uint8_t transition);

/* Screen wake: raise-to-wake and the idle time before the screen turns
 * off. Saved separately from the appearance so older saves stay valid.
 */

#define VELARIDE_SLEEP_COUNT 3

enum velaride_sleep_e
{
  VELARIDE_SLEEP_10S = 0,
  VELARIDE_SLEEP_20S,
  VELARIDE_SLEEP_NEVER
};

struct velaride_wake_prefs_s
{
  bool raise;
  uint8_t sleep;
  bool save_pending;
  int save_error;
};

void velaride_wake_prefs_get(struct velaride_wake_prefs_s *snapshot);
int velaride_wake_prefs_set(bool raise, uint8_t sleep);

#endif
