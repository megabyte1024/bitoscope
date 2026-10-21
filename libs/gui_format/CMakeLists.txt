# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_format STATIC
    display_format.cpp
    display_format.h
    log_scroll.cpp
    log_scroll.h
    property_tree_layout.cpp
    property_tree_layout.h
    version_format.cpp
    version_format.h
)

target_include_directories(bitoscope_gui_format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(bitoscope_gui_format PRIVATE ${BITOSCOPE_VERSION_HEADER_DIR})
target_compile_features(bitoscope_gui_format PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_format)
