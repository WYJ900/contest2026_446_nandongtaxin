/****************************************************************************
 * apps/examples/velaride/velaride_sport_ui.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_VELARIDE_VELARIDE_SPORT_UI_H
#define __APPS_EXAMPLES_VELARIDE_VELARIDE_SPORT_UI_H

#include <stdbool.h>
#include <lvgl/lvgl.h>

typedef void (*velaride_sport_exit_cb_t)(void *arg);

void velaride_sport_ui_init(lv_obj_t *screen,
                            velaride_sport_exit_cb_t exit_cb,
                            void *exit_arg);
void velaride_sport_ui_enter(void);
bool velaride_sport_ui_active(void);

#endif
