# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_test_socket STATIC
    remove_on_signal.cpp
    remove_on_signal.h
    test_socket_server.cpp
    test_socket_server.h
)

target_include_directories(bitoscope_gui_test_socket PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_test_socket PUBLIC bitoscope_gui_test_input PRIVATE Threads::Threads)
target_compile_features(bitoscope_gui_test_socket PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_test_socket)
