/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include "demo_stream.h"
#include "pattern_signals.h"

namespace bitoscope::sim {

inline constexpr uint32_t kDemoMaxChunkSamples = 8 * 1024 * 1024;

enum class DemoSource {
  Structured,
  Random,
  Pattern,
  Noise,
};

struct DemoFacetConfig {
  WireEncoding encoding = WireEncoding::ChannelMajor;
  bool realtime = true;
  uint32_t chunk_samples = 0;
  DemoSource source = DemoSource::Structured;
  uint64_t seed = 0;
  bool marker = false;
  uint64_t marker_at = 1;
  uint32_t marker_channel = 0;
};

inline std::expected<void, std::string> checkDemoConfig(const DemoFacetConfig& config) {
  if (config.chunk_samples != 0 && (config.chunk_samples % 8 != 0 || config.chunk_samples > kDemoMaxChunkSamples)) {
    return std::unexpected("chunk_samples must be 0 (ring slot's worth) or multiple of 8 from 8 to " +
                           std::to_string(kDemoMaxChunkSamples) + ", got " + std::to_string(config.chunk_samples));
  }
  if (config.marker_at == 0 || config.marker_at > kMaxMarkerSample) {
    return std::unexpected("marker_at must be sample from 1 to " + std::to_string(kMaxMarkerSample) + ", got " +
                           std::to_string(config.marker_at));
  }
  if (config.marker_channel > 31) {
    return std::unexpected("marker_channel must be channel from 0 to 31, got " +
                           std::to_string(config.marker_channel));
  }
  return {};
}

inline std::expected<std::optional<PatternMarker>, std::string> markerForCapture(const DemoFacetConfig& config) {
  if (!config.marker) return std::optional<PatternMarker>{};
  if (config.source != DemoSource::Pattern && config.source != DemoSource::Noise) {
    return std::unexpected(
        std::string("marker needs source=pattern or source=noise; set marker=off for another source"));
  }
  return std::optional<PatternMarker>{PatternMarker{config.marker_channel, config.marker_at}};
}

inline uint32_t resolveChunkSamples(const DemoFacetConfig& config, uint64_t sample_rate_hz,
                                    uint32_t num_channels) {
  return config.chunk_samples != 0
             ? config.chunk_samples
             : demoChunkSamples(sample_rate_hz, num_channels, config.encoding);
}

inline std::expected<uint32_t, std::string> ringChunkSamples(const DemoFacetConfig& config,
                                                             uint64_t sample_rate_hz,
                                                             uint64_t slot_payload_bytes,
                                                             uint32_t num_channels) {
  const uint64_t channels = num_channels == 0 ? 1 : num_channels;
  const uint64_t fit = config.encoding == WireEncoding::ChannelMajor
                           ? slot_payload_bytes / channels * 8
                           : slot_payload_bytes / ((channels + 7) / 8) / 8 * 8;
  if (fit < 8) {
    return std::unexpected("ring's slots of " + std::to_string(slot_payload_bytes) +
                           " bytes cannot hold 8 samples of " + std::to_string(num_channels) +
                           " channels");
  }
  uint64_t capped = fit < 0xFFFFFFF8ull ? fit : 0xFFFFFFF8ull;
  if (config.chunk_samples != 0) {
    return config.chunk_samples < capped ? config.chunk_samples : static_cast<uint32_t>(capped);
  }
  if (config.realtime) {
    const uint64_t by_time = sample_rate_hz / 50 / 8 * 8;
    capped = by_time < 8 ? 8 : (by_time < capped ? by_time : capped);
  }
  return static_cast<uint32_t>(capped);
}

}
