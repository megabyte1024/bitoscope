# SPDX-License-Identifier: MIT

# WHY: bitoscope_capture must not link HDF5; this writer is its own target.

add_library(bitoscope_hdf5_writer STATIC
    chunk_deflater.cpp
    chunk_deflater.h
    hdf5_capture_writer.cpp
)

find_package(Threads REQUIRED)

target_include_directories(bitoscope_hdf5_writer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_link_libraries(bitoscope_hdf5_writer
    PUBLIC bitoscope_capture bitoscope_hdf5_lock
    PRIVATE bitoscope_capture_read hdf5-static ZLIB::ZLIBSTATIC Threads::Threads
)

target_compile_features(bitoscope_hdf5_writer PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_hdf5_writer)
