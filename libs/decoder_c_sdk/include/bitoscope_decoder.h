/*
 * SPDX-License-Identifier: MIT
 */

#ifndef BITOSCOPE_DECODER_H
#define BITOSCOPE_DECODER_H

#include <stddef.h>
#include <stdint.h>

#include "bitoscope_plugin_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bitoscope_decode_input bitoscope_decode_input;
typedef struct bitoscope_annotations bitoscope_annotations;
typedef struct bitoscope_job_output bitoscope_job_output;
typedef struct bitoscope_job_input bitoscope_job_input;

#define BITOSCOPE_NONE (-3)

#define BITOSCOPE_EDGE_RISING 0
#define BITOSCOPE_EDGE_FALLING 1
#define BITOSCOPE_EDGE_ANY 2

uint32_t bitoscope_input_channels(const bitoscope_decode_input* in);

int bitoscope_level(bitoscope_decode_input* in, uint32_t channel, uint64_t sample);

int bitoscope_next_edge(bitoscope_decode_input* in, uint32_t channel, uint64_t from, int kind, uint64_t* at);

int bitoscope_next_edge_limit(bitoscope_decode_input* in, uint32_t channel, uint64_t from, int kind,
                              uint64_t limit, uint64_t* at);

int bitoscope_next_change2(bitoscope_decode_input* in, uint32_t channel_a, uint32_t channel_b, uint64_t from,
                           uint64_t* at, uint32_t* which);

int bitoscope_next_change2_limit(bitoscope_decode_input* in, uint32_t channel_a, uint32_t channel_b,
                                 uint64_t from, uint64_t limit, uint64_t* at, uint32_t* which);

int bitoscope_bits(bitoscope_decode_input* in, uint32_t channel, uint64_t first, uint32_t n, uint64_t* value);

int bitoscope_sample_at(bitoscope_decode_input* in, uint32_t channel, uint64_t start, uint64_t num, uint64_t den,
                        uint64_t k0, uint32_t n, uint64_t* value);

int bitoscope_clocked(bitoscope_decode_input* in, uint32_t clock, uint32_t data, uint64_t from, int edge, uint32_t n,
                      uint64_t limit, uint64_t* value, uint64_t* end);

uint64_t bitoscope_sample_count(bitoscope_decode_input* in, int* is_final);

int bitoscope_release_before(bitoscope_decode_input* in, uint64_t sample);

int bitoscope_fail_decode(bitoscope_decode_input* in, const char* reason);

const char* bitoscope_decode_error(const bitoscope_decode_input* in);

// WHY: Bits of a span's last plane word past num_samples are NOT samples and may hold anything.
typedef struct bitoscope_span {
  uint64_t first_sample;
  uint64_t num_samples;
  const uint8_t* planes;
  uint64_t plane_stride;
  const uint8_t* levels;
  const uint64_t* released_mark;
} bitoscope_span;

int bitoscope_span_at(bitoscope_decode_input* in, uint64_t sample, bitoscope_span* span);

/* IMPLEMENT: One result: the samples [begin, end) it covers, the row it is drawn in, its class (one the decoder
 * declares, with how its value is shown) and its value. This struct never grows. */
typedef struct bitoscope_annotation {
  uint64_t begin;
  uint64_t end;
  uint32_t row;
  uint32_t cls;
  uint64_t value;
} bitoscope_annotation;

int bitoscope_emit(bitoscope_annotations* out, uint64_t begin, uint64_t end, uint32_t row, uint32_t cls,
                   uint64_t value);

int bitoscope_emit_bytes(bitoscope_annotations* out, uint64_t begin, uint64_t end, uint32_t row, uint32_t cls,
                         const uint8_t* bytes, size_t length);

int bitoscope_settle(bitoscope_annotations* out, uint64_t sample);
uint64_t bitoscope_settle_due(const bitoscope_annotations* out);
uint64_t bitoscope_progress_step(const bitoscope_annotations* out);

/* IMPLEMENT: An item's kind. A words output carries DATA (value = the word) and BOUNDARY (the end of a frame); an
 * spi-transfers output carries BEGIN (chip select went active), DATA (value = mosi | miso << 32) and
 * BOUNDARY (chip select released). */
#define BITOSCOPE_ITEM_DATA 0
#define BITOSCOPE_ITEM_BOUNDARY 1
#define BITOSCOPE_ITEM_BEGIN 2

/* IMPLEMENT: An item's flags. DAMAGED: on data, the word had an error (a framing or parity error); on a boundary, the
 * stream was cut there; on begin, chip select was already active at sample 0. NO_MOSI / NO_MISO: on
 * spi-transfers data, that line is not bound and its half of the value is 0. A reader ignores bits it does
 * not know. */
#define BITOSCOPE_ITEM_FLAG_DAMAGED 1u
#define BITOSCOPE_ITEM_FLAG_NO_MOSI 2u
#define BITOSCOPE_ITEM_FLAG_NO_MISO 4u

/* IMPLEMENT: One stream item: the samples [begin, end) it covers, its kind, value and flags. This struct never grows. */
typedef struct bitoscope_stream_item {
  uint64_t begin;
  uint64_t end;
  uint64_t value;
  uint32_t kind;  /* IMPLEMENT: BITOSCOPE_ITEM_... */
  uint32_t flags; /* IMPLEMENT: BITOSCOPE_ITEM_FLAG_... */
} bitoscope_stream_item;

int bitoscope_put(bitoscope_annotations* out, uint32_t output, uint64_t begin, uint64_t end, uint32_t kind,
                  uint64_t value, uint32_t flags);

int bitoscope_next_item(bitoscope_decode_input* in, uint32_t input, bitoscope_stream_item* item);

typedef struct bitoscope_decoders bitoscope_decoders;

/* IMPLEMENT: Declares every decoder with bitoscope_add_decoder(), sets the plugin's version with
 * bitoscope_set_decoder_plugin_version() and returns BITOSCOPE_OK. Any other value means the plugin could not list
 * its decoders; the host is told so. `user` is what you gave bitoscope_run_decoder_plugin(). The plugin's version is
 * required, as X.Y.Z with an optional suffix: "1.0.0", "1.2.3-beta", "2.0.3-alpha01". Three numbers without leading
 * zeros; then "-" and letters, digits and dots (at most 32 characters in all). A plugin without one, or with one in
 * another form, does not start. */
typedef int (*bitoscope_list_decoders_fn)(void* user, bitoscope_decoders* decoders);

int bitoscope_run_decoder_plugin(int argc, char** argv, bitoscope_list_decoders_fn list_decoders, void* user);

#define BITOSCOPE_OPTION_STRING 0  /* IMPLEMENT: any text (within allowed_values, when given) */
#define BITOSCOPE_OPTION_INTEGER 1 /* IMPLEMENT: a whole number that fits an int64, within minimum and maximum */

#define BITOSCOPE_VALUE_NONE 0     /* IMPLEMENT: no value: the class's name is the whole text ("Start") */
#define BITOSCOPE_VALUE_HEX 1      /* IMPLEMENT: "0x" and upper-case hex digits ("Data: 0x41", or "0x41" with no name) */
#define BITOSCOPE_VALUE_ADDRESS7 2 /* IMPLEMENT: a 7-bit address, two hex digits */
#define BITOSCOPE_VALUE_N_OF_M 3   /* IMPLEMENT: "name (N of M bits)": N in the value's low 32 bits, M in its high 32 */
#define BITOSCOPE_VALUE_DECIMAL 4  /* IMPLEMENT: the value in decimal ("Length: 12", or "12" with no name) */
#define BITOSCOPE_VALUE_N_OF_M_BYTES 5 /* IMPLEMENT: "name (N of M bytes)": N in the low 32 bits, M in the high 32 */
#define BITOSCOPE_VALUE_BYTES 6

/* IMPLEMENT: What a decoder's stream carries. 0 is not a type: a declaration that leaves it is refused. */
#define BITOSCOPE_STREAM_WORDS 1         /* IMPLEMENT: words of 1-32 bits, and the boundaries between frames */
#define BITOSCOPE_STREAM_SPI_TRANSFERS 2 /* IMPLEMENT: SPI transfers: begin, words (mosi | miso << 32), boundary */

/* IMPLEMENT: One channel a decoder reads ("rx", "clk"). You pass arrays of it, so its size is the stride a host
 * reads them by. */
typedef struct bitoscope_decoder_channel {
  const char* name;        /* IMPLEMENT: required, unique in the decoder: what the user binds ("rx=3") */
  const char* description;
  int required;            /* IMPLEMENT: 1: every job binds it; 0: optional */
  const char* display;     /* IMPLEMENT: what a user READS in place of name; NULL or "": host makes it from name */
} bitoscope_decoder_channel;

/* IMPLEMENT: One stream a decoder writes, for an upper decoder to read. You pass arrays of it, so its size is
 * the stride a host reads them by.
 *
 * WHAT THE OUTPUT CARRIES, so a decoder designed for certain protocols (bitoscope_decoder.protocols) may
 * read it: either `carries`, a fixed protocol id as the user types it ("spi"), or `carries_input`, the
 * name of one INPUT of this same decoder whose arriving protocol this output passes on (so one filter
 * serves SPI and I-superscript-2-C alike) - never both. Neither given: the output carries NOTHING, which a
 * decoder naming protocols never accepts (SPI's own MOSI and MISO are raw lines, not SPI transfers, and say
 * nothing). NULL is "not declared"; an EMPTY
 * text is a declaration with no name and is refused, since the host cannot tell the two apart. */
typedef struct bitoscope_decoder_output {
  const char* name;        /* IMPLEMENT: required, unique among the decoder's outputs: what a user binds ("link.rx") */
  const char* description;
  int type;                /* IMPLEMENT: BITOSCOPE_STREAM_... */
  uint32_t width;          /* IMPLEMENT: the word width, 1-32; ignored when width_option names an option */
  const char* width_option; // IMPLEMENT: an INTEGER option of this decoder that sets the width (UART's "data_bits"); NULL or "": `width`
  const char* requires_channel; // IMPLEMENT: a channel of this decoder that must be bound for the output to exist; NULL or "": none
  const char* display;     /* IMPLEMENT: what a user READS in place of name; NULL or "": host makes it from name */
  const char* carries;     /* IMPLEMENT: the protocol its items carry ("spi"); NULL: none declared */
  const char* carries_input; /* IMPLEMENT: an input of this decoder whose protocol it passes on; NULL: none declared */
} bitoscope_decoder_output;

/* IMPLEMENT: One stream a decoder reads: instead of channels (an upper decoder), or beside them (a decoder that
 * reads both). This struct never grows. */
typedef struct bitoscope_decoder_input {
  const char* name;        /* IMPLEMENT: required; not a channel's, an option's or "id" */
  const char* description;
  int type;                /* IMPLEMENT: BITOSCOPE_STREAM_... */
  uint32_t width;          /* IMPLEMENT: the word width it reads; 0: any */
  int required;            /* IMPLEMENT: 1: every job binds it; 0: optional */
  const char* display;     /* IMPLEMENT: what a user READS in place of name; NULL or "": host makes it from name */
} bitoscope_decoder_input;

/* IMPLEMENT: One option of a decoder ("baudrate"). You pass arrays of it, so its size is the stride a host
 * reads them by. */
typedef struct bitoscope_decoder_option {
  const char* name;          // IMPLEMENT: required, unique in the decoder: "baudrate", "cpol" ...
  const char* description;
  int type;                  /* IMPLEMENT: BITOSCOPE_OPTION_STRING or BITOSCOPE_OPTION_INTEGER */
  const char* default_value; /* IMPLEMENT: as the user would write it; allowed, within the limits, not unsupported */
  const char* const* allowed_values; /* IMPLEMENT: the only values allowed; none: any (within the limits) */
  size_t num_allowed_values;
  int has_minimum;           /* IMPLEMENT: an integer option's limits, inclusive */
  int64_t minimum;
  int has_maximum;
  int64_t maximum;
  const char* const* unsupported_values; // IMPLEMENT: values this decoder does not implement yet: refused with "not supported yet"
  size_t num_unsupported_values;
  const char* display;       /* IMPLEMENT: what a user READS in place of name; NULL or "": host makes it from name */
} bitoscope_decoder_option;

/* IMPLEMENT: What a class MEANS: the host picks its colour from this, per theme, so a decoder never
 * names a colour that is wrong in one theme. 0, BITOSCOPE_ROLE_NONE, says nothing and is drawn in a
 * neutral colour. Payload bytes and words are DATA; an I2C address ADDRESS; start and stop conditions and
 * delimiters START or STOP; ACK OK; anything damaged, truncated, incomplete, a framing or parity error,
 * NACK ERROR; commands, transfers, packets and fields CONTROL; single bits BIT; the rest INFO. */
#define BITOSCOPE_ROLE_NONE 0
#define BITOSCOPE_ROLE_DATA 1
#define BITOSCOPE_ROLE_START 2
#define BITOSCOPE_ROLE_STOP 3
#define BITOSCOPE_ROLE_OK 4
#define BITOSCOPE_ROLE_ERROR 5
#define BITOSCOPE_ROLE_CONTROL 6
#define BITOSCOPE_ROLE_ADDRESS 7
#define BITOSCOPE_ROLE_BIT 8
#define BITOSCOPE_ROLE_INFO 9

/* IMPLEMENT: One class of annotation a decoder emits: its id (what bitoscope_emit()'s `cls` names), its name,
 * how its value is shown and what it means. You pass arrays of it, so its size is the stride a host
 * reads them by. */
typedef struct bitoscope_annotation_class {
  uint32_t id;       /* IMPLEMENT: unique in the decoder */
  const char* name;  /* IMPLEMENT: "Start", "Data", "Address write" ... ("" with a value: the value alone) */
  int format;        /* IMPLEMENT: BITOSCOPE_VALUE_... */
  uint32_t hex_digits;           /* IMPLEMENT: BITOSCOPE_VALUE_HEX: at least this many digits */
  const char* hex_digits_option; // IMPLEMENT: BITOSCOPE_VALUE_HEX, optional: INTEGER option with a bit count; digits are (value + 3) / 4; NULL: hex_digits
  int role;          /* IMPLEMENT: BITOSCOPE_ROLE_...; 0: none */
} bitoscope_annotation_class;

/* IMPLEMENT: One row annotations are drawn in (what bitoscope_emit()'s `row` names). Never grows. */
typedef struct bitoscope_annotation_row {
  uint32_t id;      /* IMPLEMENT: unique in the decoder */
  const char* name; /* IMPLEMENT: "RX", "Bits" ... */
} bitoscope_annotation_row;

/* IMPLEMENT: How a row's content comes from another row's annotations, its WORDS. 0 is not a rule: a
 * declaration that leaves it is refused. */
#define BITOSCOPE_ROW_RULE_WORD_LIST 1   /* IMPLEMENT: the row's own annotations show the words inside their span as text */
#define BITOSCOPE_ROW_RULE_BIT_GRID 2    /* IMPLEMENT: bits on a grid of bit times from the word's frame start (UART) */
#define BITOSCOPE_ROW_RULE_CLOCK_EDGES 3 /* IMPLEMENT: bits from clock edge to clock edge inside the word (SPI, I2C) */

/* IMPLEMENT: BITOSCOPE_ROW_RULE_BIT_GRID. A word [b, e) of value v: R the sample rate, B the rate option, t(j) = j * R / B
 * rounded down, o = offset_bits, n = the count option. The frame starts at s = b - t(o); bit i spans
 * [s + t(o + i), s + t(o + i + 1)); the word fits when s + t(o + n) == e. Bit i is (v >> (n - 1 - i)) & 1 when the
 * order option reads "msb-first", else (v >> i) & 1. */
typedef struct bitoscope_bit_grid {
  const char* rate_option;  /* IMPLEMENT: an INTEGER option: bits per second ("baudrate") */
  uint32_t offset_bits;     /* IMPLEMENT: bit times from the frame's start to the word's first bit (UART: 1) */
  const char* count_option; /* IMPLEMENT: an INTEGER option: bits per word ("data_bits") */
  const char* order_option; /* IMPLEMENT: a STRING option ("bit_order") */
} bitoscope_bit_grid;

/* IMPLEMENT: BITOSCOPE_ROW_RULE_CLOCK_EDGES. A word [b, e) begins at an edge of the clock; bit i begins at the i-th next
 * clock edge of the same direction and ends where bit i + 1 begins, the last at e; its value is the data
 * channel's level where it begins. */
typedef struct bitoscope_clock_edges {
  const char* clock_channel; /* IMPLEMENT: a channel of the decoder ("clk", "scl") */
  const char* data_channel;  /* IMPLEMENT: a channel of the decoder ("mosi", "sda") */
  const char* count_option;  /* IMPLEMENT: an INTEGER option: bits per word ("wordsize"); NULL or "": `count` */
  uint32_t count;            /* IMPLEMENT: bits per word, 1 to 64, when count_option is none (I2C: 8) */
} bitoscope_clock_edges;

/* IMPLEMENT: One row whose content comes from another row's annotations. With a WORD_LIST rule your
 * decoder emits the row as always and a viewer shows each of its annotations as the words inside it. With a
 * BIT_GRID or CLOCK_EDGES rule the host makes the row from the words and your decoder never emits it: the GUI
 * and the command-line program run your decoder with the enable option set to "no" and make the row when the
 * user's value is "yes". Arrays of it are passed, so it never grows. */
typedef struct bitoscope_row_source {
  uint32_t row;                   /* IMPLEMENT: a declared row: the one whose content this is */
  uint32_t cls;                   /* IMPLEMENT: a declared class: the class of that content */
  uint32_t source_row;            /* IMPLEMENT: another declared row: where the words are */
  const uint32_t* source_classes; /* IMPLEMENT: declared classes: which annotations of source_row are the words; one or more */
  size_t num_source_classes;
  const char* enable_option;      /* IMPLEMENT: STRING option with values "yes" and "no": row wanted or not; NULL or "": always */
  int rule;                       /* IMPLEMENT: BITOSCOPE_ROW_RULE_... */
  bitoscope_bit_grid grid;        /* IMPLEMENT: read for BITOSCOPE_ROW_RULE_BIT_GRID only */
  bitoscope_clock_edges edges;    /* IMPLEMENT: read for BITOSCOPE_ROW_RULE_CLOCK_EDGES only */
} bitoscope_row_source;

typedef struct bitoscope_job_channel {
  const char* name;
  uint32_t input_channel;
  uint32_t capture_channel;
} bitoscope_job_channel;

typedef struct bitoscope_job_option {
  const char* name;
  const char* value;
} bitoscope_job_option;

struct bitoscope_job_output {
  const char* name;
  int type;
  uint32_t width;
  int wanted;
};

struct bitoscope_job_input {
  const char* name;
  int type;
  uint32_t width;
  int bound;
};

typedef struct bitoscope_decode_job {
  uint64_t job_id;
  const char* decoder;
  const bitoscope_job_channel* channels;
  size_t num_channels;
  const bitoscope_job_option* options;
  size_t num_options;
  uint64_t sample_rate_milli_hz;
  uint32_t capture_channels;
  uint64_t total_samples;
  const bitoscope_job_output* outputs;
  size_t num_outputs;
  const bitoscope_job_input* inputs;
  size_t num_inputs;
  uint64_t progress_step;
} bitoscope_decode_job;

/* IMPLEMENT: Optional: your decoder's own refusals that depend on the capture - a sample rate too low for the baud
 * rate, "one of rx and tx is needed" - on the SDK's main thread, after the SDK's checks (above).
 * BITOSCOPE_OK accepts; anything else refuses the job, with the reason you give with
 * bitoscope_reply_message(reply, ...), which the host shows as it is. Called for the host's CHECK and
 * again before a DECODE is started. */
typedef int (*bitoscope_check_fn)(void* user, const bitoscope_decode_job* job, bitoscope_reply* reply);

int bitoscope_reply_look_ahead(bitoscope_reply* reply, uint64_t samples);

/* IMPLEMENT: Decodes one job: reads `in` (above), emits into `out`, and returns BITOSCOPE_OK when the capture is
 * decoded. Anything else fails the job - say why with bitoscope_fail_decode(in, reason) first, or the
 * host is told only what you returned. On the SDK's job thread. As you go, call bitoscope_settle(out, sample)
 * with the first sample of the oldest result you still hold open (no later emit begins before it), as often as
 * is handy: the SDK keeps one call a progress step and the host shows what is settled while the job runs
 * (bitoscope_settle_due(out) is the first sample a call is kept at, and a call at the capture's last sample is
 * always kept). A decoder that never settles shows its results when the job ends. */
typedef int (*bitoscope_decode_fn)(void* user, const bitoscope_decode_job* job, bitoscope_decode_input* in,
                                   bitoscope_annotations* out);

/* IMPLEMENT: One decoder. Fill it in (start from all zeros) and pass it to bitoscope_add_decoder(); the SDK copies
 * it and every text and array it points to, so it can live on the stack. */
typedef struct bitoscope_decoder {
  const char* name; /* IMPLEMENT: required, unique in the plugin: what the user names ("uart") */
  bitoscope_decode_fn decode;
  const bitoscope_decoder_channel* channels; /* IMPLEMENT: at least one, unless it declares inputs (below); may have both */
  size_t num_channels;
  const bitoscope_annotation_class* classes; /* IMPLEMENT: at least one */
  size_t num_classes;
  const bitoscope_annotation_row* rows; /* IMPLEMENT: at least one */
  size_t num_rows;
  /* IMPLEMENT: Required: the decoder's own version, shown and saved with its results, in the plugin version's form
   * (X.Y.Z with an optional suffix: "1.0.0", "1.2.3-beta"). A decoder without one, or with one in another form, is
   * refused. */
  const char* version;
  /* IMPLEMENT: Optional from here on: leave them 0. */
  const bitoscope_decoder_option* options;
  size_t num_options;
  const char* const* unsupported_options; // IMPLEMENT: option ids this decoder does not implement yet: refused with "not supported yet"
  size_t num_unsupported_options;
  int allows_shared_channels; /* IMPLEMENT: 1: two of its channels may be bound to one capture channel */
  bitoscope_check_fn check;   /* IMPLEMENT: NULL: every job that passes the SDK's checks is accepted */
  void* user;                 /* IMPLEMENT: passed to check and decode */
  /* IMPLEMENT: Streams: leave them 0 for none. */
  const bitoscope_decoder_output* outputs; /* IMPLEMENT: the streams it writes */
  size_t num_outputs;
  const bitoscope_decoder_input* inputs;   /* IMPLEMENT: the streams it reads: a decoder has channels, inputs or both */
  size_t num_inputs;
  /* IMPLEMENT: What a user READS in place of this decoder's name (a name stays what the user types). NULL or "":
   * the host makes the display form from the name. UTF-8. Each channel, option, output and input
   * carries its own `display` inside its own struct. */
  const char* display;
  /* IMPLEMENT: The decoders whose output this decoder is DESIGNED to read, by their names ("spi", "qspi" for a
   * flash decoder that needs SPI framing). NULL, or 0 of them: it reads the output of any decoder. A name no
   * decoder of the host has is no error (a protocol with no decoder yet); the host compares names BYTE FOR BYTE, so
   * write them as the user writes them. The host refuses a binding to an output of a decoder that is not
   * on the list. */
  const char* const* protocols;
  size_t num_protocols;
  /* IMPLEMENT: Rows whose content comes from another row's annotations (bitoscope_row_source).
   * NULL, or 0 of them: none. */
  const bitoscope_row_source* row_sources;
  size_t num_row_sources;
} bitoscope_decoder;

#define bitoscope_add_decoder(decoders, decoder) bitoscope_add_decoder_sized((decoders), (decoder), sizeof(*(decoder)))

int bitoscope_set_decoder_plugin_version(bitoscope_decoders* decoders, const char* version);

int bitoscope_set_decoder_plugin_runs_scripts(bitoscope_decoders* decoders, int runs_scripts);

int bitoscope_find_job_channel(const bitoscope_decode_job* job, const char* name, uint32_t* input_channel);

const char* bitoscope_find_job_option(const bitoscope_decode_job* job, const char* name);

int bitoscope_output_wanted(const bitoscope_decode_job* job, const char* name, uint32_t* index);

int bitoscope_input_bound(const bitoscope_decode_job* job, const char* name, uint32_t* index);

int bitoscope_add_decoder_sized(bitoscope_decoders* decoders, const bitoscope_decoder* decoder, size_t decoder_size);

#ifdef __cplusplus
}
#endif

#endif
