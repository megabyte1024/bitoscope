# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_prefs STATIC
    lane_name_width.cpp
    lane_name_width.h
    picker_height.cpp
    picker_height.h
    lane_pin_side.cpp
    lane_pin_side.h
    open_session_files.cpp
    open_session_files.h
    theme.cpp
    preferences_json.cpp
    preferences_file.cpp
    recent_files.cpp
    recent_files.h
    window_placement.cpp
    zoom_level.cpp
)

target_include_directories(bitoscope_gui_prefs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(bitoscope_gui_prefs PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(bitoscope_gui_prefs PUBLIC bitoscope_gui_session)
target_link_libraries(bitoscope_gui_prefs PUBLIC bitoscope_waveform_view)
target_link_libraries(bitoscope_gui_prefs PUBLIC bitoscope_decode)

target_compile_features(bitoscope_gui_prefs PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_prefs)
