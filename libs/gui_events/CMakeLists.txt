# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_events STATIC
    gui_event_handlers.h
    gui_event_queue.cpp
    device_watcher_adapter.cpp
    host_device_lost_adapter.cpp
    posting_acquisition_events.cpp
)

target_include_directories(bitoscope_gui_events PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_events
    PUBLIC bitoscope_plugin_host bitoscope_acquisition bitoscope_decode
    PRIVATE Threads::Threads
)

target_compile_features(bitoscope_gui_events PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_events)
