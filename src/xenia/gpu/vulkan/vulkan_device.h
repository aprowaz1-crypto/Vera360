/**
 * Vera360 — Xenia Edge
 * Vulkan Logical Device wrapper
 */
#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <functional>
#include <vector>
#include <cstdint>

namespace xe::gpu::vulkan {

class VulkanInstance;

class VulkanDevice {
 public:
  VulkanDevice() = default;
  ~VulkanDevice();

  bool Initialize(VulkanInstance& instance);
  void Shutdown();

  VkDevice GetHandle() const { return device_; }
  VkQueue GetGraphicsQueue() const { return graphics_queue_; }
  VkQueue GetComputeQueue() const { return compute_queue_; }
  VkQueue GetTransferQueue() const { return transfer_queue_; }
  
  uint32_t GetGraphicsFamily() const { return graphics_family_; }
  uint32_t GetComputeFamily() const { return compute_family_; }
  uint32_t GetTransferFamily() const { return transfer_family_; }

  VkCommandPool GetCommandPool() const { return command_pool_; }
  VkDescriptorPool GetDescriptorPool() const { return descriptor_pool_; }

  /// Find memory type index for given requirements
  uint32_t FindMemoryType(uint32_t type_filter, VkMemoryPropertyFlags props) const;

  /// Allocate a one-shot command buffer, execute, and wait
  void ImmediateSubmit(std::function<void(VkCommandBuffer)> func);

 private:
  bool FindQueueFamilies(VkPhysicalDevice gpu);
  bool CreateLogicalDevice(VkPhysicalDevice gpu);
  bool CreateCommandPool();
  bool CreateDescriptorPool();

  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;

  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkQueue compute_queue_ = VK_NULL_HANDLE;
  VkQueue transfer_queue_ = VK_NULL_HANDLE;

  uint32_t graphics_family_ = UINT32_MAX;
  uint32_t compute_family_ = UINT32_MAX;
  uint32_t transfer_family_ = UINT32_MAX;

  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;

  VkPhysicalDeviceMemoryProperties mem_props_ = {};
};

}  // namespace xe::gpu::vulkan
