/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::waveform_view {

enum class LaneKind : std::uint8_t { Channel, Decoder };

struct LaneKey {
  LaneKind kind = LaneKind::Channel;
  std::uint32_t index = 0;
  bool operator==(const LaneKey&) const = default;
};

enum class LanePlacement : std::uint8_t { Top, Scroll, Bottom };

enum class PinSide : std::uint8_t { Top, Bottom };

std::optional<PinSide> pinSideOf(LanePlacement placement);

LanePlacement placementOf(PinSide side);

struct LaneEntry {
  LaneKey key;
  LanePlacement placement = LanePlacement::Scroll;
  bool operator==(const LaneEntry&) const = default;
};

class LaneOrder {
public:
  LaneOrder() = default;

  static LaneOrder fresh(std::span<const std::uint32_t> deviceChannels, std::size_t decoderCount);

  static LaneOrder fromEntries(std::vector<LaneEntry> entries);

  void reset(std::span<const std::uint32_t> deviceChannels, std::size_t decoderCount);

  void sync(std::span<const std::uint32_t> deviceChannels, std::size_t decoderCount);

  bool moveTo(LaneKey key, LanePlacement placement, std::size_t indexWithinPlacement);

  bool pin(LaneKey key, PinSide side);

  bool unpin(LaneKey key);

  void removeDecoder(std::size_t decoderIndex);

  std::vector<LaneKey> keysOf(LanePlacement placement) const;

  const std::vector<LaneEntry>& entries() const { return entries_; }

  std::optional<LanePlacement> placementOfKey(LaneKey key) const;

  bool operator==(const LaneOrder&) const = default;

private:
  std::size_t regionBegin(LanePlacement placement) const;
  std::size_t regionEnd(LanePlacement placement) const;
  std::optional<std::size_t> find(LaneKey key) const;

  std::vector<LaneEntry> entries_;
};

std::string toText(const LaneOrder& order);

std::expected<LaneOrder, std::string> parse(std::string_view text);

// WHY: Decoder lanes are saved by key, not list index: an index shifts when a build skips an unknown decoder.
struct KeyedLaneOrder {
  LaneOrder order;
  std::vector<std::string> droppedKeys;
};

std::string toTextWithDecoderKeys(const LaneOrder& order, std::span<const std::string> keys);

std::expected<KeyedLaneOrder, std::string> parseWithDecoderKeys(std::string_view text,
                                                                std::span<const std::string> keys);

}
