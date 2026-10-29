/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "demo_stream.h"

namespace bitoscope::sim {

class CounterStream {
 public:
  static std::expected<CounterStream, std::string> create(uint32_t channel_mask, WireEncoding encoding);

  uint64_t payloadBytes(uint64_t num_samples) const;

  std::expected<void, std::string> writeNext(uint64_t num_samples, std::span<uint8_t> out);

  uint64_t samplesProduced() const { return produced_; }

 private:
  CounterStream() = default;

  uint32_t channel_mask_ = 0;
  uint32_t num_channels_ = 0;
  WireEncoding encoding_ = WireEncoding::ChannelMajor;
  uint64_t produced_ = 0;
};

}
