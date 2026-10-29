# SPDX-License-Identifier: MIT

add_library(bitoscope_waveform_view STATIC
    fixed_time.cpp
    viewport.cpp
    channel_render.cpp
    dense_span.cpp
    cursors.cpp
    pulse_measurement.cpp
    time_ruler.cpp
    interaction.cpp
    pointer_grab.cpp
    channel_colors.cpp
    color_choice.cpp
    signal_style.cpp
    waveform_pixel.cpp
    marker_list.cpp
    dimension_layout.cpp
    trigger_selection.cpp
    lane_layout.cpp
    lane_regions.cpp
    lane_order.cpp
    lane_display.cpp
    lanes_on_screen.cpp
    channel_selection.cpp
    flag.cpp
    extent_waveform_source.cpp
    live_view.cpp
    annotation_lane_layout.cpp
    annotation_groups.cpp
    annotation_block.cpp
    annotation_reader.cpp
    annotation_memory.cpp
    annotation_lane_index.cpp
    annotation_row_summary.cpp
    coverage_pyramid.cpp
    lane_coverage.cpp
    pass_uploads.cpp
    annotation_colors.cpp
    annotation_shape.cpp
    annotation_row_layout.cpp
    derived_lane_rows.cpp
    bracket.cpp
    strip_cut.cpp
    header_fit.cpp
    decoder_header.cpp
    lane_name_fit.cpp
    header_press.cpp
    double_click_marker.cpp
    decoding_prefix.cpp
    decoder_lane_picture.cpp
)

target_include_directories(bitoscope_waveform_view PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(bitoscope_waveform_view PUBLIC bitoscope_gui_format bitoscope_trigger bitoscope_decode)

target_compile_features(bitoscope_waveform_view PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_waveform_view)
bitoscope_enable_pch(bitoscope_waveform_view)
