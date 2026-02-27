/**
 * Vera360 — Xenia Edge
 * Vulkan Swap Chain — Android ANativeWindow based
 */
#pragma once

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

struct ANativeWindow;

namespace xe::gpu::vulkan {

class VulkanInstance;
class VulkanDevice;

class VulkanSwapChain {
 public:
  VulkanSwapChain() = default;
  ~VulkanSwapChain();

  bool Initialize(VulkanInstance& instance, VulkanDevice& device, ANativeWindow* window);
  void Shutdown();

  /// Recreate after window resize
  bool Recreate(uint32_t width, uint32_t height);

  /// Begin frame: acquire next image, return image index
  bool AcquireNextImage(uint32_t* image_index);

  /// Present the rendered image
  bool Present(uint32_t image_index);

  VkRenderPass GetRenderPass() const { return render_pass_; }
  VkFramebuffer GetFramebuffer(uint32_t index) const { return framebuffers_[index]; }
  VkExtent2D GetExtent() const { return extent_; }
  VkFormat GetFormat() const { return format_; }
  uint32_t GetImageCount() const { return static_cast<uint32_t>(images_.size()); }

  VkSemaphore GetImageAvailableSemaphore() const { return image_available_sem_; }
  VkSemaphore GetRenderFinishedSemaphore() const { return render_finished_sem_; }
  VkFence GetInFlightFence() const { return in_flight_fence_; }

 private:
  bool CreateSurface(VkInstance instance, ANativeWindow* window);
  bool CreateSwapChain();
  bool CreateImageViews();
  bool CreateRenderPass();
  bool CreateFramebuffers();
  bool CreateSyncObjects();
  void DestroySwapChainResources();

  VkInstance vk_instance_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
  VkQueue present_queue_ = VK_NULL_HANDLE;
  uint32_t present_family_ = 0;

  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_R8G8B8A8_UNORM;
  VkExtent2D extent_ = {0, 0};

  std::vector<VkImage> images_;
  std::vector<VkImageView> image_views_;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;

  VkSemaphore image_available_sem_ = VK_NULL_HANDLE;
  VkSemaphore render_finished_sem_ = VK_NULL_HANDLE;
  VkFence in_flight_fence_ = VK_NULL_HANDLE;
};

}  // namespace xe::gpu::vulkan
