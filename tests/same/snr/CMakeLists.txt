cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(same_snr)

target_sources(app PRIVATE src/main.c ../common/decode_util.c)
target_include_directories(app PRIVATE ../common)
wx_test_vectors(snr)
# The sweep writes its table here (docs/decoder-snr.md).
target_compile_definitions(app PRIVATE WX_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../docs")
