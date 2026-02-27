/**
 * Vera360 — Xenia Edge
 * Vulkan Logical Device implementation
 */

#include "xenia/gpu/vulkan/vulkan_device.h"
#include "xenia/gpu/vulkan/vulkan_instance.h"
#include "xenia/base/logging.h"

#include <cstring>
#include <functional>
#include <vector>

namespace xe::gpu::vulkan {

VulkanDevice::~VulkanDevice() {
  Shutdown();
}

bool VulkanDevice::Initialize(VulkanInstance& instance) {
  gpu_ = instance.GetPhysicalDevice();
  mem_props_ = instance.GetMemoryProperties();

  if (!FindQueueFamilies(gpu_)) return false;
  if (!CreateLogicalDevice(gpu_)) return false;
  if (!CreateCommandPool()) return false;
  if (!CreateDescriptorPool()) return false;

  XELOGI("Vulkan device created (graphics={}, compute={}, transfer={})",
         graphics_family_, compute_family_, transfer_family_);
  return true;
}

void VulkanDevice::Shutdown() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
}

bool VulkanDevice::FindQueueFamilies(VkPhysicalDevice gpu) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());

  for (uint32_t i = 0; i < count; ++i) {
    auto& qf = families[i];
    if ((qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphics_family_ == UINT32_MAX) {
      graphics_family_ = i;
    }
    if ((qf.queueFlags & VK_QUEUE_COMPUTE_BIT) && 
        !(qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) && compute_family_ == UINT32_MAX) {
      compute_family_ = i;
    }
    if ((qf.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
        !(qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
        !(qf.queueFlags & VK_QUEUE_COMPUTE_BIT) && transfer_family_ == UINT32_MAX) {
      transfer_family_ = i;
    }
  }

  // Fallback: share families
  if (compute_family_ == UINT32_MAX) compute_family_ = graphics_family_;
  if (transfer_family_ == UINT32_MAX) transfer_family_ = graphics_family_;

  return graphics_family_ != UINT32_MAX;
}

bool VulkanDevice::CreateLogicalDevice(VkPhysicalDevice gpu) {
  float priority = 1.0f;
  
  // Collect unique queue families
  std::vector<VkDeviceQueueCreateInfo> queue_infos;
  auto addQueue = [&](uint32_t family) {
    for (auto& qi : queue_infos) {
      if (qi.queueFamilyIndex == family) return;
    }
    VkDeviceQueueCreateInfo qi = {};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    queue_infos.push_back(qi);
  };

  addQueue(graphics_family_);
  addQueue(compute_family_);
  addQueue(transfer_family_);

  // Required extensions
  const char* extensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  };

  // Device features
  VkPhysicalDeviceFeatures features = {};
  features.samplerAnisotropy = VK_TRUE;
  features.shaderInt16 = VK_TRUE;
  features.fragmentStoresAndAtomics = VK_TRUE;
  features.vertexPipelineStoresAndAtomics = VK_TRUE;

  VkDeviceCreateInfo create_info = {};
  create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.queueCreateInfoCount    = static_cast<uint32_t>(queue_infos.size());
  create_info.pQueueCreateInfos       = queue_infos.data();
  create_info.enabledExtensionCount   = 1;
  create_info.ppEnabledExtensionNames = extensions;
  create_info.pEnabledFeatures        = &features;

  VkResult result = vkCreateDevice(gpu, &create_info, nullptr, &device_);
  if (result != VK_SUCCESS) {
    XELOGE("vkCreateDevice failed: {}", static_cast<int>(result));
    return false;
  }

  vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
  vkGetDeviceQueue(device_, compute_family_, 0, &compute_queue_);
  vkGetDeviceQueue(device_, transfer_family_, 0, &transfer_queue_);

  return true;
}

bool VulkanDevice::CreateCommandPool() {
  VkCommandPoolCreateInfo info = {};
  info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  info.queueFamilyIndex = graphics_family_;

  if (vkCreateCommandPool(device_, &info, nullptr, &command_pool_) != VK_SUCCESS) {
    XELOGE("Failed to create command pool");
    return false;
  }
  return true;
}

bool VulkanDevice::CreateDescriptorPool() {
  VkDescriptorPoolSize sizes[] = {
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1024 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1024 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          512  },
  };

  VkDescriptorPoolCreateInfo info = {};
  info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  info.maxSets       = 4096;
  info.poolSizeCount = 4;
  info.pPoolSizes    = sizes;

  if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptor_pool_) != VK_SUCCESS) {
    XELOGE("Failed to create descriptor pool");
    return false;
  }
  return true;
}

uint32_t VulkanDevice::FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags props) const {
  for (uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
    if ((type_filter & (1 << i)) &&
        (mem_props_.memoryTypes[i].propertyFlags & props) == props) {
      return i;
    }
  }
  return UINT32_MAX;
}

void VulkanDevice::ImmediateSubmit(std::function<void(VkCommandBuffer)> func) {
  VkCommandBufferAllocateInfo alloc_info = {};
  alloc_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandPool        = command_pool_;
  alloc_info.commandBufferCount = 1;

  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(device_, &alloc_info, &cmd);

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin_info);

  func(cmd);

  vkEndCommandBuffer(cmd);

  VkSubmitInfo submit = {};
  submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers    = &cmd;

  vkQueueSubmit(graphics_queue_, 1, &submit, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphics_queue_);

  vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
}

}  // namespace xe::gpu::vulkan
