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

static void velaride_ccc_changed(const struct bt_gatt_attr *attr,
                                 uint16_t value)
{
  (void)attr;
  g_notify_enabled = value == BT_GATT_CCC_NOTIFY;
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
  return end == pos + 1 ? -EINVAL : 0;
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
  char cmd[32];
  char review[384];
  char response[640];
  double epoch;
  double speed;
  double distance;
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
      if (json_get_number(json, "speed", &speed) < 0)
        {
          speed = 0.0;
        }

      if (json_get_number(json, "distance", &distance) < 0)
        {
          distance = 0.0;
        }

      velaride_ride_session_set_phone_data(speed, distance);
      velaride_notify_json(conn,
                           "{\"ok\":true,\"event\":\"phone_data\"}");
    }
  else if (strcmp(cmd, "ride_status") == 0)
    {
      velaride_ride_session_get(&ride);
      ret = velaride_ride_session_load_latest(&record);
      snprintf(response, sizeof(response),
               "{\"ok\":true,\"event\":\"ride_status\","
               "\"active\":%s,\"paused\":%s,\"motion\":%d,"
               "\"elapsed_s\":%" PRIu32 ",\"moving_s\":%" PRIu32 ","
               "\"still_s\":%" PRIu32 ",\"impacts\":%u,"
               "\"latest_sequence\":%" PRIu32 "}",
               ride.active ? "true" : "false",
               ride.paused ? "true" : "false", ride.motion,
               ride.elapsed_s, ride.moving_s, ride.still_s,
               ride.impact_count, ret == 0 ? record.sequence : 0);
      velaride_notify_json(conn, response);
    }
  else if (strcmp(cmd, "ride_review") == 0)
    {
      if (json_get_string(json, "text", review, sizeof(review)) < 0)
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
      velaride_notify_json(conn, "{\"ok\":true,\"event\":\"pong\"}");
      return len;
    }

  printf("VELARIDE_BLE: rx len=%u\n", len);
  for (i = 0; i < len; i++)
    {
      if (bytes[i] == '\n')
        {
          g_rx_buffer[g_rx_length] = '\0';
          velaride_handle_json(conn, g_rx_buffer);
          g_rx_length = 0;
        }
      else if (g_rx_length + 1 < VELARIDE_RX_MAX)
        {
          g_rx_buffer[g_rx_length++] = bytes[i];
        }
      else
        {
          g_rx_length = 0;
          velaride_notify_json(conn,
                               "{\"ok\":false,\"error\":\"frame_too_large\"}");
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
}

static void velaride_disconnected(struct bt_conn *conn, uint8_t reason)
{
  (void)conn;
  g_notify_enabled = false;
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
}

int main(int argc, char *argv[])
{
  int ret;

  (void)argc;
  (void)argv;

  g_rx_buffer = malloc(VELARIDE_RX_MAX);
  if (g_rx_buffer == NULL)
    {
      printf("VELARIDE_BLE: rx buffer allocation failed\n");
      return 1;
    }

  ret = bt_enable(velaride_bt_ready);
  if (ret != 0)
    {
      printf("VELARIDE_BLE: bt_enable failed ret=%d\n", ret);
      return 1;
    }

  printf("VELARIDE_BLE: enabling adapter\n");
  for (;;)
    {
      sleep(60);
    }
}
