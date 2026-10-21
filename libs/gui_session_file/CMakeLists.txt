# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_session_file STATIC
    session_load.cpp
    session_load.h
    save_command.cpp
    save_command.h
    session_save.cpp
    session_save.h
)

target_include_directories(bitoscope_gui_session_file PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(bitoscope_gui_session_file
    PUBLIC bitoscope_gui_capture bitoscope_waveform_view bitoscope_hdf5_reader bitoscope_plugin_ipc
    PRIVATE bitoscope_hdf5_writer bitoscope_device_properties bitoscope_heap_trim
)

target_compile_features(bitoscope_gui_session_file PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_session_file)
