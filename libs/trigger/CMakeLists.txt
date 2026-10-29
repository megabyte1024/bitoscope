# SPDX-License-Identifier: MIT

add_library(bitoscope_trigger_split STATIC
    trigger_split.cpp
)

target_include_directories(bitoscope_trigger_split PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_trigger_split PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_trigger_split)

add_library(bitoscope_trigger INTERFACE)

target_include_directories(bitoscope_trigger INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_trigger INTERFACE cxx_std_23)
target_link_libraries(bitoscope_trigger INTERFACE bitoscope_trigger_steps bitoscope_trigger_split)
