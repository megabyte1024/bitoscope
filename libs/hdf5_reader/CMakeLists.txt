# SPDX-License-Identifier: MIT

add_library(bitoscope_hdf5_reader STATIC
    chunk_inflate.cpp
    chunk_inflate.h
    hdf5_capture_reader.cpp
)

target_include_directories(bitoscope_hdf5_reader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_link_libraries(bitoscope_hdf5_reader
    PUBLIC bitoscope_capture bitoscope_pack_pool bitoscope_hdf5_lock
    PRIVATE hdf5-static ZLIB::ZLIBSTATIC bitoscope_acquisition_ring_config
)

target_compile_features(bitoscope_hdf5_reader PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_hdf5_reader)
