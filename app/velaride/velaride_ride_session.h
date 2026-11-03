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
};

struct velaride_ride_gps_s
{
  uint32_t distance_m;
  uint32_t time_ms;
  uint32_t avg_speed_centi;
  uint32_t max_speed_centi;
};

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
void velaride_ride_session_second_tick(void);
int velaride_ride_session_finish(struct velaride_ride_record_s *record);
void velaride_ride_session_get(struct velaride_ride_snapshot_s *snapshot);
void velaride_ride_session_get_gps(struct velaride_ride_gps_s *gps);
void velaride_ride_session_ack_alert(void);
int velaride_ride_session_load_latest(struct velaride_ride_record_s *record);
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
