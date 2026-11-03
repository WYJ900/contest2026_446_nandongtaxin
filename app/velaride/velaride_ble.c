/****************************************************************************
 * apps/examples/velaride/velaride_ble.c
 *
 * Minimal VelaRide BLE peripheral built directly on ZBlue.  It deliberately
 * avoids the Bluetooth Framework service/libuv layer because ZBlue callbacks
 * run on the Zephyr-compatible system work queue.
 *
 * Service 0xFFF0
 *   TX 0xFFF1: Notify
 *   RX 0xFFF2: Write / Write Without Response
 *
 * RX accepts newline-delimited JSON. The browser splits long JSON into ATT
 * writes; this task reassembles it until '\n'. TX responses use the same
 * framing and are split into conservative 18-byte notification chunks.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>

#include "velaride_ride_session.h"
#include "velaride_timekeeper.h"

#define VELARIDE_RX_MAX 768
#define VELARIDE_TX_CHUNK 18

static struct bt_uuid_16 g_service_uuid = BT_UUID_INIT_16(0xfff0);
static struct bt_uuid_16 g_tx_uuid = BT_UUID_INIT_16(0xfff1);
static struct bt_uuid_16 g_rx_uuid = BT_UUID_INIT_16(0xfff2);

static bool g_notify_enabled;
static char *g_rx_buffer;
static size_t g_rx_length;
static bool g_rx_discard;
static bool g_started;
static bool g_restart_adv;
static bool g_radio_ready;
static bool g_radio_connected;
static bool g_adv_paused;
static int g_adv_request;
static char *g_pending_json;
static struct bt_conn *g_pending_conn;
static sem_t g_command_ready;
static pthread_mutex_t g_command_lock = PTHREAD_MUTEX_INITIALIZER;

static void velaride_ccc_changed(const struct bt_gatt_attr *attr,
                                 uint16_t value)
{
  (void)attr;
  g_notify_enabled = value == BT_GATT_CCC_NOTIFY;
  /* A host may close its GATT session while the OS retains the physical
   * connection. Treat a subscription change as a fresh phone session.
   */

  velaride_ride_session_clear_phone_data();
  printf("VELARIDE_BLE: notify=%d\n", g_notify_enabled);
}

static ssize_t velaride_command_written(struct bt_conn *conn,
                                        const struct bt_gatt_attr *attr,
                                        const void *buf, uint16_t len,
                                        uint16_t offset, uint8_t flags);

static struct bt_gatt_attr g_attributes[] =
{
  BT_GATT_PRIMARY_SERVICE(&g_service_uuid),
  BT_GATT_CHARACTERISTIC(&g_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_NONE, NULL, NULL, NULL),
  BT_GATT_CCC(velaride_ccc_changed,
              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
  BT_GATT_CHARACTERISTIC(&g_rx_uuid.uuid,
                         BT_GATT_CHRC_WRITE |
                         BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                         BT_GATT_PERM_WRITE, NULL,
                         velaride_command_written, NULL),
};

static struct bt_gatt_service g_service = BT_GATT_SERVICE(g_attributes);

/* Notifications may wait for TX credits.  The GATT callback runs on the
 * same work queue that returns those credits, so only queue work here.
 */

static int velaride_queue_command(struct bt_conn *conn, const char *json)
{
  int ret = 0;

  pthread_mutex_lock(&g_command_lock);
  if (g_pending_conn != NULL)
    {
      ret = -EBUSY;
    }
  else
    {
      strlcpy(g_pending_json, json, VELARIDE_RX_MAX);
      g_pending_conn = bt_conn_ref(conn);
      sem_post(&g_command_ready);
    }

  pthread_mutex_unlock(&g_command_lock);
  return ret;
}

static int json_get_number(const char *json, const char *key, double *value)
{
  char needle[48];
  char *end;
  const char *pos;

  snprintf(needle, sizeof(needle), "\"%s\"", key);
  pos = strstr(json, needle);
  if (pos == NULL || (pos = strchr(pos + strlen(needle), ':')) == NULL)
    {
      return -ENOENT;
    }

  *value = strtod(pos + 1, &end);
  return end == pos + 1 || !isfinite(*value) ? -EINVAL : 0;
}

static int json_get_string(const char *json, const char *key,
                           char *value, size_t size)
{
  char needle[48];
  const char *pos;
  size_t out = 0;

  snprintf(needle, sizeof(needle), "\"%s\"", key);
  pos = strstr(json, needle);
  if (pos == NULL || (pos = strchr(pos + strlen(needle), ':')) == NULL)
    {
      return -ENOENT;
    }

  while (*++pos == ' ' || *pos == '\t')
    {
    }

  if (*pos++ != '"')
    {
      return -EINVAL;
    }

  while (*pos != '\0' && *pos != '"' && out + 1 < size)
    {
      if (*pos == '\\' && pos[1] != '\0')
        {
          pos++;
          value[out++] = *pos == 'n' ? '\n' : *pos;
        }
      else
        {
          value[out++] = *pos;
        }

      pos++;
    }

  value[out] = '\0';
  return *pos == '"' ? 0 : -EMSGSIZE;
}

static int velaride_notify_json(struct bt_conn *conn, const char *json)
{
  char framed[768];
  size_t length;
  size_t offset = 0;
  int ret = 0;

  if (!g_notify_enabled)
    {
      return -EACCES;
    }

  snprintf(framed, sizeof(framed), "%s\n", json);
  length = strlen(framed);
  while (offset < length)
    {
      size_t chunk = MIN(length - offset, VELARIDE_TX_CHUNK);
      ret = bt_gatt_notify(conn, &g_attributes[2], framed + offset, chunk);
      if (ret != 0)
        {
          break;
        }

      offset += chunk;
      usleep(5000);
    }

  return ret;
}

static void velaride_handle_json(struct bt_conn *conn, const char *json)
{
  struct velaride_ride_snapshot_s ride;
  struct velaride_ride_record_s record;
  struct velaride_ride_gps_s gps;
  char cmd[32];
  char review[384];
  char response[896];
  double epoch;
  double speed;
  double distance;
  double sequence;
  double phone_speed;
  double phone_distance;
  bool phone_valid;
  int ret;

  if (json_get_string(json, "cmd", cmd, sizeof(cmd)) < 0)
    {
      velaride_notify_json(conn,
                           "{\"ok\":false,\"error\":\"missing_cmd\"}");
      return;
    }

  if (strcmp(cmd, "ping") == 0)
    {
      velaride_notify_json(conn, "{\"ok\":true,\"event\":\"pong\"}");
    }
  else if (strcmp(cmd, "time_sync") == 0)
    {
      struct timespec ts;

      if (json_get_number(json, "epoch", &epoch) < 0 || epoch < 1704067200.0)
        {
          velaride_notify_json(conn,
                               "{\"ok\":false,\"error\":\"bad_epoch\"}");
          return;
        }

      ts.tv_sec = (time_t)epoch;
      ts.tv_nsec = 0;
      ret = clock_settime(CLOCK_REALTIME, &ts);
      if (ret == 0)
        {
          ret = velaride_timekeeper_save_now();
        }

      snprintf(response, sizeof(response),
               "{\"ok\":%s,\"event\":\"time_sync\",\"epoch\":%" PRId64 "}",
               ret == 0 ? "true" : "false", (int64_t)time(NULL));
      velaride_notify_json(conn, response);
    }
  else if (strcmp(cmd, "phone_data") == 0)
    {
      if (json_get_number(json, "speed", &speed) < 0 ||
          speed < 0 || speed > 200 ||
          json_get_number(json, "distance", &distance) < 0 ||
          distance < 0 || distance > 100000)
        {
          velaride_notify_json(conn,
            "{\"ok\":false,\"error\":\"bad_phone_data\"}");
          return;
        }

      velaride_ride_session_set_phone_data(speed, distance);
      velaride_notify_json(conn,
                           "{\"ok\":true,\"event\":\"phone_data\"}");
    }
  else if (strcmp(cmd, "phone_clear") == 0)
    {
      velaride_ride_session_clear_phone_data();
      velaride_notify_json(conn,
                           "{\"ok\":true,\"event\":\"phone_clear\"}");
    }
  else if (strcmp(cmd, "ride_status") == 0)
    {
      phone_speed = 0;
      phone_distance = 0;
      phone_valid = velaride_ride_session_get_phone_data(&phone_speed,
                                                       &phone_distance);
      velaride_ride_session_get(&ride);
      velaride_ride_session_get_gps(&gps);
      ret = velaride_ride_session_load_latest(&record);
      snprintf(response, sizeof(response),
               "{\"ok\":true,\"event\":\"ride_status\","
               "\"active\":%s,\"paused\":%s,\"motion\":%d,"
               "\"elapsed_s\":%" PRIu32 ",\"moving_s\":%" PRIu32 ","
               "\"still_s\":%" PRIu32 ",\"impacts\":%u,"
               "\"pause_count\":%u,\"rest_count\":%u,"
               "\"latest_sequence\":%" PRIu32 ",\"phone_valid\":%s,"
               "\"speed_kmh\":%.2f,\"distance_km\":%.3f,"
               "\"ride_gps_time_ms\":%" PRIu32 ",\"ride_distance_m\":%" PRIu32 ","
               "\"safety_pending\":%s,\"safety_remaining_s\":%u,"
               "\"safety_help\":%u,\"safety_last_action\":%u}",
               ride.active ? "true" : "false",
               ride.paused ? "true" : "false", ride.motion,
               ride.elapsed_s, ride.moving_s, ride.still_s,
               ride.impact_count, ride.pause_count, ride.rest_count,
               ret == 0 ? record.sequence : 0,
               phone_valid ? "true" : "false", phone_speed, phone_distance,
               gps.time_ms, gps.distance_m,
               ride.active && ride.alert == VELARIDE_ALERT_IMPACT ? "true" : "false",
               ride.safety_remaining_s, ride.safety.help, ride.safety.last_action);
      velaride_notify_json(conn, response);
    }
  else if (strcmp(cmd, "ride_history") == 0)
    {
      uint32_t sequences[VELARIDE_HISTORY_CAPACITY];
      unsigned int corrupt = 0;
      int count = velaride_ride_history_list(sequences,
                    VELARIDE_HISTORY_CAPACITY, &corrupt);
      int used;
      int i;

      if (count < 0)
        {
          velaride_notify_json(conn, "{\"ok\":false,\"error\":\"history_unavailable\"}");
          return;
        }
      used = snprintf(response, sizeof(response),
              "{\"ok\":true,\"event\":\"ride_history\",\"capacity\":%u,"
              "\"corrupt_count\":%u,\"sequences\":[",
              VELARIDE_HISTORY_CAPACITY, corrupt);
      for (i = 0; i < count; i++)
        {
          used += snprintf(response + used, sizeof(response) - used,
                           "%s%" PRIu32, i == 0 ? "" : ",", sequences[i]);
        }
      snprintf(response + used, sizeof(response) - used, "]}");
      velaride_notify_json(conn, response);
    }
  else if (strcmp(cmd, "ride_latest") == 0 || strcmp(cmd, "ride_get") == 0)
    {
      if (strcmp(cmd, "ride_get") == 0)
        {
          if (json_get_number(json, "sequence", &sequence) < 0 ||
              sequence < 0 || sequence > UINT32_MAX ||
              sequence != (uint32_t)sequence)
            {
              velaride_notify_json(conn, "{\"ok\":false,\"error\":\"bad_sequence\"}");
              return;
            }
          ret = velaride_ride_history_get((uint32_t)sequence, &record);
        }
      else
        {
          ret = velaride_ride_session_load_latest(&record);
        }
      if (ret < 0)
        {
          velaride_notify_json(conn,
            strcmp(cmd, "ride_get") == 0 ?
            "{\"ok\":false,\"error\":\"history_changed\"}" :
            "{\"ok\":false,\"error\":\"no_saved_ride\"}");
          return;
        }

      snprintf(response, sizeof(response),
               "{\"ok\":true,\"event\":\"%s\","
               "\"sequence\":%" PRIu32 ",\"active\":false,"
               "\"elapsed_s\":%" PRIu32 ",\"moving_s\":%" PRIu32 ","
               "\"still_s\":%" PRIu32 ",\"impacts\":%u,"
               "\"pause_count\":%u,\"rest_count\":%u,"
               "\"gps_time_ms\":%" PRIu32 ",\"gps_distance_m\":%" PRIu32 ","
               "\"gps_avg_centi\":%" PRIu32 ",\"gps_max_centi\":%" PRIu32 ","
               "\"safety_version\":%u,\"safety_checks\":%u,"
               "\"safety_confirmed\":%u,\"safety_pauses\":%u,"
               "\"safety_help\":%u,\"safety_unanswered\":%u,"
               "\"safety_last_action\":%u,\"safety_last_elapsed_s\":%" PRIu32 "}",
               strcmp(cmd, "ride_get") == 0 ? "ride_record" : "ride_latest",
               record.sequence, record.elapsed_s, record.moving_s,
               record.still_s, record.impact_count, record.pause_count,
               record.rest_count, record.gps.time_ms, record.gps.distance_m,
               record.gps.avg_speed_centi, record.gps.max_speed_centi,
               record.safety.version, record.safety.checks,
               record.safety.confirmed, record.safety.pauses, record.safety.help,
               record.safety.unanswered, record.safety.last_action,
               record.safety.last_elapsed_s);
      velaride_notify_json(conn, response);
    }
  else if (strcmp(cmd, "ride_review") == 0)
    {
      velaride_ride_session_get(&ride);
      if (ride.active)
        {
          velaride_notify_json(conn,
            "{\"ok\":false,\"error\":\"ride_in_progress\"}");
          return;
        }

      if (json_get_number(json, "sequence", &sequence) == 0 &&
          (velaride_ride_session_load_latest(&record) < 0 ||
           sequence != record.sequence))
        {
          velaride_notify_json(conn,
            "{\"ok\":false,\"error\":\"stale_ride\"}");
          return;
        }

      if (json_get_string(json, "text", review, sizeof(review)) < 0 ||
          review[0] == '\0' || strlen(review) > 260)
        {
          velaride_notify_json(conn,
                               "{\"ok\":false,\"error\":\"bad_review\"}");
          return;
        }

      ret = velaride_ride_session_save_review(review);
      snprintf(response, sizeof(response),
               "{\"ok\":%s,\"event\":\"ride_review\"}",
               ret == 0 ? "true" : "false");
      velaride_notify_json(conn, response);
    }
  else
    {
      velaride_notify_json(conn,
                           "{\"ok\":false,\"error\":\"unknown_cmd\"}");
    }
}

static ssize_t velaride_command_written(struct bt_conn *conn,
                                        const struct bt_gatt_attr *attr,
                                        const void *buf, uint16_t len,
                                        uint16_t offset, uint8_t flags)
{
  const uint8_t *bytes = buf;
  uint16_t i;

  (void)attr;
  (void)flags;

  if (offset != 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

  /* Keep legacy ping support for nRF Connect smoke tests. */

  if (g_rx_length == 0 && len == 4 && memcmp(buf, "ping", 4) == 0)
    {
      if (velaride_queue_command(conn, "{\"cmd\":\"ping\"}") < 0)
        {
          return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
        }

      return len;
    }

  printf("VELARIDE_BLE: rx len=%u\n", len);
  for (i = 0; i < len; i++)
    {
      if (bytes[i] == '\n')
        {
          if (g_rx_discard)
            {
              g_rx_discard = false;
              g_rx_length = 0;
              continue;
            }
          g_rx_buffer[g_rx_length] = '\0';
          g_rx_length = 0;
          if (velaride_queue_command(conn, g_rx_buffer) < 0)
            {
              return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
            }
        }
      else if (g_rx_discard)
        {
          continue;
        }
      else if (g_rx_length + 1 < VELARIDE_RX_MAX)
        {
          g_rx_buffer[g_rx_length++] = bytes[i];
        }
      else
        {
          g_rx_length = 0;
          g_rx_discard = true;
          return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
    }

  return len;
}

static void velaride_connected(struct bt_conn *conn, uint8_t err)
{
  (void)conn;
  if (err != 0)
    {
      printf("VELARIDE_BLE: connect failed err=%u\n", err);
      return;
    }

  printf("VELARIDE_BLE: connected\n");
  pthread_mutex_lock(&g_command_lock);
  g_radio_connected = true;
  pthread_mutex_unlock(&g_command_lock);
  velaride_ride_session_set_connected(true);
}

static void velaride_disconnected(struct bt_conn *conn, uint8_t reason)
{
  (void)conn;
  g_notify_enabled = false;
  velaride_ride_session_set_connected(false);
  g_rx_length = 0;
  g_rx_discard = false;
  pthread_mutex_lock(&g_command_lock);
  g_radio_connected = false;
  g_restart_adv = true;
  pthread_mutex_unlock(&g_command_lock);
  sem_post(&g_command_ready);
  printf("VELARIDE_BLE: disconnected reason=%u\n", reason);
}

static struct bt_conn_cb g_conn_callbacks =
{
  .connected = velaride_connected,
  .disconnected = velaride_disconnected,
};

static const struct bt_data g_adv_data[] =
{
  BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
  BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(0xfff0)),
};

static const struct bt_data g_scan_response[] =
{
  BT_DATA(BT_DATA_NAME_COMPLETE, "VelaRide", sizeof("VelaRide") - 1),
};

static void velaride_bt_ready(uint8_t dev_id, int err)
{
  int ret;

  (void)dev_id;
  if (err != 0)
    {
      printf("VELARIDE_BLE: enable failed err=%d\n", err);
      return;
    }

  printf("VELARIDE_BLE: adapter ready\n");

  /* ZBlue creates the per-controller GATT context during bt_enable().
   * Registering a dynamic service before this callback dereferences an
   * uninitialized GATT database. */
  ret = bt_gatt_service_register(&g_service);
  if (ret != 0 && ret != -EALREADY)
    {
      printf("VELARIDE_BLE: service register failed ret=%d\n", ret);
      return;
    }

  ret = bt_conn_cb_register(&g_conn_callbacks);
  if (ret != 0 && ret != -EEXIST)
    {
      printf("VELARIDE_BLE: callback register failed ret=%d\n", ret);
      return;
    }

  printf("VELARIDE_BLE: service ready\n");
  ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                        g_adv_data, ARRAY_SIZE(g_adv_data),
                        g_scan_response, ARRAY_SIZE(g_scan_response));
  printf("VELARIDE_BLE: advertising ret=%d\n", ret);
  pthread_mutex_lock(&g_command_lock);
  g_radio_ready = true;
  pthread_mutex_unlock(&g_command_lock);
}

int main(int argc, char *argv[])
{
  struct bt_conn *conn;
  char command[VELARIDE_RX_MAX];
  bool restart_adv;
  bool radio_ready;
  bool radio_connected;
  bool adv_paused;
  bool was_paused;
  bool pause;
  bool status;
  int adv_request;
  int ret;

  if (argc > 1)
    {
      status = strcmp(argv[1], "radio-status") == 0;
      pause = strcmp(argv[1], "adv-off") == 0;
      if (argc != 2 || (!status && !pause &&
                       strcmp(argv[1], "adv-on") != 0))
        {
          printf("Usage: velaride_ble [radio-status|adv-off|adv-on]\n");
          return 1;
        }

      pthread_mutex_lock(&g_command_lock);
      radio_ready = g_radio_ready;
      radio_connected = g_radio_connected;
      adv_paused = g_adv_paused;
      if (!status && radio_ready && (!pause || !radio_connected))
        {
          g_adv_request = pause ? 1 : -1;
        }

      pthread_mutex_unlock(&g_command_lock);
      if (status)
        {
          printf("VELARIDE_BLE: ready=%d connected=%d adv_paused=%d\n",
                 radio_ready, radio_connected, adv_paused);
          return 0;
        }

      if (!radio_ready || (pause && radio_connected))
        {
          printf("VELARIDE_BLE: radio not ready or connection active\n");
          return 1;
        }

      sem_post(&g_command_ready);
      printf("VELARIDE_BLE: advertising %s requested\n",
             pause ? "pause" : "resume");
      return 0;
    }

  if (g_started)
    {
      printf("VELARIDE_BLE: already started\n");
      return 0;
    }

  g_rx_buffer = malloc(VELARIDE_RX_MAX * 2);
  if (g_rx_buffer == NULL)
    {
      printf("VELARIDE_BLE: rx buffer allocation failed\n");
      return 1;
    }

  g_pending_json = g_rx_buffer + VELARIDE_RX_MAX;
  if (sem_init(&g_command_ready, 0, 0) < 0)
    {
      free(g_rx_buffer);
      g_rx_buffer = NULL;
      g_pending_json = NULL;
      return 1;
    }

  ret = bt_enable(velaride_bt_ready);
  if (ret != 0)
    {
      printf("VELARIDE_BLE: bt_enable failed ret=%d\n", ret);
      free(g_rx_buffer);
      g_rx_buffer = NULL;
      g_pending_json = NULL;
      sem_destroy(&g_command_ready);
      return 1;
    }

  g_started = true;
  printf("VELARIDE_BLE: enabling adapter\n");
  for (;;)
    {
      while (sem_wait(&g_command_ready) < 0)
        {
          if (errno != EINTR)
            {
              return 1;
            }
        }

      pthread_mutex_lock(&g_command_lock);
      conn = g_pending_conn;
      if (conn != NULL)
        {
          strlcpy(command, g_pending_json, sizeof(command));
        }

      g_pending_conn = NULL;
      restart_adv = g_restart_adv;
      g_restart_adv = false;
      adv_request = g_adv_request;
      g_adv_request = 0;
      was_paused = g_adv_paused;
      if (adv_request != 0)
        {
          g_adv_paused = adv_request > 0;
        }

      adv_paused = g_adv_paused;
      pthread_mutex_unlock(&g_command_lock);
      if (adv_request > 0)
        {
          ret = bt_le_adv_stop();
          printf("VELARIDE_BLE: advertising pause ret=%d\n", ret);
          if (ret < 0)
            {
              pthread_mutex_lock(&g_command_lock);
              g_adv_paused = was_paused;
              pthread_mutex_unlock(&g_command_lock);
            }
        }

      if (adv_request < 0)
        {
          restart_adv = true;
        }

      if (restart_adv && !adv_paused)
        {
          ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                               g_adv_data, ARRAY_SIZE(g_adv_data),
                               g_scan_response, ARRAY_SIZE(g_scan_response));
          printf("VELARIDE_BLE: advertising restart ret=%d\n", ret);
        }

      if (conn != NULL)
        {
          velaride_handle_json(conn, command);
          bt_conn_unref(conn);
        }
    }
}
