# SPDX-License-Identifier: MIT

add_library(bitoscope_decoder_ring STATIC
    decode_ring_reader.cpp
)

target_include_directories(bitoscope_decoder_ring PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_decoder_ring PUBLIC
    bitoscope_plugin_ring
    bitoscope_bulk_ring
)

target_compile_features(bitoscope_decoder_ring PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decoder_ring)
