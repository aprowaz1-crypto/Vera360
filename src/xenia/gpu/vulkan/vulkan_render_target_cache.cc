/**
 * Vera360 — Xenia Edge
 * Vulkan Render Target Cache — manages VkImage render targets for Xenos EDRAM
 *
 * The Xenos GPU uses a 10MB EDRAM for render targets. Multiple render targets
 * and depth buffers share this memory with configurable tile layout.
 * This cache maps Xenos EDRAM configurations to VkImage/VkImageView objects.
 */

#include "xenia/base/logging.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace xe::gpu::vulkan {

struct RenderTargetKey {
  uint32_t edram_base;
  uint32_t format;
  uint32_t pitch;
  uint32_t height;
  bool is_depth;

  bool operator==(const RenderTargetKey& o) const {
    return edram_base == o.edram_base && format == o.format &&
           pitch == o.pitch && height == o.height && is_depth == o.is_depth;
  }
};

struct RenderTargetKeyHash {
  size_t operator()(const RenderTargetKey& k) const {
    size_t h = 0;
    h ^= std::hash<uint32_t>{}(k.edram_base) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.format) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.pitch) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(k.is_depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct RenderTarget {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint32_t width = 0;
  uint32_t height = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
};

class VulkanRenderTargetCache {
 public:
  bool Initialize(VkDevice device, VkPhysicalDevice physical_device) {
    device_ = device;
    physical_device_ = physical_device;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props_);
    XELOGI("Vulkan render target cache initialized");
    return true;
  }

  void Shutdown() {
    for (auto& [key, rt] : cache_) {
      DestroyRT(rt);
    }
    cache_.clear();
  }

  RenderTarget* GetOrCreate(const RenderTargetKey& key) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;

    RenderTarget rt;
    rt.width = key.pitch;
    rt.height = key.height;
    rt.format = key.is_depth ? VK_FORMAT_D32_SFLOAT_S8_UINT
                             : MapColorFormat(key.format);

    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = rt.format;
    ci.extent = {rt.width, rt.height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = key.is_depth
        ? (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        : (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    if (vkCreateImage(device_, &ci, nullptr, &rt.image) != VK_SUCCESS) {
      XELOGW("Failed to create render target image");
      return nullptr;
    }

    // Allocate and bind memory
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device_, rt.image, &mem_req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex = FindMemoryType(mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device_, &alloc, nullptr, &rt.memory);
    vkBindImageMemory(device_, rt.image, rt.memory, 0);

    // Create image view
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = rt.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = rt.format;
    view_ci.subresourceRange.aspectMask = key.is_depth
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    vkCreateImageView(device_, &view_ci, nullptr, &rt.view);

    cache_[key] = rt;
    XELOGD("Created render target: {}x{} fmt={}", rt.width, rt.height, static_cast<int>(rt.format));
    return &cache_[key];
  }

 private:
  void DestroyRT(RenderTarget& rt) {
    if (rt.view) vkDestroyImageView(device_, rt.view, nullptr);
    if (rt.image) vkDestroyImage(device_, rt.image, nullptr);
    if (rt.memory) vkFreeMemory(device_, rt.memory, nullptr);
  }

  uint32_t FindMemoryType(uint32_t filter, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
      if ((filter & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    }
    return 0;
  }

  static VkFormat MapColorFormat(uint32_t xenos_fmt) {
    switch (xenos_fmt) {
      case 0:  return VK_FORMAT_R8G8B8A8_UNORM;
      case 1:  return VK_FORMAT_R8G8B8A8_UNORM;
      case 6:  return VK_FORMAT_R16G16B16A16_SFLOAT;
      case 12: return VK_FORMAT_R32G32B32A32_SFLOAT;
      case 14: return VK_FORMAT_R32_SFLOAT;
      default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
  }

  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mem_props_{};
  std::unordered_map<RenderTargetKey, RenderTarget, RenderTargetKeyHash> cache_;
};

}  // namespace xe::gpu::vulkan
