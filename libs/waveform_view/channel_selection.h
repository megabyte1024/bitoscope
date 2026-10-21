/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <vector>

namespace bitoscope::waveform_view {

bool toggleChannelSelection(std::vector<bool>& selected, std::uint32_t channel);

void selectAllChannels(std::vector<bool>& selected);

void selectOneChannel(std::vector<bool>& selected);

bool selectOnlyChannels(std::vector<bool>& selected, const std::vector<std::uint32_t>& channels);

std::vector<std::uint32_t> visibleChannels(const std::vector<bool>& selected);

std::uint64_t channelSelectionMask(const std::vector<bool>& selected);

std::vector<std::uint32_t> visibleLanes(const std::vector<std::uint32_t>& laneDevice, const std::vector<bool>& selected);

std::vector<std::uint32_t> shownLanes(std::size_t laneCount, const std::vector<bool>& shown);

std::vector<std::uint32_t> lanesOfChannels(const std::vector<std::uint32_t>& laneDevice,
                                           const std::vector<std::uint32_t>& channels);

}
