/*
 * SPDX-License-Identifier: MIT
 */

#include "marker_list.h"

#include <algorithm>
#include <format>

#include <display_format.h>

#include "fixed_time.h"

namespace bitoscope::waveform_view {

namespace {

std::string letterName(std::uint64_t index) {
  std::string name;
  ++index;
  while (index > 0) {
    const std::uint64_t remainder = (index - 1) % 26;
    name.insert(name.begin(), static_cast<char>('A' + remainder));
    index = (index - 1) / 26;
  }
  return name;
}

bool byPositionThenId(const Marker& a, const Marker& b) {
  if (a.sample != b.sample) return a.sample < b.sample;
  return a.id < b.id;
}

}

std::expected<MarkerList, std::string> MarkerList::restored(std::span<const Marker> markers) {
  MarkerList list;
  for (const Marker& marker : markers) {
    if (marker.id == 0) return std::unexpected(std::string("marker has id 0, which is never given out"));
    if (marker.name.empty()) return std::unexpected(std::format("marker {} has no name", marker.id));
    if (list.find(marker.id) != nullptr) return std::unexpected(std::format("two markers have id {}", marker.id));
    list.markers_.push_back(marker);
    list.nextId_ = (std::max)(list.nextId_, marker.id + 1);
  }
  std::sort(list.markers_.begin(), list.markers_.end(), byPositionThenId);
  return list;
}

MarkerId MarkerList::add(std::uint64_t sample) {
  std::uint64_t index = 0;
  std::string name = letterName(index);
  while (std::any_of(markers_.begin(), markers_.end(), [&](const Marker& m) { return m.name == name; })) {
    ++index;
    name = letterName(index);
  }
  const MarkerId id = nextId_++;
  markers_.push_back(Marker{id, sample, std::move(name)});
  std::sort(markers_.begin(), markers_.end(), byPositionThenId);
  return id;
}

bool MarkerList::rename(MarkerId id, std::string_view name) {
  if (name.empty()) return false;
  auto it = std::find_if(markers_.begin(), markers_.end(), [&](const Marker& m) { return m.id == id; });
  if (it == markers_.end()) return false;
  it->name = std::string(name);
  return true;
}

bool MarkerList::moveTo(MarkerId id, std::uint64_t sample) {
  auto it = std::find_if(markers_.begin(), markers_.end(), [&](const Marker& m) { return m.id == id; });
  if (it == markers_.end()) return false;
  it->sample = sample;
  std::sort(markers_.begin(), markers_.end(), byPositionThenId);
  return true;
}

bool MarkerList::remove(MarkerId id) {
  auto it = std::find_if(markers_.begin(), markers_.end(), [&](const Marker& m) { return m.id == id; });
  if (it == markers_.end()) return false;
  markers_.erase(it);
  return true;
}

const Marker* MarkerList::find(MarkerId id) const {
  auto it = std::find_if(markers_.begin(), markers_.end(), [&](const Marker& m) { return m.id == id; });
  return it == markers_.end() ? nullptr : &*it;
}

int64_t markerDeltaTimePicoseconds(const Marker& a, const Marker& b, std::uint64_t sampleRateMilliHz) {
  const std::uint64_t delta = a.sample > b.sample ? a.sample - b.sample : b.sample - a.sample;
  return sampleTimePicoseconds(static_cast<int64_t>(delta), sampleRateMilliHz);
}

std::optional<std::uint64_t> markerDeltaFrequencyMilliHz(const Marker& a, const Marker& b,
                                                          std::uint64_t sampleRateMilliHz) {
  const int64_t delta = markerDeltaTimePicoseconds(a, b, sampleRateMilliHz);
  if (delta <= 0) return std::nullopt;
  return frequencyMilliHzFromPeriod(delta);
}

std::optional<MarkerId> referenceMarker(std::optional<MarkerId> selectedId, std::optional<MarkerId> hoveredId) {
  if (hoveredId.has_value()) return hoveredId;
  return selectedId;
}

std::string formatMarkerRelativeTime(std::int64_t picoseconds) {
  if (picoseconds == 0) return "0";
  const std::string body = gui_format::formatTimePs(picoseconds);
  return picoseconds > 0 ? "+" + body : body;
}

std::vector<std::string> markerLabels(const MarkerList& markers, std::optional<MarkerId> selectedId,
                                      std::optional<MarkerId> hoveredId, std::uint64_t sampleRateMilliHz) {
  std::vector<std::string> labels;
  const std::vector<Marker>& all = markers.markers();
  labels.reserve(all.size());
  const std::optional<MarkerId> refId = referenceMarker(selectedId, hoveredId);
  const Marker* reference = refId.has_value() ? markers.find(*refId) : nullptr;
  for (const Marker& marker : all) {
    if (reference == nullptr || marker.id == reference->id) {
      labels.push_back(marker.name);
      continue;
    }
    const int64_t delta = sampleTimePicoseconds(static_cast<int64_t>(marker.sample), sampleRateMilliHz) -
                          sampleTimePicoseconds(static_cast<int64_t>(reference->sample), sampleRateMilliHz);
    labels.push_back(formatMarkerRelativeTime(delta));
  }
  return labels;
}

}
