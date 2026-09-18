/****************************************************************************
 * apps/examples/velaride/velaride_timekeeper.c
 *
 * The Huangshan Pi RTC domain resets when this board loses its only power
 * source.  Keep the last calibrated wall-clock value in the independent
 * config partition and use it as a boot-time floor until BLE/NTP can provide
 * a fresh value.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <kvdb.h>

#include "velaride_timekeeper.h"

#define TIME_RECORD_MAGIC 0x5652544du
#define TIME_RECORD_VERSION 1
#define TIME_RECORD_KEY "persist.velaride.wallclock"

/* 2024-01-01 00:00:00 UTC.  Earlier values mean the RTC lost power. */

#define TIME_VALID_MIN_EPOCH INT64_C(1704067200)

struct time_record_disk_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  int64_t epoch;
  uint32_t crc32;
};

static uint32_t time_crc32(const void *data, size_t size)
{
  const uint8_t *bytes = data;
  uint32_t crc = UINT32_MAX;
  size_t i;
  int bit;

  for (i = 0; i < size; i++)
    {
      crc ^= bytes[i];
      for (bit = 0; bit < 8; bit++)
        {
          crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

  return ~crc;
}

int velaride_timekeeper_read(int64_t *epoch)
{
  struct time_record_disk_s disk;
  uint32_t expected;
  ssize_t ret;

  memset(&disk, 0, sizeof(disk));
  ret = property_get_buffer(TIME_RECORD_KEY, &disk, sizeof(disk));
  if (ret != sizeof(disk) || disk.magic != TIME_RECORD_MAGIC ||
      disk.version != TIME_RECORD_VERSION || disk.size != sizeof(disk))
    {
      return ret < 0 ? (int)ret : -EINVAL;
    }

  expected = time_crc32(&disk,
                        offsetof(struct time_record_disk_s, crc32));
  if (expected != disk.crc32 || disk.epoch < TIME_VALID_MIN_EPOCH)
    {
      return -EBADMSG;
    }

  if (epoch != NULL)
    {
      *epoch = disk.epoch;
    }

  return 0;
}

int velaride_timekeeper_save_now(void)
{
  struct time_record_disk_s disk;
  time_t now;
  int ret;

  now = time(NULL);
  if ((int64_t)now < TIME_VALID_MIN_EPOCH)
    {
      return -ERANGE;
    }

  memset(&disk, 0, sizeof(disk));
  disk.magic = TIME_RECORD_MAGIC;
  disk.version = TIME_RECORD_VERSION;
  disk.size = sizeof(disk);
  disk.epoch = (int64_t)now;
  disk.crc32 = time_crc32(&disk,
                          offsetof(struct time_record_disk_s, crc32));

  ret = property_set_buffer(TIME_RECORD_KEY, &disk, sizeof(disk));
  if (ret == 0)
    {
      ret = property_commit();
    }

  printf("VELARIDE_TIME: save epoch=%" PRId64 " ret=%d\n",
         disk.epoch, ret);
  return ret;
}

int velaride_timekeeper_restore(void)
{
  struct timespec ts;
  int64_t saved_epoch;
  time_t now;
  int ret;

  now = time(NULL);
  if ((int64_t)now >= TIME_VALID_MIN_EPOCH)
    {
      printf("VELARIDE_TIME: RTC valid epoch=%" PRId64 "\n",
             (int64_t)now);
      return 0;
    }

  ret = velaride_timekeeper_read(&saved_epoch);
  if (ret < 0)
    {
      printf("VELARIDE_TIME: no fallback ret=%d rtc=%" PRId64 "\n",
             ret, (int64_t)now);
      return ret;
    }

  ts.tv_sec = (time_t)saved_epoch;
  ts.tv_nsec = 0;
  if (clock_settime(CLOCK_REALTIME, &ts) < 0)
    {
      ret = -errno;
      printf("VELARIDE_TIME: restore failed ret=%d\n", ret);
      return ret;
    }

  printf("VELARIDE_TIME: restored epoch=%" PRId64 "\n", saved_epoch);
  return 0;
}
