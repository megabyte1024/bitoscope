# SPDX-License-Identifier: MIT

# WHY: Only lib/lz4.c is built: programs/ is GPL-2.0-or-later and must stay out;
# lib/ is BSD and its notice must travel.
enable_language(C)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/lz4/lib/lz4.c")
  message(FATAL_ERROR
    "third_party/lz4 is empty - run 'git submodule update --init --recursive' first.")
endif()

add_library(bitoscope_lz4 STATIC
    ${CMAKE_SOURCE_DIR}/third_party/lz4/lib/lz4.c
)

# WHY: Intel JCC-erratum padding for MSVC 19.50+ only (owner decision);
# delete when the compiler stops emitting slow LZ4 code.
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION GREATER_EQUAL 1950)
  target_compile_options(bitoscope_lz4 PRIVATE /QIntel-jcc-erratum)
endif()

target_include_directories(bitoscope_lz4 SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/lz4/lib
)

add_library(bitoscope_lz4_block STATIC
    lz4_block.cpp
)

target_include_directories(bitoscope_lz4_block PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_lz4_block PRIVATE bitoscope_lz4)

target_compile_features(bitoscope_lz4_block PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_lz4_block)
