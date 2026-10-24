# SPDX-License-Identifier: MIT

add_library(bitoscope_demo_signal_sources INTERFACE)

target_include_directories(bitoscope_demo_signal_sources INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_demo_signal_sources INTERFACE cxx_std_23)

add_library(bitoscope_demo_signal_pattern STATIC
    pattern_signals.cpp
)

target_link_libraries(bitoscope_demo_signal_pattern PUBLIC
    bitoscope_demo_signal_sources
)

target_compile_features(bitoscope_demo_signal_pattern PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_demo_signal_pattern)
