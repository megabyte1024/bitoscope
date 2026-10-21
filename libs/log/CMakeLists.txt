# SPDX-License-Identifier: MIT

# WHY: Only the ipc_messages.h include directory is used: linking bitoscope_plugin_ipc would drag Cap'n Proto
# into the GUI.
add_library(bitoscope_log STATIC
    log_ring.cpp
    log_file_sink.cpp
    logger.cpp
    text_escape.cpp
    log_view.cpp
)

target_include_directories(bitoscope_log PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/plugin_ipc
)

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_log PUBLIC Threads::Threads)

target_compile_features(bitoscope_log PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_log)
