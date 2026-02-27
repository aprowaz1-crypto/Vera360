/**
 * Vera360 — Xenia Edge
 * Vulkan Texture Cache — manages VkImage objects for Xenos textures
 *
 * The Xenos GPU defines textures via fetch constants (SQ_TEX_RESOURCE).
 * Each texture fetch constant describes: base address, size, format, tiling.
 * This cache resolves guest GPU textures to Vulkan images with lazy upload.
 */

#include "xenia/base/logging.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>

namespace xe::gpu::vulkan {

/// Xenos texture formats (subset)
enum class XenosTextureFormat : uint32_t {
  k_8          = 2,
  k_8_8        = 3,
  k_8_8_8_8    = 6,
  k_DXT1       = 12,
  k_DXT2_3     = 13,
  k_DXT4_5     = 14,
  k_16_16_16_16_FLOAT = 26,
  k_32_FLOAT   = 36,
};

struct TextureKey {
  uint32_t base_address;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t mip_count;
  bool tiled;

  bool operator==(const TextureKey& o) const {
    return base_address == o.base_address && width == o.width &&
           height == o.height && format == o.format &&
           mip_count == o.mip_count && tiled == o.tiled;
  }
};

struct TextureKeyHash {
  size_t operator()(const TextureKey& k) const {
    size_t h = std::hash<uint32_t>{}(k.base_address);
    h ^= std::hash<uint32_t>{}(k.width) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.height) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(k.format) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

struct CachedTexture {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  uint32_t width = 0;
  uint32_t height = 0;
  bool uploaded = false;
};

class VulkanTextureCache {
 public:
  bool Initialize(VkDevice device, VkPhysicalDevice physical_device,
                  VkCommandPool cmd_pool, VkQueue queue) {
    device_ = device;
    physical_device_ = physical_device;
    cmd_pool_ = cmd_pool;
    queue_ = queue;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props_);
    CreateDefaultSampler();
    XELOGI("Vulkan texture cache initialized");
    return true;
  }

  void Shutdown() {
    for (auto& [key, tex] : cache_) {
      DestroyTexture(tex);
    }
    cache_.clear();
    if (default_sampler_) {
      vkDestroySampler(device_, default_sampler_, nullptr);
      default_sampler_ = VK_NULL_HANDLE;
    }
  }

  CachedTexture* GetOrCreate(const TextureKey& key, const uint8_t* guest_data) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;

    CachedTexture tex;
    tex.width = key.width;
    tex.height = key.height;

    VkFormat vk_fmt = MapFormat(static_cast<XenosTextureFormat>(key.format));

    // Create image
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = vk_fmt;
    ci.extent = {key.width, key.height, 1};
    ci.mipLevels = key.mip_count > 0 ? key.mip_count : 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if (vkCreateImage(device_, &ci, nullptr, &tex.image) != VK_SUCCESS) {
      XELOGW("Failed to create texture image {}x{}", key.width, key.height);
      return nullptr;
    }

    // Allocate memory
    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device_, tex.image, &mem_req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex = FindMemoryType(mem_req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &alloc, nullptr, &tex.memory);
    vkBindImageMemory(device_, tex.image, tex.memory, 0);

    // Create image view
    VkImageViewCreateInfo view_ci{};
    view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_ci.image = tex.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = vk_fmt;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = ci.mipLevels;
    view_ci.subresourceRange.layerCount = 1;
    vkCreateImageView(device_, &view_ci, nullptr, &tex.view);

    tex.sampler = default_sampler_;

    // Upload pixel data if provided
    if (guest_data) {
      UploadTextureData(tex, key, vk_fmt, guest_data);
    }

    cache_[key] = tex;
    return &cache_[key];
  }

  VkSampler default_sampler() const { return default_sampler_; }

 private:
  void CreateDefaultSampler() {
    VkSamplerCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter = VK_FILTER_LINEAR;
    ci.minFilter = VK_FILTER_LINEAR;
    ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    ci.maxLod = 16.0f;
    ci.maxAnisotropy = 1.0f;
    vkCreateSampler(device_, &ci, nullptr, &default_sampler_);
  }

  void UploadTextureData(CachedTexture& tex, const TextureKey& key,
                          VkFormat format, const uint8_t* data) {
    uint32_t bpp = FormatBPP(format);
    VkDeviceSize size = static_cast<VkDeviceSize>(key.width) * key.height * bpp / 8;

    // Create staging buffer
    VkBuffer staging;
    VkDeviceMemory staging_mem;

    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    vkCreateBuffer(device_, &buf_ci, nullptr, &staging);

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device_, staging, &mem_req);

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex = FindMemoryType(mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &alloc, nullptr, &staging_mem);
    vkBindBufferMemory(device_, staging, staging_mem, 0);

    // Copy data to staging
    void* mapped;
    vkMapMemory(device_, staging_mem, 0, size, 0, &mapped);
    memcpy(mapped, data, static_cast<size_t>(size));
    vkUnmapMemory(device_, staging_mem);

    // Record copy commands
    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cmd_ci{};
    cmd_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ci.commandPool = cmd_pool_;
    cmd_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ci.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &cmd_ci, &cmd);

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);

    // Transition image to TRANSFER_DST
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = tex.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {key.width, key.height, 1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // Transition to SHADER_READ_ONLY
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                          0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue_);

    vkFreeCommandBuffers(device_, cmd_pool_, 1, &cmd);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, staging_mem, nullptr);

    tex.uploaded = true;
  }

  void DestroyTexture(CachedTexture& tex) {
    if (tex.view) vkDestroyImageView(device_, tex.view, nullptr);
    if (tex.image) vkDestroyImage(device_, tex.image, nullptr);
    if (tex.memory) vkFreeMemory(device_, tex.memory, nullptr);
  }

  uint32_t FindMemoryType(uint32_t filter, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < mem_props_.memoryTypeCount; ++i) {
      if ((filter & (1u << i)) && (mem_props_.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    }
    return 0;
  }

  static VkFormat MapFormat(XenosTextureFormat fmt) {
    switch (fmt) {
      case XenosTextureFormat::k_8:        return VK_FORMAT_R8_UNORM;
      case XenosTextureFormat::k_8_8:      return VK_FORMAT_R8G8_UNORM;
      case XenosTextureFormat::k_8_8_8_8:  return VK_FORMAT_R8G8B8A8_UNORM;
      case XenosTextureFormat::k_DXT1:     return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
      case XenosTextureFormat::k_DXT2_3:   return VK_FORMAT_BC2_UNORM_BLOCK;
      case XenosTextureFormat::k_DXT4_5:   return VK_FORMAT_BC3_UNORM_BLOCK;
      case XenosTextureFormat::k_16_16_16_16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
      case XenosTextureFormat::k_32_FLOAT: return VK_FORMAT_R32_SFLOAT;
      default: return VK_FORMAT_R8G8B8A8_UNORM;
    }
  }

  static uint32_t FormatBPP(VkFormat fmt) {
    switch (fmt) {
      case VK_FORMAT_R8_UNORM:              return 8;
      case VK_FORMAT_R8G8_UNORM:            return 16;
      case VK_FORMAT_R8G8B8A8_UNORM:        return 32;
      case VK_FORMAT_R16G16B16A16_SFLOAT:   return 64;
      case VK_FORMAT_R32_SFLOAT:            return 32;
      default: return 32;
    }
  }

  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mem_props_{};
  VkSampler default_sampler_ = VK_NULL_HANDLE;
  std::unordered_map<TextureKey, CachedTexture, TextureKeyHash> cache_;
};

}  // namespace xe::gpu::vulkan
