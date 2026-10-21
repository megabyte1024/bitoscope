# SPDX-License-Identifier: MIT

add_library(bitoscope_decoders_common STATIC
    output_writer.cpp
)
target_include_directories(bitoscope_decoders_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_decoders_common PUBLIC bitoscope_decoder_c_sdk)
target_compile_features(bitoscope_decoders_common PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decoders_common)
