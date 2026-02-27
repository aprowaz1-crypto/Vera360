/**
 * Vera360 — Xenia Edge
 * Vulkan Swap Chain implementation — ANativeWindow / Android
 */

#include "xenia/gpu/vulkan/vulkan_swap_chain.h"
#include "xenia/gpu/vulkan/vulkan_instance.h"
#include "xenia/gpu/vulkan/vulkan_device.h"
#include "xenia/base/logging.h"

#include <android/native_window.h>
#include <algorithm>

namespace xe::gpu::vulkan {

VulkanSwapChain::~VulkanSwapChain() {
  Shutdown();
}

bool VulkanSwapChain::Initialize(VulkanInstance& instance, VulkanDevice& device,
                                  ANativeWindow* window) {
  vk_instance_ = instance.GetHandle();
  device_ = device.GetHandle();
  gpu_ = instance.GetPhysicalDevice();
  present_queue_ = device.GetGraphicsQueue();
  present_family_ = device.GetGraphicsFamily();

  if (!CreateSurface(vk_instance_, window)) return false;

  // Query surface extent from the window
  extent_.width = static_cast<uint32_t>(ANativeWindow_getWidth(window));
  extent_.height = static_cast<uint32_t>(ANativeWindow_getHeight(window));

  if (!CreateSwapChain()) return false;
  if (!CreateImageViews()) return false;
  if (!CreateRenderPass()) return false;
  if (!CreateFramebuffers()) return false;
  if (!CreateSyncObjects()) return false;

  XELOGI("Swap chain created: {}x{}, {} images, format {}",
         extent_.width, extent_.height,
         static_cast<uint32_t>(images_.size()),
         static_cast<int>(format_));
  return true;
}

void VulkanSwapChain::Shutdown() {
  if (device_) vkDeviceWaitIdle(device_);
  DestroySwapChainResources();
  
  if (in_flight_fence_) { vkDestroyFence(device_, in_flight_fence_, nullptr); in_flight_fence_ = VK_NULL_HANDLE; }
  if (render_finished_sem_) { vkDestroySemaphore(device_, render_finished_sem_, nullptr); render_finished_sem_ = VK_NULL_HANDLE; }
  if (image_available_sem_) { vkDestroySemaphore(device_, image_available_sem_, nullptr); image_available_sem_ = VK_NULL_HANDLE; }
  
  if (surface_) {
    vkDestroySurfaceKHR(vk_instance_, surface_, nullptr);
    surface_ = VK_NULL_HANDLE;
  }
}

bool VulkanSwapChain::Recreate(uint32_t width, uint32_t height) {
  vkDeviceWaitIdle(device_);
  DestroySwapChainResources();

  extent_.width = width;
  extent_.height = height;

  if (!CreateSwapChain()) return false;
  if (!CreateImageViews()) return false;
  if (!CreateFramebuffers()) return false;

  XELOGI("Swap chain recreated: {}x{}", width, height);
  return true;
}

bool VulkanSwapChain::AcquireNextImage(uint32_t* image_index) {
  vkWaitForFences(device_, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX);
  vkResetFences(device_, 1, &in_flight_fence_);

  VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                            image_available_sem_, VK_NULL_HANDLE,
                                            image_index);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    return false;  // Caller should recreate
  }
  return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

bool VulkanSwapChain::Present(uint32_t image_index) {
  VkPresentInfoKHR present_info = {};
  present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores    = &render_finished_sem_;
  present_info.swapchainCount     = 1;
  present_info.pSwapchains        = &swapchain_;
  present_info.pImageIndices      = &image_index;

  VkResult result = vkQueuePresentKHR(present_queue_, &present_info);
  return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

// ── Private ─────────────────────────────────────────────────────────────────

bool VulkanSwapChain::CreateSurface(VkInstance instance, ANativeWindow* window) {
  VkAndroidSurfaceCreateInfoKHR info = {};
  info.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  info.window = window;

  VkResult result = vkCreateAndroidSurfaceKHR(instance, &info, nullptr, &surface_);
  if (result != VK_SUCCESS) {
    XELOGE("vkCreateAndroidSurfaceKHR failed: {}", static_cast<int>(result));
    return false;
  }

  // Verify surface support
  VkBool32 supported = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, present_family_, surface_, &supported);
  if (!supported) {
    XELOGE("Surface not supported on graphics queue family");
    return false;
  }

  return true;
}

bool VulkanSwapChain::CreateSwapChain() {
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps);

  // Pick format
  uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &format_count, formats.data());

  format_ = VK_FORMAT_R8G8B8A8_UNORM;
  VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  for (auto& f : formats) {
    if (f.format == VK_FORMAT_R8G8B8A8_SRGB) {
      format_ = f.format;
      color_space = f.colorSpace;
      break;
    }
  }

  // Clamp extent
  extent_.width  = std::clamp(extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  extent_.height = std::clamp(extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) {
    image_count = std::min(image_count, caps.maxImageCount);
  }

  VkSwapchainCreateInfoKHR info = {};
  info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface          = surface_;
  info.minImageCount    = image_count;
  info.imageFormat      = format_;
  info.imageColorSpace  = color_space;
  info.imageExtent      = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform     = caps.currentTransform;
  info.compositeAlpha   = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
  info.presentMode      = VK_PRESENT_MODE_MAILBOX_KHR;  // Triple buffer preferred
  info.clipped          = VK_TRUE;
  info.oldSwapchain     = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
    // Fallback to FIFO  
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
      XELOGE("Failed to create swap chain");
      return false;
    }
  }

  uint32_t count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
  images_.resize(count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &count, images_.data());

  return true;
}

bool VulkanSwapChain::CreateImageViews() {
  image_views_.resize(images_.size());
  for (size_t i = 0; i < images_.size(); ++i) {
    VkImageViewCreateInfo info = {};
    info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image    = images_[i];
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format   = format_;
    info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel   = 0;
    info.subresourceRange.levelCount     = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(device_, &info, nullptr, &image_views_[i]) != VK_SUCCESS) {
      XELOGE("Failed to create image view {}", i);
      return false;
    }
  }
  return true;
}

bool VulkanSwapChain::CreateRenderPass() {
  VkAttachmentDescription color_attachment = {};
  color_attachment.format         = format_;
  color_attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_ref = {};
  color_ref.attachment = 0;
  color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments    = &color_ref;

  VkSubpassDependency dep = {};
  dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass    = 0;
  dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = 0;
  dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info = {};
  info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments    = &color_attachment;
  info.subpassCount    = 1;
  info.pSubpasses      = &subpass;
  info.dependencyCount = 1;
  info.pDependencies   = &dep;

  if (vkCreateRenderPass(device_, &info, nullptr, &render_pass_) != VK_SUCCESS) {
    XELOGE("Failed to create render pass");
    return false;
  }
  return true;
}

bool VulkanSwapChain::CreateFramebuffers() {
  framebuffers_.resize(image_views_.size());
  for (size_t i = 0; i < image_views_.size(); ++i) {
    VkFramebufferCreateInfo info = {};
    info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass      = render_pass_;
    info.attachmentCount = 1;
    info.pAttachments    = &image_views_[i];
    info.width           = extent_.width;
    info.height          = extent_.height;
    info.layers          = 1;

    if (vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
      XELOGE("Failed to create framebuffer {}", i);
      return false;
    }
  }
  return true;
}

bool VulkanSwapChain::CreateSyncObjects() {
  VkSemaphoreCreateInfo sem_info = {};
  sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateSemaphore(device_, &sem_info, nullptr, &image_available_sem_) != VK_SUCCESS ||
      vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_sem_) != VK_SUCCESS ||
      vkCreateFence(device_, &fence_info, nullptr, &in_flight_fence_) != VK_SUCCESS) {
    XELOGE("Failed to create sync objects");
    return false;
  }
  return true;
}

void VulkanSwapChain::DestroySwapChainResources() {
  for (auto fb : framebuffers_) {
    if (fb) vkDestroyFramebuffer(device_, fb, nullptr);
  }
  framebuffers_.clear();

  if (render_pass_) { vkDestroyRenderPass(device_, render_pass_, nullptr); render_pass_ = VK_NULL_HANDLE; }

  for (auto iv : image_views_) {
    if (iv) vkDestroyImageView(device_, iv, nullptr);
  }
  image_views_.clear();
  images_.clear();

  if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

}  // namespace xe::gpu::vulkan
