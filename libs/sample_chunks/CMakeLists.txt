# SPDX-License-Identifier: MIT

add_library(bitoscope_sample_decoder STATIC
    sample_decoder.cpp
)

target_include_directories(bitoscope_sample_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
)

target_compile_features(bitoscope_sample_decoder PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_sample_decoder)

add_library(bitoscope_decode_ring_producer STATIC
    decode_ring_producer.cpp
)

target_include_directories(bitoscope_decode_ring_producer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
)

target_link_libraries(bitoscope_decode_ring_producer PUBLIC bitoscope_bulk_ring)
target_compile_features(bitoscope_decode_ring_producer PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decode_ring_producer)
