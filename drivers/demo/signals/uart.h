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

enum class Parity { None, Even, Odd };

struct UartConfig {
  uint32_t baud = 115200;
  uint8_t data_bits = 8;
  Parity parity = Parity::None;
  uint8_t stop_bits = 1;
};

inline std::expected<SampleIndex, std::string> encodeUart(LineTrace& tx, uint64_t sample_rate_hz,
                                                          SampleIndex start,
                                                          std::span<const uint8_t> bytes,
                                                          const UartConfig& cfg = {},
                                                          uint32_t idle_bits_after = 2) {
  if (cfg.data_bits < 5 || cfg.data_bits > 8) {
    return std::unexpected("UART data bits must be 5 to 8, got " + std::to_string(cfg.data_bits));
  }
  if (cfg.stop_bits < 1 || cfg.stop_bits > 2) {
    return std::unexpected("UART stop bits must be 1 or 2, got " + std::to_string(cfg.stop_bits));
  }
  if (!tx.idleLevel()) {
    return std::unexpected("UART line idles high, but trace given idles low");
  }
  const auto bits = Timebase::create(sample_rate_hz, cfg.baud);
  if (!bits.has_value()) {
    return std::unexpected(bits.error());
  }

  uint64_t tick = 0;
  const auto put = [&](bool level) {
    tx.set(start + bits->at(tick), level);
    ++tick;
  };
  for (const uint8_t byte : bytes) {
    put(false);
    unsigned ones = 0;
    for (unsigned b = 0; b < cfg.data_bits; ++b) {
      const bool bit = ((byte >> b) & 1u) != 0;
      ones += bit ? 1u : 0u;
      put(bit);
    }
    if (cfg.parity != Parity::None) {
      const bool odd_ones = (ones % 2) == 1;
      put(cfg.parity == Parity::Even ? odd_ones : !odd_ones);
    }
    for (unsigned s = 0; s < cfg.stop_bits; ++s) {
      put(true);
    }
  }
  tick += idle_bits_after;
  return start + bits->at(tick);
}

}
