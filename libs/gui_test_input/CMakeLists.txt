# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_test_input STATIC
    command_channel.cpp
    command_channel.h
    coordinates.cpp
    coordinates.h
    dialog_answers.cpp
    dialog_answers.h
    frame_times.cpp
    frame_times.h
    frame_times_probe.cpp
    frame_times_probe.h
    image_scale.cpp
    image_scale.h
    key_chord.cpp
    key_chord.h
    line_buffer.cpp
    line_buffer.h
    observation.cpp
    observation.h
    pipe_name.cpp
    pipe_name.h
    pixel_rows.cpp
    pixel_rows.h
    preferences_choice.cpp
    preferences_choice.h
    reply_format.cpp
    reply_format.h
    sequencer.cpp
    sequencer.h
    socket_path.cpp
    socket_path.h
    test_command.cpp
    test_command.h
)

target_include_directories(bitoscope_gui_test_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_test_input PUBLIC bitoscope_gui_frame_probe PRIVATE Threads::Threads)

target_compile_features(bitoscope_gui_test_input PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_test_input)
