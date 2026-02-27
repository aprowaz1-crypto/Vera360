/**
 * Vera360 — Xenia Edge
 * Vulkan Instance implementation
 */

#include "xenia/gpu/vulkan/vulkan_instance.h"
#include "xenia/base/logging.h"

#include <cstring>

namespace xe::gpu::vulkan {

namespace {

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    XELOGW("[Vulkan] {}", data->pMessage);
  }
  return VK_FALSE;
}

}  // namespace

VulkanInstance::~VulkanInstance() {
  Shutdown();
}

bool VulkanInstance::Initialize() {
  if (!CreateInstance()) return false;
  if (!PickPhysicalDevice()) return false;
  
  XELOGI("Vulkan initialized — GPU: {}", gpu_name_);
  XELOGI("  API: {}.{}.{}",
      VK_VERSION_MAJOR(device_props_.apiVersion),
      VK_VERSION_MINOR(device_props_.apiVersion),
      VK_VERSION_PATCH(device_props_.apiVersion));
  XELOGI("  Driver: 0x{:08X}", device_props_.driverVersion);
  
  return true;
}

void VulkanInstance::Shutdown() {
  if (debug_messenger_ != VK_NULL_HANDLE) {
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) func(instance_, debug_messenger_, nullptr);
    debug_messenger_ = VK_NULL_HANDLE;
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

bool VulkanInstance::CreateInstance() {
  VkApplicationInfo app_info = {};
  app_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName   = "Vera360 — Xenia Edge";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  app_info.pEngineName        = "XeniaEdge";
  app_info.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
  app_info.apiVersion         = VK_API_VERSION_1_1;

  // Required extensions for Android  
  required_extensions_ = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
  };

  // Optional: debug utils
  validation_enabled_ = CheckValidationLayerSupport();
  if (validation_enabled_) {
    required_extensions_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  const char* validation_layer = "VK_LAYER_KHRONOS_validation";

  VkInstanceCreateInfo create_info = {};
  create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo        = &app_info;
  create_info.enabledExtensionCount   = static_cast<uint32_t>(required_extensions_.size());
  create_info.ppEnabledExtensionNames = required_extensions_.data();
  
  if (validation_enabled_) {
    create_info.enabledLayerCount   = 1;
    create_info.ppEnabledLayerNames = &validation_layer;
  }

  VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
  if (result != VK_SUCCESS) {
    XELOGE("vkCreateInstance failed: {}", static_cast<int>(result));
    return false;
  }

  // Setup debug messenger
  if (validation_enabled_) {
    VkDebugUtilsMessengerCreateInfoEXT dbg_info = {};
    dbg_info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    dbg_info.pfnUserCallback = DebugCallback;

    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (func) func(instance_, &dbg_info, nullptr, &debug_messenger_);
  }

  return true;
}

bool VulkanInstance::PickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) {
    XELOGE("No Vulkan physical devices found");
    return false;
  }

  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());

  // Prefer discrete GPU, then integrated
  physical_device_ = devices[0];
  for (auto& dev : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      physical_device_ = dev;
      break;
    }
  }

  vkGetPhysicalDeviceProperties(physical_device_, &device_props_);
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props_);
  gpu_name_ = device_props_.deviceName;

  return true;
}

bool VulkanInstance::CheckValidationLayerSupport() {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());

  for (auto& layer : layers) {
    if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace xe::gpu::vulkan
