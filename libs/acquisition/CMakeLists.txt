# SPDX-License-Identifier: MIT

add_library(bitoscope_acquisition_ring_config STATIC
    ring_config.cpp
)

target_include_directories(bitoscope_acquisition_ring_config PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_acquisition_ring_config PRIVATE bitoscope_bulk_ring)
target_compile_features(bitoscope_acquisition_ring_config PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_acquisition_ring_config)

add_library(bitoscope_acquisition_host_ring STATIC
    host_ring.cpp
)

target_include_directories(bitoscope_acquisition_host_ring PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_acquisition_host_ring PUBLIC
    bitoscope_bulk_ring
    bitoscope_acquisition_ring_config
)
target_compile_features(bitoscope_acquisition_host_ring PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_acquisition_host_ring)

add_library(bitoscope_acquisition_facets STATIC
    facet_access.cpp
    start_messages.cpp
)

target_include_directories(bitoscope_acquisition_facets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
)
target_link_libraries(bitoscope_acquisition_facets PUBLIC bitoscope_pipe_clients)
target_compile_features(bitoscope_acquisition_facets PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_acquisition_facets)

find_package(Threads REQUIRED)

add_library(bitoscope_acquisition STATIC
    acquisition.cpp
    ring_pump.cpp
    thread_name.cpp
)

target_include_directories(bitoscope_acquisition PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
)
target_link_libraries(bitoscope_acquisition PUBLIC
    bitoscope_bulk_ring
    bitoscope_acquisition_ring_config
    bitoscope_acquisition_host_ring
    bitoscope_acquisition_facets
    bitoscope_capture
    bitoscope_pack_pool
    Threads::Threads
)
target_link_libraries(bitoscope_acquisition PRIVATE bitoscope_display_text bitoscope_heap_trim)
target_compile_features(bitoscope_acquisition PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_acquisition)
bitoscope_enable_pch(bitoscope_acquisition)
