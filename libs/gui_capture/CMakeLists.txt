# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_capture STATIC
    capture_end_recorder.cpp
    capture_end_recorder.h
    capture_outcome.cpp
    capture_outcome.h
    capture_runner.cpp
    capture_runner.h
    capture_settings.cpp
    capture_settings.h
    capture_status.cpp
    capture_status.h
    channel_names.cpp
    channel_names.h
    close_flow.cpp
    close_flow.h
    default_channel_colors.cpp
    default_channel_colors.h
    length_text.cpp
    length_text.h
    live_capture_slot.cpp
    live_capture_slot.h
    panel_capture.cpp
    panel_capture.h
    start_flow.cpp
    start_flow.h
    unsaved_policy.h
)

target_include_directories(bitoscope_gui_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_capture
    PUBLIC bitoscope_acquisition bitoscope_gui_events bitoscope_trigger bitoscope_waveform_view
    PRIVATE Threads::Threads
)

target_compile_features(bitoscope_gui_capture PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_capture)
bitoscope_enable_pch(bitoscope_gui_capture)
