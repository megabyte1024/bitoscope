# SPDX-License-Identifier: MIT

# WHY: Own library: it joins libs/core's plugin host and libs/capture; libs/core must not depend on libs/capture.
add_library(bitoscope_decoder_host STATIC
    decoder_plugin_client.cpp
    decoder_plugin_pool.cpp
    item_feed.cpp
    pacing_policy.cpp
    pacing_policy.h
    plugin_decoder.cpp
    ring_feed.cpp
)

target_include_directories(bitoscope_decoder_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
)

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_decoder_host
    PUBLIC
        bitoscope_plugin_host
        bitoscope_decoder_ipc
        bitoscope_decode_ring_producer
        bitoscope_bulk_ring
        bitoscope_decode
        bitoscope_capture
    PRIVATE
        Threads::Threads
        bitoscope_display_text
)

target_compile_features(bitoscope_decoder_host PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decoder_host)
