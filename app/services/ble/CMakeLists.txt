# Bluetooth settings: codec and time zones are plain C99 (no Zephyr
# includes); the service logic uses hal/ only; the binding to Zephyr's
# Bluetooth host is ble_zephyr.c.
zephyr_library_named(wx_ble)
zephyr_library_sources(codec.c tz.c)
zephyr_library_sources_ifdef(CONFIG_WX_BLE_SERVICE ble_service.c)
