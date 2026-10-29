# SPDX-License-Identifier: MIT

add_library(bitoscope_decoders_lowlevel STATIC
    lowlevel_decoders.cpp
    native_i2c.cpp
    native_spi.cpp
    native_uart.cpp
)
target_include_directories(bitoscope_decoders_lowlevel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_decoders_lowlevel PUBLIC bitoscope_decoder_c_sdk)
target_compile_features(bitoscope_decoders_lowlevel PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decoders_lowlevel)

add_executable(decoders_lowlevel main.cpp)
target_link_libraries(decoders_lowlevel PRIVATE bitoscope_decoders_lowlevel)
bitoscope_decoder_plugin_executable(decoders_lowlevel)
target_compile_features(decoders_lowlevel PRIVATE cxx_std_23)
bitoscope_enable_warnings(decoders_lowlevel)
