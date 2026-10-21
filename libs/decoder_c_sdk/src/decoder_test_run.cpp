/*
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bitoscope_decoder_testing.h"
#include "bitoscope_decoder_testing_cpp.h"
#include "decode_input.h"
#include "decode_job.h"
#include "declared_decoder.h"
#include "decoder_model.h"
#include "decoder_plugin.h"
#include "derived_row_run.h"

namespace sdk = bitoscope::plugin_c_sdk;
namespace dec = bitoscope::ipc::decoder;
namespace decode = bitoscope::decode;

struct bitoscope_test_plugin {
  bitoscope_decoders decoders;
  std::string error;
};

struct bitoscope_test_run {
  std::unique_ptr<sdk::PreparedJob> job;
  std::unique_ptr<sdk::DeliveryCheck> check;
  std::unique_ptr<bitoscope_decode_input> input;
  std::unique_ptr<bitoscope_annotations> out;
  std::vector<bitoscope_annotation> shown;
  std::string error;
};

namespace {

bool planeBit(const uint8_t* plane, uint64_t i) { return ((plane[i / 8] >> (i % 8)) & 1u) != 0; }

class CaptureView final : public decode::SignalView {
 public:
  explicit CaptureView(const bitoscope_test_capture& capture) : capture_(capture) {}

  uint32_t channelCount() const override { return capture_.num_channels; }
  uint64_t sampleCount() const override { return capture_.num_samples; }
  uint64_t sampleRateMilliHz() const override { return capture_.sample_rate_milli_hz; }
  bool level(uint32_t channel, uint64_t index) const override {
    return channel < capture_.num_channels && index < capture_.num_samples &&
           planeBit(capture_.planes[channel], index);
  }
  std::optional<uint64_t> nextEdge(uint32_t channel, uint64_t from, decode::EdgeKind edge) const override {
    if (channel >= capture_.num_channels) return std::nullopt;
    const uint8_t* plane = capture_.planes[channel];
    for (uint64_t i = from + 1; i < capture_.num_samples; ++i) {
      const bool now = planeBit(plane, i);
      if (now == planeBit(plane, i - 1)) continue;
      if (edge == decode::EdgeKind::Any || (edge == decode::EdgeKind::Rising) == now) return i;
    }
    return std::nullopt;
  }

 private:
  const bitoscope_test_capture& capture_;
};

std::string text(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

dec::StreamType wireStreamType(int type) {
  if (type == BITOSCOPE_STREAM_WORDS) return dec::StreamType::words;
  if (type == BITOSCOPE_STREAM_SPI_TRANSFERS) return dec::StreamType::spiTransfers;
  return dec::StreamType::unspecified;
}

dec::JobSetup setupOf(const bitoscope_test_capture& capture, const bitoscope_test_job& job) {
  dec::JobSetup setup;
  setup.decoder = text(job.decoder);
  for (size_t i = 0; job.channels != nullptr && i < job.num_channels; ++i) {
    setup.channels.push_back({text(job.channels[i].name), job.channels[i].capture_channel});
  }
  for (size_t i = 0; job.options != nullptr && i < job.num_options; ++i) {
    setup.options.push_back({text(job.options[i].name), text(job.options[i].value)});
  }
  for (size_t i = 0; job.inputs != nullptr && i < job.num_inputs; ++i) {
    setup.inputs.push_back({text(job.inputs[i].name), wireStreamType(job.inputs[i].type), job.inputs[i].width});
  }
  for (size_t i = 0; job.outputs != nullptr && i < job.num_outputs; ++i) setup.outputs.push_back(text(job.outputs[i]));
  setup.facts = {capture.sample_rate_milli_hz, capture.num_channels, capture.num_samples};
  setup.progressStep = job.progress_step;
  return setup;
}

decode::OptionMap resolvedOf(const sdk::PreparedJob& job) {
  decode::OptionMap out;
  for (const auto& [name, value] : job.optionTexts) out[name] = value;
  return out;
}

std::vector<std::vector<bitoscope_stream_item>> itemsOf(const sdk::PreparedJob& prepared,
                                                        const bitoscope_test_job& job) {
  std::vector<std::vector<bitoscope_stream_item>> items;
  for (const sdk::JobInput& declared : prepared.declaredInputs) {
    std::vector<bitoscope_stream_item>& list = items.emplace_back();
    for (size_t i = 0; declared.bound && job.inputs != nullptr && i < job.num_inputs; ++i) {
      const bitoscope_test_input& given = job.inputs[i];
      if (text(given.name) != declared.name || given.items == nullptr) continue;
      list.assign(given.items, given.items + given.num_items);
    }
  }
  return items;
}

std::unique_ptr<bitoscope_decode_input> inputOf(const sdk::PreparedJob& prepared, const bitoscope_test_capture& capture,
                                                const bitoscope_test_job& job) {
  std::vector<sdk::ItemInputSpec> specs;
  for (const sdk::JobInput& i : prepared.declaredInputs) specs.push_back({i.name, i.bound});
  std::unique_ptr<bitoscope_decode_input> input;
  if (prepared.channels.empty() && !prepared.declaredInputs.empty()) {
    input = bitoscope_decode_input::fromItems(capture.num_samples, std::move(specs),
                                              sdk::memoryItems(itemsOf(prepared, job)));
  } else {
    std::vector<const uint8_t*> planes;
    for (const bitoscope_job_channel& c : prepared.channels) {
      planes.push_back(capture.planes != nullptr ? capture.planes[c.capture_channel] : nullptr);
    }
    const std::span<const bitoscope_memory_record> records =
        capture.records == nullptr ? std::span<const bitoscope_memory_record>()
                                   : std::span(capture.records, capture.num_records);
    input = bitoscope_decode_input::fromMemory(planes.data(), static_cast<uint32_t>(planes.size()),
                                               capture.num_samples, records);
    if (!input->failed() && !prepared.declaredInputs.empty()) {
      if (auto added = input->addItems(std::move(specs), sdk::memoryItems(itemsOf(prepared, job))); !added) {
        input->fail(added.error());
      }
    }
  }
  input->setProgressStep(job.progress_step);
  return input;
}

void decodeAsHost(bitoscope_test_run& run, const bitoscope_test_plugin& plugin, const bitoscope_test_capture& capture,
                  const bitoscope_test_job& job) {
  dec::JobSetup setup = setupOf(capture, job);
  auto prepared = sdk::prepareJob(plugin.decoders.decoders, 1, setup);
  if (!prepared) {
    run.error = std::move(prepared.error());
    return;
  }
  const sdk::DeclaredDecoder declared((*prepared)->decoder->info);
  const std::vector<decode::RowSource> sources = declared.rowSources();
  std::vector<decode::RowSource> requested;
  if (job.as_emitted == 0) {
    const decode::OptionMap resolved = resolvedOf(**prepared);
    requested = decode::requestedDerivedRows(sources, resolved);
    const decode::OptionMap forced = decode::withDerivedRowsOff(sources, resolved);
    if (forced != resolved) {
      setup.options.clear();
      for (const auto& [name, value] : forced) setup.options.push_back({name, value});
      prepared = sdk::prepareJob(plugin.decoders.decoders, 1, setup);
      if (!prepared) {
        run.error = std::move(prepared.error());
        return;
      }
    }
  }
  run.job = std::move(*prepared);
  sdk::PreparedJob& prepared_job = *run.job;
  run.input = inputOf(prepared_job, capture, job);
  if (run.input->failed()) {
    run.error = run.input->error();
    return;
  }
  run.check = std::make_unique<sdk::DeliveryCheck>(*prepared_job.decoder);
  run.out = std::make_unique<bitoscope_annotations>(run.input.get());
  run.out->checkBatches([check = run.check.get()](std::span<const bitoscope_annotation> batch, size_t values) {
    return check->refusal(batch, values);
  });
  sdk::decodePrepared(prepared_job, *run.input, *run.out, {});
  if (run.input->failed()) {
    run.error = run.input->error().empty() ? run.check->prefix() + "job failed" : run.input->error();
    return;
  }
  std::vector<bitoscope_annotation> emitted = run.out->collected().all();
  if (job.as_emitted != 0) {
    run.shown = std::move(emitted);
    return;
  }
  std::vector<decode::Annotation> shown;
  shown.reserve(emitted.size());
  for (const bitoscope_annotation& a : emitted) shown.push_back({a.begin, a.end, a.row, a.cls, a.value});
  decode::ChannelMap channels;
  for (const bitoscope_job_channel& c : prepared_job.channels) channels[c.name] = c.capture_channel;
  const CaptureView view(capture);
  if (auto made = decode::insertDerivedRowsByRow(shown, requested, declared.rows(), resolvedOf(prepared_job),
                                                 channels, view, decode::hostDerivedRows(sources));
      !made) {
    run.error = std::move(made.error());
    return;
  }
  run.shown.reserve(shown.size());
  for (const decode::Annotation& a : shown) run.shown.push_back({a.begin, a.end, a.row, a.cls, a.value});
}

std::optional<uint32_t> outputIndex(const bitoscope_test_run* run, const char* output) {
  if (run == nullptr || run->job == nullptr || run->out == nullptr || output == nullptr) return std::nullopt;
  const std::vector<decode::GuardedOutput>& outputs = run->job->declaredOutputs;
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (outputs[i].name == output) return static_cast<uint32_t>(i);
  }
  return std::nullopt;
}

}

extern "C" {

bitoscope_test_plugin* bitoscope_test_plugin_load(bitoscope_list_decoders_fn list_decoders, void* user) {
  try {
    auto plugin = std::make_unique<bitoscope_test_plugin>();
    int listed = BITOSCOPE_ERROR;
    try {
      if (list_decoders != nullptr) listed = list_decoders(user, &plugin->decoders);
    } catch (...) {
      listed = BITOSCOPE_ERROR;
    }
    if (list_decoders == nullptr) {
      plugin->error = "plugin has no list function";
    } else if (listed != BITOSCOPE_OK) {
      plugin->error = "plugin could not list its decoders (its list function returned " + std::to_string(listed) + ")";
    } else if (auto version = sdk::checkPluginVersion(plugin->decoders.pluginVersion); !version) {
      plugin->error = std::move(version.error());
    } else if (!plugin->decoders.refusals.empty()) {
      plugin->error = plugin->decoders.refusals.front();
    }
    return plugin.release();
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

const char* bitoscope_test_plugin_error(const bitoscope_test_plugin* plugin) {
  if (plugin == nullptr) return "no plugin was loaded";
  return plugin->error.empty() ? nullptr : plugin->error.c_str();
}

void bitoscope_test_plugin_free(bitoscope_test_plugin* plugin) { delete plugin; }

bitoscope_test_run* bitoscope_test_decode(const bitoscope_test_plugin* plugin, const bitoscope_test_capture* capture,
                                          const bitoscope_test_job* job) {
  try {
    auto run = std::make_unique<bitoscope_test_run>();
    if (plugin == nullptr || capture == nullptr || job == nullptr) {
      run->error = "test run needs plugin, capture and job";
    } else {
      try {
        decodeAsHost(*run, *plugin, *capture, *job);
      } catch (const std::bad_alloc&) {
        run->error = "out of memory for test run";
        run->shown.clear();
      }
    }
    return run.release();
  } catch (const std::bad_alloc&) {
    return nullptr;
  }
}

const char* bitoscope_test_error(const bitoscope_test_run* run) {
  if (run == nullptr) return "out of memory for test run";
  return run->error.empty() ? nullptr : run->error.c_str();
}

uint64_t bitoscope_test_look_ahead(const bitoscope_test_run* run) {
  return run == nullptr || run->job == nullptr ? 0 : run->job->lookAhead;
}

size_t bitoscope_test_annotation_count(const bitoscope_test_run* run) {
  return run == nullptr || !run->error.empty() ? 0 : run->shown.size();
}

const bitoscope_annotation* bitoscope_test_annotation_at(const bitoscope_test_run* run, size_t index) {
  if (index >= bitoscope_test_annotation_count(run)) return nullptr;
  return &run->shown[index];
}

size_t bitoscope_test_item_count(const bitoscope_test_run* run, const char* output) {
  const std::optional<uint32_t> index = outputIndex(run, output);
  return index ? run->out->collectedItems(*index).size() : 0;
}

const bitoscope_stream_item* bitoscope_test_item_at(const bitoscope_test_run* run, const char* output, size_t index) {
  const std::optional<uint32_t> found = outputIndex(run, output);
  if (!found) return nullptr;
  const std::span<const bitoscope_stream_item> items = run->out->collectedItems(*found);
  return index < items.size() ? &items[index] : nullptr;
}

const bitoscope_annotations* bitoscope_test_output(const bitoscope_test_run* run) {
  return run == nullptr ? nullptr : run->out.get();
}

void bitoscope_test_free(bitoscope_test_run* run) { delete run; }

}

namespace bitoscope::plugin {

TestPlugin::TestPlugin(bitoscope_list_decoders_fn list, void* user) : loaded_(bitoscope_test_plugin_load(list, user)) {}

TestPlugin::TestPlugin(DecoderListFunction list)
    : plugin_(std::make_unique<DecoderPlugin>(std::move(list))),
      loaded_(bitoscope_test_plugin_load(&DecoderPlugin::list, plugin_.get())) {}

TestPlugin::~TestPlugin() { bitoscope_test_plugin_free(loaded_); }

std::string TestPlugin::error() const {
  const char* e = bitoscope_test_plugin_error(loaded_);
  return e == nullptr ? std::string() : std::string(e);
}

std::expected<TestDecode, std::string> TestPlugin::decode(const TestCapture& capture, const TestJob& job) const {
  std::vector<const uint8_t*> planes;
  for (const auto& p : capture.planes) planes.push_back(p.data());
  const bitoscope_test_capture c{planes.empty() ? nullptr : planes.data(), static_cast<uint32_t>(planes.size()),
                                 capture.samples, capture.sampleRateMilliHz,
                                 capture.records.empty() ? nullptr : capture.records.data(), capture.records.size()};
  std::vector<bitoscope_test_binding> channels;
  for (const auto& [name, channel] : job.channels) channels.push_back({name.c_str(), channel});
  std::vector<bitoscope_job_option> options;
  for (const auto& [name, value] : job.options) options.push_back({name.c_str(), value.c_str()});
  std::vector<bitoscope_test_input> inputs;
  for (const TestJob::Input& i : job.inputs) {
    inputs.push_back({i.name.c_str(), static_cast<int>(i.type), i.width, i.items.empty() ? nullptr : i.items.data(),
                      i.items.size()});
  }
  std::vector<const char*> outputs;
  for (const std::string& o : job.outputs) outputs.push_back(o.c_str());
  bitoscope_test_job j{};
  j.decoder = job.decoder.c_str();
  j.channels = channels.empty() ? nullptr : channels.data();
  j.num_channels = channels.size();
  j.options = options.empty() ? nullptr : options.data();
  j.num_options = options.size();
  j.inputs = inputs.empty() ? nullptr : inputs.data();
  j.num_inputs = inputs.size();
  j.outputs = outputs.empty() ? nullptr : outputs.data();
  j.num_outputs = outputs.size();
  j.progress_step = job.progressStep;
  j.as_emitted = job.asEmitted ? 1 : 0;
  const std::unique_ptr<bitoscope_test_run, void (*)(bitoscope_test_run*)> run(bitoscope_test_decode(loaded_, &c, &j),
                                                                               &bitoscope_test_free);
  if (run == nullptr) throw std::bad_alloc();
  if (const char* e = bitoscope_test_error(run.get())) return std::unexpected(std::string(e));
  TestDecode out;
  const size_t n = bitoscope_test_annotation_count(run.get());
  out.annotations.reserve(n);
  for (size_t i = 0; i < n; ++i) out.annotations.push_back(*bitoscope_test_annotation_at(run.get(), i));
  for (const std::string& o : job.outputs) {
    std::vector<StreamItem>& items = out.items[o];
    const size_t count = bitoscope_test_item_count(run.get(), o.c_str());
    for (size_t i = 0; i < count; ++i) items.push_back(*bitoscope_test_item_at(run.get(), o.c_str(), i));
  }
  const bitoscope_annotations* delivered = bitoscope_test_output(run.get());
  for (size_t i = 0; i < bitoscope_settled_count(delivered); ++i) out.settles.push_back(*bitoscope_settled_at(delivered, i));
  for (uint64_t reference = 0;; ++reference) {
    size_t length = 0;
    const uint8_t* bytes = bitoscope_collected_bytes(delivered, reference, &length);
    if (bytes == nullptr) break;
    out.bytes.emplace_back(bytes, bytes + length);
  }
  out.lookAhead = bitoscope_test_look_ahead(run.get());
  return out;
}

}
