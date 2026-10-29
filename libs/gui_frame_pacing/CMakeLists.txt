# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_frame_pacing STATIC
    frame_loop.cpp
    frame_loop.h
    frame_pacer.cpp
    frame_pacer.h
)

target_include_directories(bitoscope_gui_frame_pacing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_gui_frame_pacing PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_frame_pacing)
