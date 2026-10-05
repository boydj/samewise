# Pocket WX Radio components, enabled with Kconfig (see zephyr/Kconfig).
#
# Everything includes headers relative to app/, e.g. "hal/audio_in.h" or
# "services/same/same_decoder.h", and app logic relative to app/src
# ("app/settings.h").

set(WX_APP_DIR ${CMAKE_CURRENT_LIST_DIR}/../app)
zephyr_include_directories(${WX_APP_DIR} ${WX_APP_DIR}/src)

add_subdirectory_ifdef(CONFIG_WX_SAME ${WX_APP_DIR}/services/same services/same)
add_subdirectory_ifdef(CONFIG_WX_MATCH ${WX_APP_DIR}/services/match services/match)
add_subdirectory_ifdef(CONFIG_WX_BLE_CODEC ${WX_APP_DIR}/services/ble services/ble)
add_subdirectory_ifdef(CONFIG_WX_POWER ${WX_APP_DIR}/services/power services/power)
add_subdirectory_ifdef(CONFIG_WX_APP_LOGIC ${WX_APP_DIR}/src/app app_logic)
add_subdirectory(${WX_APP_DIR}/fakes fakes)
add_subdirectory(${WX_APP_DIR}/drivers drivers)
include(${CMAKE_CURRENT_LIST_DIR}/../tools/vectors/vectors.cmake)
