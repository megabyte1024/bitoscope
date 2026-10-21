/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <vulkan/vulkan.h>

#include <deferred_releases.h>

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <image_scale.h>
#endif

#include "vulkan_pass_host.h"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow;

namespace bitoscope::gui::linux_platform {

class VulkanDevice final : public VulkanPassHost {
 public:
  static std::expected<std::unique_ptr<VulkanDevice>, std::string> create(GLFWwindow* window,
                                                                          bool readableSwapchain = false);

  ~VulkanDevice() override;
  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
  VkDevice device() const override { return device_; }
  std::uint32_t queueFamily() const { return queueFamily_; }
  VkQueue queue() const { return queue_; }
  VkRenderPass renderPass() const override { return renderPass_; }
  VkDescriptorPool descriptorPool() const override { return descriptorPool_; }
  std::uint32_t minImageCount() const { return minImageCount_; }
  std::uint32_t imageCount() const { return static_cast<std::uint32_t>(images_.size()); }
  const std::string& description() const { return description_; }

  enum class FrameStart {
    Recording,
    Skipped,
  };

  std::uint32_t frameSlot() const override { return frameSlot_; }

  std::expected<void, std::string> waitForFrameSlot();

  std::expected<FrameStart, std::string> beginFrame(std::uint32_t width, std::uint32_t height,
                                                    const std::array<float, 4>& clearRgba);

  VkCommandBuffer commandBuffer() const { return recording_ ? frames_[frameSlot_].commandBuffer : VK_NULL_HANDLE; }

  std::expected<void, std::string> endFrame();

  void deferRelease(std::function<void()> release) override;

  std::expected<void, std::string> submitOnce(const std::function<void(VkCommandBuffer)>& record);

  std::optional<std::uint32_t> memoryType(std::uint32_t typeBits,
                                          VkMemoryPropertyFlags properties) const override;

  void waitIdleAndRelease();

#ifdef BITOSCOPE_GUI_TEST_HOOK
  std::expected<void, std::string> setVsync(bool vsync, std::uint32_t width, std::uint32_t height);
  std::string presentModeName() const;
  void setSubmitStamp(std::function<void()> stamp) { submitStamp_ = std::move(stamp); }
  void requestReadback() { readbackRequested_ = true; }
  std::expected<bitoscope::gui_test_input::RgbaPicture, std::string> takeReadback();
#endif

 private:
  struct Frame {
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence done = VK_NULL_HANDLE;
    VkSemaphore imageAcquired = VK_NULL_HANDLE;
    std::uint64_t serial = 0;
  };
  struct SwapchainImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
  };

  VulkanDevice() = default;
  std::expected<void, std::string> createInstanceAndSurface(GLFWwindow* window);
  std::expected<void, std::string> pickPhysicalDevice();
  std::expected<void, std::string> createDeviceAndQueue();
  std::expected<void, std::string> createRenderPassAndPools();
#ifdef BITOSCOPE_GUI_TEST_HOOK
  void recordReadback(VkCommandBuffer commands);
  void destroyReadbackBuffer();
#endif
  std::expected<void, std::string> createFrames();
  std::expected<bool, std::string> rebuildSwapchain(std::uint32_t width, std::uint32_t height);
  void destroySwapchainImages();

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memoryProperties_{};
  std::string description_;
  std::uint32_t queueFamily_ = 0;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surfaceFormat_{};
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkCommandPool oneTimePool_ = VK_NULL_HANDLE;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D extent_{};
  std::uint32_t minImageCount_ = 2;
  std::vector<SwapchainImage> images_;
  bool swapchainStale_ = false;

  bool readableRequested_ = false;
  bool readable_ = false;
  bool vsync_ = true;
  VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
#ifdef BITOSCOPE_GUI_TEST_HOOK
  std::function<void()> submitStamp_;
  bool readbackRequested_ = false;
  bool readbackRecorded_ = false;
  VkFence readbackFence_ = VK_NULL_HANDLE;
  VkExtent2D readbackExtent_{};
  std::string readbackError_;
  VkBuffer readbackBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
  VkDeviceSize readbackSize_ = 0;
  bool readbackCoherent_ = false;
#endif

  std::array<Frame, kFramesInFlight> frames_{};
  std::uint32_t frameSlot_ = 0;
  std::uint32_t imageIndex_ = 0;
  bool recording_ = false;
  gui_linux::DeferredReleases releases_;
};

}
