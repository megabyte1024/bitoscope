# SPDX-License-Identifier: MIT

add_library(bitoscope_pulse_runs INTERFACE)

target_include_directories(bitoscope_pulse_runs INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_pulse_runs INTERFACE bitoscope_demo_signal_sources)
target_compile_features(bitoscope_pulse_runs INTERFACE cxx_std_23)
