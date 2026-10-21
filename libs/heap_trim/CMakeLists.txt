# SPDX-License-Identifier: MIT

add_library(bitoscope_heap_trim STATIC
    heap_trim.cpp
    heap_trim.h
)

target_include_directories(bitoscope_heap_trim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_heap_trim PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_heap_trim)
