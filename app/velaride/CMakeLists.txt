# ##############################################################################
# apps/examples/velaride/CMakeLists.txt
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# ##############################################################################

if(CONFIG_EXAMPLES_VELARIDE)
  set(VELARIDE_TITLE_FONT velaride_font_cn_28.c)
  if(CONFIG_EXAMPLES_VELARIDE_COUNTDOWN)
    set(VELARIDE_TITLE_FONT velaride_font_countdown_28.c)
  endif()
  if(CONFIG_EXAMPLES_VELARIDE_ALARM)
    set(VELARIDE_TITLE_FONT velaride_font_alarm_28.c)
  endif()
  if(CONFIG_EXAMPLES_VELARIDE_SOUND)
    set(VELARIDE_TITLE_FONT velaride_font_sound_28.c)
  endif()
  if(CONFIG_EXAMPLES_VELARIDE_NIGHT)
    set(VELARIDE_TITLE_FONT velaride_font_night_28.c)
  endif()
  nuttx_add_application(
    NAME
    velaride
    PRIORITY
    ${CONFIG_EXAMPLES_VELARIDE_PRIORITY}
    STACKSIZE
    ${CONFIG_EXAMPLES_VELARIDE_STACKSIZE}
    MODULE
    ${CONFIG_EXAMPLES_VELARIDE}
    DEPENDS
    lvgl
    framework_utils
    SRCS
    velaride_shell.c
    velaride_sport_ui.c
    velaride_ride_session.c
    velaride_memo.c
    velaride_activity.c
    velaride_phone.c
    velaride_timekeeper.c
    velaride_device.c
    velaride_compass.c
    velaride_font_cn_20.c
    ${VELARIDE_TITLE_FONT}
    velaride_num_136.c
    velaride_num_92.c
    velaride_num_60.c
    velaride_num_44.c
    velaride_num_30.c
    velaride_num_24.c)

  if(CONFIG_EXAMPLES_VELARIDE_COUNTDOWN)
    target_sources(apps_velaride PRIVATE velaride_countdown.c)
  endif()
  if(CONFIG_EXAMPLES_VELARIDE_ALARM)
    target_sources(apps_velaride PRIVATE velaride_alarm.c)
  endif()
  if(CONFIG_EXAMPLES_VELARIDE_SOUND)
    target_sources(apps_velaride PRIVATE velaride_sound.c
      velaride_sound_render.c velaride_sound_assets.c)
  endif()
  if(CONFIG_EXAMPLES_VELARIDE_NIGHT)
    target_sources(apps_velaride PRIVATE velaride_night.c velaride_night_ui.c)
  endif()

  if(CONFIG_BT AND CONFIG_BT_GATT_DYNAMIC_DB)
    nuttx_add_application(
      NAME
      velaride_ble
      PRIORITY
      100
      STACKSIZE
      8192
      MODULE
      ${CONFIG_EXAMPLES_VELARIDE}
      SRCS
      velaride_ble.c
      DEPENDS
      zblue)
    target_link_libraries(apps_velaride_ble PRIVATE zblue_headers)
  endif()
endif()
