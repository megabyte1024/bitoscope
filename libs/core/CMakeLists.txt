# SPDX-License-Identifier: MIT

add_library(bitoscope_sample_decoder STATIC
    sample_decoder.cpp
)

target_include_directories(bitoscope_sample_decoder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_sample_decoder PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_sample_decoder)

add_library(bitoscope_plugin_host STATIC
    device_watcher.cpp
    pipe_plugin_host.cpp
    plugin_connection.cpp
    plugin_process.cpp
)

target_include_directories(bitoscope_plugin_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_plugin_host
    PUBLIC bitoscope_plugin_ipc bitoscope_cpu_placement
    PRIVATE Threads::Threads bitoscope_display_text
)

target_compile_features(bitoscope_plugin_host PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_plugin_host)

add_library(bitoscope_pipe_clients STATIC
    la_pipe_client.cpp
    siggen_pipe_client.cpp
)

target_include_directories(bitoscope_pipe_clients PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_pipe_clients PUBLIC bitoscope_driver_ipc)

target_compile_features(bitoscope_pipe_clients PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_pipe_clients)

add_library(bitoscope_decode_ring_producer STATIC
    decode_ring_producer.cpp
)

target_include_directories(bitoscope_decode_ring_producer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_decode_ring_producer PUBLIC bitoscope_bulk_ring)

target_compile_features(bitoscope_decode_ring_producer PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decode_ring_producer)
