/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <new>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "annotation_format.h"
#include "cli_annotation_filter.h"
#include "cli_args.h"
#include "decoder_chain.h"
#include "decoder_registry.h"
#include "derived_row_run.h"
#include "plugin_decoder.h"
#include "spec_display.h"
#include "raw_buffer_signal_view.h"
#include "raw_capture_buffer.h"

namespace bitoscope::cli {

using ConfiguredDecoder = bitoscope::decode::ChainInstance;

// WHY: samples_per_window is a parameter so this file holds no window-size policy and cannot drift from AcquireSeams.
inline std::expected<std::vector<ConfiguredDecoder>, std::string> validateDecoders(
    const CliOptions& opts, const bitoscope::decode::DecoderRegistry& registry,
    uint32_t samples_per_window) {
  std::vector<bitoscope::decode::CapturedChannel> captured;
  for (const Channel& channel : opts.channels) captured.push_back({channel.index, channel.name});

  // WHY: L comes from sizeTriggerRange(), the same function runAcquire() uses, so the two cannot differ by rounding.
  uint64_t capture_samples = opts.total_samples;
  if (opts.trigger.has_value()) {
    const LengthRequest length_request{opts.total_samples, opts.time_ms};
    auto sizes = sizeTriggerRange(length_request, preTriggerRequest(*opts.trigger),
                                  opts.sample_rate_milli_hz, samples_per_window);
    if (sizes.has_value()) capture_samples = sizes->length;
  }
  const bitoscope::decode::CaptureFacts facts{opts.sample_rate_milli_hz,
                                              static_cast<uint32_t>(opts.channels.size()),
                                              capture_samples};
  auto plan = bitoscope::decode::planChain(opts.decoders, registry, captured, facts);
  if (!plan) return std::unexpected(std::move(plan.error()));
  for (ConfiguredDecoder& instance : plan->instances) {
    auto rows = bitoscope::decode::takeDerivedRows(*instance.decoder);
    if (!rows) return std::unexpected(std::format("-P '{}': {}", instance.spec, rows.error()));
    instance.derived_rows = std::move(*rows);
  }
  return std::move(plan->instances);
}

namespace decode_detail {

inline std::string annotationText(const std::vector<bitoscope::decode::AnnotationClass>& classes,
                                  const bitoscope::decode::Annotation& annotation,
                                  const bitoscope::decode::OptionMap& options,
                                  const std::vector<std::vector<uint8_t>>& byte_values) {
  if (!bitoscope::decode::showsBytes(classes, annotation.cls)) {
    return bitoscope::decode::formatAnnotation(classes, annotation, options);
  }
  const bool known = annotation.value < byte_values.size();
  return bitoscope::decode::formatAnnotation(classes, annotation, options,
                                             known ? std::span<const uint8_t>(byte_values[annotation.value])
                                                   : std::span<const uint8_t>());
}

inline constexpr std::string_view kResultsOutOfMemory = "out of memory";

// WHY: add() catches std::bad_alloc itself: a decoder's batch helper may call add() from a destructor.
class CollectingSink final : public bitoscope::decode::AnnotationSink {
 public:
  explicit CollectingSink(std::function<void(uint64_t)> before_collect = {})
      : before_collect_(std::move(before_collect)) {}
  void add(std::span<const bitoscope::decode::Annotation> batch) override {
    if (out_of_memory_) return;
    try {
      if (before_collect_) before_collect_(got.size());
      got.insert(got.end(), batch.begin(), batch.end());
    } catch (const std::bad_alloc&) {
      out_of_memory_ = true;
      std::vector<bitoscope::decode::Annotation>().swap(got);
    }
  }
  bool takesBytes() const override { return true; }
  void addBytes(const bitoscope::decode::Annotation& annotation, std::span<const uint8_t> bytes) override {
    if (out_of_memory_) return;
    try {
      if (before_collect_) before_collect_(got.size());
      byte_values.emplace_back(bytes.begin(), bytes.end());
      bitoscope::decode::Annotation stored = annotation;
      stored.value = byte_values.size() - 1;
      got.push_back(stored);
    } catch (const std::bad_alloc&) {
      out_of_memory_ = true;
      std::vector<bitoscope::decode::Annotation>().swap(got);
      std::vector<std::vector<uint8_t>>().swap(byte_values);
    }
  }
  bool outOfMemory() const { return out_of_memory_; }
  std::vector<bitoscope::decode::Annotation> got;
  std::vector<std::vector<uint8_t>> byte_values;

 private:
  bool out_of_memory_ = false;
  std::function<void(uint64_t)> before_collect_;
};

}

struct DecodedResultOut {
  std::vector<bitoscope::decode::Annotation> annotations;
  std::vector<std::vector<uint8_t>> byte_values;
  std::vector<bitoscope::decode::AnnotationClass> classes;
  std::string decode_error;
};

inline std::expected<void, std::string> runDecoders(
    std::span<ConfiguredDecoder> decoders, const bitoscope::capture::RawCaptureBuffer& buffer,
    bitoscope::capture::CaptureExtent extent, std::ostream& out,
    bitoscope::capture::ExtentTail tail = bitoscope::capture::ExtentTail::Exact,
    std::vector<DecodedResultOut>* results = nullptr, std::function<void(uint64_t)> before_collect = {},
    const AnnotationFilter* filter = nullptr) {
  if (decoders.empty()) return {};
  auto view = bitoscope::capture::RawBufferSignalView::create(buffer, extent, tail);
  if (!view) return std::unexpected(std::format("cannot decode capture: {}", view.error()));
  const double milli_hz = static_cast<double>(view->sampleRateMilliHz());

  std::vector<decode_detail::CollectingSink> sinks;
  sinks.reserve(decoders.size());
  for (size_t i = 0; i < decoders.size(); ++i) sinks.emplace_back(before_collect);
  namespace dec = bitoscope::decode;
  const auto guarded = [](auto&& run) -> dec::ChainOutcome {
    try {
      return run();
    } catch (const std::bad_alloc&) {
      throw;
    } catch (const std::exception& e) {
      return std::unexpected(std::string("decoder failed: ") + e.what());
    } catch (...) {
      return std::unexpected(std::string("decoder failed with unknown error"));
    }
  };
  dec::ChainRunHooks hooks;
  hooks.decode_samples = [&](std::size_t, dec::Decoder& decoder, dec::AnnotationSink& sink,
                             dec::StreamOutputs* outputs) -> dec::ChainOutcome {
    return guarded([&]() -> dec::ChainOutcome {
      auto* plugin_decoder = dynamic_cast<bitoscope::decoder_host::PluginDecoder*>(&decoder);
      return plugin_decoder != nullptr ? plugin_decoder->decodeCapture(buffer, extent, tail, sink, outputs)
                                       : decoder.decode(*view, sink, outputs);
    });
  };
  hooks.decode_mixed = [&](std::size_t, dec::Decoder& decoder, dec::StreamInputs& inputs, const dec::CaptureFacts& facts,
                           dec::AnnotationSink& sink, dec::StreamOutputs* outputs) -> dec::ChainOutcome {
    return guarded([&]() -> dec::ChainOutcome {
      auto* plugin_decoder = dynamic_cast<bitoscope::decoder_host::PluginDecoder*>(&decoder);
      return plugin_decoder != nullptr
                 ? plugin_decoder->decodeCaptureMixed(buffer, extent, tail, inputs, sink, outputs)
                 : decoder.decodeMixed(*view, inputs, facts, sink, outputs);
    });
  };
  hooks.decode_streams = [&](std::size_t, dec::Decoder& decoder, dec::StreamInputs& inputs,
                             const dec::CaptureFacts& facts, dec::AnnotationSink& sink,
                             dec::StreamOutputs* outputs) -> dec::ChainOutcome {
    return guarded([&] { return decoder.decodeStreams(inputs, facts, sink, outputs); });
  };
  const std::vector<bitoscope::decode::ChainOutcome> outcomes = bitoscope::decode::runChain(
      decoders, *view, [&sinks](std::size_t i) -> bitoscope::decode::AnnotationSink& { return sinks[i]; }, hooks);

  std::string errors;
  for (size_t i = 0; i < decoders.size(); ++i) {
    ConfiguredDecoder& entry = decoders[i];
    decode_detail::CollectingSink& sink = sinks[i];
    auto decoded = outcomes[i];
    auto* plugin_decoder = dynamic_cast<bitoscope::decoder_host::PluginDecoder*>(entry.decoder.get());
    if (sink.outOfMemory()) decoded = std::unexpected(std::string(decode_detail::kResultsOutOfMemory));
    const std::vector<dec::RowSource> host_rows = dec::hostDerivedRows(entry.decoder->rowSources());
    if (decoded) {
      try {
        if (before_collect) before_collect(sink.got.size());
        if (auto made = dec::insertDerivedRowsByRow(sink.got, entry.derived_rows, entry.decoder->rows(),
                                                    entry.decoder->resolvedOptions(), entry.decoder->boundChannels(),
                                                    *view, host_rows);
            !made) {
          decoded = std::unexpected(std::move(made).error());
        }
      } catch (const std::bad_alloc&) {
        std::vector<bitoscope::decode::Annotation>().swap(sink.got);
        std::vector<std::vector<uint8_t>>().swap(sink.byte_values);
        decoded = std::unexpected(std::string(decode_detail::kResultsOutOfMemory));
      }
    }
    if (!decoded) dec::dropAnnotationsOfRows(sink.got, host_rows);
    // WHY: A failed plugin decoder has already delivered annotations while running: none of them is printed.
    if (!decoded && plugin_decoder != nullptr) {
      std::vector<bitoscope::decode::Annotation>().swap(sink.got);
      std::vector<std::vector<uint8_t>>().swap(sink.byte_values);
    }
    if (!decoded) {
      try {
        dec::orderByBeginAndRow(sink.got, host_rows);
      } catch (const std::bad_alloc&) {
        std::vector<bitoscope::decode::Annotation>().swap(sink.got);
        std::vector<std::vector<uint8_t>>().swap(sink.byte_values);
      }
    }
    const std::vector<bitoscope::decode::AnnotationClass> classes = bitoscope::decode::displayClassesOf(*entry.decoder);
    const bitoscope::decode::OptionMap& options = entry.decoder->resolvedOptions();
    for (const auto& a : sink.got) {
      if (filter != nullptr && !filter->wants(i, a.cls)) continue;
      out << entry.id << "  " << a.begin << "-" << a.end << "  "
          << std::format("{:.9f}", static_cast<double>(a.begin) * 1000.0 / milli_hz) << " s  "
          << decode_detail::annotationText(classes, a, options, sink.byte_values) << "\n";
    }
    if (!decoded) {
      errors += (errors.empty() ? "" : "; ") +
                std::format("decoder '{}' failed: {}", entry.spec, decoded.error());
    }
    if (results != nullptr) {
      DecodedResultOut r;
      if (decoded) {
        r.annotations = std::move(sink.got);
        r.byte_values = std::move(sink.byte_values);
        r.classes = classes;
      } else {
        r.decode_error = decoded.error();
      }
      results->push_back(std::move(r));
    }
  }
  out << std::flush;
  if (!errors.empty()) return std::unexpected(errors);
  return {};
}

inline std::vector<std::pair<std::string, std::string>> decoderSetup(
    std::span<const ConfiguredDecoder> decoders) {
  std::vector<std::pair<std::string, std::string>> setup;
  for (const ConfiguredDecoder& entry : decoders) {
    const size_t colon = entry.spec.find(':');
    setup.emplace_back(entry.spec.substr(0, colon),
                       colon == std::string::npos ? std::string() : entry.spec.substr(colon + 1));
  }
  return setup;
}

}
