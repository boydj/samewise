cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drivers_battery)

target_sources(app PRIVATE src/main.c src/emul_bq25180.c)
