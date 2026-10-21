# SPDX-License-Identifier: MIT

add_library(bitoscope_waveform_capture_source STATIC
    capture_waveform_source.cpp
    live_tail_source.cpp
)

target_include_directories(bitoscope_waveform_capture_source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_include_directories(bitoscope_waveform_capture_source PUBLIC ${CMAKE_SOURCE_DIR}/libs/core)

target_link_libraries(bitoscope_waveform_capture_source PUBLIC
    bitoscope_waveform_view
    bitoscope_capture
)

target_compile_features(bitoscope_waveform_capture_source PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_waveform_capture_source)
