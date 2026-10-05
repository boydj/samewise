# Runs in the native simulator runner, against the host C library.
target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/host_file_native.c)
