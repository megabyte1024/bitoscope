# SPDX-License-Identifier: MIT

add_library(bitoscope_trigger INTERFACE)

target_include_directories(bitoscope_trigger INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_trigger INTERFACE cxx_std_23)
target_link_libraries(bitoscope_trigger INTERFACE bitoscope_trigger_steps)
