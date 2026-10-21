/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace bitoscope::gui::linux_platform {

class VulkanPassHost {
 public:
  static constexpr std::uint32_t kFramesInFlight = 2;

  virtual ~VulkanPassHost() = default;

  virtual VkDevice device() const = 0;
  virtual VkRenderPass renderPass() const = 0;
  virtual VkDescriptorPool descriptorPool() const = 0;
  virtual std::uint32_t frameSlot() const = 0;
  virtual void deferRelease(std::function<void()> release) = 0;
  virtual std::optional<std::uint32_t> memoryType(std::uint32_t typeBits,
                                                  VkMemoryPropertyFlags properties) const = 0;
};

}
