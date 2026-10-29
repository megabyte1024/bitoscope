/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::waveform_view {

using MarkerId = std::uint64_t;

struct Marker {
  MarkerId id = 0;
  std::uint64_t sample = 0;
  std::string name;
};

class MarkerList {
 public:
  static std::expected<MarkerList, std::string> restored(std::span<const Marker> markers);

  MarkerId add(std::uint64_t sample);

  bool rename(MarkerId id, std::string_view name);

  bool moveTo(MarkerId id, std::uint64_t sample);

  bool remove(MarkerId id);

  bool removeAll();

  bool trim(std::uint64_t first, std::uint64_t count);

  bool empty() const { return markers_.empty(); }
  std::size_t size() const { return markers_.size(); }

  const std::vector<Marker>& markers() const { return markers_; }

  const Marker* find(MarkerId id) const;

 private:
  std::vector<Marker> markers_;
  MarkerId nextId_ = 1;
};

int64_t markerDeltaTimePicoseconds(const Marker& a, const Marker& b, std::uint64_t sampleRateMilliHz);
std::optional<std::uint64_t> markerDeltaFrequencyMilliHz(const Marker& a, const Marker& b,
                                                          std::uint64_t sampleRateMilliHz);

std::optional<MarkerId> referenceMarker(std::optional<MarkerId> selectedId, std::optional<MarkerId> hoveredId);

std::string formatMarkerRelativeTime(std::int64_t picoseconds);

std::vector<std::string> markerLabels(const MarkerList& markers, std::optional<MarkerId> selectedId,
                                      std::optional<MarkerId> hoveredId, std::uint64_t sampleRateMilliHz);

}
