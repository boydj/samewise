# native_sim fakes for the hal/ interfaces.

add_subdirectory_ifdef(CONFIG_WX_FAKE_HOST_FILE host_file)

if(CONFIG_WX_FAKE_AUDIO_IN OR CONFIG_WX_FAKE_CLOCK OR CONFIG_WX_FAKE_WATCHDOG OR
   CONFIG_WX_FAKE_STORAGE OR CONFIG_WX_FAKE_ALERT_OUT OR CONFIG_WX_FAKE_AUDIO_OUT OR
   CONFIG_WX_FAKE_INPUT OR CONFIG_WX_FAKE_TUNER OR CONFIG_WX_FAKE_BATTERY)
  zephyr_library_named(wx_fakes)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_AUDIO_IN audio_in_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_CLOCK clock_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_WATCHDOG watchdog_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_STORAGE storage_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_ALERT_OUT alert_out_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_AUDIO_OUT audio_out_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_INPUT input_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_TUNER tuner_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_BATTERY battery_fake.c)
endif()
