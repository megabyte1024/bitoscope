# SPDX-License-Identifier: MIT

add_library(bitoscope_i18n STATIC
    catalog.cpp
    plural_rule.cpp
    language_choice.cpp
    translator.cpp
)

target_include_directories(bitoscope_i18n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_i18n PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_i18n)
