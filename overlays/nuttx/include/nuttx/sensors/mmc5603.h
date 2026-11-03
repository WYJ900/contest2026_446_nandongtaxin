/****************************************************************************
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/
#ifndef __INCLUDE_NUTTX_SENSORS_MMC5603_H
#define __INCLUDE_NUTTX_SENSORS_MMC5603_H
#include <nuttx/i2c/i2c_master.h>
/* Fetch-mode sensor_magN, O_NONBLOCK. Values in microtesla; uncalibrated. */
int mmc5603_register(int devno, FAR struct i2c_master_s *i2c);
#endif
