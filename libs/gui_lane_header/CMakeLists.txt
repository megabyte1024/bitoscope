# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_lane_header STATIC
    lane_header.cpp
    lane_header.h
)

target_include_directories(bitoscope_gui_lane_header PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_lane_header PUBLIC bitoscope_imgui bitoscope_waveform_view)
target_compile_features(bitoscope_gui_lane_header PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_lane_header)
