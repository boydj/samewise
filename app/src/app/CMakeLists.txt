# Application logic: settings, alert manager, health supervisor.
# Includes only hal/ and services/ headers.
zephyr_library_named(wx_app_logic)
zephyr_library_sources(alert_log.c alert_manager.c ble_link.c clock_sync.c health.c radio.c screens.c settings.c ui_model.c ui_render.c)
