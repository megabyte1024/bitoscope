# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_linux STATIC
    color_scheme.cpp
    color_scheme.h
    color_scheme_state.cpp
    color_scheme_state.h
    cursor_theme.cpp
    cursor_theme.h
    deferred_releases.cpp
    deferred_releases.h
    frame_loop.cpp
    frame_loop.h
    locale_languages.cpp
    locale_languages.h
    portal_file_chooser.cpp
    portal_file_chooser.h
    serial_ports.cpp
    serial_ports.h
    system_font.cpp
    system_font.h
    wayland_output.cpp
    wayland_output.h
    wake_gate.cpp
    wake_gate.h
    wayland_restore_size.cpp
    wayland_restore_size.h
    window_system.cpp
    window_system.h
    xdg_config.cpp
    xdg_config.h
)

target_include_directories(bitoscope_gui_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(bitoscope_gui_linux PUBLIC cxx_std_23)
target_link_libraries(bitoscope_gui_linux PUBLIC bitoscope_gui_frame_pacing)
bitoscope_enable_warnings(bitoscope_gui_linux)
