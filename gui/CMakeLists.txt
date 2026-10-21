# SPDX-License-Identifier: MIT

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # WHY: GLOBAL: targets outside gui/ link Vulkan::Vulkan and PkgConfig::DBUS too.
  find_package(Vulkan REQUIRED GLOBAL)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET GLOBAL dbus-1)
endif()

add_subdirectory(imgui)

add_library(bitoscope_gui_app STATIC
    app/assets.cpp
    app/assets.h
    app/capture_panel.cpp
    app/color_popup.cpp
    app/color_popup.h
    app/decoder_panel.cpp
    app/gui_app.cpp
    app/gui_app.h
    app/panels.cpp
    app/platform_services.h
    app/session_file_panel.cpp
    app/session_waveform.h
    app/system_resources.cpp
    app/system_resources.h
    app/theme.cpp
    app/theme.h
    app/tr.cpp
    app/tr.h
    app/waveform_panel.cpp
    app/waveform_renderer.h
)
target_include_directories(bitoscope_gui_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(bitoscope_gui_app PUBLIC bitoscope_imgui bitoscope_svg_raster bitoscope_log
    bitoscope_gui_icons bitoscope_gui_session bitoscope_gui_wizard bitoscope_gui_format
    bitoscope_i18n bitoscope_gui_locale bitoscope_gui_prefs bitoscope_waveform_view
    bitoscope_display_text_file
    bitoscope_waveform_capture_source bitoscope_gui_waveform_frame bitoscope_gui_events
    bitoscope_gui_frame_pacing bitoscope_gui_devices
    bitoscope_gui_capture bitoscope_gui_properties bitoscope_gui_session_file bitoscope_gui_decode
    bitoscope_gui_popup bitoscope_gui_lane_header bitoscope_gui_text_field bitoscope_gui_frame_probe)
if(BITOSCOPE_GUI_TEST_HOOK)
  target_compile_definitions(bitoscope_gui_app PRIVATE BITOSCOPE_GUI_TEST_HOOK)
endif()
target_compile_features(bitoscope_gui_app PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_app)
bitoscope_enable_pch(bitoscope_gui_app)

if(WIN32)
  add_executable(bitoscope_gui WIN32
      platform/win32/main_win32.cpp
      platform/win32/d3d11_device.cpp
      platform/win32/d3d11_device.h
      platform/win32/d3d11_waveform_renderer.cpp
      platform/win32/d3d11_waveform_renderer.h
      platform/win32/win32_platform.cpp
      platform/win32/win32_platform.h
      platform/win32/bitoscope_gui.manifest
      platform/win32/app_icon.rc
  )
  target_link_libraries(bitoscope_gui PRIVATE bitoscope_gui_app bitoscope_imgui d3d11 d3dcompiler dwmapi
      shell32 ole32)
  target_compile_definitions(bitoscope_gui PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  if(BITOSCOPE_GUI_TEST_HOOK)
    target_sources(bitoscope_gui PRIVATE
        platform/win32/test_hook.cpp
        platform/win32/test_hook.h
        platform/win32/test_pipe_server.cpp
        platform/win32/test_pipe_server.h
    )
    target_compile_definitions(bitoscope_gui PRIVATE BITOSCOPE_GUI_TEST_HOOK)
    target_link_libraries(bitoscope_gui PRIVATE bitoscope_gui_test_input bitoscope_gui_test_input_imgui
        bitoscope_gui_save_name bitoscope_png_write advapi32)
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(bitoscope_gui_persistent_mappings STATIC
      platform/linux/persistent_mappings.cpp
      platform/linux/persistent_mappings.h
  )
  target_include_directories(bitoscope_gui_persistent_mappings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/linux)
  target_link_libraries(bitoscope_gui_persistent_mappings PUBLIC Vulkan::Vulkan)
  target_compile_features(bitoscope_gui_persistent_mappings PUBLIC cxx_std_23)
  bitoscope_enable_warnings(bitoscope_gui_persistent_mappings)

  # WHY: Linux GUI links ${CMAKE_DL_LIBS}, not libX11 or fontconfig: both load at run time, so no dev headers needed.
  add_executable(bitoscope_gui
      platform/linux/main_linux.cpp
      platform/linux/color_scheme_watcher.cpp
      platform/linux/color_scheme_watcher.h
      platform/linux/file_chooser_dialog.cpp
      platform/linux/file_chooser_dialog.h
      platform/linux/fontconfig_lookup.cpp
      platform/linux/fontconfig_lookup.h
      platform/linux/linux_platform.cpp
      platform/linux/linux_platform.h
      platform/linux/portal_settings.cpp
      platform/linux/portal_settings.h
      platform/linux/present_mode.cpp
      platform/linux/present_mode.h
      platform/linux/vulkan_device.cpp
      platform/linux/vulkan_device.h
      platform/linux/vulkan_pass_host.h
      platform/linux/vulkan_waveform_renderer.cpp
      platform/linux/vulkan_waveform_renderer.h
      platform/linux/waveform_spirv.cpp
      platform/linux/waveform_spirv.h
      platform/linux/window_icon.cpp
      platform/linux/window_icon.h
      platform/linux/shaders/waveform.frag
      platform/linux/shaders/waveform.vert
      platform/linux/shaders/waveform_lane.glsl
      platform/linux/x11_theme_variant.cpp
      platform/linux/x11_theme_variant.h
  )
  target_link_libraries(bitoscope_gui PRIVATE bitoscope_gui_app bitoscope_imgui bitoscope_gui_linux
      bitoscope_gui_persistent_mappings glfw Vulkan::Vulkan PkgConfig::DBUS ${CMAKE_DL_LIBS})
  if(BITOSCOPE_GUI_TEST_HOOK)
    target_sources(bitoscope_gui PRIVATE
        platform/linux/test_hook.cpp
        platform/linux/test_hook.h
    )
    target_compile_definitions(bitoscope_gui PRIVATE BITOSCOPE_GUI_TEST_HOOK)
    target_link_libraries(bitoscope_gui PRIVATE bitoscope_gui_test_input bitoscope_gui_test_input_imgui
        bitoscope_gui_test_socket bitoscope_png_write)
  endif()
else()
  message(FATAL_ERROR "bitoscope_gui has no platform layer for ${CMAKE_SYSTEM_NAME} (G4)")
endif()
target_compile_features(bitoscope_gui PRIVATE cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui)

add_custom_target(bitoscope_gui_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:bitoscope_gui>/assets"
    COMMENT "Copying the GUI's assets beside bitoscope_gui"
    VERBATIM
)
add_dependencies(bitoscope_gui bitoscope_gui_assets)

get_property(_bitoscope_plugins GLOBAL PROPERTY BITOSCOPE_PLUGIN_TARGETS)
add_custom_target(bitoscope_gui_plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${BITOSCOPE_PLUGIN_OUTPUT_DIR}" "$<TARGET_FILE_DIR:bitoscope_gui>/drivers"
    COMMENT "Copying the plugins beside bitoscope_gui"
    VERBATIM
)
if(_bitoscope_plugins)
  add_dependencies(bitoscope_gui_plugins ${_bitoscope_plugins})
endif()
add_dependencies(bitoscope_gui bitoscope_gui_plugins)

get_property(_bitoscope_decoder_plugins GLOBAL PROPERTY BITOSCOPE_DECODER_PLUGIN_TARGETS)
add_custom_target(bitoscope_gui_decoders
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            "${BITOSCOPE_DECODER_OUTPUT_DIR}" "$<TARGET_FILE_DIR:bitoscope_gui>/decoders"
    COMMENT "Copying the decoder plugins beside bitoscope_gui"
    VERBATIM
)
if(_bitoscope_decoder_plugins)
  add_dependencies(bitoscope_gui_decoders ${_bitoscope_decoder_plugins})
endif()
add_dependencies(bitoscope_gui bitoscope_gui_decoders)

