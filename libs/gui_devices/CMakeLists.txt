# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_devices STATIC
    device_host.h
    device_catalog.cpp
    device_catalog.h
    device_label.cpp
    device_label.h
    device_router.cpp
    device_router.h
    device_status.cpp
    device_status.h
    device_worker.cpp
    device_worker.h
    driver_version.cpp
    driver_version.h
    driver_watch_mark.cpp
    driver_watch_mark.h
    list_again.cpp
    list_again.h
    pipe_device_host.cpp
    pipe_device_host.h
)

target_include_directories(bitoscope_gui_devices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_devices
    PUBLIC bitoscope_plugin_host bitoscope_gui_events bitoscope_gui_session bitoscope_gui_wizard
    PRIVATE Threads::Threads
)

target_compile_features(bitoscope_gui_devices PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_devices)
