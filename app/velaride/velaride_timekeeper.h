/****************************************************************************
 * apps/examples/velaride/velaride_timekeeper.h
 *
 * VelaRide wall-clock fallback persisted in the config partition.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELARIDE_VELARIDE_TIMEKEEPER_H
#define __APPS_EXAMPLES_VELARIDE_VELARIDE_TIMEKEEPER_H

#include <stdint.h>

int velaride_timekeeper_restore(void);
int velaride_timekeeper_save_now(void);
int velaride_timekeeper_read(int64_t *epoch);

#endif
