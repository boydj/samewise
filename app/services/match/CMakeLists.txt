# Event table, matcher, filter and duplicates: plain C99, no Zephyr includes.
zephyr_library_named(wx_match)
zephyr_library_sources(event_table.c event_table_default.c)
