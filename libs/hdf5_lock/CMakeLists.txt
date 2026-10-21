# SPDX-License-Identifier: MIT

add_library(bitoscope_hdf5_lock STATIC
    hdf5_lock.cpp
)

target_include_directories(bitoscope_hdf5_lock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_features(bitoscope_hdf5_lock PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_hdf5_lock)
