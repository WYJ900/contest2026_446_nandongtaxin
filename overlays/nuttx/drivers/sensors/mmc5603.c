/****************************************************************************
 * MMC5603 magnetic field sensor, bounded one-shot fetch.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#include <nuttx/config.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>
#include <nuttx/sensors/mmc5603.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb.h>

#define MMC5603_ADDR 0x30
#define MMC5603_ID 0x39
#define MMC5603_CTRL0 0x1b
#define MMC5603_CTRL1 0x1c
#define MMC5603_STATUS 0x18

struct mmc5603_dev_s
{
  struct sensor_lowerhalf_s lower;
  FAR struct i2c_master_s *i2c;
  bool enabled;
};

static int mmc5603_read(FAR struct mmc5603_dev_s *priv, uint8_t reg,
                       FAR uint8_t *data, int size)
{
  struct i2c_msg_s msg[2] =
  {
    {.frequency = 100000, .addr = MMC5603_ADDR, .flags = I2C_M_NOSTOP,
     .buffer = &reg, .length = 1},
    {.frequency = 100000, .addr = MMC5603_ADDR, .flags = I2C_M_READ,
     .buffer = data, .length = size}
  };
  return I2C_TRANSFER(priv->i2c, msg, 2);
}

static int mmc5603_write(FAR struct mmc5603_dev_s *priv, uint8_t reg,
                        uint8_t value)
{
  uint8_t data[] = {reg, value};
  struct i2c_msg_s msg =
  {
    .frequency = 100000, .addr = MMC5603_ADDR, .flags = 0,
    .buffer = data, .length = 2
  };
  return I2C_TRANSFER(priv->i2c, &msg, 1);
}

static int mmc5603_activate(FAR struct sensor_lowerhalf_s *lower,
                           FAR struct file *filep, bool enable)
{
  FAR struct mmc5603_dev_s *priv = (FAR struct mmc5603_dev_s *)lower;
  int ret;

  if (enable)
    {
      /* Set/reset clears residual magnetization; no continuous conversions. */
      ret = mmc5603_write(priv, MMC5603_CTRL0, 0x08);
      if (ret < 0) return ret;
      nxsig_usleep(1000);
      ret = mmc5603_write(priv, MMC5603_CTRL0, 0x10);
      if (ret < 0) return ret;
      nxsig_usleep(1000);
    }

  priv->enabled = enable;
  return 0;
}

static int mmc5603_fetch(FAR struct sensor_lowerhalf_s *lower,
                        FAR struct file *filep, FAR char *buffer, size_t len)
{
  FAR struct mmc5603_dev_s *priv = (FAR struct mmc5603_dev_s *)lower;
  struct sensor_mag result;
  uint8_t raw[9];
  uint8_t status;
  float xyz[3];
  int ret;
  int i;

  if (buffer == NULL || len != sizeof(result)) return -EINVAL;
  if (!priv->enabled) return -EAGAIN;
  ret = mmc5603_write(priv, MMC5603_CTRL0, 0x01);
  if (ret < 0) return ret;

  /* 20 ms bounded wait in the caller's device thread, never the UI thread. */
  for (i = 0; i < 20; i++)
    {
      nxsig_usleep(1000);
      ret = mmc5603_read(priv, MMC5603_STATUS, &status, 1);
      if (ret < 0) return ret;
      if (status & 0x40) break;
    }
  if (i == 20) return -ETIMEDOUT;
  ret = mmc5603_read(priv, 0, raw, sizeof(raw));
  if (ret < 0) return ret;

  for (i = 0; i < 3; i++)
    {
      uint32_t value = ((uint32_t)raw[i * 2] << 12) |
                       ((uint32_t)raw[i * 2 + 1] << 4) | (raw[6 + i] >> 4);
      if (value == 0 || value == 0xfffff) return -ERANGE;
      xyz[i] = ((int32_t)value - 524288) * 0.00625f;
    }

  memset(&result, 0, sizeof(result));
  result.timestamp = sensor_get_timestamp();
  result.x = xyz[0]; result.y = xyz[1]; result.z = xyz[2];
  result.temperature = NAN;
  result.status = 0;
  memcpy(buffer, &result, sizeof(result));
  return sizeof(result);
}

static const struct sensor_ops_s g_mmc5603_ops =
{
  .activate = mmc5603_activate,
  .fetch = mmc5603_fetch,
};

int mmc5603_register(int devno, FAR struct i2c_master_s *i2c)
{
  FAR struct mmc5603_dev_s *priv;
  uint8_t id;
  int ret;

  if (devno < 0 || i2c == NULL) return -EINVAL;
  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL) return -ENOMEM;
  priv->i2c = i2c;
  priv->lower.type = SENSOR_TYPE_MAGNETIC_FIELD;
  priv->lower.ops = &g_mmc5603_ops;
  ret = mmc5603_read(priv, MMC5603_ID, &id, 1);
  if (ret >= 0 && id != 0x10) ret = -ENODEV;
  if (ret >= 0)
    {
      ret = mmc5603_write(priv, MMC5603_CTRL1, 0x80);
      if (ret >= 0) nxsig_usleep(20000);
    }
  if (ret >= 0) ret = sensor_register(&priv->lower, devno);
  if (ret < 0) kmm_free(priv);
  return ret;
}
