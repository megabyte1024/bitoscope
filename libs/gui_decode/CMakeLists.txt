# SPDX-License-Identifier: MIT

add_library(bitoscope_gui_decode STATIC
    annotation_store.cpp
    annotation_store.h
    annotation_text.cpp
    annotation_text.h
    byte_search.cpp
    byte_search.h
    channel_choices.cpp
    channel_choices.h
    byte_values.cpp
    byte_values.h
    decoded_annotations.cpp
    decoded_annotations.h
    decode_budget.cpp
    decode_budget.h
    decode_placement.cpp
    decode_placement.h
    decode_progress.cpp
    decode_progress.h
    decode_signal_view.cpp
    decode_signal_view.h
    derivation_context.cpp
    derivation_context.h
    find_lanes.cpp
    find_lanes.h
    find_marks.cpp
    find_marks.h
    find_matches.cpp
    find_matches.h
    find_pattern.cpp
    find_pattern.h
    find_search.cpp
    find_search.h
    decoder_chain_config.cpp
    decoder_chain_config.h
    decoder_config.cpp
    decoder_config.h
    decoder_labels.cpp
    decoder_labels.h
    decoder_pairing.cpp
    decoder_pairing.h
    decoder_picker.cpp
    decoder_versions.cpp
    decoder_plugin_set.cpp
    decoder_plugin_set.h
    decoder_plugins.cpp
    decoder_plugins.h
    decoder_picker.h
    decoder_versions.h
    decoder_presentation.cpp
    decoder_presentation.h
    decoder_runner.cpp
    decoder_runner.h
    decoder_save.cpp
    decoder_save.h
    decoder_settings.cpp
    decoder_settings.h
    gui_decoders.cpp
    gui_decoders.h
    lane_derivation.cpp
    decoder_lane_model.cpp
    lane_text_source.cpp
    lane_picture_cache.cpp
    lane_derivation.h
    live_run_capture.cpp
    live_run_capture.h
    option_text_buffers.cpp
    option_text_buffers.h
    option_widget.cpp
    option_widget.h
    panel_decodes.cpp
    panel_decodes.h
    row_data_format.cpp
    row_data_format.h
    signal_view_source.h
    word_list_text.cpp
    word_list_text.h
)

target_include_directories(bitoscope_gui_decode PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(bitoscope_gui_decode
    PUBLIC bitoscope_decode bitoscope_decoder_host bitoscope_cpu_placement bitoscope_capture bitoscope_gui_capture bitoscope_gui_events bitoscope_waveform_view
    PRIVATE Threads::Threads
            bitoscope_display_text
            bitoscope_cli_decoder_plugins
)

target_compile_features(bitoscope_gui_decode PUBLIC cxx_std_23)
bitoscope_enable_warnings(bitoscope_gui_decode)
bitoscope_enable_pch(bitoscope_gui_decode)
