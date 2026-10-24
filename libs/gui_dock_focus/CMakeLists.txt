# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_dock_focus STATIC
    dock_focus.cpp
    dock_focus.h
)

target_include_directories(bitoscope_gui_dock_focus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_dock_focus PUBLIC bitoscope_imgui)
target_compile_features(bitoscope_gui_dock_focus PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_dock_focus)
