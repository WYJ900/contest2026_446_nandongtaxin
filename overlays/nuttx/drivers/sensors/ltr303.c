/****************************************************************************
 * nuttx/drivers/sensors/ltr303.c
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
#include <nuttx/sensors/ltr303.h>
#include <nuttx/sensors/sensor.h>
#include <nuttx/uorb.h>

#define LTR303_ADDR       0x29
#define LTR303_CONTROL    0x80
#define LTR303_RATE       0x85
#define LTR303_ID         0x86
#define LTR303_DATA       0x88
#define LTR303_STATUS     0x8c

struct ltr303_dev_s
{
  struct sensor_lowerhalf_s lower;
  FAR struct i2c_master_s *i2c;
  bool enabled;
};

static int ltr303_read(FAR struct ltr303_dev_s *priv, uint8_t reg,
                       FAR uint8_t *data, int size)
{
  struct i2c_msg_s msg[2] =
  {
    {.frequency = 100000, .addr = LTR303_ADDR, .flags = I2C_M_NOSTOP,
     .buffer = &reg, .length = 1},
    {.frequency = 100000, .addr = LTR303_ADDR, .flags = I2C_M_READ,
     .buffer = data, .length = size}
  };

  return I2C_TRANSFER(priv->i2c, msg, 2);
}

static int ltr303_write(FAR struct ltr303_dev_s *priv, uint8_t reg,
                        uint8_t value)
{
  uint8_t data[2] = {reg, value};
  struct i2c_msg_s msg =
  {
    .frequency = 100000, .addr = LTR303_ADDR, .flags = 0,
    .buffer = data, .length = sizeof(data)
  };

  return I2C_TRANSFER(priv->i2c, &msg, 1);
}

/* The upper half serializes activate/fetch. No polling thread or interrupt
 * is needed: the chip measures autonomously while a reader is open.
 */

static int ltr303_activate(FAR struct sensor_lowerhalf_s *lower,
                           FAR struct file *filep, bool enable)
{
  FAR struct ltr303_dev_s *priv = (FAR struct ltr303_dev_s *)lower;
  int ret;

  if (enable)
    {
      ret = ltr303_write(priv, LTR303_RATE, 0x02);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = ltr303_write(priv, LTR303_CONTROL, enable ? 0x01 : 0x00);
  if (ret >= 0)
    {
      priv->enabled = enable;
    }

  return ret < 0 ? ret : 0;
}

static float ltr303_lux(uint16_t ch0, uint16_t ch1)
{
  uint32_t sum = (uint32_t)ch0 + ch1;
  float ratio;

  if (sum == 0)
    {
      return 0.0f;
    }

  ratio = (float)ch1 / sum;
  if (ratio < 0.45f)
    {
      return 1.7743f * ch0 + 1.1059f * ch1;
    }
  else if (ratio < 0.64f)
    {
      return 4.2785f * ch0 - 1.9548f * ch1;
    }
  else if (ratio < 0.85f)
    {
      return 0.5926f * ch0 + 0.1185f * ch1;
    }

  return 0.0f;
}

static int ltr303_fetch(FAR struct sensor_lowerhalf_s *lower,
                        FAR struct file *filep, FAR char *buffer, size_t len)
{
  FAR struct ltr303_dev_s *priv = (FAR struct ltr303_dev_s *)lower;
  struct sensor_light result;
  uint8_t status;
  uint8_t raw[4];
  uint16_t ch0;
  uint16_t ch1;
  int ret;

  if (buffer == NULL || len != sizeof(result))
    {
      return -EINVAL;
    }

  if (!priv->enabled)
    {
      return -EAGAIN;
    }

  ret = ltr303_read(priv, LTR303_STATUS, &status, 1);
  if (ret < 0)
    {
      return ret;
    }

  if ((status & 0x04) == 0)
    {
      return -EAGAIN;
    }

  /* Ascending burst CH1 then CH0: reading 0x8b releases the data latch.
   * Drain invalid samples too, so a later valid sample can be acquired.
   */

  ret = ltr303_read(priv, LTR303_DATA, raw, sizeof(raw));
  if (ret < 0)
    {
      return ret;
    }

  ch1 = raw[0] | ((uint16_t)raw[1] << 8);
  ch0 = raw[2] | ((uint16_t)raw[3] << 8);
  if ((status & 0xf0) != 0 || ch0 == UINT16_MAX || ch1 == UINT16_MAX)
    {
      return -ERANGE;
    }

  memset(&result, 0, sizeof(result));
  result.timestamp = sensor_get_timestamp();
  result.light = ltr303_lux(ch0, ch1);
  result.ir = NAN;  /* Raw infrared counts are not calibrated SI lux. */
  memcpy(buffer, &result, sizeof(result));
  return sizeof(result);
}

static const struct sensor_ops_s g_ltr303_ops =
{
  .activate = ltr303_activate,
  .fetch = ltr303_fetch,
};

int ltr303_register(int devno, FAR struct i2c_master_s *i2c)
{
  FAR struct ltr303_dev_s *priv;
  uint8_t id[2];
  int ret;

  if (i2c == NULL || devno < 0)
    {
      return -EINVAL;
    }

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  priv->i2c = i2c;
  priv->lower.type = SENSOR_TYPE_LIGHT;
  priv->lower.ops = &g_ltr303_ops;
  nxsig_usleep(100000);  /* Datasheet power-on startup time. */
  ret = ltr303_read(priv, LTR303_ID, id, sizeof(id));
  if (ret >= 0 && (id[0] != 0xa0 || id[1] != 0x05))
    {
      ret = -ENODEV;
    }

  if (ret >= 0)
    {
      ret = ltr303_write(priv, LTR303_CONTROL, 0);
    }

  if (ret >= 0)
    {
      ret = sensor_register(&priv->lower, devno);
    }

  if (ret < 0)
    {
      kmm_free(priv);
    }

  return ret;
}
