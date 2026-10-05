# Builds the real app/src/main.c and checks the banner on the console.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_smoke)

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main.c)
