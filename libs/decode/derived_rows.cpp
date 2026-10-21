/*
 * SPDX-License-Identifier: MIT
 */

#include "derived_rows.h"

#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace bitoscope::decode {

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

std::expected<std::string_view, std::string> optionText(const OptionMap& resolved, std::string_view name) {
  const auto it = resolved.find(std::string(name));
  if (it == resolved.end()) return std::unexpected(std::format("no option '{}'", name));
  return std::string_view(it->second);
}

std::expected<int64_t, std::string> optionInteger(const OptionMap& resolved, std::string_view name) {
  const std::expected<std::string_view, std::string> text = optionText(resolved, name);
  if (!text) return std::unexpected(text.error());
  int64_t value = 0;
  const char* end = text->data() + text->size();
  const auto r = std::from_chars(text->data(), end, value);
  if (text->empty() || r.ec != std::errc() || r.ptr != end) {
    return std::unexpected(std::format("option '{}' needs whole number, got '{}'", name, *text));
  }
  return value;
}

uint64_t gridAt(const BitGrid& grid, uint64_t j) {
  return j * grid.sample_rate_milli_hz / (grid.bits_per_second * 1000);
}

}

std::expected<BitGrid, std::string> bitGridFrom(const BitGridRule& rule, const OptionMap& resolved,
                                                uint64_t sample_rate_milli_hz) {
  const std::expected<int64_t, std::string> rate = optionInteger(resolved, rule.rate_option);
  if (!rate) return std::unexpected(rate.error());
  if (*rate <= 0) return std::unexpected(std::format("option '{}' must be above 0", rule.rate_option));
  const uint64_t bps = static_cast<uint64_t>(*rate);
  if (bps > kMax / 1000) return std::unexpected(std::format("option '{}' is too high for bit grid", rule.rate_option));

  const std::expected<int64_t, std::string> count = optionInteger(resolved, rule.count_option);
  if (!count) return std::unexpected(count.error());
  if (*count < 1 || *count > 64) {
    return std::unexpected(std::format("option '{}' must be 1 to 64, got {}", rule.count_option, *count));
  }
  const std::expected<std::string_view, std::string> order = optionText(resolved, rule.order_option);
  if (!order) return std::unexpected(order.error());

  if (sample_rate_milli_hz == 0) return std::unexpected("capture has no sample rate");
  const uint64_t last = uint64_t{rule.offset_bits} + static_cast<uint64_t>(*count);
  if (sample_rate_milli_hz > kMax / last) {
    return std::unexpected(std::format("sample rate {} Hz is too high for bit grid", sample_rate_milli_hz / 1000));
  }
  if (sample_rate_milli_hz < 2 * 1000 * bps) {
    return std::unexpected(
        std::format("sample rate {} Hz is too low for {} bits per second: at least two samples per bit are needed",
                    sample_rate_milli_hz / 1000, bps));
  }
  return BitGrid{sample_rate_milli_hz, bps, rule.offset_bits, static_cast<uint32_t>(*count), *order == "msb-first"};
}

std::expected<BitGrid, std::string> uartBitGridFrom(const OptionMap& resolved, uint64_t sample_rate_milli_hz) {
  return bitGridFrom(kUartBitGridRule, resolved, sample_rate_milli_hz);
}

std::expected<uint64_t, std::string> gridOrigin(const WordSpan& word, const BitGrid& grid) {
  const uint64_t lead = gridAt(grid, grid.offset_bits);
  if (word.begin < lead || word.begin - lead + gridAt(grid, uint64_t{grid.offset_bits} + grid.count) != word.end) {
    return std::unexpected(std::format("word at sample {} does not fit declared bit rule", word.begin));
  }
  return word.begin - lead;
}

DerivedBit gridBit(const WordSpan& word, const BitGrid& grid, uint64_t origin, uint32_t i) {
  const uint64_t j = uint64_t{grid.offset_bits} + i;
  const uint32_t shift = grid.msb_first ? grid.count - 1 - i : i;
  return {origin + gridAt(grid, j), origin + gridAt(grid, j + 1), static_cast<uint8_t>((word.value >> shift) & 1u)};
}

std::expected<std::vector<DerivedBit>, std::string> gridBits(const WordSpan& word, const BitGrid& grid) {
  std::vector<DerivedBit> out;
  out.reserve(grid.count);
  const std::expected<void, std::string> r = forEachGridBit(word, grid, [&](const DerivedBit& b) { out.push_back(b); });
  if (!r) return std::unexpected(r.error());
  return out;
}

std::expected<ClockEdges, std::string> clockEdgesFrom(const ClockEdgesRule& rule, const ChannelMap& bound,
                                                      const OptionMap& resolved) {
  const auto clock = bound.find(std::string(rule.clock_name));
  if (clock == bound.end()) return std::unexpected(std::format("no channel bound to '{}'", rule.clock_name));
  const auto data = bound.find(std::string(rule.data_name));
  if (data == bound.end()) return std::unexpected(std::format("no channel bound to '{}'", rule.data_name));

  int64_t count = rule.count;
  if (!rule.count_option.empty()) {
    const std::expected<int64_t, std::string> option = optionInteger(resolved, rule.count_option);
    if (!option) return std::unexpected(option.error());
    count = *option;
    if (count < 1 || count > kMaxEdgeBits) {
      return std::unexpected(std::format("option '{}' must be 1 to {}, got {}", rule.count_option, kMaxEdgeBits, count));
    }
  } else if (count < 1 || count > kMaxEdgeBits) {
    return std::unexpected(std::format("declared bit count must be 1 to {}, got {}", kMaxEdgeBits, count));
  }
  return ClockEdges{clock->second, data->second, static_cast<uint32_t>(count)};
}

std::expected<EdgeWord, std::string> edgeWord(const WordSpan& word, const ClockEdges& edges, const SignalView& view) {
  const auto misfit = [&] {
    return std::unexpected(std::format("word at sample {} does not fit declared bit rule", word.begin));
  };
  const uint32_t c = edges.clock_channel;
  if (edges.count == 0 || edges.count > kMaxEdgeBits) return misfit();
  if (word.begin == 0 || word.end > view.sampleCount()) return misfit();
  const bool rising = view.level(c, word.begin);
  if (view.level(c, word.begin - 1) == rising) return misfit();
  const EdgeKind direction = rising ? EdgeKind::Rising : EdgeKind::Falling;

  EdgeWord out;
  out.count = edges.count;
  out.end = word.end;
  out.begin[0] = word.begin;
  for (uint32_t i = 1; i < edges.count; ++i) {
    const std::optional<uint64_t> next = view.nextEdge(c, out.begin[i - 1], direction);
    if (!next || *next >= word.end) return misfit();
    out.begin[i] = *next;
  }
  const uint64_t last = out.begin[edges.count - 1];
  if (last >= word.end) return misfit();
  if (word.end - last >= 3) {
    const std::optional<uint64_t> turn = view.nextEdge(c, last, EdgeKind::Any);
    if (turn && *turn < word.end) {
      const std::optional<uint64_t> again = view.nextEdge(c, *turn, EdgeKind::Any);
      if (again && *again < word.end) return misfit();
    }
  }
  for (uint32_t i = 0; i < edges.count; ++i) {
    if (view.level(edges.data_channel, out.begin[i])) out.levels |= uint64_t{1} << i;
  }
  if (std::string failed = view.readError(); !failed.empty()) return std::unexpected(std::move(failed));
  return out;
}

DerivedBit edgeBit(const EdgeWord& word, uint32_t i) {
  return {word.begin[i], i + 1 < word.count ? word.begin[i + 1] : word.end,
          static_cast<uint8_t>((word.levels >> i) & 1u)};
}

std::expected<std::vector<DerivedBit>, std::string> edgeBits(const WordSpan& word, const ClockEdges& edges,
                                                             const SignalView& view) {
  std::vector<DerivedBit> out;
  const std::expected<void, std::string> r =
      forEachEdgeBit(word, edges, view, [&](const DerivedBit& b) { out.push_back(b); });
  if (!r) return std::unexpected(r.error());
  return out;
}

}
