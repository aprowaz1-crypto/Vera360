/**
 * Vera360 — Xenia Edge
 * Vulkan Instance + Android Surface creation
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

namespace xe::gpu::vulkan {

class VulkanInstance {
 public:
  VulkanInstance() = default;
  ~VulkanInstance();

  bool Initialize();
  void Shutdown();

  VkInstance GetHandle() const { return instance_; }
  const std::string& GetGpuName() const { return gpu_name_; }
  bool IsAvailable() const { return instance_ != VK_NULL_HANDLE; }

  VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
  const VkPhysicalDeviceProperties& GetDeviceProperties() const { return device_props_; }
  const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return mem_props_; }

 private:
  bool CreateInstance();
  bool PickPhysicalDevice();
  bool CheckValidationLayerSupport();

  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties device_props_ = {};
  VkPhysicalDeviceMemoryProperties mem_props_ = {};
  std::string gpu_name_;
  
  std::vector<const char*> required_extensions_;
  bool validation_enabled_ = false;
};

}  // namespace xe::gpu::vulkan
