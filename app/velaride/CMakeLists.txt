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
    velaride_timekeeper.c
    velaride_font_cn_20.c
    velaride_font_cn_24.c)

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
