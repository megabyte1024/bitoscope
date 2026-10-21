# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_wizard STATIC
    new_session_wizard.cpp
    new_session_wizard.h
)

target_include_directories(bitoscope_gui_wizard PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_wizard PRIVATE bitoscope_gui_format)
target_compile_features(bitoscope_gui_wizard PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_wizard)
