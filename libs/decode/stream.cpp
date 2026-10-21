/*
 * SPDX-License-Identifier: MIT
 */

#include "stream.h"

#include <format>
#include <utility>

namespace bitoscope::decode {

std::string_view streamTypeName(StreamType type) {
  switch (type) {
    case StreamType::Words: return "words";
    case StreamType::SpiTransfers: return "spi-transfers";
  }
  return "unknown";
}

bool kindBelongs(StreamType type, uint32_t kind) {
  switch (type) {
    case StreamType::Words: return kind == kKindData || kind == kKindBoundary;
    case StreamType::SpiTransfers: return kind == kKindData || kind == kKindBoundary || kind == kKindBegin;
  }
  return false;
}

namespace {

bool fits(uint64_t value, uint32_t width) { return width >= 64 || (value >> width) == 0; }

bool valueFits(StreamType type, uint32_t width, uint64_t value) {
  if (type == StreamType::SpiTransfers) {
    return fits(value & 0xFFFF'FFFFu, width) && fits(value >> 32, width);
  }
  return fits(value, width);
}

}

GuardedStreamSink::GuardedStreamSink(StreamSink& out, uint64_t sample_count, std::vector<GuardedOutput> outputs)
    : out_(out),
      sample_count_(sample_count),
      outputs_(std::move(outputs)),
      last_begin_(outputs_.size(), 0),
      seen_(outputs_.size(), false) {}

void GuardedStreamSink::add(uint32_t output, std::span<const StreamItem> batch) {
  if (!error_.empty()) return;
  if (output >= outputs_.size()) {
    error_ = std::format("stream output {} is not one this decoder declared", output);
    return;
  }
  if (!outputs_[output].wanted) {
    error_ = std::format("output '{}' was not asked for", outputs_[output].name);
    return;
  }
  for (std::size_t i = 0; i < batch.size(); ++i) {
    if (std::string refusal = check(output, batch[i]); !refusal.empty()) {
      error_ = std::move(refusal);
      if (i > 0) out_.add(output, batch.subspan(0, i));
      return;
    }
    last_begin_[output] = batch[i].begin;
    seen_[output] = true;
  }
  out_.add(output, batch);
}

std::string GuardedStreamSink::check(uint32_t output, const StreamItem& item) const {
  const GuardedOutput& o = outputs_[output];
  if (!kindBelongs(o.type, item.kind)) {
    return std::format("output '{}': item [{}, {}) has kind {}, which {} does not have", o.name, item.begin,
                       item.end, item.kind, streamTypeName(o.type));
  }
  if (item.begin >= item.end) {
    return std::format("output '{}': item [{}, {}) is empty or reversed", o.name, item.begin, item.end);
  }
  if (item.end > sample_count_) {
    return std::format("output '{}': item [{}, {}) is not inside capture's {} samples", o.name, item.begin,
                       item.end, sample_count_);
  }
  if (seen_[output] && item.begin < last_begin_[output]) {
    return std::format("output '{}': item [{}, {}) begins before previous item's begin {}", o.name,
                       item.begin, item.end, last_begin_[output]);
  }
  if (item.kind == kKindData && !valueFits(o.type, o.width, item.value)) {
    return std::format("output '{}': item [{}, {}) has value 0x{:X}, wider than {} bits", o.name, item.begin,
                       item.end, item.value, o.width);
  }
  return {};
}

VectorStreamInput::VectorStreamInput(std::span<const StreamItem> items) : items_(items) {}

std::optional<StreamItem> VectorStreamInput::next() {
  if (read_ == items_.size()) return std::nullopt;
  return items_[read_++];
}

namespace {

std::string describe(StreamType type, uint32_t width) {
  if (width == 0) return std::string(streamTypeName(type));
  return std::format("{}-bit {}", width, streamTypeName(type));
}

}

std::expected<void, std::string> checkBinding(std::string_view reader, const InputSpec& input,
                                              std::string_view source, const OutputSpec& output,
                                              uint32_t width, std::string_view width_note) {
  if (input.type == output.type && (input.width == 0 || input.width == width)) return {};
  std::string message = std::format("{} reads {}; {} carries {}", reader, describe(input.type, input.width),
                                    source, describe(output.type, width));
  if (!width_note.empty()) message += std::format(" ({})", width_note);
  return std::unexpected(std::move(message));
}

}
