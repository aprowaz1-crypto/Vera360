/**
 * Vera360 — Xenia Edge
 * Vulkan Texture Cache — placeholder
 */
#include "xenia/base/logging.h"

namespace xe::gpu::vulkan {
class VulkanTextureCache {
 public:
  bool Initialize() { XELOGI("Vulkan texture cache initialized"); return true; }
  void Shutdown() {}
};
}  // namespace xe::gpu::vulkan
