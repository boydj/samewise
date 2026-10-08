cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(drivers_tuner)

target_sources(app PRIVATE src/main.c src/emul_si4743.c)
