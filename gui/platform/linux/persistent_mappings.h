/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <vector>

#include <vulkan/vulkan.h>

namespace bitoscope::gui::linux_platform {

struct MemoryMappingCalls {
  PFN_vkMapMemory mapMemory = nullptr;
  PFN_vkUnmapMemory unmapMemory = nullptr;
  PFN_vkFreeMemory freeMemory = nullptr;
};

class PersistentMappings {
 public:
  explicit PersistentMappings(MemoryMappingCalls driver);

  VkResult map(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size,
               VkMemoryMapFlags flags, void** data);
  void unmap(VkDevice device, VkDeviceMemory memory);
  void free(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator);

  std::size_t held() const;

 private:
  struct Held {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::byte* base = nullptr;
  };

  MemoryMappingCalls driver_;
  std::vector<Held> held_;
};

using VulkanFunctionResolver = PFN_vkVoidFunction (*)(const char* name, void* userData);

struct BackendFunctionLoader {
  PersistentMappings* mappings = nullptr;
  VulkanFunctionResolver resolve = nullptr;
  void* resolveUserData = nullptr;
};

PFN_vkVoidFunction loadBackendFunction(const char* name, void* loader);

}
