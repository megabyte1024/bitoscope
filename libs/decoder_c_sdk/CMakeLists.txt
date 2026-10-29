# SPDX-License-Identifier: MIT

add_library(bitoscope_decoder_c_sdk STATIC
    src/annotation_blocks.cpp
    src/c_declaration.cpp
    src/decode_api.cpp
    src/decode_cpp_api.cpp
    src/decode_input.cpp
    src/decode_job.cpp
    src/declared_decoder.cpp
    src/declared_ids.cpp
    src/decoder_plugin.cpp
    src/decoder_runtime.cpp
    src/decoder_test_run.cpp
)

target_include_directories(bitoscope_decoder_c_sdk
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(bitoscope_decoder_c_sdk
    PUBLIC
        bitoscope_plugin_c_sdk
    PRIVATE
        bitoscope_plugin_c_sdk_internal
        bitoscope_decoder_ipc
        bitoscope_decoder_capnp
        bitoscope_decoder_ring
        bitoscope_decode
        bitoscope_display_text
)

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_decoder_c_sdk PRIVATE Threads::Threads)

target_compile_features(bitoscope_decoder_c_sdk PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_decoder_c_sdk)

add_library(bitoscope_decoder_c_sdk_internal INTERFACE)
target_include_directories(bitoscope_decoder_c_sdk_internal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bitoscope_decoder_c_sdk_internal INTERFACE
    bitoscope_decoder_c_sdk
    bitoscope_plugin_c_sdk_internal
    bitoscope_decoder_ipc
    bitoscope_decoder_ring
    bitoscope_decode
)
