/*
 * SPDX-License-Identifier: MIT
 */

#ifndef BITOSCOPE_DECODER_TESTING_H
#define BITOSCOPE_DECODER_TESTING_H

#include <stddef.h>
#include <stdint.h>

#include "bitoscope_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitoscope_memory_record {
  uint64_t num_samples; /* IMPLEMENT: at least 1 */
  int skip;             /* IMPLEMENT: 1: one flat span; every channel must keep one level over it */
} bitoscope_memory_record;

bitoscope_decode_input* bitoscope_memory_input(const uint8_t* const* planes, uint32_t num_channels,
                                               uint64_t num_samples, const bitoscope_memory_record* records,
                                               size_t num_records);

typedef struct bitoscope_memory_items {
  const bitoscope_stream_item* items;
  size_t num_items;
} bitoscope_memory_items;
bitoscope_decode_input* bitoscope_memory_item_input(uint64_t total_samples, const bitoscope_memory_items* inputs,
                                                    size_t num_inputs);

int bitoscope_memory_add_items(bitoscope_decode_input* in, const bitoscope_memory_items* inputs, size_t num_inputs);

int bitoscope_memory_set_progress_step(bitoscope_decode_input* in, uint64_t step);

bitoscope_annotations* bitoscope_collect_annotations(bitoscope_decode_input* in);

int bitoscope_finish_annotations(bitoscope_annotations* out);

size_t bitoscope_annotation_count(const bitoscope_annotations* out);
const bitoscope_annotation* bitoscope_annotation_at(const bitoscope_annotations* out, size_t index);

const uint8_t* bitoscope_collected_bytes(const bitoscope_annotations* out, uint64_t reference, size_t* length);

typedef struct bitoscope_settled {
  uint64_t sample;
  uint64_t annotations_before;
} bitoscope_settled;

size_t bitoscope_settled_count(const bitoscope_annotations* out);
const bitoscope_settled* bitoscope_settled_at(const bitoscope_annotations* out, size_t index);

int bitoscope_collect_items(bitoscope_annotations* out, const bitoscope_job_output* outputs, size_t num_outputs);

size_t bitoscope_item_count(const bitoscope_annotations* out, uint32_t output);
const bitoscope_stream_item* bitoscope_item_at(const bitoscope_annotations* out, uint32_t output, size_t index);

void bitoscope_free_annotations(bitoscope_annotations* out);
void bitoscope_free_input(bitoscope_decode_input* in);

typedef struct bitoscope_test_plugin bitoscope_test_plugin;
typedef struct bitoscope_test_run bitoscope_test_run;

bitoscope_test_plugin* bitoscope_test_plugin_load(bitoscope_list_decoders_fn list_decoders, void* user);
const char* bitoscope_test_plugin_error(const bitoscope_test_plugin* plugin);
void bitoscope_test_plugin_free(bitoscope_test_plugin* plugin);

/* IMPLEMENT: The capture a test decodes, as the host holds it. */
typedef struct bitoscope_test_capture {
  const uint8_t* const* planes; /* IMPLEMENT: one per capture channel: sample i is bit i % 8 of byte i / 8 */
  uint32_t num_channels;        /* IMPLEMENT: 0 for a decoder that reads only inputs */
  uint64_t num_samples;
  uint64_t sample_rate_milli_hz;
  const bitoscope_memory_record* records; /* IMPLEMENT: how the input is cut into spans; none: one span */
  size_t num_records;
} bitoscope_test_capture;

/* IMPLEMENT: One channel of the decoder bound to a channel of the capture ("rx=3"). */
typedef struct bitoscope_test_binding {
  const char* name;
  uint32_t capture_channel;
} bitoscope_test_binding;

/* IMPLEMENT: One input of the decoder bound to an output of a lower decoder: its type and width, and its items. */
typedef struct bitoscope_test_input {
  const char* name;
  int type; /* IMPLEMENT: BITOSCOPE_STREAM_... */
  uint32_t width;
  const bitoscope_stream_item* items;
  size_t num_items;
} bitoscope_test_input;

/* IMPLEMENT: One job, as a user asks for it: options left out take their defaults. Start from all zeros. */
typedef struct bitoscope_test_job {
  const char* decoder;
  const bitoscope_test_binding* channels;
  size_t num_channels;
  const bitoscope_job_option* options;
  size_t num_options;
  const bitoscope_test_input* inputs;
  size_t num_inputs;
  const char* const* outputs; /* IMPLEMENT: the outputs whose items the test reads */
  size_t num_outputs;
  uint64_t progress_step;     /* IMPLEMENT: 0: the SDK's own */
  /* IMPLEMENT: 0: as the GUI and the command-line program show it: rows the host makes are made, the decoder runs with
   * their enable options "no", annotations ordered by begin, then row. 1: the decoder's annotations as it emitted
   * them, options as given. */
  int as_emitted;
} bitoscope_test_job;

bitoscope_test_run* bitoscope_test_decode(const bitoscope_test_plugin* plugin, const bitoscope_test_capture* capture,
                                          const bitoscope_test_job* job);
const char* bitoscope_test_error(const bitoscope_test_run* run);
uint64_t bitoscope_test_look_ahead(const bitoscope_test_run* run);
size_t bitoscope_test_annotation_count(const bitoscope_test_run* run);
const bitoscope_annotation* bitoscope_test_annotation_at(const bitoscope_test_run* run, size_t index);
size_t bitoscope_test_item_count(const bitoscope_test_run* run, const char* output);
const bitoscope_stream_item* bitoscope_test_item_at(const bitoscope_test_run* run, const char* output, size_t index);
const bitoscope_annotations* bitoscope_test_output(const bitoscope_test_run* run);
void bitoscope_test_free(bitoscope_test_run* run);

#ifdef __cplusplus
}
#endif

#endif
