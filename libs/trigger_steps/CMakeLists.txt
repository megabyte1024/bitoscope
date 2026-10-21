# SPDX-License-Identifier: MIT

add_library(bitoscope_trigger_steps STATIC
    fold_steps.cpp
    fold_steps.h
)

target_include_directories(bitoscope_trigger_steps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_trigger_steps PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_trigger_steps)
