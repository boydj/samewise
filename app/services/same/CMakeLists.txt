# SAME decoder and header parser: plain C99, no Zephyr includes.
zephyr_library_named(wx_same)
zephyr_library_sources(same_decoder.c same_header.c)
