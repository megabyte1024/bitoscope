# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_text_field STATIC
    text_field.cpp
    text_field.h
)

target_include_directories(bitoscope_gui_text_field PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_text_field PUBLIC bitoscope_imgui)
target_compile_features(bitoscope_gui_text_field PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_text_field)
