# SPDX-License-Identifier: MIT

add_library(bitoscope_decoder_ipc STATIC
    ipc_decoder.cpp
)

target_include_directories(bitoscope_decoder_ipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bitoscope_decoder_ipc
    PUBLIC bitoscope_plugin_ipc
    PRIVATE bitoscope_decoder_capnp
)

target_compile_features(bitoscope_decoder_ipc PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decoder_ipc)
