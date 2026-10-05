# SAME decoder and header parser: plain C99, no Zephyr includes.
zephyr_library_named(wx_same)
zephyr_library_sources(same_decoder.c same_header.c)

# No double-precision math hides in the decoder (Cortex-M4F has single-precision
# hardware only): any implicit float-to-double promotion fails the build.
zephyr_library_compile_options(-Wdouble-promotion -Werror)
