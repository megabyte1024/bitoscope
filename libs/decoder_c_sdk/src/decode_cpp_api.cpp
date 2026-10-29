/*
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <charconv>
#include <exception>
#include <new>
#include <system_error>
#include <utility>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "bitoscope_decoder.h"
#include "bitoscope_decoder_cpp.h"
#include "bitoscope_decoder_testing.h"
#include "bitoscope_decoder_testing_cpp.h"
#include "c_declaration.h"

namespace bitoscope::plugin {

namespace {

constexpr uint64_t kViewWordSamples = 64;

}

DecodeInput::SampleCount DecodeInput::sampleCount() const {
  int is_final = 0;
  SampleCount c;
  c.samples = bitoscope_sample_count(in_, &is_final);
  c.final = is_final != 0;
  return c;
}

std::optional<uint64_t> DecodeInput::sampleAt(uint32_t channel, uint64_t start, uint64_t num, uint64_t den,
                                              uint64_t k0, uint32_t n) {
  if (reportIfDue(start) != BITOSCOPE_OK) return std::nullopt;
  uint64_t value = 0;
  if (bitoscope_sample_at(in_, channel, start, num, den, k0, n, &value) != BITOSCOPE_OK) return std::nullopt;
  return value;
}

DecodeInput::Clocked DecodeInput::clocked(uint32_t clock, uint32_t data, uint64_t from, Edge edge, uint32_t n,
                                          uint64_t limit) {
  Clocked c;
  c.end = from;
  if (reportIfDue(from) != BITOSCOPE_OK) return c;
  const int taken = bitoscope_clocked(in_, clock, data, from, static_cast<int>(edge), n, limit, &c.value, &c.end);
  if (taken < 0) {
    c = Clocked{};
    c.end = from;
    return c;
  }
  c.bits = static_cast<uint32_t>(taken);
  return c;
}

std::string DecodeInput::error() const {
  const char* e = bitoscope_decode_error(in_);
  return e == nullptr ? std::string() : std::string(e);
}

void DecodeInput::fail(std::string_view reason) {
  stopped_ = true;
  (void)bitoscope_fail_decode(in_, std::string(reason).c_str());
}

void DecodeInput::setProgress(uint64_t (*ask)(void*, uint64_t), void* user) {
  if (out_ == nullptr) return;
  ask_ = ask;
  askUser_ = user;
  const uint64_t step = (std::min)(bitoscope_progress_step(out_), uint64_t{1} << 62);
  viewSamples_ = (step + kViewWordSamples - 1) / kViewWordSamples * kViewWordSamples;
  nextReport_ = 0;
}

int DecodeInput::report(uint64_t position) {
  nextReport_ = position > UINT64_MAX - viewSamples_ ? UINT64_MAX : position + viewSamples_;
  return bitoscope_settle(out_, (std::min)(ask_(askUser_, position), position));
}

int DecodeInput::progressSpanAt(uint64_t sample, bitoscope_span& view) {
  bitoscope_span whole{};
  const int got = bitoscope_span_at(in_, sample, &whole);
  if (got != BITOSCOPE_OK) return got;
  if (const int reported = reportIfDue(sample); reported != BITOSCOPE_OK) return reported;
  view = whole;
  if (whole.num_samples <= viewSamples_) return BITOSCOPE_OK;
  const uint64_t skip = (sample - whole.first_sample) / kViewWordSamples * kViewWordSamples;
  view.first_sample += skip;
  view.num_samples = (std::min)(whole.num_samples - skip, viewSamples_);
  if (view.planes != nullptr) view.planes += skip / 8;
  return BITOSCOPE_OK;
}


DecodeJob::DecodeJob(const bitoscope_decode_job& job)
    : id(job.job_id),
      decoder(job.decoder != nullptr ? job.decoder : ""),
      sampleRateMilliHz(job.sample_rate_milli_hz),
      captureChannels(job.capture_channels),
      totalSamples(job.total_samples),
      totalIsBound(job.total_is_bound != 0),
      progressStep(job.progress_step) {
  for (size_t i = 0; i < job.num_channels; ++i) {
    const bitoscope_job_channel& c = job.channels[i];
    channels.push_back(Channel{c.name != nullptr ? c.name : "", c.input_channel, c.capture_channel});
  }
  for (size_t i = 0; i < job.num_options; ++i) {
    const bitoscope_job_option& o = job.options[i];
    options[o.name != nullptr ? o.name : ""] = o.value != nullptr ? o.value : "";
  }
  for (size_t i = 0; job.outputs != nullptr && i < job.num_outputs; ++i) {
    const bitoscope_job_output& o = job.outputs[i];
    outputs.push_back(Output{o.name != nullptr ? o.name : "", static_cast<StreamType>(o.type), o.width, o.wanted != 0});
  }
  for (size_t i = 0; job.inputs != nullptr && i < job.num_inputs; ++i) {
    const bitoscope_job_input& n = job.inputs[i];
    inputs.push_back(Input{n.name != nullptr ? n.name : "", static_cast<StreamType>(n.type), n.width, n.bound != 0});
  }
}

std::optional<uint32_t> DecodeJob::channel(std::string_view name) const {
  for (const Channel& c : channels) {
    if (c.name == name) return c.input;
  }
  return std::nullopt;
}

std::string_view DecodeJob::option(std::string_view name) const {
  const auto it = options.find(name);
  return it == options.end() ? std::string_view() : std::string_view(it->second);
}

std::optional<int64_t> DecodeJob::optionInt(std::string_view name) const {
  const std::string_view text = option(name);
  int64_t value = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || r.ec != std::errc() || r.ptr != text.data() + text.size()) return std::nullopt;
  return value;
}

std::expected<void, std::string> ProtocolDecoder::check(const DecodeJob&) const { return {}; }

uint64_t ProtocolDecoder::lookAheadSamples(const DecodeJob&) const { return 0; }

namespace {

int decodeThrough(void* user, const bitoscope_decode_job* job, bitoscope_decode_input* in, bitoscope_annotations* out) {
  try {
    const DecodeJob values(*job);
    return ProtocolDecoder::run(*static_cast<ProtocolDecoder*>(user), values, in, out);
  } catch (...) {
    (void)bitoscope_fail_decode(in, "decode job could not be handed to decoder: out of memory");
    return BITOSCOPE_ERROR;
  }
}

int checkThrough(void* user, const bitoscope_decode_job* job, bitoscope_reply* reply) {
  try {
    const DecodeJob values(*job);
    const auto* decoder = static_cast<const ProtocolDecoder*>(user);
    auto checked = decoder->check(values);
    if (checked) return bitoscope_reply_look_ahead_samples(reply, decoder->lookAheadSamples(values));
    (void)bitoscope_reply_message(reply, checked.error().c_str());
  } catch (const std::exception& e) {
    (void)bitoscope_reply_message(reply, (std::string("decoder's check threw: ") + e.what()).c_str());
  } catch (...) {
    (void)bitoscope_reply_message(reply, "decoder's check threw exception that is not std::exception");
  }
  return BITOSCOPE_ERROR;
}

}

bool Decoders::add(std::unique_ptr<ProtocolDecoder> decoder) {
  if (!decoder || decoders_ == nullptr) return false;
  try {
    const plugin_c_sdk::CDeclaration c(decoder->declaration(), &decodeThrough, &checkThrough, decoder.get());
    if (bitoscope_add_decoder(decoders_, c.get()) != BITOSCOPE_OK) return false;
    keep_->push_back(std::move(decoder));
    return true;
  } catch (const std::exception& e) {
    bitoscope_log(BITOSCOPE_LOG_ERROR, "decoder was refused: its declaration threw: %s", e.what());
  } catch (...) {
    bitoscope_log(BITOSCOPE_LOG_ERROR, "decoder was refused: its declaration threw");
  }
  return false;
}

void Decoders::setPluginVersion(std::string_view version) {
  const std::string text(version);
  (void)bitoscope_set_decoder_plugin_version(decoders_, text.c_str());
}

void Decoders::setRunsScripts(bool runsScripts) {
  (void)bitoscope_set_decoder_plugin_runs_scripts(decoders_, runsScripts ? 1 : 0);
}

int DecoderPlugin::list(void* user, bitoscope_decoders* decoders) {
  auto* self = static_cast<DecoderPlugin*>(user);
  try {
    Decoders list(decoders, self->decoders_);
    auto listed = self->list_(list);
    if (listed) return BITOSCOPE_OK;
    bitoscope_log(BITOSCOPE_LOG_ERROR, "decoder list failed: %s", listed.error().c_str());
  } catch (const std::exception& e) {
    bitoscope_log(BITOSCOPE_LOG_ERROR, "decoder list threw: %s", e.what());
  } catch (...) {
    bitoscope_log(BITOSCOPE_LOG_ERROR, "decoder list threw");
  }
  return BITOSCOPE_ERROR;
}

int DecoderPlugin::run(int argc, char** argv) {
  return bitoscope_run_decoder_plugin(argc, argv, &DecoderPlugin::list, this);
}

int runDecoderPlugin(int argc, char** argv, DecoderListFunction list) {
  DecoderPlugin plugin(std::move(list));
  return plugin.run(argc, argv);
}

int ProtocolDecoder::run(ProtocolDecoder& decoder, const DecodeJob& job, bitoscope_decode_input* in,
                         bitoscope_annotations* out) {
  DecodeInput input(in, out);
  Annotations annotations(out, input.stopFlag());
  try {
    auto done = decoder.decode(job, input, annotations);
    if (!done) input.fail(done.error().empty() ? std::string("decoder failed") : done.error());
  } catch (const std::exception& e) {
    input.fail(std::string("decoder threw: ") + e.what());
  } catch (...) {
    input.fail("decoder threw exception that is not std::exception");
  }
  return input.failed() ? BITOSCOPE_ERROR : BITOSCOPE_OK;
}

MemoryInput::MemoryInput(std::span<const std::span<const uint8_t>> planes, uint64_t numSamples,
                         std::span<const bitoscope_memory_record> records) {
  std::vector<const uint8_t*> pointers;
  pointers.reserve(planes.size());
  for (const auto& p : planes) pointers.push_back(p.data());
  in_ = bitoscope_memory_input(pointers.data(), static_cast<uint32_t>(pointers.size()), numSamples,
                               records.empty() ? nullptr : records.data(), records.size());
  if (in_ == nullptr) throw std::bad_alloc();
}

MemoryInput::~MemoryInput() { bitoscope_free_input(in_); }

bool MemoryInput::addItems(const std::vector<std::vector<StreamItem>>& inputs) {
  std::vector<bitoscope_memory_items> c;
  c.reserve(inputs.size());
  for (const std::vector<StreamItem>& items : inputs) c.push_back({items.empty() ? nullptr : items.data(), items.size()});
  return bitoscope_memory_add_items(in_, c.empty() ? nullptr : c.data(), c.size()) == BITOSCOPE_OK;
}

CollectedAnnotations::CollectedAnnotations(MemoryInput& input) : out_(bitoscope_collect_annotations(input.get())) {
  if (out_ == nullptr) throw std::bad_alloc();
}

CollectedAnnotations::CollectedAnnotations(MemoryInput& input, const DecodeJob& job) : CollectedAnnotations(input) {
  std::vector<bitoscope_job_output> outputs;
  outputs.reserve(job.outputs.size());
  for (const DecodeJob::Output& o : job.outputs) {
    outputs_.push_back(o.name);
    outputs.push_back({o.name.c_str(), static_cast<int>(o.type), o.width, o.wanted ? 1 : 0});
  }
  if (bitoscope_collect_items(out_, outputs.empty() ? nullptr : outputs.data(), outputs.size()) != BITOSCOPE_OK) {
    throw std::invalid_argument("job's outputs cannot be collected (output of no stream type?)");
  }
}

std::vector<StreamItem> CollectedAnnotations::items(std::string_view output) const {
  std::vector<StreamItem> out;
  for (size_t o = 0; o < outputs_.size(); ++o) {
    if (outputs_[o] != output) continue;
    const auto index = static_cast<uint32_t>(o);
    const size_t n = bitoscope_item_count(out_, index);
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) out.push_back(*bitoscope_item_at(out_, index, i));
    break;
  }
  return out;
}

std::expected<StreamWriter, std::string> Annotations::output(const DecodeJob& job, std::string_view name) const {
  if (job.outputs.empty()) return StreamWriter();
  for (size_t o = 0; o < job.outputs.size(); ++o) {
    if (job.outputs[o].name != name) continue;
    if (!job.outputs[o].wanted) return StreamWriter();
    return StreamWriter(out_, static_cast<uint32_t>(o), stop_);
  }
  return std::unexpected("decoder '" + job.decoder + "' has no output '" + std::string(name) + "'");
}

std::expected<StreamReader, std::string> DecodeInput::input(const DecodeJob& job, std::string_view name) {
  for (size_t i = 0; i < job.inputs.size(); ++i) {
    if (job.inputs[i].name != name) continue;
    if (!job.inputs[i].bound) return StreamReader();
    return StreamReader(in_, static_cast<uint32_t>(i), this);
  }
  return std::unexpected("decoder '" + job.decoder + "' has no input '" + std::string(name) + "'");
}

MemoryItemInput::MemoryItemInput(uint64_t totalSamples, const std::vector<std::vector<StreamItem>>& inputs) {
  std::vector<bitoscope_memory_items> c;
  c.reserve(inputs.size());
  for (const std::vector<StreamItem>& items : inputs) c.push_back({items.empty() ? nullptr : items.data(), items.size()});
  in_ = bitoscope_memory_item_input(totalSamples, c.empty() ? nullptr : c.data(), c.size());
  if (in_ == nullptr) throw std::bad_alloc();
}

MemoryItemInput::~MemoryItemInput() { bitoscope_free_input(in_); }

CollectedAnnotations::~CollectedAnnotations() { bitoscope_free_annotations(out_); }

std::vector<bitoscope_annotation> CollectedAnnotations::all() const {
  std::vector<bitoscope_annotation> out;
  const size_t n = bitoscope_annotation_count(out_);
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) out.push_back(*bitoscope_annotation_at(out_, i));
  return out;
}

std::vector<bitoscope_settled> CollectedAnnotations::settles() const {
  std::vector<bitoscope_settled> out;
  const size_t n = bitoscope_settled_count(out_);
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) out.push_back(*bitoscope_settled_at(out_, i));
  return out;
}

}
