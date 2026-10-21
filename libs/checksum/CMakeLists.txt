# SPDX-License-Identifier: MIT

# WHY: Kept out of bitoscope_core on purpose: parts of core link Cap'n Proto, which a checksum does not need.
add_library(bitoscope_checksum INTERFACE)

target_include_directories(bitoscope_checksum INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(bitoscope_checksum INTERFACE cxx_std_23)
