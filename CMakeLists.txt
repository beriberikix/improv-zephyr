# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module build entry for improv-zephyr. Sources are only compiled when the
# consuming application sets CONFIG_IMPROV_WIFI=y.

if(CONFIG_IMPROV_WIFI)
  zephyr_library()

  zephyr_include_directories(include)

  zephyr_library_sources(
    src/improv.c
    src/improv_handler.c
    src/improv_wifi.c
    src/wifi_prov.c
  )

  zephyr_library_sources_ifdef(CONFIG_IMPROV_SERIAL src/transport_serial.c)
  zephyr_library_sources_ifdef(CONFIG_IMPROV_BLE    src/transport_ble.c)
endif()
