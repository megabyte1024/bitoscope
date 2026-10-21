/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"
#include "decoder_registry.h"

namespace bitoscope::decode {

struct ChainBinding {
  std::string input;
  std::size_t from = 0;
  std::string output;
  bool operator==(const ChainBinding&) const = default;
};

struct ChainInstance {
  std::string spec;
  std::string id;
  std::unique_ptr<Decoder> decoder;
  std::vector<ChainBinding> inputs;
  std::vector<std::string> wanted;
  std::vector<RowSource> derived_rows{};
};

struct ChainPlan {
  std::vector<ChainInstance> instances;
};

std::expected<ChainPlan, std::string> planChain(std::span<const std::string> specs, const DecoderRegistry& registry,
                                                std::span<const CapturedChannel> captured, const CaptureFacts& facts);

bool isValidDecoderId(std::string_view id);

using ChainOutcome = std::expected<void, std::string>;

struct ChainRunHooks {
  std::function<ChainOutcome(std::size_t instance, Decoder& decoder, AnnotationSink& sink, StreamOutputs* outputs)>
      decode_samples;
  std::function<ChainOutcome(std::size_t instance, Decoder& decoder, StreamInputs& inputs, const CaptureFacts& facts,
                             AnnotationSink& sink, StreamOutputs* outputs)>
      decode_mixed;
  std::function<ChainOutcome(std::size_t instance, Decoder& decoder, StreamInputs& inputs, const CaptureFacts& facts,
                             AnnotationSink& sink, StreamOutputs* outputs)>
      decode_streams;
  std::function<void(std::size_t instance, std::string_view output, std::size_t items)> before_collect;
  std::function<void(std::size_t instance, std::string_view output, std::size_t items, std::size_t capacity_left)>
      on_release;
  std::function<ChainOutcome(std::size_t instance)> before_instance;
  std::function<void(std::size_t instance, const ChainOutcome& outcome)> after_instance;
};

std::vector<ChainOutcome> runChain(ChainPlan& plan, const SignalView& view,
                                   const std::function<AnnotationSink&(std::size_t)>& sink_for,
                                   const ChainRunHooks& hooks = {});
std::vector<ChainOutcome> runChain(std::span<ChainInstance> instances, const SignalView& view,
                                   const std::function<AnnotationSink&(std::size_t)>& sink_for,
                                   const ChainRunHooks& hooks = {});

}
