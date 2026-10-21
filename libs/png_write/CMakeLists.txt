# SPDX-License-Identifier: MIT

add_library(bitoscope_png_write STATIC
    png_write.cpp
    png_write.h
)

target_include_directories(bitoscope_png_write PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_png_write PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_png_write)
