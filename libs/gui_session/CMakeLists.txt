# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_session STATIC
    session_list.cpp
    session_list.h
    device_identity.cpp
    device_identity.h
    properties_panel.cpp
    properties_panel.h
    properties_layout.cpp
    properties_layout.h
)

target_include_directories(bitoscope_gui_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_gui_session PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_session)
