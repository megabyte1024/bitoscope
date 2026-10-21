# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_properties STATIC
    generator_facet.cpp
    generator_facet.h
    generator_switch_off.cpp
    generator_switch_off.h
    property_editor_model.cpp
    property_editor_model.h
)

target_include_directories(bitoscope_gui_properties PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bitoscope_gui_properties PRIVATE bitoscope_display_text)

target_link_libraries(bitoscope_gui_properties PUBLIC
    bitoscope_driver_ipc
    bitoscope_device_properties
    bitoscope_pipe_clients
)
target_compile_features(bitoscope_gui_properties PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_properties)
