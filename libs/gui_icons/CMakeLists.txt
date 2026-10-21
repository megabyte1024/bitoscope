# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_icons STATIC
    app_icon.cpp
    app_icon.h
    icon_cache.cpp
    icon_cache.h
    icon_size.cpp
    icon_size.h
)

target_include_directories(bitoscope_gui_icons PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_icons PUBLIC bitoscope_svg_raster)

target_compile_features(bitoscope_gui_icons PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_icons)
