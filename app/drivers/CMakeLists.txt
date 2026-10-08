# Real implementations of the hal/ interfaces on Zephyr subsystems, and the
# embedded SAME clip that stands in for the ADC until milestone 4.
if(CONFIG_WX_DRV_CLOCK OR CONFIG_WX_DRV_WATCHDOG OR CONFIG_WX_DRV_STORAGE OR CONFIG_WX_AUDIO_CLIP OR
   CONFIG_WX_DRV_DISPLAY OR CONFIG_WX_DRV_BATTERY)
  zephyr_library_named(wx_drivers)
  zephyr_library_sources_ifdef(CONFIG_WX_DRV_CLOCK clock_zephyr.c)
  zephyr_library_sources_ifdef(CONFIG_WX_DRV_WATCHDOG watchdog_zephyr.c)
  zephyr_library_sources_ifdef(CONFIG_WX_DRV_STORAGE storage_settings.c)
  zephyr_library_sources_ifdef(CONFIG_WX_DRV_DISPLAY display_zephyr.c)
  zephyr_library_sources_ifdef(CONFIG_WX_DRV_BATTERY battery_zephyr.c)
endif()

if(CONFIG_WX_AUDIO_CLIP)
  set(clip_dir ${CMAKE_BINARY_DIR}/wx_clip)
  set(clip_tool ${CMAKE_CURRENT_LIST_DIR}/../../tools/vectors/build_clip.py)
  add_custom_command(
    OUTPUT ${clip_dir}/wx_clip.c ${clip_dir}/wx_clip.h
    COMMAND ${PYTHON_EXECUTABLE} ${clip_tool} --out ${clip_dir} --bits ${CONFIG_WX_AUDIO_CLIP_BITS}
    DEPENDS ${clip_tool} ${CMAKE_CURRENT_LIST_DIR}/../../tools/samegen/samegen.py
    COMMENT "Generating the embedded SAME clip"
    VERBATIM
  )
  zephyr_library_sources(audio_in_clip.c ${clip_dir}/wx_clip.c)
  zephyr_library_include_directories(${clip_dir})
endif()
