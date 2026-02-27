/**
 * Vera360 — Xenia Edge
 * Emulator implementation — top-level orchestrator
 *
 * Owns and initialises every subsystem, loads XEX executables,
 * and runs the main emulation loop.
 */

#include "xenia/app/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/platform_android.h"
#include "xenia/base/clock.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xex2_loader.h"

// Forward-declare subsystem init/shutdown:
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

#include <android/native_window.h>
#include <fstream>
#include <cstring>

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

  running_ = true;
  XELOGI("Emulator initialised OK");
  return true;
}

void Emulator::Shutdown() {
  if (!running_) return;
  running_ = false;

  XELOGI("Shutting down emulator...");
  xe::hid::Shutdown();

  if (kernel_state_) {
    kernel::KernelState::SetShared(nullptr);
    delete kernel_state_;
    kernel_state_ = nullptr;
  }

  xe::memory::Shutdown();
  XELOGI("Emulator shut down");
}

bool Emulator::InitMemory() {
  if (!xe::memory::Initialize()) {
    XELOGE("Failed to initialise guest memory");
    return false;
  }
  XELOGI("Guest memory mapped at {}", static_cast<void*>(xe::memory::GetGuestBase()));
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
  // TODO: Processor::Initialize() — allocate code cache, JIT compiler
  return true;
}

bool Emulator::InitKernel() {
  XELOGI("Kernel subsystem init");
  kernel_state_ = new kernel::KernelState();
  kernel::KernelState::SetShared(kernel_state_);

  xe::kernel::xboxkrnl::RegisterAllExports();
  xe::kernel::xam::RegisterAllExports();

  XELOGI("Kernel state initialized, handle base=0x{:08X}",
         kernel_state_->AllocateHandle());
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

  // ── Step 1: Detect format ──────────────────────────────────────────────
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    XELOGE("Failed to open: {}", path);
    return false;
  }
  auto file_size = file.tellg();
  file.seekg(0);
  XELOGI("File size: {} bytes", static_cast<uint64_t>(file_size));

  // Read magic
  uint8_t magic[4] = {};
  file.read(reinterpret_cast<char*>(magic), 4);
  file.seekg(0);

  uint32_t magic32 = (uint32_t(magic[0]) << 24) | (uint32_t(magic[1]) << 16) |
                      (uint32_t(magic[2]) << 8) | magic[3];

  bool is_xex = (magic32 == 0x58455832 || magic32 == 0x58455831); // XEX2 / XEX1
  bool is_stfs = (magic32 == 0x434F4E20 || magic32 == 0x4C495645 || magic32 == 0x50495253);
  bool is_iso = false;

  if (!is_xex && !is_stfs) {
    // Check for XISO at sector 32
    if (file_size > 0x10004) {
      file.seekg(0x10000);
      uint8_t iso_hdr[4];
      file.read(reinterpret_cast<char*>(iso_hdr), 4);
      if (iso_hdr[0] == 'M' && iso_hdr[1] == 'I' && iso_hdr[2] == 'C' && iso_hdr[3] == 'R') {
        is_iso = true;
      }
      file.seekg(0);
    }
  }

  // ── Step 2: Load executable ────────────────────────────────────────────
  if (is_xex) {
    return LoadXex(path, file, file_size);
  } else if (is_stfs) {
    XELOGI("STFS container detected — extracting default.xex...");
    return LoadStfsPackage(path);
  } else if (is_iso) {
    XELOGI("ISO disc image detected");
    return LoadDiscImage(path);
  } else {
    // Try loading as raw XEX anyway
    XELOGW("Unknown format (magic=0x{:08X}), trying as XEX", magic32);
    return LoadXex(path, file, file_size);
  }
}

bool Emulator::LoadXex(const std::string& path, std::ifstream& file,
                        std::streampos file_size) {
  // Use the XEX2 loader which has its own Load(path)
  xe::loader::Xex2Loader loader;
  file.close();  // Close our handle — the loader will reopen

  if (!loader.Load(path)) {
    XELOGE("Failed to parse XEX2 header");
    return false;
  }

  const auto& module = loader.module();
  XELOGI("XEX2 loaded: entry=0x{:08X}, base=0x{:08X}, image_size=0x{:X}",
         module.entry_point, module.base_address, module.image_size);

  // Map into guest memory
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (guest_base && module.image_size > 0) {
    xe::memory::Commit(guest_base + module.base_address, module.image_size,
                       xe::memory::PageAccess::kExecuteReadWrite);

    if (!module.pe_image.empty()) {
      memcpy(guest_base + module.base_address,
             module.pe_image.data(),
             module.pe_image.size());
      XELOGI("Mapped {} bytes to guest 0x{:08X}",
             module.pe_image.size(), module.base_address);
    }
  }

  // Create kernel module object
  auto* xmod = kernel_state_->LoadModule(path);
  xmod->set_base_address(module.base_address);
  xmod->set_entry_point(module.entry_point);
  kernel_state_->SetExecutableModule(xmod);

  // Log imports
  for (auto& lib : module.import_libs) {
    XELOGI("Import library: {} ({} records)", lib.name, lib.records.size());
  }

  // Resolve imports via HLE thunks
  loader.ResolveImports(guest_base);

  // ── Create main thread ─────────────────────────────────────────────────
  constexpr uint32_t kDefaultStackSize = 256 * 1024;

  // Allocate stack in guest memory
  static uint32_t stack_alloc_ptr = 0x70000000;
  uint32_t stack_base = stack_alloc_ptr;
  stack_alloc_ptr += kDefaultStackSize;
  xe::memory::Commit(guest_base + stack_base, kDefaultStackSize,
                     xe::memory::PageAccess::kReadWrite);

  auto* main_thread = kernel_state_->CreateThread(
      kDefaultStackSize, module.entry_point, 0, false);
  main_thread->set_name("XThread Main");
  main_thread->set_thread_id(1);

  XELOGI("Main thread created: entry=0x{:08X}, stack=0x{:08X}-0x{:08X}",
         module.entry_point, stack_base, stack_base + kDefaultStackSize);

  game_loaded_ = true;
  return true;
}

bool Emulator::LoadStfsPackage(const std::string& path) {
  // TODO: Parse STFS structure, find default.xex, extract and load
  XELOGW("STFS loading not yet fully implemented: {}", path);
  return false;
}

bool Emulator::LoadDiscImage(const std::string& path) {
  // TODO: Parse XISO, find default.xex, extract and load
  XELOGW("ISO loading not yet fully implemented: {}", path);
  return false;
}

void Emulator::Tick() {
  if (!running_ || !game_loaded_) return;

  // ── Step 1: Advance guest clock ────────────────────────────────────────
  frame_count_++;

  // ── Step 2: Step CPU ───────────────────────────────────────────────────
  auto* thread = kernel_state_ ? kernel_state_->GetCurrentThread() : nullptr;
  if (thread && thread->is_terminated()) {
    XELOGI("Main thread terminated with code {}", thread->exit_code());
    running_ = false;
    return;
  }

  // TODO: For each active XThread:
  //   1. Restore thread context (GPR, FPR, CR, LR, CTR, XER)
  //   2. Look up JIT block at PC
  //   3. Execute block (call into emitted ARM64 code)
  //   4. Handle block exits: branch, syscall, exception
  //   5. Process pending interrupts

  // ── Step 3: Process GPU command buffer ─────────────────────────────────
  // TODO: Read GPU ring buffer write pointer from guest memory
  // Call GpuCommandProcessor::ProcessRingBuffer()

  // ── Step 4: Present frame ──────────────────────────────────────────────
  // TODO: VulkanSwapChain::Present()
}

void Emulator::Pause() {
  running_ = false;
  xe::Clock::PauseGuest();
  XELOGI("Emulator paused (frame {})", frame_count_);
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
  // TODO: Recreate VulkanSwapChain
}

void Emulator::OnSurfaceDestroyed() {
  XELOGI("Surface destroyed");
  // TODO: Destroy VulkanSwapChain
}

}  // namespace xe
