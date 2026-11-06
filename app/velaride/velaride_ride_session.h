/****************************************************************************
 * apps/examples/velaride/velaride_ride_session.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELARIDE_VELARIDE_RIDE_SESSION_H
#define __APPS_EXAMPLES_VELARIDE_VELARIDE_RIDE_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VELARIDE_HISTORY_CAPACITY 16

enum velaride_motion_e
{
  VELARIDE_MOTION_UNKNOWN = 0,
  VELARIDE_MOTION_STILL,
  VELARIDE_MOTION_MOVING,
};

enum velaride_alert_e
{
  VELARIDE_ALERT_NONE = 0,
  VELARIDE_ALERT_REST,
  VELARIDE_ALERT_STILL,
  VELARIDE_ALERT_IMPACT,
  VELARIDE_ALERT_DRINK,
  VELARIDE_ALERT_RESUME,
};

enum velaride_safety_action_e
{
  VELARIDE_SAFETY_NONE = 0,
  VELARIDE_SAFETY_OK,
  VELARIDE_SAFETY_PAUSE,
  VELARIDE_SAFETY_HELP,
  VELARIDE_SAFETY_UNANSWERED,
};

/* Version zero means an older record without safety response information.
 * HELP records intent only: it never means that a message was delivered. */

struct velaride_ride_safety_s
{
  uint16_t checks;
  uint16_t confirmed;
  uint16_t pauses;
  uint16_t help;
  uint16_t unanswered;
  uint8_t last_action;
  uint8_t version;
  uint32_t last_elapsed_s;
};

struct velaride_ride_prefs_s
{
  uint16_t rest_min;
  uint16_t drink_min;
  uint32_t distance_goal_m;
  uint16_t lap_m;
  uint8_t pause_advice;
};

struct velaride_ride_extras_s
{
  uint32_t distance_goal_m;
  uint16_t lap_count;
  uint16_t rest_taken;
  uint16_t rest_snoozed;
  uint16_t rest_ignored;
  uint16_t drink_count;
  uint16_t drink_done;
  uint16_t drink_snoozed;
  uint16_t drink_ignored;
  uint8_t version;
  uint8_t reserved;
};

enum velaride_reminder_action_e
{
  VELARIDE_REMINDER_TAKE = 1,
  VELARIDE_REMINDER_SNOOZE,
  VELARIDE_REMINDER_IGNORE,
};

struct velaride_ride_snapshot_s
{
  bool active;
  bool paused;
  bool sensor_ready;
  enum velaride_motion_e motion;
  enum velaride_alert_e alert;
  uint32_t elapsed_s;
  uint32_t moving_s;
  uint32_t still_s;
  uint16_t pause_count;
  uint16_t impact_count;
  uint16_t rest_count;
  uint32_t goal_s;
  bool goal_reached;
  uint8_t safety_remaining_s;
  struct velaride_ride_safety_s safety;
  struct velaride_ride_extras_s extras;
  uint32_t last_lap_s;
  uint32_t last_lap_distance_m;
  bool distance_goal_reached;
};

struct velaride_ride_gps_s
{
  uint32_t distance_m;
  uint32_t time_ms;
  uint32_t avg_speed_centi;
  uint32_t max_speed_centi;
};

struct velaride_nav_s
{
  uint32_t distance_m;
  uint8_t direction;
  bool off_route;
};

int velaride_nav_set(uint8_t direction, uint32_t distance_m, bool off_route);
void velaride_nav_clear(void);
bool velaride_nav_get(struct velaride_nav_s *nav);

struct velaride_ride_record_s
{
  uint32_t sequence;
  int64_t start_epoch;
  int64_t end_epoch;
  uint32_t elapsed_s;
  uint32_t moving_s;
  uint32_t still_s;
  uint16_t pause_count;
  uint16_t impact_count;
  uint16_t rest_count;
  struct velaride_ride_gps_s gps;
  struct velaride_ride_safety_s safety;
  struct velaride_ride_extras_s extras;
};

int velaride_ride_session_init(void);
void velaride_ride_session_start(void);
void velaride_ride_session_start_with_goal(uint32_t goal_s);
int velaride_ride_session_probe_sensor(void);

/* Raw accelerometer sample for the level tool; opens the sensor if needed. */

int velaride_ride_session_read_accel(int16_t *x, int16_t *y, int16_t *z);
void velaride_ride_session_set_connected(bool connected);
bool velaride_ride_session_connected(void);
void velaride_ride_session_set_paused(bool paused);
void velaride_ride_session_sensor_tick(void);
void velaride_ride_session_motion_debug(int32_t *accel_delta,
                                        int32_t *gyro_raw,
                                        int32_t *gyro_sum,
                                        int32_t bias[3]);
void velaride_ride_session_second_tick(void);
int velaride_ride_session_finish(struct velaride_ride_record_s *record);
void velaride_ride_session_get(struct velaride_ride_snapshot_s *snapshot);
void velaride_ride_session_get_gps(struct velaride_ride_gps_s *gps);
void velaride_ride_session_ack_alert(void);
int velaride_ride_session_respond_safety(enum velaride_safety_action_e action);
void velaride_ride_prefs_get(struct velaride_ride_prefs_s *prefs);
int velaride_ride_prefs_set(const struct velaride_ride_prefs_s *prefs);
int velaride_ride_session_respond_reminder(enum velaride_reminder_action_e action);
int velaride_ride_session_lap(void);
int velaride_ride_session_load_latest(struct velaride_ride_record_s *record);
int velaride_ride_history_list(uint32_t *sequences, size_t capacity,
                               unsigned int *corrupt);
int velaride_ride_history_get(uint32_t sequence,
                              struct velaride_ride_record_s *record);
int velaride_ride_session_save_record(
  const struct velaride_ride_record_s *record);

/* Phone gateway data is shared between the BLE task and the UI task. */

void velaride_ride_session_set_phone_data(double speed_kmh,
                                          double distance_km);
void velaride_ride_session_clear_phone_data(void);
bool velaride_ride_session_get_phone_data(double *speed_kmh,
                                          double *distance_km);

/* Latest cloud/phone generated review, persisted for reconnect and reboot. */

int velaride_ride_session_save_review(const char *review);
int velaride_ride_session_load_review(char *review, size_t size);
int velaride_ride_session_clear_review(void);
bool velaride_ride_session_take_review_view_request(void);

#endif
