/**
 * Vera360 — Xenia Edge
 * Emulator implementation — top-level orchestrator
 *
 * Owns and initialises every subsystem, loads XEX executables,
 * and runs the main emulation loop with Vulkan rendering + PPC interpretation.
 */

#include "xenia/app/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/platform_android.h"
#include "xenia/base/clock.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xex2_loader.h"
#include "xenia/gpu/gpu_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_instance.h"
#include "xenia/gpu/vulkan/vulkan_device.h"
#include "xenia/gpu/vulkan/vulkan_swap_chain.h"

// Forward-declare subsystem init/shutdown:
namespace xe::kernel::xboxkrnl {
  void RegisterAllExports();
  bool Dispatch(uint32_t ordinal, uint32_t* args);
}
namespace xe::kernel::xam {
  void RegisterAllExports();
  bool Dispatch(uint32_t ordinal, uint32_t* args);
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
  native_window_ = window;
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
  if (!running_ && !processor_ && !kernel_state_) return;
  running_ = false;

  XELOGI("Shutting down emulator...");
  xe::hid::Shutdown();

  gpu_command_processor_.reset();
  vulkan_swap_chain_.reset();
  vulkan_device_.reset();
  vulkan_instance_.reset();

  processor_.reset();

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

  // Vulkan Instance
  vulkan_instance_ = std::make_unique<gpu::vulkan::VulkanInstance>();
  if (!vulkan_instance_->Initialize()) {
    XELOGW("VulkanInstance init failed — continuing without GPU");
    vulkan_instance_.reset();
    return true;
  }

  // Vulkan Device
  vulkan_device_ = std::make_unique<gpu::vulkan::VulkanDevice>();
  if (!vulkan_device_->Initialize(*vulkan_instance_)) {
    XELOGW("VulkanDevice init failed — continuing without GPU");
    vulkan_device_.reset();
    return true;
  }

  // Vulkan Swap Chain
  vulkan_swap_chain_ = std::make_unique<gpu::vulkan::VulkanSwapChain>();
  if (!vulkan_swap_chain_->Initialize(*vulkan_instance_, *vulkan_device_, window)) {
    XELOGW("VulkanSwapChain init failed — continuing without GPU");
    vulkan_swap_chain_.reset();
    return true;
  }

  // GPU command processor
  gpu_command_processor_ = std::make_unique<gpu::GpuCommandProcessor>();

  XELOGI("Vulkan graphics pipeline initialised");
  return true;
}

bool Emulator::InitCpu() {
  XELOGI("CPU subsystem init (PPC interpreter + ARM64 JIT)");

  processor_ = std::make_unique<cpu::Processor>();
  uint8_t* guest_base = xe::memory::GetGuestBase();

  if (!processor_->Initialize(guest_base, cpu::ExecMode::kInterpreter)) {
    XELOGE("Failed to initialise CPU processor");
    return false;
  }

  return true;
}

bool Emulator::InitKernel() {
  XELOGI("Kernel subsystem init");
  kernel_state_ = new kernel::KernelState();
  kernel::KernelState::SetShared(kernel_state_);

  xe::kernel::xboxkrnl::RegisterAllExports();
  xe::kernel::xam::RegisterAllExports();

  // Wire kernel dispatch into the CPU processor
  if (processor_) {
    processor_->SetKernelDispatch(
      [this](cpu::ThreadState* ts, uint32_t ordinal) {
        // Build args array from r3-r10 (PPC calling convention)
        uint32_t args[8];
        for (int i = 0; i < 8; ++i) {
          args[i] = static_cast<uint32_t>(ts->gpr[3 + i]);
        }

        bool handled = false;
        if (ordinal & 0x10000) {
          // XAM export (ordinal high bit set by thunk patching)
          handled = xe::kernel::xam::Dispatch(ordinal & 0xFFFF, args);
        } else {
          // xboxkrnl export
          handled = xe::kernel::xboxkrnl::Dispatch(ordinal, args);
        }

        if (!handled) {
          XELOGW("Unhandled kernel call: ordinal={} (0x{:04X})", ordinal, ordinal);
        }

        // Return value goes in r3
        ts->gpr[3] = args[0];
      });
  }

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

  // ── Step 1: Detect format ─────────────────────────────────────────────
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

  bool is_xex = (magic32 == 0x58455832 || magic32 == 0x58455831);
  bool is_stfs = (magic32 == 0x434F4E20 || magic32 == 0x4C495645 || magic32 == 0x50495253);
  bool is_iso = false;

  if (!is_xex && !is_stfs) {
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

  if (is_xex) {
    return LoadXex(path, file, file_size);
  } else if (is_stfs) {
    XELOGI("STFS container detected — extracting default.xex...");
    return LoadStfsPackage(path);
  } else if (is_iso) {
    XELOGI("ISO disc image detected");
    return LoadDiscImage(path);
  } else {
    XELOGW("Unknown format (magic=0x{:08X}), trying as XEX", magic32);
    return LoadXex(path, file, file_size);
  }
}

bool Emulator::LoadXex(const std::string& path, std::ifstream& file,
                        std::streampos file_size) {
  xe::loader::Xex2Loader loader;
  file.close();

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

  // Resolve imports and register thunks with the CPU processor
  loader.ResolveImports(guest_base, processor_.get());

  // ── Allocate stack for main thread ────────────────────────────────────
  constexpr uint32_t kDefaultStackSize = 256 * 1024;
  static uint32_t stack_alloc_ptr = 0x70000000;
  uint32_t stack_base = stack_alloc_ptr;
  stack_alloc_ptr += kDefaultStackSize;
  xe::memory::Commit(guest_base + stack_base, kDefaultStackSize,
                     xe::memory::PageAccess::kReadWrite);

  // Create CPU thread state
  auto* cpu_thread = processor_->CreateThreadState(1);
  cpu_thread->pc = module.entry_point;
  cpu_thread->gpr[1] = stack_base + kDefaultStackSize - 128;  // SP below top
  cpu_thread->gpr[13] = 0;  // Small data area (r13) — zero for now

  // Create kernel thread object
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
  // Open the STFS file and look for default.xex in the file listing
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  auto fsize = file.tellg();
  file.seekg(0);

  // Read entire file into memory for parsing
  std::vector<uint8_t> data(static_cast<size_t>(fsize));
  file.read(reinterpret_cast<char*>(data.data()), fsize);
  file.close();

  // STFS structure:
  // 0x000: Magic (CON / LIVE / PIRS)
  // 0x340: Content metadata (title, description, etc.)
  // 0x379: Content type (4 bytes)
  // 0x37D: Metadata version
  // 0x3A0: Title ID
  // File table at volume descriptor offset

  if (data.size() < 0x1000) {
    XELOGE("STFS file too small");
    return false;
  }

  // Read volume descriptor type at offset 0x379
  uint8_t vol_desc_type = data[0x379];
  XELOGD("STFS volume descriptor type: {}", vol_desc_type);

  // Content type at 0x344 (4 bytes, big-endian)
  uint32_t content_type = (data[0x344] << 24) | (data[0x345] << 16) |
                           (data[0x346] << 8) | data[0x347];
  XELOGD("STFS content type: 0x{:08X}", content_type);

  // Title ID at 0x360 (4 bytes, big-endian)
  uint32_t title_id = (data[0x360] << 24) | (data[0x361] << 16) |
                       (data[0x362] << 8) | data[0x363];
  XELOGI("STFS Title ID: {:08X}", title_id);

  // Volume descriptor starts at 0x379
  // STFS: block size = 0x1000 (4KB)
  // Hash table at block 0, file table follows
  // For simplicity: scan for "default.xex" in the file table area

  const char* xex_name = "default.xex";
  size_t xex_name_len = strlen(xex_name);

  // STFS file table entries are at the beginning of data blocks
  // Each entry: 0x40 bytes: name (0x28), flags, block count (3 bytes LE),
  // etc. Scan from 0xC000 onwards for file table entries
  size_t scan_start = 0xB000;
  size_t scan_end = std::min(data.size(), static_cast<size_t>(0x20000));

  for (size_t off = scan_start; off + 0x40 <= scan_end; off += 0x40) {
    // Check for "default.xex" at the entry name field
    if (memcmp(data.data() + off, xex_name, xex_name_len) == 0 &&
        data[off + xex_name_len] == 0) {
      XELOGI("Found default.xex entry in STFS at offset 0x{:X}", off);

      // Read starting block (3 bytes LE at offset +0x2F)
      uint32_t start_block = data[off + 0x2F] |
                              (data[off + 0x30] << 8) |
                              (data[off + 0x31] << 16);
      uint32_t file_size_stfs = data[off + 0x34] |
                                (data[off + 0x35] << 8) |
                                (data[off + 0x36] << 16) |
                                (data[off + 0x37] << 24);

      XELOGD("STFS default.xex: start_block={}, size={}", start_block, file_size_stfs);

      // Extract the XEX data from STFS blocks
      // Block data offset = 0xC000 + block * 0x1000
      constexpr uint32_t kStfsDataStart = 0xC000;
      constexpr uint32_t kStfsBlockSize = 0x1000;

      std::vector<uint8_t> xex_data;
      xex_data.reserve(file_size_stfs);
      uint32_t current_block = start_block;
      uint32_t remaining = file_size_stfs;

      while (remaining > 0 && current_block != 0xFFFFFF) {
        uint32_t block_offset = kStfsDataStart + current_block * kStfsBlockSize;
        if (block_offset + kStfsBlockSize > data.size()) break;

        uint32_t to_copy = std::min(remaining, kStfsBlockSize);
        xex_data.insert(xex_data.end(),
                        data.data() + block_offset,
                        data.data() + block_offset + to_copy);
        remaining -= to_copy;
        current_block++;  // Simplified: assume sequential blocks
      }

      if (xex_data.size() >= 4) {
        // Save extracted XEX to temp file and load
        std::string temp_path = storage_root_ + "/temp_default.xex";
        std::ofstream out(temp_path, std::ios::binary);
        if (out.is_open()) {
          out.write(reinterpret_cast<const char*>(xex_data.data()), xex_data.size());
          out.close();
          XELOGI("Extracted default.xex ({} bytes)", xex_data.size());

          std::ifstream xex_file(temp_path, std::ios::binary | std::ios::ate);
          auto xex_size = xex_file.tellg();
          xex_file.seekg(0);
          return LoadXex(temp_path, xex_file, xex_size);
        }
      }
      break;
    }
  }

  XELOGW("Could not find default.xex in STFS package");
  return false;
}

bool Emulator::LoadDiscImage(const std::string& path) {
  // XISO (Xbox ISO) format:
  // Sector 32 (0x10000): "MICROSOFT*XBOX*MEDIA" magic
  // Root directory table follows the volume descriptor
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  auto fsize = file.tellg();
  file.seekg(0);

  // Read the volume descriptor at sector 32
  constexpr uint32_t kSectorSize = 0x800;
  constexpr uint64_t kRootSector = 32;
  constexpr uint64_t kVolumeDescOffset = kRootSector * kSectorSize;

  if (static_cast<uint64_t>(fsize) < kVolumeDescOffset + 0x24) {
    XELOGE("ISO too small for XISO header");
    return false;
  }

  file.seekg(kVolumeDescOffset);
  uint8_t vol_hdr[0x24];
  file.read(reinterpret_cast<char*>(vol_hdr), 0x24);

  // Verify magic: "MICROSOFT*XBOX*MEDIA"
  if (memcmp(vol_hdr, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
    XELOGE("Not a valid XISO image");
    return false;
  }

  // Root directory entry at offset 0x14 in the volume descriptor:
  // 4 bytes LE: root dir table sector
  // 4 bytes LE: root dir table size
  uint32_t root_sector = vol_hdr[0x14] | (vol_hdr[0x15] << 8) |
                          (vol_hdr[0x16] << 16) | (vol_hdr[0x17] << 24);
  uint32_t root_size = vol_hdr[0x18] | (vol_hdr[0x19] << 8) |
                        (vol_hdr[0x1A] << 16) | (vol_hdr[0x1B] << 24);

  XELOGD("XISO root dir: sector={}, size={}", root_sector, root_size);

  // Read root directory table
  uint64_t root_offset = static_cast<uint64_t>(root_sector) * kSectorSize;
  if (static_cast<uint64_t>(fsize) < root_offset + root_size) {
    XELOGE("ISO truncated at root directory");
    return false;
  }

  std::vector<uint8_t> root_data(root_size);
  file.seekg(root_offset);
  file.read(reinterpret_cast<char*>(root_data.data()), root_size);

  // Scan directory entries for "default.xex"
  // Each entry: left_offset(2), right_offset(2), sector(4), size(4),
  //             attributes(1), name_len(1), name(variable)
  for (size_t off = 0; off + 14 <= root_data.size();) {
    uint16_t left = root_data[off] | (root_data[off + 1] << 8);
    uint16_t right = root_data[off + 2] | (root_data[off + 3] << 8);
    uint32_t sector = root_data[off + 4] | (root_data[off + 5] << 8) |
                      (root_data[off + 6] << 16) | (root_data[off + 7] << 24);
    uint32_t size = root_data[off + 8] | (root_data[off + 9] << 8) |
                    (root_data[off + 10] << 16) | (root_data[off + 11] << 24);
    uint8_t attrib = root_data[off + 12];
    uint8_t name_len = root_data[off + 13];

    if (name_len == 0 || off + 14 + name_len > root_data.size()) break;

    std::string name(reinterpret_cast<const char*>(root_data.data() + off + 14),
                      name_len);

    XELOGD("XISO entry: {} sector={} size={}", name, sector, size);

    if (name == "default.xex" && size > 0) {
      // Extract and load
      uint64_t xex_offset = static_cast<uint64_t>(sector) * kSectorSize;
      if (static_cast<uint64_t>(fsize) < xex_offset + size) {
        XELOGE("ISO truncated at default.xex data");
        return false;
      }

      std::vector<uint8_t> xex_data(size);
      file.seekg(xex_offset);
      file.read(reinterpret_cast<char*>(xex_data.data()), size);

      // Save to temp and load
      std::string temp_path = storage_root_ + "/temp_default.xex";
      std::ofstream out(temp_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(xex_data.data()), xex_data.size());
      out.close();

      XELOGI("Extracted default.xex from ISO ({} bytes)", size);
      std::ifstream xex_file(temp_path, std::ios::binary | std::ios::ate);
      auto xex_size = xex_file.tellg();
      xex_file.seekg(0);
      return LoadXex(temp_path, xex_file, xex_size);
    }

    // Move to next entry (aligned to 4 bytes)
    off += 14 + name_len;
    off = (off + 3) & ~3u;
  }

  XELOGW("Could not find default.xex in ISO image");
  file.close();
  return false;
}

void Emulator::Tick() {
  if (!running_ || !game_loaded_) return;

  frame_count_++;

  // ── Step 1: Execute PPC instructions ──────────────────────────────────
  // Run the interpreter for a bounded number of instructions per frame
  auto* thread = kernel_state_ ? kernel_state_->GetCurrentThread() : nullptr;
  if (thread && thread->is_terminated()) {
    XELOGI("Main thread terminated with code {}", thread->exit_code());
    running_ = false;
    return;
  }

  if (processor_) {
    // Get the first CPU thread state and execute a time slice
    auto* cpu_thread = processor_->CreateThreadState(1);
    if (cpu_thread && cpu_thread->running) {
      processor_->ExecuteBounded(cpu_thread, cpu_thread->pc, kInstructionsPerTick);
    }
  }

  // ── Step 2: Process GPU command buffer ────────────────────────────────
  if (gpu_command_processor_) {
    // The GPU ring buffer write pointer is updated by guest code.
    // For now, do nothing — real ring buffer processing requires
    // the guest to have set up the GPU registers.
  }

  // ── Step 3: Present frame via Vulkan ──────────────────────────────────
  if (vulkan_swap_chain_) {
    uint32_t image_index = 0;
    if (vulkan_swap_chain_->AcquireNextImage(&image_index)) {
      // TODO: Record and submit actual render commands
      vulkan_swap_chain_->Present(image_index);
    }
  }
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
  native_window_ = window;
  surface_width_ = width;
  surface_height_ = height;
  XELOGI("Surface changed: {}x{}", width, height);

  if (vulkan_swap_chain_) {
    vulkan_swap_chain_->Recreate(static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height));
  }
}

void Emulator::OnSurfaceDestroyed() {
  XELOGI("Surface destroyed");
  native_window_ = nullptr;
  vulkan_swap_chain_.reset();
}

}  // namespace xe
