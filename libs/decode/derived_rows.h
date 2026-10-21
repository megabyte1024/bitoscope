/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

// WHY: UART bit grid starts at the frame origin, not at an equal split of the word: bit times are not whole samples.
// SPI and I2C data bit is level(d, e_i) on the first sample of the new clock level, not the sample before.

#include <array>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "decoder.h"

namespace bitoscope::decode {

struct WordSpan {
  uint64_t begin = 0;
  uint64_t end = 0;
  uint64_t value = 0;
};

struct DerivedBit {
  uint64_t begin = 0;
  uint64_t end = 0;
  uint8_t value = 0;
  bool operator==(const DerivedBit&) const = default;
};

struct BitGridRule {
  std::string_view rate_option;
  uint32_t offset_bits = 0;
  std::string_view count_option;
  std::string_view order_option;
};

inline constexpr BitGridRule kUartBitGridRule{"baudrate", 1, "data_bits", "bit_order"};

struct BitGrid {
  uint64_t sample_rate_milli_hz = 0;
  uint64_t bits_per_second = 0;
  uint32_t offset_bits = 0;
  uint32_t count = 0;
  bool msb_first = false;
};

std::expected<BitGrid, std::string> bitGridFrom(const BitGridRule& rule, const OptionMap& resolved,
                                                uint64_t sample_rate_milli_hz);
std::expected<BitGrid, std::string> uartBitGridFrom(const OptionMap& resolved, uint64_t sample_rate_milli_hz);

std::expected<uint64_t, std::string> gridOrigin(const WordSpan& word, const BitGrid& grid);
DerivedBit gridBit(const WordSpan& word, const BitGrid& grid, uint64_t origin, uint32_t i);

template <class F>
std::expected<void, std::string> forEachGridBit(const WordSpan& word, const BitGrid& grid, F&& f) {
  const std::expected<uint64_t, std::string> origin = gridOrigin(word, grid);
  if (!origin) return std::unexpected(origin.error());
  for (uint32_t i = 0; i < grid.count; ++i) f(gridBit(word, grid, *origin, i));
  return {};
}

std::expected<std::vector<DerivedBit>, std::string> gridBits(const WordSpan& word, const BitGrid& grid);

struct ClockEdgesRule {
  std::string_view clock_name;
  std::string_view data_name;
  std::string_view count_option;
  uint32_t count = 0;
};

inline constexpr ClockEdgesRule kSpiMosiRule{"clk", "mosi", "wordsize", 0};
inline constexpr ClockEdgesRule kSpiMisoRule{"clk", "miso", "wordsize", 0};
inline constexpr ClockEdgesRule kI2cByteRule{"scl", "sda", "", 8};

inline constexpr uint32_t kMaxEdgeBits = 64;

struct ClockEdges {
  uint32_t clock_channel = 0;
  uint32_t data_channel = 0;
  uint32_t count = 0;
};

std::expected<ClockEdges, std::string> clockEdgesFrom(const ClockEdgesRule& rule, const ChannelMap& bound,
                                                      const OptionMap& resolved);

struct EdgeWord {
  std::array<uint64_t, kMaxEdgeBits> begin{};
  uint64_t end = 0;
  uint64_t levels = 0;
  uint32_t count = 0;
};

std::expected<EdgeWord, std::string> edgeWord(const WordSpan& word, const ClockEdges& edges, const SignalView& view);
DerivedBit edgeBit(const EdgeWord& word, uint32_t i);

template <class F>
std::expected<void, std::string> forEachEdgeBit(const WordSpan& word, const ClockEdges& edges, const SignalView& view,
                                                F&& f) {
  const std::expected<EdgeWord, std::string> read = edgeWord(word, edges, view);
  if (!read) return std::unexpected(read.error());
  for (uint32_t i = 0; i < read->count; ++i) f(edgeBit(*read, i));
  return {};
}

std::expected<std::vector<DerivedBit>, std::string> edgeBits(const WordSpan& word, const ClockEdges& edges,
                                                             const SignalView& view);

}
