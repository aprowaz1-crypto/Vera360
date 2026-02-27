/**
 * Vera360 — Xenia Edge
 * Vulkan Command Processor — translates Xenos draw calls to Vulkan commands
 *
 * Sits between the GpuCommandProcessor (which parses PM4 packets) and the
 * actual Vulkan rendering pipeline. Manages command buffer recording,
 * render pass lifecycle, descriptor sets, and draw submission.
 */

#include "xenia/base/logging.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

namespace xe::gpu::vulkan {

class VulkanCommandProcessor {
 public:
  bool Initialize(VkDevice device, VkQueue graphics_queue,
                  uint32_t queue_family_index) {
    device_ = device;
    queue_ = graphics_queue;

    // Create command pool
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = queue_family_index;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device_, &pool_ci, nullptr, &cmd_pool_) != VK_SUCCESS) {
      XELOGE("Failed to create Vulkan command pool");
      return false;
    }

    // Allocate primary command buffer
    VkCommandBufferAllocateInfo alloc_ci{};
    alloc_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_ci.commandPool = cmd_pool_;
    alloc_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_ci.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &alloc_ci, &cmd_buffer_);

    // Create fence for synchronization
    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device_, &fence_ci, nullptr, &frame_fence_);

    XELOGI("Vulkan command processor initialized");
    return true;
  }

  void Shutdown() {
    if (device_ == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device_);
    if (frame_fence_) vkDestroyFence(device_, frame_fence_, nullptr);
    if (cmd_pool_) vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    frame_fence_ = VK_NULL_HANDLE;
    cmd_pool_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
  }

  // Begin recording a new frame's commands
  void BeginFrame() {
    vkWaitForFences(device_, 1, &frame_fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &frame_fence_);

    VkCommandBufferBeginInfo begin_ci{};
    begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_buffer_, &begin_ci);

    recording_ = true;
  }

  // Begin a render pass for the current framebuffer
  void BeginRenderPass(VkRenderPass render_pass, VkFramebuffer framebuffer,
                       uint32_t width, uint32_t height) {
    VkClearValue clear_values[2] = {};
    clear_values[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear_values[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rp_begin{};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = render_pass;
    rp_begin.framebuffer = framebuffer;
    rp_begin.renderArea.extent = {width, height};
    rp_begin.clearValueCount = 2;
    rp_begin.pClearValues = clear_values;

    vkCmdBeginRenderPass(cmd_buffer_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    // Set default viewport and scissor
    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buffer_, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd_buffer_, 0, 1, &scissor);

    in_render_pass_ = true;
  }

  // Submit a Xenos draw call translated to Vulkan
  void SubmitDraw(VkPipeline pipeline, VkPipelineLayout layout,
                  VkBuffer vertex_buffer, uint32_t vertex_count,
                  VkBuffer index_buffer, uint32_t index_count,
                  VkDescriptorSet* desc_sets, uint32_t desc_set_count) {
    if (!recording_ || !in_render_pass_) return;

    vkCmdBindPipeline(cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    if (desc_set_count > 0 && desc_sets) {
      vkCmdBindDescriptorSets(cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                               layout, 0, desc_set_count, desc_sets, 0, nullptr);
    }

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd_buffer_, 0, 1, &vertex_buffer, &offset);

    if (index_buffer != VK_NULL_HANDLE && index_count > 0) {
      vkCmdBindIndexBuffer(cmd_buffer_, index_buffer, 0, VK_INDEX_TYPE_UINT16);
      vkCmdDrawIndexed(cmd_buffer_, index_count, 1, 0, 0, 0);
    } else {
      vkCmdDraw(cmd_buffer_, vertex_count, 1, 0, 0);
    }
  }

  void EndRenderPass() {
    if (in_render_pass_) {
      vkCmdEndRenderPass(cmd_buffer_);
      in_render_pass_ = false;
    }
  }

  void EndFrame() {
    if (in_render_pass_) EndRenderPass();
    vkEndCommandBuffer(cmd_buffer_);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buffer_;

    vkQueueSubmit(queue_, 1, &submit, frame_fence_);
    recording_ = false;
  }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_buffer_ = VK_NULL_HANDLE;
  VkFence frame_fence_ = VK_NULL_HANDLE;
  bool recording_ = false;
  bool in_render_pass_ = false;
};

}  // namespace xe::gpu::vulkan
