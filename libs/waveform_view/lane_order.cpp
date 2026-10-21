/*
 * SPDX-License-Identifier: MIT
 */

#include "lane_order.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <utility>

namespace bitoscope::waveform_view {

std::optional<PinSide> pinSideOf(LanePlacement placement) {
  switch (placement) {
    case LanePlacement::Top: return PinSide::Top;
    case LanePlacement::Bottom: return PinSide::Bottom;
    case LanePlacement::Scroll: break;
  }
  return std::nullopt;
}

LanePlacement placementOf(PinSide side) {
  return side == PinSide::Top ? LanePlacement::Top : LanePlacement::Bottom;
}

namespace {

int rank(LanePlacement p) { return static_cast<int>(p); }

std::vector<std::uint32_t> sortedUnique(std::span<const std::uint32_t> channels) {
  std::vector<std::uint32_t> sorted(channels.begin(), channels.end());
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  return sorted;
}

}

LaneOrder LaneOrder::fromEntries(std::vector<LaneEntry> entries) {
  std::stable_sort(entries.begin(), entries.end(),
                   [](const LaneEntry& a, const LaneEntry& b) { return rank(a.placement) < rank(b.placement); });
  LaneOrder order;
  order.entries_ = std::move(entries);
  return order;
}

LaneOrder LaneOrder::fresh(std::span<const std::uint32_t> deviceChannels, std::size_t decoderCount) {
  LaneOrder order;
  for (const std::uint32_t channel : sortedUnique(deviceChannels)) {
    order.entries_.push_back({{LaneKind::Channel, channel}, LanePlacement::Scroll});
  }
  for (std::size_t d = 0; d < decoderCount; ++d) {
    order.entries_.push_back({{LaneKind::Decoder, static_cast<std::uint32_t>(d)}, LanePlacement::Scroll});
  }
  return order;
}

void LaneOrder::reset(std::span<const std::uint32_t> deviceChannels, std::size_t decoderCount) {
  *this = fresh(deviceChannels, decoderCount);
}

std::size_t LaneOrder::regionBegin(LanePlacement placement) const {
  return static_cast<std::size_t>(std::count_if(
      entries_.begin(), entries_.end(), [&](const LaneEntry& e) { return rank(e.placement) < rank(placement); }));
}

std::size_t LaneOrder::regionEnd(LanePlacement placement) const {
  return static_cast<std::size_t>(std::count_if(
      entries_.begin(), entries_.end(), [&](const LaneEntry& e) { return rank(e.placement) <= rank(placement); }));
}

std::optional<std::size_t> LaneOrder::find(LaneKey key) const {
  const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const LaneEntry& e) { return e.key == key; });
  if (it == entries_.end()) return std::nullopt;
  return static_cast<std::size_t>(it - entries_.begin());
}

void LaneOrder::sync(std::span<const std::uint32_t> deviceChannels, std::size_t decoderCount) {
  std::erase_if(entries_, [&](const LaneEntry& e) { return e.key.kind == LaneKind::Decoder && e.key.index >= decoderCount; });

  for (const std::uint32_t channel : sortedUnique(deviceChannels)) {
    const LaneKey key{LaneKind::Channel, channel};
    if (find(key)) continue;
    std::size_t position = regionBegin(LanePlacement::Scroll);
    for (std::size_t i = position; i < regionEnd(LanePlacement::Scroll); ++i) {
      if (entries_[i].key.kind == LaneKind::Channel) position = i + 1;
    }
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(position), {key, LanePlacement::Scroll});
  }

  for (std::size_t d = 0; d < decoderCount; ++d) {
    const LaneKey key{LaneKind::Decoder, static_cast<std::uint32_t>(d)};
    if (find(key)) continue;
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(regionEnd(LanePlacement::Scroll)),
                    {key, LanePlacement::Scroll});
  }
}

bool LaneOrder::moveTo(LaneKey key, LanePlacement placement, std::size_t indexWithinPlacement) {
  const auto at = find(key);
  if (!at) return false;
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(*at));
  const std::size_t begin = regionBegin(placement);
  const std::size_t count = regionEnd(placement) - begin;
  const std::size_t index = (std::min)(indexWithinPlacement, count);
  entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(begin + index), {key, placement});
  return true;
}

bool LaneOrder::pin(LaneKey key, PinSide side) {
  return moveTo(key, placementOf(side), (std::numeric_limits<std::size_t>::max)());
}

bool LaneOrder::unpin(LaneKey key) {
  const auto at = find(key);
  if (!at) return false;
  switch (entries_[*at].placement) {
    case LanePlacement::Scroll: return true;
    case LanePlacement::Top: return moveTo(key, LanePlacement::Scroll, 0);
    case LanePlacement::Bottom: return moveTo(key, LanePlacement::Scroll, (std::numeric_limits<std::size_t>::max)());
  }
  return true;
}

void LaneOrder::removeDecoder(std::size_t decoderIndex) {
  std::erase_if(entries_, [&](const LaneEntry& e) { return e.key.kind == LaneKind::Decoder && e.key.index == decoderIndex; });
  for (LaneEntry& e : entries_) {
    if (e.key.kind == LaneKind::Decoder && e.key.index > decoderIndex) --e.key.index;
  }
}

std::vector<LaneKey> LaneOrder::keysOf(LanePlacement placement) const {
  std::vector<LaneKey> keys;
  for (const LaneEntry& e : entries_) {
    if (e.placement == placement) keys.push_back(e.key);
  }
  return keys;
}

std::optional<LanePlacement> LaneOrder::placementOfKey(LaneKey key) const {
  const auto at = find(key);
  if (!at) return std::nullopt;
  return entries_[*at].placement;
}

std::string toText(const LaneOrder& order) {
  std::string out;
  for (const LaneEntry& e : order.entries()) {
    if (!out.empty()) out += ' ';
    out += e.placement == LanePlacement::Top ? 'T' : e.placement == LanePlacement::Bottom ? 'B' : 'S';
    out += ':';
    out += e.key.kind == LaneKind::Decoder ? 'd' : 'c';
    out += std::to_string(e.key.index);
  }
  return out;
}

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

bool writableKey(std::string_view key) {
  return !key.empty() && key.find(']') == std::string_view::npos &&
         std::none_of(key.begin(), key.end(), [](char c) { return isSpace(c); });
}

std::string quoted(std::string_view token) {
  std::string out = "'";
  out += token;
  out += '\'';
  return out;
}

}

namespace {

std::expected<KeyedLaneOrder, std::string> parseTokens(std::string_view text,
                                                       std::optional<std::span<const std::string>> keys) {
  std::vector<LaneEntry> entries;
  std::vector<std::string> dropped;
  std::set<std::pair<int, std::uint32_t>> seen;

  std::size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() && isSpace(text[pos])) ++pos;
    if (pos >= text.size()) break;
    const std::size_t start = pos;
    while (pos < text.size() && !isSpace(text[pos])) ++pos;
    const std::string_view token = text.substr(start, pos - start);

    LaneEntry entry;
    switch (token[0]) {
      case 'T': entry.placement = LanePlacement::Top; break;
      case 'S': entry.placement = LanePlacement::Scroll; break;
      case 'B': entry.placement = LanePlacement::Bottom; break;
      default: return std::unexpected("unknown placement in lane token " + quoted(token));
    }
    if (token.size() < 2 || token[1] != ':') {
      return std::unexpected("lane token " + quoted(token) + " needs ':' after placement");
    }
    if (token.size() < 3 || (token[2] != 'c' && token[2] != 'd')) {
      return std::unexpected("unknown lane kind in lane token " + quoted(token));
    }
    entry.key.kind = token[2] == 'd' ? LaneKind::Decoder : LaneKind::Channel;

    const std::string_view rest = token.substr(3);
    if (entry.key.kind == LaneKind::Decoder && !rest.empty() && rest.front() == '[') {
      if (rest.back() != ']' || rest.size() < 3) {
        return std::unexpected("decoder key is not closed in lane token " + quoted(token));
      }
      const std::string_view key = rest.substr(1, rest.size() - 2);
      if (!writableKey(key)) return std::unexpected("bad decoder key in lane token " + quoted(token));
      if (!keys) return std::unexpected("lane token " + quoted(token) + " names decoder by key");
      const auto at = std::find(keys->begin(), keys->end(), key);
      if (at == keys->end()) {
        dropped.emplace_back(key);
        continue;
      }
      entry.key.index = static_cast<std::uint32_t>(at - keys->begin());
    } else {
      const std::string_view digits = rest;
      if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return std::unexpected("bad lane number in lane token " + quoted(token));
      }
      std::uint32_t value = 0;
      const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
      if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::unexpected("lane number out of range in lane token " + quoted(token));
      }
      entry.key.index = value;
    }

    if (!seen.insert({static_cast<int>(entry.key.kind), entry.key.index}).second) {
      return std::unexpected("lane " + quoted(token.substr(2)) + " is listed twice");
    }
    entries.push_back(entry);
  }
  return KeyedLaneOrder{LaneOrder::fromEntries(std::move(entries)), std::move(dropped)};
}

}

std::string toTextWithDecoderKeys(const LaneOrder& order, std::span<const std::string> keys) {
  std::string out;
  for (const LaneEntry& e : order.entries()) {
    if (!out.empty()) out += ' ';
    out += e.placement == LanePlacement::Top ? 'T' : e.placement == LanePlacement::Bottom ? 'B' : 'S';
    out += ':';
    out += e.key.kind == LaneKind::Decoder ? 'd' : 'c';
    const bool keyed = e.key.kind == LaneKind::Decoder && e.key.index < keys.size() &&
                       writableKey(keys[e.key.index]);
    if (keyed) {
      out += '[';
      out += keys[e.key.index];
      out += ']';
    } else {
      out += std::to_string(e.key.index);
    }
  }
  return out;
}

std::expected<LaneOrder, std::string> parse(std::string_view text) {
  auto parsed = parseTokens(text, std::nullopt);
  if (!parsed) return std::unexpected(parsed.error());
  return std::move(parsed->order);
}

std::expected<KeyedLaneOrder, std::string> parseWithDecoderKeys(std::string_view text,
                                                                std::span<const std::string> keys) {
  return parseTokens(text, keys);
}

}
