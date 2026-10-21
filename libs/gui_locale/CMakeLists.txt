# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_locale STATIC
    gui_locale.cpp
    gui_locale.h
)

target_include_directories(bitoscope_gui_locale PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_locale PUBLIC bitoscope_i18n)

target_compile_features(bitoscope_gui_locale PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_locale)
