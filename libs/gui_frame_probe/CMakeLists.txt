# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_frame_probe STATIC
    frame_probe.cpp
    frame_probe.h
)

target_include_directories(bitoscope_gui_frame_probe PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_gui_frame_probe PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_frame_probe)
