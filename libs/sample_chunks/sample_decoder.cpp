/*
 * SPDX-License-Identifier: MIT
 */

#include "sample_decoder.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

#include "sample_encoding.h"

namespace bitoscope::host {

std::expected<ChannelView, std::string> ChannelView::create(
    std::span<const ChunkView> chunks, uint32_t channel_index) {
  if (chunks.empty()) {
    return ChannelView({}, {}, channel_index, 0, SampleEncoding::Unspecified);
  }

  const auto encoding = chunks.front().encoding;
  const auto num_channels = chunks.front().num_channels;
  if (encoding != SampleEncoding::ChannelMajorPacked && encoding != SampleEncoding::SampleMajorPacked) {
    return std::unexpected(
        "unsupported SampleEncoding " + std::to_string(static_cast<uint32_t>(encoding)) +
        " - only ChannelMajorPacked and SampleMajorPacked are implemented");
  }
  if (channel_index >= num_channels) {
    return std::unexpected("channel_index " + std::to_string(channel_index) +
                            " is out of range for num_channels " +
                            std::to_string(num_channels));
  }

  std::vector<std::span<const uint8_t>> payloads;
  std::vector<size_t> cumulative;
  payloads.reserve(chunks.size());
  cumulative.reserve(chunks.size() + 1);
  cumulative.push_back(0);

  for (const auto& chunk : chunks) {
    if (chunk.encoding != encoding || chunk.num_channels != num_channels) {
      return std::unexpected("inconsistent encoding/num_channels across chunks - "
                              "ChannelView spans one continuous capture, not "
                              "mix of different plugins/configurations");
    }
    const std::span<const uint8_t> payload = chunk.payload;
    const size_t expected_bytes = expectedPayloadBytes(encoding, num_channels, chunk.num_samples);
    if (payload.size() != expected_bytes) {
      return std::unexpected(
          "chunk payload is " + std::to_string(payload.size()) + " bytes, expected " +
          std::to_string(expected_bytes) + " for " + std::to_string(chunk.num_samples) +
          " samples/channel - malformed or truncated chunk");
    }
    payloads.push_back(payload);
    cumulative.push_back(cumulative.back() + chunk.num_samples);
  }

  return ChannelView(std::move(payloads), std::move(cumulative), channel_index, num_channels,
                      encoding);
}

bool ChannelView::valueAt(size_t i) const {
  assert(i < sampleCount());
  const auto it = std::upper_bound(cumulative_.begin() + 1, cumulative_.end(), i);
  const size_t chunk_index = static_cast<size_t>(it - cumulative_.begin()) - 1;
  const size_t local_index = i - cumulative_[chunk_index];
  return valueAtInChunk(payloads_[chunk_index], local_index);
}

ChannelView::ChannelView(std::vector<std::span<const uint8_t>> payloads,
                         std::vector<size_t> cumulative, uint32_t channel_index,
                         uint32_t num_channels, SampleEncoding encoding)
    : payloads_(std::move(payloads)),
      cumulative_(cumulative.empty() ? std::vector<size_t>{0} : std::move(cumulative)),
      channel_index_(channel_index),
      num_channels_(num_channels),
      encoding_(encoding) {}

bool ChannelView::valueAtInChunk(std::span<const uint8_t> payload, size_t local_index) const {
  if (encoding_ == SampleEncoding::ChannelMajorPacked) {
    const size_t byte_within_channel = local_index / 8;
    const unsigned bit = static_cast<unsigned>(local_index % 8);
    const size_t offset = byte_within_channel * num_channels_ + channel_index_;
    return ((payload[offset] >> bit) & 1) != 0;
  }
  const size_t bytes_per_sample = (static_cast<size_t>(num_channels_) + 7) / 8;
  const size_t byte_within_sample = channel_index_ / 8;
  const unsigned bit = channel_index_ % 8;
  const size_t offset = local_index * bytes_per_sample + byte_within_sample;
  return ((payload[offset] >> bit) & 1) != 0;
}

// WHY: Walk chunks directly, not valueAt() per sample: its binary search per call is ruinous for a bulk pass.
std::expected<std::span<uint8_t>, std::string> materialize(const ChannelView& view,
                                                             std::span<uint8_t> out) {
  const size_t required = (view.sampleCount() + 7) / 8;
  if (out.size() < required) {
    return std::unexpected("output buffer is " + std::to_string(out.size()) +
                            " bytes, need at least " + std::to_string(required));
  }
  std::memset(out.data(), 0, required);

  size_t first_sample = 0;
  for (size_t c = 0; c < view.payloads_.size(); ++c) {
    const std::span<const uint8_t> payload = view.payloads_[c];
    const size_t count = view.cumulative_[c + 1] - view.cumulative_[c];

    if (view.encoding_ == SampleEncoding::ChannelMajorPacked &&
        first_sample % 8 == 0 && count % 8 == 0) {
      const size_t num_bytes = count / 8;
      uint8_t* __restrict dst = out.data() + first_sample / 8;
      const uint8_t* __restrict src = payload.data() + view.channel_index_;
      if (view.num_channels_ == 1) {
        std::memcpy(dst, src, num_bytes);
      } else {
        for (size_t b = 0; b < num_bytes; ++b) {
          dst[b] = src[b * view.num_channels_];
        }
      }
    } else {
      for (size_t k = 0; k < count; ++k) {
        if (view.valueAtInChunk(payload, k)) {
          const size_t bit = first_sample + k;
          out[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
        }
      }
    }
    first_sample += count;
  }
  return out.first(required);
}

}
