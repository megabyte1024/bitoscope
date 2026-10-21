/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "vulkan_pass_host.h"
#include "waveform_renderer.h"

namespace bitoscope::gui::linux_platform {

class VulkanWaveformRenderer final : public WaveformRenderer {
 public:
  explicit VulkanWaveformRenderer(VulkanPassHost& vulkan);
  ~VulkanWaveformRenderer() override;
  VulkanWaveformRenderer(const VulkanWaveformRenderer&) = delete;
  VulkanWaveformRenderer& operator=(const VulkanWaveformRenderer&) = delete;

  void upload(std::uint64_t view, std::uint32_t width, std::uint32_t rows,
              std::span<const std::uint8_t> levels) override;
  bool holds(std::uint64_t view, std::uint32_t width, std::uint32_t rows) const override;
  void draw(ImDrawList* drawList, std::uint64_t view, float left, std::span<const Lane> lanes,
            float lineWidth) override;
  void release(std::uint64_t view) override;
  std::vector<std::string> takeProblems() override;

 private:
  struct SlotBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint64_t generation = 0;
  };
  struct View {
    std::uint32_t width = 0;
    std::uint32_t rows = 0;
    std::vector<std::uint8_t> levels;
    std::uint64_t generation = 0;
    std::array<SlotBuffer, VulkanPassHost::kFramesInFlight> slots{};
    float left = 0;
    float lineWidth = 1;
    std::vector<Lane> lanes;
  };
  struct Payload {
    VulkanWaveformRenderer* renderer;
    std::uint64_t view;
    std::uint32_t slot;
  };

  static void callback(const ImDrawList* list, const ImDrawCmd* cmd);
  void render(const Payload& payload, const ImDrawCmd* cmd);
  bool ensurePipeline();
  bool syncSlot(View& view, std::uint32_t slot);
  void releaseSlots(View& view);
  void problem(std::string line);

  VulkanPassHost& vulkan_;
  bool pipelineTried_ = false;
  bool pipelineReady_ = false;
  VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  std::map<std::uint64_t, View> views_;
  std::vector<std::string> problems_;
};

}
