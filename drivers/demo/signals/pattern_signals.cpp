/*
 * SPDX-License-Identifier: MIT
 */

#include "pattern_signals.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <new>

#include "channel_pack.h"
#include "random_signals.h"

namespace bitoscope::sim {

namespace {

constexpr std::array<uint64_t, 18> kPatternEdgeLengths = {2,  3,  5,  7,  8,  9,  15,  16,  17,
                                                          31, 32, 33, 63, 64, 65, 127, 128, 129};
constexpr std::array<uint64_t, 16> kPatternPrimeLengths = {11, 13, 17, 19, 23, 29, 31, 37,
                                                           41, 43, 47, 53, 59, 61, 67, 71};
constexpr uint64_t kLongExtra = 131072;

uint64_t pulseLength(uint64_t v, uint64_t i) {
  const uint64_t sel = v & 0xFFu;
  const uint64_t r = v >> 8;
  if (i == 1) return 1;
  if (i == 3) return kPatternWindowSamples + 1 + r % kLongExtra;
  if (sel < 48) return 1;
  if (sel < 96) return kPatternEdgeLengths[r % kPatternEdgeLengths.size()];
  if (sel < 144) return kPatternPrimeLengths[r % kPatternPrimeLengths.size()];
  if (sel < 248) return 1 + r % 1000;
  return 1 + r % 100000;
}

void setBits(std::vector<uint8_t>& bytes, uint64_t from, uint64_t count) {
  uint64_t at = from;
  const uint64_t end = from + count;
  while (at < end && at % 8 != 0) {
    bytes[at / 8] |= static_cast<uint8_t>(1u << (at % 8));
    ++at;
  }
  if (end - at >= 8) {
    const uint64_t whole = (end - at) / 8;
    std::memset(bytes.data() + at / 8, 0xFF, static_cast<size_t>(whole));
    at += whole * 8;
  }
  while (at < end) {
    bytes[at / 8] |= static_cast<uint8_t>(1u << (at % 8));
    ++at;
  }
}

void renderPeriod(ReplayContent content, uint64_t seed, uint32_t c, std::vector<uint8_t>& bytes) {
  if (content == ReplayContent::Noise) {
    bytes = noisePeriod(seed, c);
    return;
  }
  bytes.assign(static_cast<size_t>(kPatternPeriodBytes), 0);
  const PatternTrain t = patternTrain(seed, c);
  uint64_t at = 0;
  for (size_t j = 0; j < t.lengths.size(); ++j) {
    const bool high = t.first_level != (j % 2 == 1);
    if (high) setBits(bytes, at, t.lengths[j]);
    at += t.lengths[j];
  }
}

std::string wrongSize(uint64_t num_samples, size_t got, uint64_t want) {
  return "pattern: " + std::to_string(num_samples) + " samples take " + std::to_string(want) +
         " bytes, but span holds " + std::to_string(got);
}

}

PatternTrain patternTrain(uint64_t seed, uint32_t physical_channel) {
  PatternTrain t;
  uint64_t state = seed ^ ((uint64_t{physical_channel} + 1) * 0xD1B54A32D192ED03ull);
  t.first_level = (splitmix64Next(state) & 1u) != 0;
  uint64_t total = 0;
  for (uint64_t i = 0; total < kPatternPeriodSamples; ++i) {
    const uint64_t n = (std::min)(pulseLength(splitmix64Next(state), i), kPatternPeriodSamples - total);
    t.lengths.push_back(n);
    total += n;
  }
  if (t.lengths.size() % 2 == 1) {
    const uint64_t last = t.lengths.back();
    t.lengths.pop_back();
    t.lengths.back() += last;
  }
  return t;
}

std::vector<uint8_t> noisePeriod(uint64_t seed, uint32_t physical_channel) {
  const size_t p = static_cast<size_t>(kPatternPeriodBytes);
  std::vector<uint8_t> bytes((p + 7) / 8 * 8);
  uint64_t state = seed ^ ((uint64_t{physical_channel} + 1) * kNoiseChannelSalt);
  for (size_t at = 0; at < bytes.size(); at += 8) {
    const uint64_t v = splitmix64Next(state);
    for (size_t i = 0; i < 8; ++i) {
      bytes[at + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
  }
  bytes.resize(p);
  return bytes;
}

std::expected<PatternStream, std::string> PatternStream::create(uint64_t seed, uint32_t channel_mask,
                                                                WireEncoding encoding, uint32_t chunk_samples,
                                                                std::optional<PatternMarker> marker) {
  return build(ReplayContent::PulseTrain, seed, channel_mask, encoding, chunk_samples, marker);
}

std::expected<PatternStream, std::string> PatternStream::createNoise(uint64_t seed, uint32_t channel_mask,
                                                                     WireEncoding encoding,
                                                                     uint32_t chunk_samples,
                                                                     std::optional<PatternMarker> marker) {
  return build(ReplayContent::Noise, seed, channel_mask, encoding, chunk_samples, marker);
}

std::expected<PatternStream, std::string> PatternStream::build(ReplayContent content, uint64_t seed,
                                                               uint32_t channel_mask, WireEncoding encoding,
                                                               uint32_t chunk_samples,
                                                               std::optional<PatternMarker> marker) {
  if (channel_mask == 0) {
    return std::unexpected(std::string("channel mask selects no channels"));
  }
  if (chunk_samples == 0 || chunk_samples % 8 != 0) {
    return std::unexpected("chunk size must be nonzero multiple of 8 samples, got " +
                           std::to_string(chunk_samples));
  }
  PatternStream s;
  s.encoding_ = encoding;
  s.content_ = content;
  s.num_channels_ = static_cast<uint32_t>(std::popcount(channel_mask));
  s.chunk_samples_ = chunk_samples;
  std::vector<uint32_t> phys;
  for (uint32_t c = 0; c < 32; ++c) {
    if (((channel_mask >> c) & 1u) != 0) phys.push_back(c);
  }
  const size_t n = phys.size();
  if (marker.has_value()) {
    if (marker->physical_channel > 31) {
      return std::unexpected("marker: channel " + std::to_string(marker->physical_channel) +
                             " is not channel (0 to 31)");
    }
    if (((channel_mask >> marker->physical_channel) & 1u) == 0) {
      return std::unexpected("marker: channel " + std::to_string(marker->physical_channel) +
                             " is not one of capture's channels");
    }
    if (marker->at_sample == 0 || marker->at_sample > kMaxMarkerSample) {
      return std::unexpected("marker: marker sample must be from 1 to " + std::to_string(kMaxMarkerSample) +
                             ", got " + std::to_string(marker->at_sample));
    }
    s.marker_ = marker;
    s.marker_wire_channel_ = static_cast<uint32_t>(
        std::popcount(channel_mask & ((uint32_t{1} << marker->physical_channel) - 1u)));
  }
  const auto render = [&](uint32_t c, std::vector<uint8_t>& bytes) {
    if (marker.has_value() && c == marker->physical_channel) {
      bytes.assign(static_cast<size_t>(kPatternPeriodBytes), 0);
    } else {
      renderPeriod(content, seed, c, bytes);
    }
  };
  const size_t p = static_cast<size_t>(kPatternPeriodBytes);
  try {
    std::vector<uint8_t> period;
    if (encoding == WireEncoding::ChannelMajor) {
      s.table_.assign(p * n, 0);
      for (size_t k = 0; k < n; ++k) {
        render(phys[k], period);
        for (size_t r = 0; r < p; ++r) {
          s.table_[r * n + k] = period[r];
        }
      }
    } else {
      const size_t b = (n + 7) / 8;
      s.table_.assign(static_cast<size_t>(kPatternPeriodSamples) * b, 0);
      std::vector<std::vector<uint8_t>> lane(8);
      for (size_t j = 0; j < b; ++j) {
        const size_t first = j * 8;
        const size_t take = (std::min)(size_t{8}, n - first);
        for (size_t c = 0; c < take; ++c) {
          render(phys[first + c], lane[c]);
        }
        for (size_t r = 0; r < p; ++r) {
          uint64_t x = 0;
          for (size_t c = 0; c < take; ++c) {
            x |= static_cast<uint64_t>(lane[c][r]) << (8 * c);
          }
          x = detail::transpose8x8(x);
          for (size_t k = 0; k < 8; ++k) {
            s.table_[(r * 8 + k) * b + j] = static_cast<uint8_t>((x >> (8 * k)) & 0xFFu);
          }
        }
      }
    }
    if (s.marker_.has_value()) {
      s.table_high_ = s.table_;
      const size_t k = s.marker_wire_channel_;
      if (encoding == WireEncoding::ChannelMajor) {
        for (size_t r = 0; r < p; ++r) s.table_high_[r * n + k] = 0xFF;
      } else {
        const size_t b = (n + 7) / 8;
        const uint8_t bit = static_cast<uint8_t>(1u << (k % 8));
        for (size_t t = 0; t < static_cast<size_t>(kPatternPeriodSamples); ++t) s.table_high_[t * b + k / 8] |= bit;
      }
    }
  } catch (const std::bad_alloc&) {
    return std::unexpected(std::string(content == ReplayContent::Noise ? "noise" : "pattern") +
                           ": cannot allocate " + std::to_string(n) + "-channel " +
                           (content == ReplayContent::Noise ? "noise" : "pulse-train") + " table (" +
                           std::to_string(p * n) + " bytes" + (marker.has_value() ? ", twice for marker" : "") +
                           ")");
  }
  return s;
}

uint64_t PatternStream::payloadBytes(uint64_t num_samples) const {
  return encoding_ == WireEncoding::ChannelMajor ? num_samples / 8 * num_channels_
                                                 : num_samples * ((uint64_t{num_channels_} + 7) / 8);
}

size_t PatternStream::offsetOf(uint64_t first) const {
  if (encoding_ == WireEncoding::ChannelMajor) {
    return static_cast<size_t>((first / 8) % kPatternPeriodBytes) * num_channels_;
  }
  return static_cast<size_t>(first % kPatternPeriodSamples) * ((num_channels_ + 7) / 8);
}

void PatternStream::copyOut(const std::vector<uint8_t>& table, uint64_t first, uint64_t num_samples,
                            std::span<uint8_t> out) const {
  size_t pos = offsetOf(first);
  size_t written = 0;
  const size_t size = static_cast<size_t>(payloadBytes(num_samples));
  while (written < size) {
    const size_t take = (std::min)(size - written, table.size() - pos);
    std::memcpy(out.data() + written, table.data() + pos, take);
    written += take;
    pos += take;
    if (pos == table.size()) pos = 0;
  }
}

std::expected<void, std::string> PatternStream::writeAt(uint64_t first_sample, uint64_t num_samples,
                                                       std::span<uint8_t> out) const {
  if (num_samples == 0 || num_samples % 8 != 0) {
    return std::unexpected("pattern: chunk is nonzero multiple of 8 samples, got " +
                           std::to_string(num_samples));
  }
  if (first_sample % 8 != 0) {
    return std::unexpected("pattern: chunk starts on multiple of 8 samples, got " +
                           std::to_string(first_sample));
  }
  const uint64_t want = payloadBytes(num_samples);
  if (out.size() != want) {
    return std::unexpected(wrongSize(num_samples, out.size(), want));
  }
  if (!marker_.has_value() || first_sample + num_samples <= marker_->at_sample) {
    copyOut(table_, first_sample, num_samples, out);
  } else if (first_sample >= marker_->at_sample) {
    copyOut(table_high_, first_sample, num_samples, out);
  } else {
    const uint64_t s = marker_->at_sample - first_sample;
    const uint64_t low = encoding_ == WireEncoding::ChannelMajor ? s / 8 * 8 : s;
    const size_t low_bytes = static_cast<size_t>(payloadBytes(low));
    if (low != 0) copyOut(table_, first_sample, low, out.first(low_bytes));
    copyOut(table_high_, first_sample + low, num_samples - low, out.subspan(low_bytes));
    if (encoding_ == WireEncoding::ChannelMajor && s % 8 != 0) {
      out[low_bytes + marker_wire_channel_] = static_cast<uint8_t>(0xFFu << (s % 8));
    }
  }
  return {};
}

std::expected<void, std::string> PatternStream::writeNext(uint64_t num_samples, std::span<uint8_t> out) {
  auto written = writeAt(produced_, num_samples, out);
  if (!written.has_value()) return written;
  produced_ += num_samples;
  return {};
}

DemoChunk PatternStream::nextChunk(uint32_t max_samples) {
  uint32_t n = chunk_samples_;
  if (max_samples != 0) {
    n = (std::min)(n, max_samples & ~7u);
  }
  DemoChunk chunk;
  chunk.first_sample_index = produced_;
  chunk.num_channels = num_channels_;
  chunk.num_samples = n;
  chunk.encoding = encoding_;
  if (n == 0) {
    return chunk;
  }
  chunk.payload.resize(static_cast<size_t>(payloadBytes(n)));
  (void)writeNext(n, chunk.payload);
  return chunk;
}

}
