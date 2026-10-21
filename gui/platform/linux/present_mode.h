/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <span>
#include <string>

#include <vulkan/vulkan.h>

namespace bitoscope::gui::linux_platform {

VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> available, bool vsync);

std::string presentModeName(VkPresentModeKHR mode);

}
