# SPDX-License-Identifier: MIT

find_package(Threads REQUIRED)

add_library(bitoscope_gui_firmware STATIC
    firmware_update_model.cpp
    firmware_update_model.h
)

target_include_directories(bitoscope_gui_firmware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_firmware PUBLIC bitoscope_pipe_clients bitoscope_plugin_host)
target_link_libraries(bitoscope_gui_firmware PRIVATE Threads::Threads)
target_compile_features(bitoscope_gui_firmware PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_firmware)
