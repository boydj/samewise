# wx_test_vectors(<group>)
#
# Generates a group of synthetic SAME vectors (tools/vectors/build_vectors.py)
# into this test's build directory and puts the generated vectors.h on the
# app's include path. Needs numpy and scipy in Zephyr's Python.

set(WX_VECTORS_TOOLS_DIR ${CMAKE_CURRENT_LIST_DIR}/.. CACHE INTERNAL "samewise tools/ directory")

function(wx_test_vectors group)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/vectors/${group})
  set(header ${out}/vectors.h)
  add_custom_command(
    OUTPUT ${header}
    COMMAND ${PYTHON_EXECUTABLE} ${WX_VECTORS_TOOLS_DIR}/vectors/build_vectors.py
            --group ${group} --out ${out}
    DEPENDS ${WX_VECTORS_TOOLS_DIR}/vectors/build_vectors.py
            ${WX_VECTORS_TOOLS_DIR}/samegen/samegen.py
    COMMENT "Generating SAME test vectors: ${group}"
    VERBATIM
  )
  add_custom_target(wx_vectors_${group} DEPENDS ${header})
  add_dependencies(app wx_vectors_${group})
  target_include_directories(app PRIVATE ${out})
endfunction()
