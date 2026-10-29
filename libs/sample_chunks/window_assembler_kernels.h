/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace bitoscope::host {

class WindowLease;

namespace assembler_kernels {

struct CopyJob {
  std::span<const uint8_t> bytes;
  WindowLease* dst;
  uint32_t num_channels;
};

inline constexpr size_t kChannelMajorTileSource = 8192;

constexpr size_t channelMajorTileBytes(uint32_t num_channels) {
  const size_t per_channel = kChannelMajorTileSource / (num_channels == 0 ? 1 : num_channels);
  return (std::min)(size_t{1024}, (std::max)(size_t{64}, per_channel / 16 * 16));
}

void alignedSampleMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit, uint32_t count);

void alignedChannelMajor(const CopyJob& job, uint32_t src_index, uint32_t dst_bit, uint32_t count);

}

}
