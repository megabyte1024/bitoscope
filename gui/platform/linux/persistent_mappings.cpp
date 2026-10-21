/*
 * SPDX-License-Identifier: MIT
 */

#include "persistent_mappings.h"

#include <algorithm>
#include <iterator>
#include <string_view>

namespace bitoscope::gui::linux_platform {

PersistentMappings::PersistentMappings(MemoryMappingCalls driver) : driver_(driver) {}

VkResult PersistentMappings::map(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                 [[maybe_unused]] VkDeviceSize size, VkMemoryMapFlags flags, void** data) {
  auto it = std::ranges::find(held_, memory, &Held::memory);
  if (it == held_.end()) {
    void* base = nullptr;
    // WHY: The whole memory is mapped: a later map of the same memory may ask for any of its ranges.
    if (const VkResult r = driver_.mapMemory(device, memory, 0, VK_WHOLE_SIZE, flags, &base); r != VK_SUCCESS) {
      return r;
    }
    held_.push_back({memory, static_cast<std::byte*>(base)});
    it = std::prev(held_.end());
  }
  *data = it->base + offset;
  return VK_SUCCESS;
}

void PersistentMappings::unmap([[maybe_unused]] VkDevice device, [[maybe_unused]] VkDeviceMemory memory) {}

void PersistentMappings::free(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator) {
  std::erase_if(held_, [memory](const Held& h) { return h.memory == memory; });
  driver_.freeMemory(device, memory, allocator);
}

std::size_t PersistentMappings::held() const { return held_.size(); }

namespace {

PersistentMappings* gBackendMappings = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL backendMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                                                VkDeviceSize size, VkMemoryMapFlags flags, void** data) {
  return gBackendMappings->map(device, memory, offset, size, flags, data);
}

VKAPI_ATTR void VKAPI_CALL backendUnmapMemory(VkDevice device, VkDeviceMemory memory) {
  gBackendMappings->unmap(device, memory);
}

VKAPI_ATTR void VKAPI_CALL backendFreeMemory(VkDevice device, VkDeviceMemory memory,
                                             const VkAllocationCallbacks* allocator) {
  gBackendMappings->free(device, memory, allocator);
}

}

PFN_vkVoidFunction loadBackendFunction(const char* name, void* loader) {
  const auto& l = *static_cast<const BackendFunctionLoader*>(loader);
  const std::string_view n(name);
  if (n == "vkMapMemory" || n == "vkUnmapMemory" || n == "vkFreeMemory") {
    gBackendMappings = l.mappings;
    if (n == "vkMapMemory") {
      return reinterpret_cast<PFN_vkVoidFunction>(&backendMapMemory);
    }
    if (n == "vkUnmapMemory") {
      return reinterpret_cast<PFN_vkVoidFunction>(&backendUnmapMemory);
    }
    return reinterpret_cast<PFN_vkVoidFunction>(&backendFreeMemory);
  }
  return l.resolve(name, l.resolveUserData);
}

}
