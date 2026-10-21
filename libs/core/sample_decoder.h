/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "chunk_view.h"
#include "sample_encoding.h"

namespace bitoscope::host {

class ChannelView;
std::expected<std::span<uint8_t>, std::string> materialize(const ChannelView& view,
                                                             std::span<uint8_t> out);

class ChannelView {
 public:
  // WHY: create() rejects a chunk whose payload size is inconsistent, so valueAt() never needs a bounds check.
  static std::expected<ChannelView, std::string> create(
      std::span<const ChunkView> chunks, uint32_t channel_index);

  size_t sampleCount() const { return cumulative_.empty() ? 0 : cumulative_.back(); }

  bool valueAt(size_t i) const;

 private:
  ChannelView(std::vector<std::span<const uint8_t>> payloads, std::vector<size_t> cumulative,
              uint32_t channel_index, uint32_t num_channels, SampleEncoding encoding);

  bool valueAtInChunk(std::span<const uint8_t> payload, size_t local_index) const;

  std::vector<std::span<const uint8_t>> payloads_;
  std::vector<size_t> cumulative_;
  uint32_t channel_index_;
  uint32_t num_channels_;
  SampleEncoding encoding_;

  friend std::expected<std::span<uint8_t>, std::string> materialize(const ChannelView&,
                                                                      std::span<uint8_t>);
};

std::expected<std::span<uint8_t>, std::string> materialize(const ChannelView& view,
                                                             std::span<uint8_t> out);

}
