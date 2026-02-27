/**
 * Vera360 — Xenia Edge
 * Vulkan Render Target Cache — placeholder
 */
#include "xenia/base/logging.h"

namespace xe::gpu::vulkan {
class VulkanRenderTargetCache {
 public:
  bool Initialize() { XELOGI("Vulkan render target cache initialized"); return true; }
  void Shutdown() {}
};
}  // namespace xe::gpu::vulkan
