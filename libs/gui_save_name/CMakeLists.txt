# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_save_name STATIC
    save_name.cpp
    save_name.h
)

target_include_directories(bitoscope_gui_save_name PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_gui_save_name PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_save_name)
