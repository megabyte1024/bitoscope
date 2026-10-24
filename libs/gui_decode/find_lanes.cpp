/*
 * SPDX-License-Identifier: MIT
 */

#include "find_lanes.h"

#include <algorithm>
#include <array>

namespace bitoscope::gui_decode {

namespace wv = bitoscope::waveform_view;

std::vector<std::uint32_t> findLaneRanks(const wv::LaneDisplay& display, std::size_t decoderCount) {
  constexpr std::array<wv::LanePlacement, 3> kTopToBottom{wv::LanePlacement::Top, wv::LanePlacement::Scroll,
                                                          wv::LanePlacement::Bottom};
  std::vector<std::optional<std::uint32_t>> shown(decoderCount);
  std::uint32_t next = 0;
  for (const wv::LanePlacement placement : kTopToBottom) {
    for (const wv::DisplayedLane& lane : display.lanesOf(placement)) {
      if (lane.key.kind != wv::LaneKind::Decoder || lane.key.index >= decoderCount || shown[lane.key.index]) continue;
      shown[lane.key.index] = next++;
    }
  }
  std::vector<std::uint32_t> ranks(decoderCount);
  for (std::size_t d = 0; d < decoderCount; ++d) {
    ranks[d] = shown[d] ? *shown[d] : next++;
  }
  return ranks;
}

std::vector<std::uint32_t> findRowRanks(std::span<const bitoscope::decode::AnnotationRow> shownRows) {
  if (shownRows.empty()) return {};
  const std::uint32_t highest = std::ranges::max(shownRows, {}, &bitoscope::decode::AnnotationRow::id).id;
  std::vector<std::uint32_t> ranks(std::size_t{highest} + 1);
  for (std::size_t id = 0; id < ranks.size(); ++id) {
    ranks[id] = static_cast<std::uint32_t>(ranks.size() + id);
  }
  for (std::size_t slot = 0; slot < shownRows.size(); ++slot) {
    ranks[shownRows[slot].id] = static_cast<std::uint32_t>(slot);
  }
  return ranks;
}

std::optional<std::size_t> shownRowSlot(std::span<const bitoscope::decode::AnnotationRow> shownRows,
                                        std::uint32_t row) {
  const auto found = std::ranges::find(shownRows, row, &bitoscope::decode::AnnotationRow::id);
  if (found == shownRows.end()) return std::nullopt;
  return static_cast<std::size_t>(found - shownRows.begin());
}

std::optional<double> findRowScrollTop(const wv::LaneDisplay& display, std::uint32_t decoder,
                                       std::optional<std::size_t> slot, double rowPitch) {
  const wv::LaneKey key{wv::LaneKind::Decoder, decoder};
  const auto lane = std::ranges::find(display.scroll, key, &wv::DisplayedLane::key);
  if (lane == display.scroll.end()) return std::nullopt;
  return lane->offset + static_cast<double>(slot.value_or(0)) * rowPitch;
}

}
