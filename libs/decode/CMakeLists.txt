# SPDX-License-Identifier: MIT

add_library(bitoscope_decode STATIC
    annotation_format.cpp
    annotation_role.cpp
    stream.cpp
    decoder_chain.cpp
    chain_stepper.cpp
    derived_rows.cpp
    derived_row_run.cpp
    row_source.cpp
    protocol_fit.cpp
    spec_display.cpp
    option_text.cpp
)

target_include_directories(bitoscope_decode PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_decode PRIVATE bitoscope_display_text)

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_decode PUBLIC Threads::Threads)

target_compile_features(bitoscope_decode PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decode)
