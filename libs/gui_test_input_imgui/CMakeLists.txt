# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_test_input_imgui STATIC
    imgui_bridge.cpp
    imgui_bridge.h
)

target_include_directories(bitoscope_gui_test_input_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_test_input_imgui PUBLIC bitoscope_gui_test_input bitoscope_imgui)
target_compile_features(bitoscope_gui_test_input_imgui PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_test_input_imgui)
