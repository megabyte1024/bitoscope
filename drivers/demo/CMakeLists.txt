# SPDX-License-Identifier: MIT

add_subdirectory(signals)

find_package(Threads REQUIRED)

add_library(bitoscope_demo_pacer STATIC demo_pacer.cpp)
target_include_directories(bitoscope_demo_pacer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_demo_pacer PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_demo_pacer)

add_library(bitoscope_demo_analyzer STATIC demo_analyzer.cpp)
target_include_directories(bitoscope_demo_analyzer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_demo_analyzer PUBLIC
    bitoscope_driver_c_sdk
    bitoscope_demo_pacer
    bitoscope_display_text
    bitoscope_demo_signal_sources
    bitoscope_demo_signal_pattern
)
target_compile_features(bitoscope_demo_analyzer PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_demo_analyzer)

add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE bitoscope_demo_analyzer)
bitoscope_plugin_executable(demo)
target_compile_features(demo PRIVATE cxx_std_23)
bitoscope_enable_warnings(demo)
