# SPDX-License-Identifier: MIT

add_library(bitoscope_hdf5_reader STATIC
    hdf5_capture_reader.cpp
)

target_include_directories(bitoscope_hdf5_reader PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
)

target_link_libraries(bitoscope_hdf5_reader
    PUBLIC bitoscope_capture bitoscope_hdf5_lock
    PRIVATE hdf5-static bitoscope_acquisition_ring_config
)

target_compile_features(bitoscope_hdf5_reader PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_hdf5_reader)
