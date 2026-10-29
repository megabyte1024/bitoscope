# SPDX-License-Identifier: MIT

add_library(bitoscope_driver_conformance STATIC
    driver_conformance.cpp
    driver_runs.cpp
)
target_include_directories(bitoscope_driver_conformance PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_driver_conformance PUBLIC bitoscope_plugin_host bitoscope_plugin_ipc)
target_compile_features(bitoscope_driver_conformance PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_driver_conformance)

add_library(bitoscope_driver_capture_check STATIC
    capture_check.cpp
    test_pattern_check.cpp
)
target_include_directories(bitoscope_driver_capture_check PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_driver_capture_check
    PUBLIC bitoscope_driver_conformance
    PRIVATE bitoscope_acquisition bitoscope_capture_read bitoscope_device_properties bitoscope_pulse_runs
)
target_compile_features(bitoscope_driver_capture_check PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_driver_capture_check)
