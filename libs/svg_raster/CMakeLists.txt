# SPDX-License-Identifier: MIT

add_library(bitoscope_svg_raster STATIC
    svg_raster.cpp
    svg_raster.h
)

target_include_directories(bitoscope_svg_raster PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_svg_raster PRIVATE plutosvg::plutosvg plutovg::plutovg)

target_compile_features(bitoscope_svg_raster PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_svg_raster)
