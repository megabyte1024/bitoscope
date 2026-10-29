# SPDX-License-Identifier: MIT

add_library(bitoscope_capture INTERFACE)

target_include_directories(bitoscope_capture INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_capture INTERFACE bitoscope_decode bitoscope_lz4_block bitoscope_checksum bitoscope_trigger bitoscope_window_assembler
    bitoscope_raw_capture_buffer bitoscope_history_ring_floor bitoscope_triggered_window
    bitoscope_capture_snapshot bitoscope_block_clearing bitoscope_arena_growth bitoscope_cgroup_memory_limit)

target_compile_features(bitoscope_capture INTERFACE cxx_std_23)

# WHY: bitoscope_raw_capture_buffer must not link bitoscope_capture back: that would be a cycle.
add_library(bitoscope_raw_capture_buffer STATIC
    pack_scratch.cpp
    raw_capture_buffer.cpp
    capture_index.cpp
    live_pyramid.cpp
)

target_include_directories(bitoscope_raw_capture_buffer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/libs/core
            ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_link_libraries(bitoscope_raw_capture_buffer PRIVATE bitoscope_lz4_block bitoscope_checksum)

target_compile_features(bitoscope_raw_capture_buffer PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_raw_capture_buffer)

add_library(bitoscope_capture_read STATIC
    read_samples.cpp
)

target_include_directories(bitoscope_capture_read PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_link_libraries(bitoscope_capture_read PUBLIC bitoscope_capture)

target_compile_features(bitoscope_capture_read PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_capture_read)

add_library(bitoscope_pack_pool STATIC
    pack_pool.cpp
)

target_include_directories(bitoscope_pack_pool PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_pack_pool PUBLIC bitoscope_capture Threads::Threads)

target_compile_features(bitoscope_pack_pool PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_pack_pool)

add_library(bitoscope_history_ring_floor STATIC
    history_ring_floor.cpp
)

target_include_directories(bitoscope_history_ring_floor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_history_ring_floor PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_history_ring_floor)

add_library(bitoscope_triggered_window STATIC
    triggered_window.cpp
)

target_include_directories(bitoscope_triggered_window PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_triggered_window PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_triggered_window)

add_library(bitoscope_capture_snapshot STATIC
    capture_snapshot.cpp
)

target_include_directories(bitoscope_capture_snapshot
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/libs/core
            ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_compile_features(bitoscope_capture_snapshot PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_capture_snapshot)

add_library(bitoscope_block_clearing STATIC
    block_clearing.cpp
)

target_include_directories(bitoscope_block_clearing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_block_clearing PUBLIC Threads::Threads)

target_compile_features(bitoscope_block_clearing PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_block_clearing)

add_library(bitoscope_arena_growth STATIC
    arena_growth.cpp
    block_supply_ahead.cpp
)

target_include_directories(bitoscope_arena_growth PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_arena_growth PUBLIC bitoscope_block_clearing bitoscope_history_ring_floor)

target_compile_features(bitoscope_arena_growth PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_arena_growth)

add_library(bitoscope_capture_session STATIC
    capture_session.cpp
)

target_include_directories(bitoscope_capture_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_link_libraries(bitoscope_capture_session PUBLIC bitoscope_pack_pool)

target_compile_features(bitoscope_capture_session PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_capture_session)

add_library(bitoscope_cgroup_memory_limit STATIC
    cgroup_memory_limit.cpp
)

target_include_directories(bitoscope_cgroup_memory_limit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_cgroup_memory_limit PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cgroup_memory_limit)
