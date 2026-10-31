# SPDX-License-Identifier: MIT

find_package(Threads REQUIRED)

add_library(bitoscope_cli_ring_summary STATIC
    ring_summary.cpp
)

target_include_directories(bitoscope_cli_ring_summary PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)
target_link_libraries(bitoscope_cli_ring_summary PUBLIC
    bitoscope_bulk_ring
    Threads::Threads
)
target_link_libraries(bitoscope_cli_ring_summary PRIVATE bitoscope_display_text)
target_compile_features(bitoscope_cli_ring_summary PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_ring_summary)

add_library(bitoscope_cli_status_line STATIC
    status_line.cpp
)

target_include_directories(bitoscope_cli_status_line
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libs/acquisition
    PRIVATE ${CMAKE_SOURCE_DIR}/libs/capture
)
target_compile_features(bitoscope_cli_status_line PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_status_line)

add_library(bitoscope_cli_settings STATIC
    cli_settings.cpp
)

target_include_directories(bitoscope_cli_settings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)
target_link_libraries(bitoscope_cli_settings PUBLIC
    bitoscope_driver_ipc
    bitoscope_device_properties
)
target_compile_features(bitoscope_cli_settings PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_settings)

add_library(bitoscope_cli_units STATIC
    cli_units.cpp
)

target_include_directories(bitoscope_cli_units PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_units PUBLIC bitoscope_capture bitoscope_trigger)
target_compile_features(bitoscope_cli_units PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_units)

add_library(bitoscope_cli_args STATIC
    cli_args.cpp
)

target_include_directories(bitoscope_cli_args PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_args PUBLIC bitoscope_cli_units bitoscope_acquisition_ring_config)
target_compile_features(bitoscope_cli_args PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_args)

add_library(bitoscope_cli_acquire STATIC
    cli_acquire.cpp
)

target_include_directories(bitoscope_cli_acquire PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/libs/core
    ${CMAKE_SOURCE_DIR}/libs/sample_chunks
)

target_link_libraries(bitoscope_cli_acquire PUBLIC
    bitoscope_cli_args
    bitoscope_cli_units
    bitoscope_capture
    bitoscope_capture_read
    bitoscope_capture_session
    bitoscope_acquisition
    bitoscope_cli_status_line
    bitoscope_cli_settings
    bitoscope_cli_annotation_filter
    bitoscope_pipe_clients
)
target_compile_features(bitoscope_cli_acquire PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_acquire)

add_library(bitoscope_cli_annotation_filter STATIC
    cli_annotation_filter.cpp
)

target_include_directories(bitoscope_cli_annotation_filter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_annotation_filter PUBLIC bitoscope_decode)
target_link_libraries(bitoscope_cli_annotation_filter PRIVATE bitoscope_display_text)
target_compile_features(bitoscope_cli_annotation_filter PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_annotation_filter)

add_library(bitoscope_cli_drivers STATIC
    cli_drivers.cpp
)

target_include_directories(bitoscope_cli_drivers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_drivers PRIVATE bitoscope_display_text)
target_link_libraries(bitoscope_cli_drivers PUBLIC
    bitoscope_plugin_host
    bitoscope_pipe_clients
    bitoscope_device_properties
)
target_compile_features(bitoscope_cli_drivers PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_drivers)

add_library(bitoscope_cli_decoder_plugins STATIC
    cli_decoder_plugins.cpp
)

target_include_directories(bitoscope_cli_decoder_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_decoder_plugins PUBLIC
    bitoscope_decoder_host
    bitoscope_plugin_host
)
target_link_libraries(bitoscope_cli_decoder_plugins PRIVATE bitoscope_display_text)
target_compile_features(bitoscope_cli_decoder_plugins PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_decoder_plugins)

add_library(bitoscope_cli_check_driver STATIC
    cli_check_driver.cpp
)

target_include_directories(bitoscope_cli_check_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_check_driver PRIVATE bitoscope_driver_capture_check)
target_compile_features(bitoscope_cli_check_driver PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_check_driver)

add_library(bitoscope_cli_run STATIC
    cli_run.cpp
)

target_include_directories(bitoscope_cli_run PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_cli_run PUBLIC
    bitoscope_cli_check_driver
    bitoscope_cli_drivers
    bitoscope_cli_acquire
    bitoscope_cli_ring_summary
    bitoscope_cli_decoder_plugins
    bitoscope_acquisition_host_ring
    bitoscope_acquisition_ring_config
    bitoscope_capture
    bitoscope_capture_session
    bitoscope_hdf5_writer
)
target_compile_features(bitoscope_cli_run PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli_run)

add_executable(bitoscope_cli main.cpp)

target_link_libraries(bitoscope_cli PRIVATE
    bitoscope_cli_run
    bitoscope_cli_drivers
    bitoscope_cli_status_line
    bitoscope_console_code_page
    bitoscope_display_text_file
)

set_target_properties(bitoscope_cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BITOSCOPE_CLI_OUTPUT_DIR}")

target_compile_features(bitoscope_cli PRIVATE cxx_std_23)
bitoscope_enable_warnings(bitoscope_cli)
