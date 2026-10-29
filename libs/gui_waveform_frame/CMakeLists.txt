# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_waveform_frame STATIC
    column_cache.cpp
    column_cache.h
    frame_render.cpp
    frame_render.h
    held_rows.cpp
    held_rows.h
    source_chain.cpp
    source_chain.h
    view_table.cpp
    view_table.h
)

target_include_directories(bitoscope_gui_waveform_frame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(bitoscope_gui_waveform_frame PUBLIC
    bitoscope_waveform_view
    bitoscope_waveform_capture_source
    bitoscope_gui_capture
    bitoscope_gui_task_pool
)

target_compile_features(bitoscope_gui_waveform_frame PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_waveform_frame)
