/*
 * SPDX-License-Identifier: MIT
 */

#include "vulkan_waveform_renderer.h"

#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>

#include "waveform_spirv.h"

namespace bitoscope::gui::linux_platform {

namespace {

// WHY: Push constant blocks follow std430: vec4 on 16 bytes, vec2 on 8, scalars on 4.
struct LaneConstants {
  float band[4];
  float highColor[4];
  float lowColor[4];
  float edgeColor[4];
  float fillColor[4];
  float outline[4];
  float display[2];
  float lineWidth;
  std::uint32_t row;
  std::uint32_t width;
};
static_assert(offsetof(LaneConstants, highColor) == 16);
static_assert(offsetof(LaneConstants, lowColor) == 32);
static_assert(offsetof(LaneConstants, edgeColor) == 48);
static_assert(offsetof(LaneConstants, fillColor) == 64);
static_assert(offsetof(LaneConstants, outline) == 80);
static_assert(offsetof(LaneConstants, display) == 96);
static_assert(offsetof(LaneConstants, lineWidth) == 104);
static_assert(offsetof(LaneConstants, row) == 108);
static_assert(offsetof(LaneConstants, width) == 112);
static_assert(sizeof(LaneConstants) == 116);

constexpr std::uint32_t kMaxEdge = 8192;

std::string failure(const char* what, VkResult result) {
  return std::format("Waveform pass: {} failed (VkResult {})", what, static_cast<int>(result));
}

}

VulkanWaveformRenderer::VulkanWaveformRenderer(VulkanPassHost& vulkan) : vulkan_(vulkan) {}

VulkanWaveformRenderer::~VulkanWaveformRenderer() {
  for (auto& [id, view] : views_) {
    releaseSlots(view);
  }
  VkDevice device = vulkan_.device();
  const VkPipeline pipeline = pipeline_;
  const VkPipelineLayout layout = pipelineLayout_;
  const VkDescriptorSetLayout setLayout = setLayout_;
  vulkan_.deferRelease([device, pipeline, layout, setLayout] {
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (layout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, layout, nullptr);
    }
    if (setLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    }
  });
}

void VulkanWaveformRenderer::problem(std::string line) {
  if (problems_.empty() || problems_.back() != line) {
    problems_.push_back(std::move(line));
  }
}

std::vector<std::string> VulkanWaveformRenderer::takeProblems() {
  std::vector<std::string> out;
  out.swap(problems_);
  return out;
}

bool VulkanWaveformRenderer::ensurePipeline() {
  if (pipelineTried_) {
    return pipelineReady_;
  }
  pipelineTried_ = true;
  VkDevice device = vulkan_.device();

  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo setInfo{};
  setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setInfo.bindingCount = 1;
  setInfo.pBindings = &binding;
  if (const VkResult r = vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout_);
      r != VK_SUCCESS) {
    problem(failure("vkCreateDescriptorSetLayout", r));
    return false;
  }
  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.offset = 0;
  push.size = sizeof(LaneConstants);
  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &setLayout_;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &push;
  if (const VkResult r = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout_);
      r != VK_SUCCESS) {
    problem(failure("vkCreatePipelineLayout", r));
    return false;
  }

  const auto module = [&](std::span<const std::uint32_t> words, VkShaderModule& out) {
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size_bytes();
    info.pCode = words.data();
    const VkResult r = vkCreateShaderModule(device, &info, nullptr, &out);
    if (r != VK_SUCCESS) {
      problem(failure("vkCreateShaderModule", r));
    }
    return r == VK_SUCCESS;
  };
  VkShaderModule vertex = VK_NULL_HANDLE;
  VkShaderModule fragment = VK_NULL_HANDLE;
  const bool modules = module(waveformVertexSpirv(), vertex) && module(waveformFragmentSpirv(), fragment);

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragment;
  stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  // WHY: Standard alpha blending: the high-level fill has a real alpha, every other colour is written with alpha 1.0.
  VkPipelineColorBlendAttachmentState attachment{};
  attachment.blendEnable = VK_TRUE;
  attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  attachment.colorBlendOp = VK_BLEND_OP_ADD;
  attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  attachment.alphaBlendOp = VK_BLEND_OP_ADD;
  attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &attachment;
  const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{};
  dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dynamicStates;
  VkGraphicsPipelineCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewportState;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &blend;
  info.pDynamicState = &dynamic;
  info.layout = pipelineLayout_;
  info.renderPass = vulkan_.renderPass();
  info.subpass = 0;
  VkResult created = VK_ERROR_INITIALIZATION_FAILED;
  if (modules) {
    created = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline_);
    if (created != VK_SUCCESS) {
      problem(failure("vkCreateGraphicsPipelines", created));
    }
  }
  if (vertex != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, vertex, nullptr);
  }
  if (fragment != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, fragment, nullptr);
  }
  pipelineReady_ = created == VK_SUCCESS;
  return pipelineReady_;
}

void VulkanWaveformRenderer::upload(std::uint64_t view, std::uint32_t width, std::uint32_t rows,
                                    std::span<const std::uint8_t> levels) {
  View& v = views_[view];
  const bool fits = width != 0 && rows != 0 && levels.size() == static_cast<std::size_t>(width) * rows;
  if (!fits || width > kMaxEdge || rows > kMaxEdge) {
    // WHY: Generation goes back to 0 too: keeping it made the next draw() sync a slot for a 0 x 0 view,
    // which is invalid Vulkan usage.
    releaseSlots(v);
    v = View{};
    if (fits) {
      problem(std::format("Waveform pass: view of {} x {} columns and rows is larger than it "
                          "may be ({})",
                          width, rows, kMaxEdge));
    }
    return;
  }
  if (v.width != width || v.rows != rows) {
    releaseSlots(v);
    v.width = width;
    v.rows = rows;
  }
  v.levels.assign(levels.begin(), levels.end());
  ++v.generation;
}

bool VulkanWaveformRenderer::holds(std::uint64_t view, std::uint32_t width, std::uint32_t rows) const {
  const auto it = views_.find(view);
  return it != views_.end() && it->second.generation != 0 && it->second.width == width &&
         it->second.rows == rows;
}

bool VulkanWaveformRenderer::syncSlot(View& view, std::uint32_t slot) {
  SlotBuffer& s = view.slots[slot];
  if (s.generation == view.generation && s.buffer != VK_NULL_HANDLE) {
    return true;
  }
  VkDevice device = vulkan_.device();
  if (s.buffer == VK_NULL_HANDLE) {
    const VkDeviceSize bytes = (static_cast<VkDeviceSize>(view.width) * view.rows + 3) & ~VkDeviceSize{3};
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bytes;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (const VkResult r = vkCreateBuffer(device, &bufferInfo, nullptr, &s.buffer); r != VK_SUCCESS) {
      s.buffer = VK_NULL_HANDLE;
      problem(failure("vkCreateBuffer (levels)", r));
      return false;
    }
    VkMemoryRequirements needs;
    vkGetBufferMemoryRequirements(device, s.buffer, &needs);
    const std::optional<std::uint32_t> type = vulkan_.memoryType(
      needs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = needs.size;
    alloc.memoryTypeIndex = type.value_or(0);
    VkResult r = type ? vkAllocateMemory(device, &alloc, nullptr, &s.memory)
                      : VK_ERROR_FEATURE_NOT_PRESENT;
    if (r == VK_SUCCESS) {
      r = vkBindBufferMemory(device, s.buffer, s.memory, 0);
    }
    if (r == VK_SUCCESS) {
      r = vkMapMemory(device, s.memory, 0, VK_WHOLE_SIZE, 0, &s.mapped);
    }
    VkDescriptorSetAllocateInfo setAlloc{};
    setAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAlloc.descriptorPool = vulkan_.descriptorPool();
    setAlloc.descriptorSetCount = 1;
    setAlloc.pSetLayouts = &setLayout_;
    if (r == VK_SUCCESS) {
      r = vkAllocateDescriptorSets(device, &setAlloc, &s.set);
    }
    if (r != VK_SUCCESS) {
      problem(failure(type ? "levels' memory or descriptor set" : "host-visible memory type", r));
      if (s.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, s.memory, nullptr);
      }
      vkDestroyBuffer(device, s.buffer, nullptr);
      s = SlotBuffer{};
      return false;
    }
    VkDescriptorBufferInfo bufferDescriptor{s.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = s.set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bufferDescriptor;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  }
  std::memcpy(s.mapped, view.levels.data(), view.levels.size());
  s.generation = view.generation;
  return true;
}

void VulkanWaveformRenderer::releaseSlots(View& view) {
  VkDevice device = vulkan_.device();
  VkDescriptorPool pool = vulkan_.descriptorPool();
  for (SlotBuffer& s : view.slots) {
    if (s.buffer == VK_NULL_HANDLE) {
      continue;
    }
    vulkan_.deferRelease([device, pool, s] {
      if (s.set != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(device, pool, 1, &s.set);
      }
      vkDestroyBuffer(device, s.buffer, nullptr);
      vkFreeMemory(device, s.memory, nullptr);
    });
    s = SlotBuffer{};
  }
}

void VulkanWaveformRenderer::draw(ImDrawList* drawList, std::uint64_t view, float left,
                                  std::span<const Lane> lanes, float lineWidth) {
  const auto it = views_.find(view);
  if (it == views_.end() || it->second.generation == 0 || lanes.empty() || !ensurePipeline()) {
    return;
  }
  View& v = it->second;
  const std::uint32_t slot = vulkan_.frameSlot();
  if (!syncSlot(v, slot)) {
    return;
  }
  v.left = left;
  v.lineWidth = lineWidth;
  v.lanes.assign(lanes.begin(), lanes.end());
  Payload payload{this, view, slot};
  drawList->AddCallback(&VulkanWaveformRenderer::callback, &payload, sizeof(payload));
  drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
}

void VulkanWaveformRenderer::callback(const ImDrawList*, const ImDrawCmd* cmd) {
  Payload payload;
  std::memcpy(&payload, cmd->UserCallbackData, sizeof(payload));
  payload.renderer->render(payload, cmd);
}

void VulkanWaveformRenderer::render(const Payload& payload, const ImDrawCmd* cmd) {
  const auto it = views_.find(payload.view);
  if (it == views_.end() || !pipelineReady_) {
    return;
  }
  const View& v = it->second;
  const SlotBuffer& s = v.slots[payload.slot];
  const ImDrawData* drawData = ImGui::GetDrawData();
  const auto* state =
    static_cast<const ImGui_ImplVulkan_RenderState*>(ImGui::GetPlatformIO().Renderer_RenderState);
  if (s.set == VK_NULL_HANDLE || drawData == nullptr || state == nullptr ||
      state->CommandBuffer == VK_NULL_HANDLE) {
    return;
  }
  VkCommandBuffer commands = state->CommandBuffer;
  const ImVec2 origin = drawData->DisplayPos;
  const ImVec2 scale = drawData->FramebufferScale;
  const float displayWidth = drawData->DisplaySize.x * scale.x;
  const float displayHeight = drawData->DisplaySize.y * scale.y;
  const float clipLeft = (std::max)(0.0f, (cmd->ClipRect.x - origin.x) * scale.x);
  const float clipTop = (std::max)(0.0f, (cmd->ClipRect.y - origin.y) * scale.y);
  const float clipRight = (std::min)(displayWidth, (cmd->ClipRect.z - origin.x) * scale.x);
  const float clipBottom = (std::min)(displayHeight, (cmd->ClipRect.w - origin.y) * scale.y);
  if (clipRight <= clipLeft || clipBottom <= clipTop || displayWidth <= 0 || displayHeight <= 0) {
    return;
  }
  VkRect2D scissor{};
  scissor.offset.x = static_cast<std::int32_t>(clipLeft);
  scissor.offset.y = static_cast<std::int32_t>(clipTop);
  scissor.extent.width = static_cast<std::uint32_t>(clipRight - clipLeft);
  scissor.extent.height = static_cast<std::uint32_t>(clipBottom - clipTop);
  const VkViewport viewport{0.0f, 0.0f, displayWidth, displayHeight, 0.0f, 1.0f};

  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
  vkCmdSetViewport(commands, 0, 1, &viewport);
  vkCmdSetScissor(commands, 0, 1, &scissor);
  vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &s.set,
                          0, nullptr);

  const float left = std::round((v.left - origin.x) * scale.x);
  const float lineWidth = (std::max)(1.0f, std::round(v.lineWidth * scale.y));
  for (const Lane& lane : v.lanes) {
    if (lane.row >= v.rows) {
      continue;
    }
    LaneConstants c{};
    c.band[0] = left;
    c.band[1] = std::round((lane.top - origin.y) * scale.y);
    c.band[2] = left + static_cast<float>(v.width);
    c.band[3] = std::round((lane.bottom - origin.y) * scale.y);
    std::memcpy(c.highColor, lane.highColor.data(), sizeof(c.highColor));
    std::memcpy(c.lowColor, lane.lowColor.data(), sizeof(c.lowColor));
    std::memcpy(c.edgeColor, lane.edgeColor.data(), sizeof(c.edgeColor));
    std::memcpy(c.fillColor, lane.fillColor.data(), sizeof(c.fillColor));
    std::memcpy(c.outline, lane.outlineColor.data(), sizeof(c.outline));
    c.outline[3] = lane.needsOutline ? 1.0f : 0.0f;
    c.display[0] = displayWidth;
    c.display[1] = displayHeight;
    c.lineWidth = lineWidth;
    c.row = lane.row;
    c.width = v.width;
    vkCmdPushConstants(commands, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(c), &c);
    vkCmdDraw(commands, 4, 1, 0, 0);
  }
}

void VulkanWaveformRenderer::release(std::uint64_t view) {
  const auto it = views_.find(view);
  if (it != views_.end()) {
    releaseSlots(it->second);
    views_.erase(it);
  }
}

}
