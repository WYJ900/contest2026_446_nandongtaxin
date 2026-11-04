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

enum velaride_info_e
{
  VELARIDE_INFO_SAFETY = 0,
  VELARIDE_INFO_CONNECTION,
  VELARIDE_INFO_DEVICE,
  VELARIDE_INFO_HELP,
  VELARIDE_INFO_DISPLAY,
  VELARIDE_INFO_COMPASS,
  VELARIDE_INFO_HARDWARE,
  VELARIDE_INFO_APPEARANCE,
  VELARIDE_INFO_LIGHT,
  VELARIDE_INFO_STOPWATCH,
  VELARIDE_INFO_LEVEL,
  VELARIDE_INFO_WAKE,
#ifdef CONFIG_EXAMPLES_VELARIDE_COUNTDOWN
  VELARIDE_INFO_COUNTDOWN,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_ALARM
  VELARIDE_INFO_ALARM,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_SOUND
  VELARIDE_INFO_SOUND,
#endif
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
  VELARIDE_INFO_NIGHT,
#endif
  VELARIDE_INFO_ACTIVITY,
  VELARIDE_INFO_PHONE,
};

void velaride_sport_ui_init(lv_obj_t *screen,
                            velaride_sport_exit_cb_t exit_cb,
                            void *exit_arg);
void velaride_sport_ui_enter(void);
void velaride_sport_ui_show_latest(void);
void velaride_sport_ui_show_records(void);
void velaride_sport_ui_show_info(enum velaride_info_e info);
void velaride_sport_ui_back(void);
bool velaride_sport_ui_active(void);

/* True while a page needs the screen to stay on: a ride or the night
 * light.
 */

bool velaride_sport_ui_keep_awake(void);
#ifdef CONFIG_EXAMPLES_VELARIDE_NIGHT
bool velaride_sport_ui_night_exempt(void);
#endif

#endif
