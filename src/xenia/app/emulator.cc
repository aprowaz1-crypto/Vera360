/**
 * Vera360 — Xenia Edge
 * Emulator implementation
 */

#include "xenia/app/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/platform_android.h"
#include "xenia/base/clock.h"

// Forward-declare subsystem init/shutdown:
namespace xe::gpu::vulkan {
  // Defined in vulkan_instance.cc / vulkan_device.cc / vulkan_swap_chain.cc
}
namespace xe::kernel {
  class KernelState;
}
namespace xe::kernel::xboxkrnl {
  void RegisterAllExports();
}
namespace xe::kernel::xam {
  void RegisterAllExports();
}
namespace xe::hid {
  bool Initialize();
  void Shutdown();
}
namespace xe::apu {
  // ApuSystem class
}

#include <android/native_window.h>

namespace xe {

Emulator::Emulator() = default;
Emulator::~Emulator() { Shutdown(); }

bool Emulator::Initialize(ANativeWindow* window, const std::string& storage_root) {
  storage_root_ = storage_root;
  XELOGI("=== Vera360 / Xenia Edge ===");
  XELOGI("Initialising emulator... storage={}", storage_root);

  if (!InitMemory()) return false;
  if (!InitGraphics(window)) return false;
  if (!InitCpu()) return false;
  if (!InitKernel()) return false;
  if (!InitApu()) return false;
  if (!InitHid()) return false;

  // Clock is static, no init needed

  running_ = true;
  XELOGI("Emulator initialised OK");
  return true;
}

void Emulator::Shutdown() {
  if (!running_) return;
  running_ = false;

  XELOGI("Shutting down emulator...");
  xe::hid::Shutdown();
  xe::memory::Shutdown();
  XELOGI("Emulator shut down");
}

bool Emulator::InitMemory() {
  if (!xe::memory::Initialize()) {
    XELOGE("Failed to initialise guest memory");
    return false;
  }
  return true;
}

bool Emulator::InitGraphics(ANativeWindow* window) {
  if (!window) {
    XELOGW("No ANativeWindow supplied — headless mode");
    return true;
  }
  surface_width_ = ANativeWindow_getWidth(window);
  surface_height_ = ANativeWindow_getHeight(window);
  XELOGI("Graphics init: {}x{}", surface_width_, surface_height_);
  // TODO: Create VulkanInstance → VulkanDevice → VulkanSwapChain
  return true;
}

bool Emulator::InitCpu() {
  XELOGI("CPU subsystem init (ARM64 JIT)");
  // TODO: Processor::Initialize()
  return true;
}

bool Emulator::InitKernel() {
  XELOGI("Kernel subsystem init");
  xe::kernel::xboxkrnl::RegisterAllExports();
  // xam: RegisterAllExports() defined in xam_module.cc
  return true;
}

bool Emulator::InitApu() {
  XELOGI("APU subsystem init");
  return true;
}

bool Emulator::InitHid() {
  return xe::hid::Initialize();
}

bool Emulator::LoadGame(const std::string& path) {
  XELOGI("Loading game: {}", path);
  // 1. Detect format (XEX / ISO / STFS)
  // 2. Mount VFS devices
  // 3. Parse XEX header, map sections into guest memory
  // 4. Resolve kernel imports
  // 5. Create main thread at entry point
  // TODO: implement loader
  return true;
}

void Emulator::Tick() {
  if (!running_) return;
  // 1. Advance guest clock
  // 2. Step CPU threads
  // 3. Process GPU command buffer
  // 4. Present frame
}

void Emulator::Pause() {
  running_ = false;
  xe::Clock::PauseGuest();
  XELOGI("Emulator paused");
}

void Emulator::Resume() {
  running_ = true;
  xe::Clock::ResumeGuest();
  XELOGI("Emulator resumed");
}

void Emulator::OnSurfaceChanged(ANativeWindow* window, int width, int height) {
  surface_width_ = width;
  surface_height_ = height;
  XELOGI("Surface changed: {}x{}", width, height);
  // TODO: Recreate swap chain
}

void Emulator::OnSurfaceDestroyed() {
  XELOGI("Surface destroyed");
  // TODO: Release Vulkan surface/swap chain
}

}  // namespace xe
