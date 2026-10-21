/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace bitoscope::waveform_view {

static_assert(std::endian::native == std::endian::little, "bit-packed blocks are read little-endian");

inline constexpr std::size_t kBlockEntries = 32;
// WHY: Zero padding after last payload byte keeps the 8-byte load of any field, and a ninth byte, in bounds.
inline constexpr std::size_t kBitPadding = 16;
inline constexpr std::uint8_t kStepsPresent = 1;
inline constexpr std::uint8_t kLengthsPresent = 2;
inline constexpr std::uint8_t kValuesPresent = 4;
inline constexpr std::uint32_t kLengthMarker = 0xFFFFFFFFu;
inline constexpr std::size_t kMostPayloadBytes =
  1 + (1 + 10 + (kBlockEntries - 1) * 8) + (1 + 5 + kBlockEntries * 4) + (1 + 10 + kBlockEntries * 8);
inline constexpr std::uint64_t kNarrowEntryMost = 0xFFFFFFFFu;

inline std::uint64_t readBits(const std::uint8_t* base, std::size_t bit, unsigned w) {
  if (w == 0) return 0;
  const std::uint8_t* const p = base + (bit >> 3);
  const unsigned s = static_cast<unsigned>(bit & 7);
  std::uint64_t v;
  std::memcpy(&v, p, 8);
  v >>= s;
  if (w + s > 64) v |= static_cast<std::uint64_t>(p[8]) << (64 - s);
  return w == 64 ? v : (v & ((std::uint64_t{1} << w) - 1));
}

struct BlockField {
  std::uint64_t min = 0;
  unsigned width = 0;
  const std::uint8_t* bits = nullptr;
};
using StepField = BlockField;

std::uint64_t fieldEntry(const BlockField& field, std::size_t t);

struct UsualValues {
  std::uint64_t step = 0;
  std::uint32_t length = 0;
  std::uint64_t value = 0;
};

struct BlockFields {
  BlockField steps;
  BlockField lengths;
  BlockField values;
};

std::uint64_t usualStepOf(std::span<const std::uint64_t> firstBlock);
std::uint32_t usualLengthOf(std::span<const std::uint32_t> firstBlock);
std::uint64_t usualValueOf(std::span<const std::uint64_t> firstBlock);
std::size_t blockPayloadBytes(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                              std::span<const std::uint64_t> values, const UsualValues& usual);
std::size_t packBlock(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                      std::span<const std::uint64_t> values, const UsualValues& usual, std::uint8_t* out);
std::size_t blockPayloadBytes(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                              const UsualValues& usual);
std::size_t packBlock(std::span<const std::uint64_t> begins, std::span<const std::uint32_t> lengths,
                      const UsualValues& usual, std::uint8_t* out);
BlockFields describeBlock(const std::uint8_t* payload, std::size_t count, const UsualValues& usual);

std::size_t stepsBytes(std::span<const std::uint64_t> begins, std::uint64_t usualStep);
std::size_t packSteps(std::span<const std::uint64_t> begins, std::uint64_t usualStep, std::uint8_t* out);
StepField describeSteps(const std::uint8_t* payload);
void sumBegins(const StepField& steps, std::uint64_t* begins, std::size_t from, std::size_t to);
std::uint64_t beginInBlock(const StepField& steps, std::uint64_t base, std::size_t j);

struct BlockTableForms {
  std::uint8_t beginWidth = 8;
  std::uint8_t offsetWidth = 8;
  bool operator==(const BlockTableForms&) const = default;
};
BlockTableForms blockTableForms(std::uint64_t largestDistance, std::uint64_t payloadBytes);
std::uint8_t coveredWidthOf(bool lengthsField, std::uint32_t usualLength, bool regular, std::uint64_t total);

}
