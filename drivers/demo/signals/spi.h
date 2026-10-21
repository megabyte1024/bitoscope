/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "line_trace.h"

namespace bitoscope::sim {

struct SpiConfig {
  uint32_t clock_hz = 250'000;
  uint8_t mode = 0;
};

struct SpiLines {
  LineTrace& cs;
  LineTrace& sclk;
  LineTrace& mosi;
  LineTrace& miso;
};

inline std::expected<SampleIndex, std::string> encodeSpiTransaction(
    SpiLines lines, uint64_t sample_rate_hz, SampleIndex start, const SpiConfig& cfg,
    std::span<const uint8_t> mosi_bytes, std::span<const uint8_t> miso_bytes) {
  if (cfg.mode > 3) {
    return std::unexpected("SPI mode must be 0 to 3, got " + std::to_string(cfg.mode));
  }
  if (mosi_bytes.size() != miso_bytes.size()) {
    return std::unexpected("SPI clocks data both ways at once: MOSI has " +
                            std::to_string(mosi_bytes.size()) + " bytes, MISO has " +
                            std::to_string(miso_bytes.size()));
  }
  if (mosi_bytes.empty()) {
    return std::unexpected("SPI transaction needs at least one byte");
  }
  const bool cpol = (cfg.mode >> 1) != 0;
  const bool cpha = (cfg.mode & 1) != 0;
  if (lines.sclk.idleLevel() != cpol) {
    return std::unexpected("SPI mode " + std::to_string(cfg.mode) + " idles clock " +
                            (cpol ? "high" : "low") + ", but trace given idles " +
                            (lines.sclk.idleLevel() ? "high" : "low"));
  }
  if (!lines.cs.idleLevel()) {
    return std::unexpected("SPI chip select idles high, but trace given idles low");
  }
  const auto ticks = Timebase::create(sample_rate_hz, 2ull * cfg.clock_hz);
  if (!ticks.has_value()) {
    return std::unexpected(ticks.error());
  }
  const auto at = [&](uint64_t tick) { return start + ticks->at(tick); };

  const size_t bits = mosi_bytes.size() * 8;
  lines.cs.set(at(0), false);
  for (size_t k = 0; k < bits; ++k) {
    const unsigned bit = 7 - static_cast<unsigned>(k % 8);
    const uint64_t data = 2 + 2 * k;
    const uint64_t lead = data + (cpha ? 0 : 1);
    const uint64_t trail = lead + 1;
    lines.mosi.set(at(data), ((mosi_bytes[k / 8] >> bit) & 1u) != 0);
    lines.miso.set(at(data), ((miso_bytes[k / 8] >> bit) & 1u) != 0);
    lines.sclk.set(at(lead), !cpol);
    lines.sclk.set(at(trail), cpol);
  }
  const uint64_t release = 2 * bits + 4;
  lines.mosi.set(at(release), false);
  lines.miso.set(at(release), false);
  lines.cs.set(at(release), true);
  return at(release + 2);
}

}
