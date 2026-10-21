/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bitoscope::gui_capture {

inline constexpr std::size_t kChannelNameMaxBytes = 63;

std::string shownChannelName(std::span<const std::string> userNames, std::span<const std::string> ownNames,
                             std::uint32_t deviceChannel);

std::string channelChoiceLabel(std::span<const std::string> userNames, std::span<const std::string> ownNames,
                               std::uint32_t deviceChannel);

std::string cleanedChannelName(std::string_view typed);

bool renameChannel(std::vector<std::string>& userNames, std::span<const std::string> ownNames,
                   std::uint32_t deviceChannel, std::string_view typed);

}
