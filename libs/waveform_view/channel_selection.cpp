/*
 * SPDX-License-Identifier: MIT
 */

#include "channel_selection.h"

#include <algorithm>
#include <utility>

#include "waveform_source.h"

namespace bitoscope::waveform_view {

bool toggleChannelSelection(std::vector<bool>& selected, std::uint32_t channel) {
  if (channel >= selected.size()) {
    return false;
  }
  if (selected[channel]) {
    // WHY: Last selected channel cannot be deselected: a capture needs at least one channel.
    const auto selectedCount = std::count(selected.begin(), selected.end(), true);
    if (selectedCount <= 1) {
      return false;
    }
    selected[channel] = false;
    return true;
  }
  selected[channel] = true;
  return true;
}

void selectAllChannels(std::vector<bool>& selected) {
  std::fill(selected.begin(), selected.end(), true);
}

void selectOneChannel(std::vector<bool>& selected) {
  std::fill(selected.begin(), selected.end(), false);
  if (!selected.empty()) {
    selected[0] = true;
  }
}

bool selectOnlyChannels(std::vector<bool>& selected, const std::vector<std::uint32_t>& channels) {
  std::vector<bool> result(selected.size(), false);
  bool any = false;
  for (const std::uint32_t channel : channels) {
    if (channel < result.size()) {
      result[channel] = true;
      any = true;
    }
  }
  if (any) {
    selected = std::move(result);
  }
  return any;
}

std::vector<std::uint32_t> visibleChannels(const std::vector<bool>& selected) {
  std::vector<std::uint32_t> visible;
  for (std::size_t i = 0; i < selected.size(); ++i) {
    if (selected[i]) {
      visible.push_back(static_cast<std::uint32_t>(i));
    }
  }
  return visible;
}

std::uint64_t channelSelectionMask(const std::vector<bool>& selected) {
  std::uint64_t mask = 0;
  for (std::size_t i = 0; i < selected.size() && i < 64; ++i) {
    if (selected[i]) {
      mask |= (std::uint64_t{1} << i);
    }
  }
  return mask;
}

std::vector<std::uint32_t> visibleLanes(const std::vector<std::uint32_t>& laneDevice, const std::vector<bool>& selected) {
  std::vector<std::uint32_t> lanes;
  for (std::uint32_t lane = 0; lane < laneDevice.size(); ++lane) {
    const std::uint32_t device = laneDevice[lane];
    if (device >= selected.size() || selected[device]) {
      lanes.push_back(lane);
    }
  }
  if (lanes.empty()) {
    for (std::uint32_t lane = 0; lane < laneDevice.size(); ++lane) {
      lanes.push_back(lane);
    }
  }
  return lanes;
}

std::vector<std::uint32_t> shownLanes(std::size_t laneCount, const std::vector<bool>& shown) {
  std::vector<std::uint32_t> lanes;
  for (std::uint32_t lane = 0; lane < laneCount; ++lane) {
    if (lane >= shown.size() || shown[lane]) {
      lanes.push_back(lane);
    }
  }
  if (lanes.empty()) {
    for (std::uint32_t lane = 0; lane < laneCount; ++lane) {
      lanes.push_back(lane);
    }
  }
  return lanes;
}

std::vector<std::uint32_t> lanesOfChannels(const std::vector<std::uint32_t>& laneDevice,
                                           const std::vector<std::uint32_t>& channels) {
  std::vector<std::uint32_t> lanes;
  for (std::uint32_t lane = 0; lane < laneDevice.size(); ++lane) {
    if (std::find(channels.begin(), channels.end(), laneDevice[lane]) != channels.end()) {
      lanes.push_back(lane);
    }
  }
  return lanes;
}

std::vector<std::uint32_t> channelsWithPulses(const WaveformSource& source,
                                              const std::vector<std::uint32_t>& laneDevice) {
  std::vector<std::uint32_t> channels;
  const std::uint32_t lanes = (std::min)(source.channelCount(), static_cast<std::uint32_t>(laneDevice.size()));
  for (std::uint32_t lane = 0; lane < lanes; ++lane) {
    if (source.nextEdge(lane, 0).has_value()) {
      channels.push_back(laneDevice[lane]);
    }
  }
  return channels;
}

}
