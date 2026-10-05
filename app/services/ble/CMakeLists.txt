# Bluetooth settings: codec and time zones are plain C99 (no Zephyr
# includes); the GATT service itself uses Zephyr's Bluetooth host.
zephyr_library_named(wx_ble)
zephyr_library_sources(codec.c tz.c)
