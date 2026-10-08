cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(display_screens)

target_sources(app PRIVATE src/main.c)

# Golden images live beside the test; every render is also written to the
# build directory (screens/*.pbm) for tools/display/render_png.py.
# -DGOLDEN_UPDATE=1 rewrites the goldens instead of comparing.
target_compile_definitions(app PRIVATE
  GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
  RENDER_DIR="${CMAKE_BINARY_DIR}/screens"
  GOLDEN_UPDATE=$<BOOL:${GOLDEN_UPDATE}>)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/screens)
