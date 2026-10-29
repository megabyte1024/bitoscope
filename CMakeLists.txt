# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)

project(bitoscope VERSION 0.1.0 LANGUAGES CXX)

# WHY: CMAKE_CXX_STANDARD 17 only pins third-party builds; every own target must declare cxx_std_23.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_language(C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/private/CMakeLists.txt")
  set(BITOSCOPE_PRIVATE_TREE ON)
else()
  set(BITOSCOPE_PRIVATE_TREE OFF)
endif()

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_bitoscope_gui_platform ON)
else()
  set(_bitoscope_gui_platform OFF)
endif()
option(BITOSCOPE_BUILD_GUI "Build bitoscope_gui (Windows and Linux so far)" ${_bitoscope_gui_platform})
option(BITOSCOPE_GUI_TEST_HOOK "Compile the GUI's test-input hook (Windows and Linux)" ${BITOSCOPE_PRIVATE_TREE})
set(BITOSCOPE_GUI_BUILT OFF)
if(BITOSCOPE_BUILD_GUI AND _bitoscope_gui_platform)
  set(BITOSCOPE_GUI_BUILT ON)
elseif(NOT _bitoscope_gui_platform)
  message(STATUS "bitoscope_gui is not built on ${CMAKE_SYSTEM_NAME}: its platform layer is not "
                 "written yet (Cocoa + Metal on macOS)")
endif()

# WHY: Apply warnings per own target, never globally: third-party code (lz4) compiled here would flood the log.
function(bitoscope_enable_warnings target)
  # WHY: C4324 only reports padding an alignas asks for, GCC and Clang have no such warning (owner, 2026-10-21).
  target_compile_options(${target} PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/W4;/wd4324>
      $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
  )
endfunction()

include(cmake/BitoscopeOwnTargetWarnings.cmake)

include(cmake/BitoscopeBuildJobs.cmake)

include(cmake/BitoscopeDeps.cmake)

include(cmake/BitoscopeVersion.cmake)

include(cmake/BitoscopePch.cmake)

if(BITOSCOPE_PRIVATE_TREE)
  # WHY: Here, not in private dir: ctest reads test files from the root down, and dirs added before write none.
  enable_testing()
endif()

add_subdirectory(proto)
add_subdirectory(proto/capnp)
add_subdirectory(proto/driver_capnp)
add_subdirectory(proto/decoder_capnp)
add_subdirectory(libs/lz4)
add_subdirectory(libs/checksum)
add_subdirectory(libs/display_text)
add_subdirectory(libs/console_code_page)
add_subdirectory(libs/display_text_file)
add_subdirectory(libs/png_write)
add_subdirectory(libs/plugin_ipc)
add_subdirectory(libs/driver_ipc)
add_subdirectory(libs/decoder_ipc)
add_subdirectory(libs/device_properties)
add_subdirectory(libs/bulk_ring)
add_subdirectory(libs/decode)
add_subdirectory(libs/capture)
add_subdirectory(libs/hdf5_lock)
add_subdirectory(libs/hdf5_writer)
add_subdirectory(libs/hdf5_reader)
add_subdirectory(libs/plugin_ring)
add_subdirectory(libs/driver_ring)
add_subdirectory(libs/decoder_ring)
add_subdirectory(libs/plugin_c_sdk)
add_subdirectory(libs/driver_c_sdk)
add_subdirectory(libs/decoder_c_sdk)
add_subdirectory(libs/cpu_placement)
add_subdirectory(libs/heap_trim)
add_subdirectory(libs/core)
add_subdirectory(libs/sample_chunks)
add_subdirectory(libs/trigger)
add_subdirectory(libs/trigger_steps)
add_subdirectory(libs/acquisition)
add_subdirectory(libs/pulse_runs)
add_subdirectory(libs/driver_conformance)
add_subdirectory(libs/decoder_host)
add_subdirectory(libs/gui_events)
add_subdirectory(libs/gui_frame_pacing)
add_subdirectory(libs/gui_frame_probe)
add_subdirectory(libs/gui_save_name)
add_subdirectory(libs/gui_task_pool)
add_subdirectory(libs/gui_test_input)
add_subdirectory(libs/gui_capture)
add_subdirectory(libs/log)
add_subdirectory(libs/i18n)
add_subdirectory(libs/gui_locale)
add_subdirectory(libs/gui_prefs)
add_subdirectory(libs/gui_format)
add_subdirectory(libs/gui_session)
add_subdirectory(libs/gui_properties)
add_subdirectory(libs/gui_wizard)
add_subdirectory(libs/gui_devices)
add_subdirectory(libs/waveform_view)
add_subdirectory(libs/waveform_capture_source)
add_subdirectory(libs/gui_waveform_frame)
add_subdirectory(libs/gui_session_file)
add_subdirectory(libs/gui_decode)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(libs/gui_linux)
  add_subdirectory(libs/gui_test_socket)
endif()
# WHY: Plugins get a directory of their own: bitoscope_cli takes every executable beside it for a plugin.
get_property(BITOSCOPE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(BITOSCOPE_MULTI_CONFIG)
  set(BITOSCOPE_CLI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cli/$<CONFIG>")
else()
  set(BITOSCOPE_CLI_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cli")
endif()
set(BITOSCOPE_PLUGIN_OUTPUT_DIR "${BITOSCOPE_CLI_OUTPUT_DIR}/drivers")

function(bitoscope_plugin_executable target)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BITOSCOPE_PLUGIN_OUTPUT_DIR}")
  set_property(GLOBAL APPEND PROPERTY BITOSCOPE_PLUGIN_TARGETS ${target})
endfunction()

set(BITOSCOPE_DECODER_OUTPUT_DIR "${BITOSCOPE_CLI_OUTPUT_DIR}/decoders")

function(bitoscope_decoder_plugin_executable target)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BITOSCOPE_DECODER_OUTPUT_DIR}")
  set_property(GLOBAL APPEND PROPERTY BITOSCOPE_DECODER_PLUGIN_TARGETS ${target})
endfunction()

add_subdirectory(cli)
add_subdirectory(drivers)
add_subdirectory(decoders)
if(BITOSCOPE_GUI_BUILT)
  add_subdirectory(libs/svg_raster)
  add_subdirectory(libs/gui_icons)
  add_subdirectory(libs/gui_test_input_imgui)
  add_subdirectory(libs/gui_popup)
  add_subdirectory(libs/gui_dock_focus)
  add_subdirectory(libs/gui_lane_header)
  add_subdirectory(libs/gui_text_field)
  add_subdirectory(gui)
endif()

include(cmake/BitoscopeAudience.cmake)

include(cmake/BitoscopeInstall.cmake)

if(BITOSCOPE_PRIVATE_TREE)
  add_subdirectory(private)
endif()

# WHY: Last, after every add_subdirectory: a target added below this line would get no warnings.
bitoscope_enable_warnings_on_own_targets("${CMAKE_SOURCE_DIR}")
