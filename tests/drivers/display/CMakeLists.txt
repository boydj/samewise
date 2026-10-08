cmake_minimum_required(VERSION 3.20.0)

# The recording display's binding lives with the test.
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drivers_display)

target_sources(app PRIVATE src/main.c)
