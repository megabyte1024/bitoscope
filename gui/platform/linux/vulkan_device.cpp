/*
 * SPDX-License-Identifier: MIT
 */

#include "vulkan_device.h"

#include "present_mode.h"

// WHY: Include vulkan.h before glfw3.h, so GLFW declares its Vulkan functions.
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

#ifdef BITOSCOPE_GUI_TEST_HOOK
#include <pixel_rows.h>
#endif

namespace bitoscope::gui::linux_platform {

namespace {

std::string failure(const char* what, VkResult result) {
  return std::format("{} failed (VkResult {})", what, static_cast<int>(result));
}

int typeRank(VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return 4;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return 3;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return 2;
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return 1;
  default:
    return 0;
  }
}

bool hasSwapchainExtension(VkPhysicalDevice device) {
  std::uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
  return std::ranges::any_of(extensions, [](const VkExtensionProperties& e) {
    return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
  });
}

std::optional<std::uint32_t> graphicsAndPresentFamily(VkPhysicalDevice device, VkSurfaceKHR surface) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
  for (std::uint32_t i = 0; i < count; ++i) {
    VkBool32 presents = VK_FALSE;
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presents) == VK_SUCCESS &&
        presents == VK_TRUE) {
      return i;
    }
  }
  return std::nullopt;
}

// WHY: UNORM, not sRGB: Dear ImGui colours are written as they are, so both platform layers show the same colours.
std::optional<VkSurfaceFormatKHR> chooseSurfaceFormat(VkPhysicalDevice device, VkSurfaceKHR surface) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());
  for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM}) {
    for (const VkSurfaceFormatKHR& format : formats) {
      if (format.format == wanted && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return format;
      }
    }
  }
  return std::nullopt;
}

}

std::expected<std::unique_ptr<VulkanDevice>, std::string> VulkanDevice::create(GLFWwindow* window,
                                                                              bool readableSwapchain) {
  std::unique_ptr<VulkanDevice> d(new VulkanDevice());
  d->readableRequested_ = readableSwapchain;
  d->vsync_ = !readableSwapchain;
  if (auto made = d->createInstanceAndSurface(window); !made) {
    return std::unexpected(made.error());
  }
  if (auto made = d->pickPhysicalDevice(); !made) {
    return std::unexpected(made.error());
  }
  if (auto made = d->createDeviceAndQueue(); !made) {
    return std::unexpected(made.error());
  }
  if (auto made = d->createRenderPassAndPools(); !made) {
    return std::unexpected(made.error());
  }
  if (auto made = d->createFrames(); !made) {
    return std::unexpected(made.error());
  }
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  if (auto made = d->rebuildSwapchain(static_cast<std::uint32_t>(std::max(width, 0)),
                                      static_cast<std::uint32_t>(std::max(height, 0)));
      !made) {
    return std::unexpected(made.error());
  }
  return d;
}

std::expected<void, std::string> VulkanDevice::createInstanceAndSurface(GLFWwindow* window) {
  std::uint32_t extensionCount = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
  if (extensions == nullptr) {
    return std::unexpected(
      "GLFW found no Vulkan loader or no surface extension for this window system "
      "(glfwGetRequiredInstanceExtensions)");
  }
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "bitoscope";
  app.pEngineName = "bitoscope";
  app.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = extensionCount;
  info.ppEnabledExtensionNames = extensions;
  if (const VkResult r = vkCreateInstance(&info, nullptr, &instance_); r != VK_SUCCESS) {
    return std::unexpected(failure("vkCreateInstance", r));
  }
  if (const VkResult r = glfwCreateWindowSurface(instance_, window, nullptr, &surface_);
      r != VK_SUCCESS) {
    return std::unexpected(failure("glfwCreateWindowSurface", r));
  }
  return {};
}

std::expected<void, std::string> VulkanDevice::pickPhysicalDevice() {
  std::uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  int bestRank = -1;
  for (VkPhysicalDevice device : devices) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(device, &properties);
    const std::optional<std::uint32_t> family = graphicsAndPresentFamily(device, surface_);
    if (!family || !hasSwapchainExtension(device) || !chooseSurfaceFormat(device, surface_)) {
      continue;
    }
    if (const int rank = typeRank(properties.deviceType); rank > bestRank) {
      bestRank = rank;
      physicalDevice_ = device;
      queueFamily_ = *family;
      description_ = properties.deviceName;
    }
  }
  if (physicalDevice_ == VK_NULL_HANDLE) {
    return std::unexpected(std::format(
      "none of {} Vulkan devices draws and presents to this window with 8-bit UNORM format",
      count));
  }
  surfaceFormat_ = *chooseSurfaceFormat(physicalDevice_, surface_);
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties_);
  return {};
}

std::expected<void, std::string> VulkanDevice::createDeviceAndQueue() {
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = queueFamily_;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;
  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.queueCreateInfoCount = 1;
  info.pQueueCreateInfos = &queueInfo;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = extensions;
  if (const VkResult r = vkCreateDevice(physicalDevice_, &info, nullptr, &device_); r != VK_SUCCESS) {
    return std::unexpected(failure("vkCreateDevice", r));
  }
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
  return {};
}

std::expected<void, std::string> VulkanDevice::createRenderPassAndPools() {
  VkAttachmentDescription color{};
  color.format = surfaceFormat_.format;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;
  // WHY: The layout transition waits for the image the acquire semaphore guards;
  // the submit waits on it at colour-attachment-output.
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  // WHY: Without this dependency the implicit one to VK_SUBPASS_EXTERNAL ends at BOTTOM_OF_PIPE
  // and orders nothing after it.
  VkSubpassDependency toCopy{};
  toCopy.srcSubpass = 0;
  toCopy.dstSubpass = VK_SUBPASS_EXTERNAL;
  toCopy.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  toCopy.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  toCopy.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  const VkSubpassDependency dependencies[] = {dependency, toCopy};
  VkRenderPassCreateInfo passInfo{};
  passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.attachmentCount = 1;
  passInfo.pAttachments = &color;
  passInfo.subpassCount = 1;
  passInfo.pSubpasses = &subpass;
  passInfo.dependencyCount = readableRequested_ ? 2 : 1;
  passInfo.pDependencies = dependencies;
  if (const VkResult r = vkCreateRenderPass(device_, &passInfo, nullptr, &renderPass_); r != VK_SUCCESS) {
    return std::unexpected(failure("vkCreateRenderPass", r));
  }

  // WHY: FREE_DESCRIPTOR_SET is needed: textures and waveform sets are freed one by one.
  const VkDescriptorPoolSize sizes[] = {
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 256},
    {VK_DESCRIPTOR_TYPE_SAMPLER, 16},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
  };
  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 528;
  poolInfo.poolSizeCount = static_cast<std::uint32_t>(std::size(sizes));
  poolInfo.pPoolSizes = sizes;
  if (const VkResult r = vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_);
      r != VK_SUCCESS) {
    return std::unexpected(failure("vkCreateDescriptorPool", r));
  }

  VkCommandPoolCreateInfo commandPoolInfo{};
  commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  commandPoolInfo.queueFamilyIndex = queueFamily_;
  if (const VkResult r = vkCreateCommandPool(device_, &commandPoolInfo, nullptr, &oneTimePool_);
      r != VK_SUCCESS) {
    return std::unexpected(failure("vkCreateCommandPool", r));
  }
  return {};
}

std::expected<void, std::string> VulkanDevice::createFrames() {
  for (Frame& frame : frames_) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamily_;
    if (const VkResult r = vkCreateCommandPool(device_, &poolInfo, nullptr, &frame.commandPool);
        r != VK_SUCCESS) {
      return std::unexpected(failure("vkCreateCommandPool", r));
    }
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = frame.commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (const VkResult r = vkAllocateCommandBuffers(device_, &allocInfo, &frame.commandBuffer);
        r != VK_SUCCESS) {
      return std::unexpected(failure("vkAllocateCommandBuffers", r));
    }
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // WHY: Signaled, so the first wait must not block.
    if (const VkResult r = vkCreateFence(device_, &fenceInfo, nullptr, &frame.done); r != VK_SUCCESS) {
      return std::unexpected(failure("vkCreateFence", r));
    }
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (const VkResult r = vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAcquired);
        r != VK_SUCCESS) {
      return std::unexpected(failure("vkCreateSemaphore", r));
    }
  }
  return {};
}

std::expected<bool, std::string> VulkanDevice::rebuildSwapchain(std::uint32_t width,
                                                               std::uint32_t height) {
  vkDeviceWaitIdle(device_);
  releases_.completedUpTo(releases_.submittedSerial());

  VkSurfaceCapabilitiesKHR caps;
  if (const VkResult r = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);
      r != VK_SUCCESS) {
    return std::unexpected(failure("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r));
  }
  // WHY: X11 sizes the surface itself in currentExtent; Wayland leaves it to the swapchain with 0xFFFFFFFF.
  VkExtent2D extent = caps.currentExtent;
  if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
    extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
  }
  if (extent.width == 0 || extent.height == 0) {
    swapchainStale_ = true;
    return false;
  }
  minImageCount_ = std::max(caps.minImageCount + 1, 2u);
  if (caps.maxImageCount != 0) {
    minImageCount_ = std::min(minImageCount_, caps.maxImageCount);
  }
  VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if ((caps.supportedCompositeAlpha & alpha) == 0) {
    for (const VkCompositeAlphaFlagBitsKHR other :
         {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR}) {
      if ((caps.supportedCompositeAlpha & other) != 0) {
        alpha = other;
        break;
      }
    }
  }

  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface_;
  info.minImageCount = minImageCount_;
  info.imageFormat = surfaceFormat_.format;
  info.imageColorSpace = surfaceFormat_.colorSpace;
  info.imageExtent = extent;
  info.imageArrayLayers = 1;
  readable_ = readableRequested_ && (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (readable_ ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = caps.currentTransform;
  info.compositeAlpha = alpha;
  std::uint32_t modeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, nullptr);
  std::vector<VkPresentModeKHR> modes(modeCount);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &modeCount, modes.data());
  info.presentMode = choosePresentMode(modes, vsync_);
  info.clipped = VK_TRUE;
  info.oldSwapchain = swapchain_;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  const VkResult created = vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain);
  destroySwapchainImages();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  if (created != VK_SUCCESS) {
    return std::unexpected(failure("vkCreateSwapchainKHR", created));
  }
  swapchain_ = swapchain;
  extent_ = extent;
  presentMode_ = info.presentMode;
  swapchainStale_ = false;

  std::uint32_t count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  std::vector<VkImage> images(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data());
  images_.resize(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    SwapchainImage& image = images_[i];
    image.image = images[i];
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = surfaceFormat_.format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (const VkResult r = vkCreateImageView(device_, &viewInfo, nullptr, &image.view); r != VK_SUCCESS) {
      return std::unexpected(failure("vkCreateImageView", r));
    }
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &image.view;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;
    if (const VkResult r = vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &image.framebuffer);
        r != VK_SUCCESS) {
      return std::unexpected(failure("vkCreateFramebuffer", r));
    }
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (const VkResult r = vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &image.renderFinished);
        r != VK_SUCCESS) {
      return std::unexpected(failure("vkCreateSemaphore", r));
    }
  }
  return true;
}

void VulkanDevice::destroySwapchainImages() {
  for (SwapchainImage& image : images_) {
    if (image.renderFinished != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, image.renderFinished, nullptr);
    }
    if (image.framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, image.framebuffer, nullptr);
    }
    if (image.view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, image.view, nullptr);
    }
  }
  images_.clear();
}

std::expected<VulkanDevice::FrameStart, std::string> VulkanDevice::beginFrame(
  std::uint32_t width, std::uint32_t height, const std::array<float, 4>& clearRgba) {
  if (width == 0 || height == 0) {
    return FrameStart::Skipped;
  }
  if (swapchainStale_ || swapchain_ == VK_NULL_HANDLE || width != extent_.width ||
      height != extent_.height) {
    const std::expected<bool, std::string> rebuilt = rebuildSwapchain(width, height);
    if (!rebuilt) {
      return std::unexpected(rebuilt.error());
    }
    if (!*rebuilt) {
      return FrameStart::Skipped;
    }
  }

  Frame& frame = frames_[frameSlot_];
  if (auto waited = waitForFrameSlot(); !waited) {
    return std::unexpected(waited.error());
  }

  const VkResult acquired =
    vkAcquireNextImageKHR(device_, swapchain_, std::numeric_limits<std::uint64_t>::max(),
                          frame.imageAcquired, VK_NULL_HANDLE, &imageIndex_);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    swapchainStale_ = true;
    return FrameStart::Skipped;
  }
  if (acquired == VK_SUBOPTIMAL_KHR) {
    swapchainStale_ = true;
  } else if (acquired != VK_SUCCESS) {
    return std::unexpected(failure("vkAcquireNextImageKHR", acquired));
  }

  vkResetFences(device_, 1, &frame.done);
  vkResetCommandPool(device_, frame.commandPool, 0);
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (const VkResult r = vkBeginCommandBuffer(frame.commandBuffer, &beginInfo); r != VK_SUCCESS) {
    return std::unexpected(failure("vkBeginCommandBuffer", r));
  }
  VkClearValue clear{};
  std::copy(clearRgba.begin(), clearRgba.end(), clear.color.float32);
  VkRenderPassBeginInfo passInfo{};
  passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  passInfo.renderPass = renderPass_;
  passInfo.framebuffer = images_[imageIndex_].framebuffer;
  passInfo.renderArea.extent = extent_;
  passInfo.clearValueCount = 1;
  passInfo.pClearValues = &clear;
  vkCmdBeginRenderPass(frame.commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
  recording_ = true;
  return FrameStart::Recording;
}

std::expected<void, std::string> VulkanDevice::endFrame() {
  if (!recording_) {
    return {};
  }
  recording_ = false;
  Frame& frame = frames_[frameSlot_];
  vkCmdEndRenderPass(frame.commandBuffer);
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (readbackRequested_) {
    recordReadback(frame.commandBuffer);
  }
#endif
  if (const VkResult r = vkEndCommandBuffer(frame.commandBuffer); r != VK_SUCCESS) {
    return std::unexpected(failure("vkEndCommandBuffer", r));
  }
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSemaphore renderFinished = images_[imageIndex_].renderFinished;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &frame.imageAcquired;
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &frame.commandBuffer;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &renderFinished;
  if (const VkResult r = vkQueueSubmit(queue_, 1, &submit, frame.done); r != VK_SUCCESS) {
    return std::unexpected(failure("vkQueueSubmit", r));
  }
  frame.serial = releases_.submitted();
#ifdef BITOSCOPE_GUI_TEST_HOOK
  if (submitStamp_) {
    submitStamp_();
  }
  if (readbackRecorded_) {
    readbackFence_ = frame.done;
  }
#endif
  frameSlot_ = (frameSlot_ + 1) % kFramesInFlight;

  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &renderFinished;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &imageIndex_;
  const VkResult presented = vkQueuePresentKHR(queue_, &present);
  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    swapchainStale_ = true;
  } else if (presented != VK_SUCCESS) {
    return std::unexpected(failure("vkQueuePresentKHR", presented));
  }
  return {};
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
std::expected<void, std::string> VulkanDevice::setVsync(bool vsync, std::uint32_t width, std::uint32_t height) {
  if (vsync == vsync_) {
    return {};
  }
  vsync_ = vsync;
  if (swapchain_ == VK_NULL_HANDLE) {
    return {};
  }
  swapchainStale_ = true;
  if (width == 0 || height == 0) {
    return {};
  }
  if (auto rebuilt = rebuildSwapchain(width, height); !rebuilt) {
    return std::unexpected(rebuilt.error());
  }
  return {};
}

std::string VulkanDevice::presentModeName() const {
  return swapchain_ != VK_NULL_HANDLE ? linux_platform::presentModeName(presentMode_) : std::string("none");
}
#endif

std::expected<void, std::string> VulkanDevice::waitForFrameSlot() {
  Frame& frame = frames_[frameSlot_];
  if (const VkResult r = vkWaitForFences(device_, 1, &frame.done, VK_TRUE,
                                         std::numeric_limits<std::uint64_t>::max());
      r != VK_SUCCESS) {
    return std::unexpected(failure("vkWaitForFences", r));
  }
  releases_.completedUpTo(frame.serial);
  return {};
}

void VulkanDevice::deferRelease(std::function<void()> release) {
  // WHY: A signalled fence covers every earlier submission on the queue:
  // DeferredReleases::completedUpTo() relies on it.
  releases_.defer(std::move(release));
}

std::expected<void, std::string> VulkanDevice::submitOnce(
  const std::function<void(VkCommandBuffer)>& record) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = oneTimePool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  if (const VkResult r = vkAllocateCommandBuffers(device_, &allocInfo, &commands); r != VK_SUCCESS) {
    return std::unexpected(failure("vkAllocateCommandBuffers", r));
  }
  VkFence fence = VK_NULL_HANDLE;
  const auto cleanUp = [&] {
    if (fence != VK_NULL_HANDLE) {
      vkDestroyFence(device_, fence, nullptr);
    }
    vkFreeCommandBuffers(device_, oneTimePool_, 1, &commands);
  };
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commands, &beginInfo);
  record(commands);
  if (const VkResult r = vkEndCommandBuffer(commands); r != VK_SUCCESS) {
    cleanUp();
    return std::unexpected(failure("vkEndCommandBuffer", r));
  }
  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  if (const VkResult r = vkCreateFence(device_, &fenceInfo, nullptr, &fence); r != VK_SUCCESS) {
    cleanUp();
    return std::unexpected(failure("vkCreateFence", r));
  }
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commands;
  VkResult r = vkQueueSubmit(queue_, 1, &submit, fence);
  if (r == VK_SUCCESS) {
    r = vkWaitForFences(device_, 1, &fence, VK_TRUE, std::numeric_limits<std::uint64_t>::max());
  }
  cleanUp();
  if (r != VK_SUCCESS) {
    return std::unexpected(failure("one-time submission", r));
  }
  releases_.completedUpTo(releases_.submittedSerial());
  return {};
}

std::optional<std::uint32_t> VulkanDevice::memoryType(std::uint32_t typeBits,
                                                      VkMemoryPropertyFlags properties) const {
  for (std::uint32_t i = 0; i < memoryProperties_.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0 &&
        (memoryProperties_.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  return std::nullopt;
}

void VulkanDevice::waitIdleAndRelease() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  releases_.runAll();
}

VulkanDevice::~VulkanDevice() {
  waitIdleAndRelease();
  if (device_ != VK_NULL_HANDLE) {
#ifdef BITOSCOPE_GUI_TEST_HOOK
    destroyReadbackBuffer();
#endif
    destroySwapchainImages();
    if (swapchain_ != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    for (Frame& frame : frames_) {
      if (frame.imageAcquired != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, frame.imageAcquired, nullptr);
      }
      if (frame.done != VK_NULL_HANDLE) {
        vkDestroyFence(device_, frame.done, nullptr);
      }
      if (frame.commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, frame.commandPool, nullptr);
      }
    }
    if (oneTimePool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, oneTimePool_, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    vkDestroyDevice(device_, nullptr);
  }
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
  }
}

#ifdef BITOSCOPE_GUI_TEST_HOOK
void VulkanDevice::destroyReadbackBuffer() {
  if (readbackBuffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, readbackBuffer_, nullptr);
    readbackBuffer_ = VK_NULL_HANDLE;
  }
  if (readbackMemory_ != VK_NULL_HANDLE) {
    vkFreeMemory(device_, readbackMemory_, nullptr);
    readbackMemory_ = VK_NULL_HANDLE;
  }
  readbackSize_ = 0;
}

void VulkanDevice::recordReadback(VkCommandBuffer commands) {
  readbackRequested_ = false;
  readbackRecorded_ = false;
  readbackError_.clear();
  if (!readable_) {
    readbackError_ = "the swapchain's images cannot be copied from (no TRANSFER_SRC usage on this surface)";
    return;
  }
  const VkDeviceSize size = static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4;
  if (size != readbackSize_) {
    vkDeviceWaitIdle(device_);
    destroyReadbackBuffer();
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (const VkResult r = vkCreateBuffer(device_, &bufferInfo, nullptr, &readbackBuffer_); r != VK_SUCCESS) {
      readbackError_ = failure("vkCreateBuffer(readback)", r);
      readbackBuffer_ = VK_NULL_HANDLE;
      return;
    }
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, readbackBuffer_, &requirements);
    std::optional<std::uint32_t> type = memoryType(
      requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (!type) {
      type = memoryType(requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (!type) {
      readbackError_ = "no host-visible memory type for the readback buffer";
      destroyReadbackBuffer();
      return;
    }
    readbackCoherent_ =
      (memoryProperties_.memoryTypes[*type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = *type;
    if (const VkResult r = vkAllocateMemory(device_, &allocInfo, nullptr, &readbackMemory_); r != VK_SUCCESS) {
      readbackError_ = failure("vkAllocateMemory(readback)", r);
      readbackMemory_ = VK_NULL_HANDLE;
      destroyReadbackBuffer();
      return;
    }
    if (const VkResult r = vkBindBufferMemory(device_, readbackBuffer_, readbackMemory_, 0); r != VK_SUCCESS) {
      readbackError_ = failure("vkBindBufferMemory(readback)", r);
      destroyReadbackBuffer();
      return;
    }
    readbackSize_ = size;
  }

  const VkImage image = images_[imageIndex_].image;
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkImageMemoryBarrier toSource{};
  toSource.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  toSource.srcAccessMask = 0;
  toSource.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  toSource.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  toSource.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toSource.image = image;
  toSource.subresourceRange = range;
  vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toSource);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {extent_.width, extent_.height, 1};
  vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuffer_, 1, &region);

  VkImageMemoryBarrier toPresent = toSource;
  toPresent.srcAccessMask = 0;  // WHY: A read before a layout transition needs an execution dependency only.
  toPresent.dstAccessMask = 0;
  toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkBufferMemoryBarrier toHost{};
  toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toHost.buffer = readbackBuffer_;
  toHost.offset = 0;
  toHost.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &toHost,
                       1, &toPresent);
  readbackRecorded_ = true;
  readbackExtent_ = extent_;
}

std::expected<bitoscope::gui_test_input::RgbaPicture, std::string> VulkanDevice::takeReadback() {
  readbackRequested_ = false;
  if (!readbackRecorded_) {
    std::string error = readbackError_.empty() ? std::string("no frame was drawn (the window has no area, or the "
                                                             "swapchain was being rebuilt); try again")
                                               : readbackError_;
    readbackError_.clear();
    return std::unexpected(std::move(error));
  }
  readbackRecorded_ = false;
  if (const VkResult r = vkWaitForFences(device_, 1, &readbackFence_, VK_TRUE,
                                         std::numeric_limits<std::uint64_t>::max());
      r != VK_SUCCESS) {
    return std::unexpected(failure("vkWaitForFences(readback)", r));
  }
  void* mapped = nullptr;
  if (const VkResult r = vkMapMemory(device_, readbackMemory_, 0, VK_WHOLE_SIZE, 0, &mapped); r != VK_SUCCESS) {
    return std::unexpected(failure("vkMapMemory(readback)", r));
  }
  if (!readbackCoherent_) {
    VkMappedMemoryRange whole{};
    whole.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    whole.memory = readbackMemory_;
    whole.offset = 0;
    whole.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(device_, 1, &whole);
  }
  const auto order = surfaceFormat_.format == VK_FORMAT_B8G8R8A8_UNORM
                       ? bitoscope::gui_test_input::SourcePixelOrder::Bgra
                       : bitoscope::gui_test_input::SourcePixelOrder::Rgba;
  const std::size_t pitch = static_cast<std::size_t>(readbackExtent_.width) * 4;
  auto picture = bitoscope::gui_test_input::opaqueRgbaFromRows(
    readbackExtent_.width, readbackExtent_.height, pitch, order,
    std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(mapped), static_cast<std::size_t>(readbackSize_)));
  vkUnmapMemory(device_, readbackMemory_);
  return picture;
}
#endif

}
