/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "line_trace.h"

namespace bitoscope::sim {

inline std::vector<Word> extractChannels(std::span<const Word> samples, uint32_t mask) {
  std::vector<Word> out(samples.begin(), samples.end());
  if (mask == 0xFFFFFFFFu) {
    return out;
  }
  std::array<uint32_t, 32> position{};
  uint32_t count = 0;
  for (uint32_t b = 0; b < 32; ++b) {
    if (((mask >> b) & 1u) != 0) {
      position[count++] = b;
    }
  }
  for (Word& w : out) {
    Word packed = 0;
    for (uint32_t j = 0; j < count; ++j) {
      packed |= ((w >> position[j]) & 1u) << j;
    }
    w = packed;
  }
  return out;
}

inline std::vector<uint8_t> packSampleMajor(std::span<const Word> samples, uint32_t num_channels) {
  const size_t bytes_per_sample = (num_channels + 7) / 8;
  std::vector<uint8_t> out(samples.size() * bytes_per_sample);
  for (size_t i = 0; i < samples.size(); ++i) {
    for (size_t b = 0; b < bytes_per_sample; ++b) {
      out[i * bytes_per_sample + b] = static_cast<uint8_t>((samples[i] >> (8 * b)) & 0xFFu);
    }
  }
  return out;
}

namespace detail {

constexpr uint64_t transpose8x8(uint64_t x) {
  uint64_t t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAull;
  x = x ^ t ^ (t << 7);
  t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCull;
  x = x ^ t ^ (t << 14);
  t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ull;
  x = x ^ t ^ (t << 28);
  return x;
}

}

inline std::expected<std::vector<uint8_t>, std::string> packChannelMajor(
    std::span<const Word> samples, uint32_t num_channels) {
  if (num_channels == 0 || num_channels > kMaxLines) {
    return std::unexpected("channel count must be 1 to 32, got " + std::to_string(num_channels));
  }
  if (samples.size() % 8 != 0) {
    return std::unexpected("channel-major packing needs sample count that is multiple of 8, got " +
                            std::to_string(samples.size()));
  }
  const size_t groups = samples.size() / 8;
  const uint32_t lanes = (num_channels + 7) / 8;
  std::vector<uint8_t> out(groups * num_channels);
  for (size_t g = 0; g < groups; ++g) {
    const Word* s = samples.data() + g * 8;
    for (uint32_t lane = 0; lane < lanes; ++lane) {
      uint64_t x = 0;
      for (unsigned k = 0; k < 8; ++k) {
        x |= static_cast<uint64_t>((s[k] >> (8 * lane)) & 0xFFu) << (8 * k);
      }
      x = detail::transpose8x8(x);
      const uint32_t first = lane * 8;
      const uint32_t take = std::min<uint32_t>(8, num_channels - first);
      for (uint32_t c = 0; c < take; ++c) {
        out[g * num_channels + first + c] = static_cast<uint8_t>((x >> (8 * c)) & 0xFFu);
      }
    }
  }
  return out;
}

}
