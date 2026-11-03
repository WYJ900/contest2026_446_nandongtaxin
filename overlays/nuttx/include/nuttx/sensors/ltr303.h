/****************************************************************************
 * nuttx/include/nuttx/sensors/ltr303.h
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_SENSORS_LTR303_H
#define __INCLUDE_NUTTX_SENSORS_LTR303_H

#include <nuttx/config.h>
#include <nuttx/i2c/i2c_master.h>

/* Register /dev/uorb/sensor_lightN. Fetch mode: open with O_NONBLOCK,
 * then read struct sensor_light. -EAGAIN means no new measurement.
 * Fixed 1x gain, 100 ms integration, 200 ms repeat interval.
 */

int ltr303_register(int devno, FAR struct i2c_master_s *i2c);

#endif
