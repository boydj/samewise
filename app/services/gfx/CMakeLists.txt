# Drawing and bitmap fonts: plain C99, no Zephyr includes.
zephyr_library_named(wx_gfx)
zephyr_library_sources(gfx.c font_vt323.c)
zephyr_library_compile_options(-Wdouble-promotion -Werror)
