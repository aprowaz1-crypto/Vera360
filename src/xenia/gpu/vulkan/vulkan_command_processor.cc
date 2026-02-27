/**
 * Vera360 — Xenia Edge
 * Vulkan Command Processor — Xenos draw → Vulkan commands — placeholder
 */

#include "xenia/base/logging.h"

namespace xe::gpu::vulkan {

class VulkanCommandProcessor {
 public:
  bool Initialize() {
    XELOGI("Vulkan command processor initialized");
    return true;
  }
  void Shutdown() {}
};

}  // namespace xe::gpu::vulkan
