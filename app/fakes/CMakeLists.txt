# native_sim fakes for the hal/ interfaces.

add_subdirectory_ifdef(CONFIG_WX_FAKE_HOST_FILE host_file)

if(CONFIG_WX_FAKE_AUDIO_IN OR CONFIG_WX_FAKE_CLOCK)
  zephyr_library_named(wx_fakes)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_AUDIO_IN audio_in_fake.c)
  zephyr_library_sources_ifdef(CONFIG_WX_FAKE_CLOCK clock_fake.c)
endif()
