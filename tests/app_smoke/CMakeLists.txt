# Builds the real app (app/src/main.c with app/prj.conf and the native_sim
# board config) and checks the console: the banner, then the embedded SAME
# clip decoded to its header and EOM. The same lines are checked on the
# emulated XIAO in tests/renode.
cmake_minimum_required(VERSION 3.20.0)

set(WX_APP ${CMAKE_CURRENT_SOURCE_DIR}/../../app)
set(CONF_FILE ${WX_APP}/prj.conf ${WX_APP}/boards/native_sim.conf)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_smoke)

target_sources(app PRIVATE ${WX_APP}/src/main.c)
