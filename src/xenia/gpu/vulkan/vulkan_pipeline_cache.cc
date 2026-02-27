/**
 * Vera360 — Xenia Edge
 * Vulkan Pipeline Cache — placeholder
 */

#include "xenia/base/logging.h"

namespace xe::gpu::vulkan {

class VulkanPipelineCache {
 public:
  bool Initialize() {
    XELOGI("Vulkan pipeline cache initialized");
    return true;
  }
  void Shutdown() {}
};

}  // namespace xe::gpu::vulkan
