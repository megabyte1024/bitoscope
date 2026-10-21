/*
 * SPDX-License-Identifier: MIT
 */

#include "present_mode.h"

#include <algorithm>
#include <format>

namespace bitoscope::gui::linux_platform {

VkPresentModeKHR choosePresentMode(std::span<const VkPresentModeKHR> available, bool vsync) {
  if (!vsync) {
    for (const VkPresentModeKHR wanted : {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR}) {
      if (std::ranges::find(available, wanted) != available.end()) {
        return wanted;
      }
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

std::string presentModeName(VkPresentModeKHR mode) {
  switch (mode) {
  case VK_PRESENT_MODE_IMMEDIATE_KHR:
    return "immediate";
  case VK_PRESENT_MODE_MAILBOX_KHR:
    return "mailbox";
  case VK_PRESENT_MODE_FIFO_KHR:
    return "fifo";
  case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
    return "fifo-relaxed";
  default:
    return std::format("other:{}", static_cast<int>(mode));
  }
}

}
