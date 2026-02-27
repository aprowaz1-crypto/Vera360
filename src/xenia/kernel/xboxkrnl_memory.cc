/**
 * Vera360 — Xenia Edge
 * xboxkrnl memory shim — NtAllocateVirtualMemory, MmMapIoSpace, etc.
 *
 * Xbox 360 memory map (4 GB flat):
 *   0x00000000–0x3FFFFFFF  User virtual (1 GB, title address space)
 *   0x40000000–0x7FFFFFFF  User virtual aliased (1 GB)
 *   0x80000000–0x8FFFFFFF  XEX / executable image space
 *   0x90000000–0x9FFFFFFF  Physical memory alias (256 MB)
 *   0xA0000000–0xBFFFFFFF  Physical (contiguous) allocations
 *   0xC0000000–0xDFFFFFFF  GPU/display memory
 *   0xE0000000–0xFFFFFFFF  Kernel space
 *
 * We emulate this with a flat 4 GB mmap and mprotect/madvise.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"
#include <functional>
#include <cstring>
#include <algorithm>

namespace xe::kernel::xboxkrnl {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

// ── Status codes ─────────────────────────────────────────────────────────────
static constexpr uint32_t STATUS_SUCCESS          = 0x00000000;
static constexpr uint32_t STATUS_NO_MEMORY        = 0xC0000017;
static constexpr uint32_t STATUS_INVALID_PARAMETER= 0xC000000D;
static constexpr uint32_t STATUS_ACCESS_DENIED    = 0xC0000022;
static constexpr uint32_t STATUS_NOT_IMPLEMENTED  = 0xC0000002;
static constexpr uint32_t STATUS_CONFLICTING_ADDRESSES = 0xC0000018;

// ── Guest memory helpers ─────────────────────────────────────────────────────
static inline void GW32(uint32_t addr, uint32_t v) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}
static inline uint32_t GR32(uint32_t addr) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8)  | p[3];
}

// ── Simple bump allocator for virtual memory requests ────────────────────────
namespace {
  uint32_t g_virtual_alloc_ptr = 0x10000000;  // Start at 256MB
  uint32_t g_physical_alloc_ptr = 0xA0000000; // Physical region
  constexpr uint32_t kPageSize = 4096;
  constexpr uint32_t kLargePageSize = 64 * 1024;

  uint32_t AlignUp(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
  }
}

// ── Map Xbox protect flags to our PageAccess ─────────────────────────────────
static xe::memory::PageAccess MapProtect(uint32_t xbox_protect) {
  // PAGE_NOACCESS       = 0x01
  // PAGE_READONLY       = 0x02
  // PAGE_READWRITE      = 0x04
  // PAGE_EXECUTE        = 0x10
  // PAGE_EXECUTE_READ   = 0x20
  // PAGE_EXECUTE_READWRITE = 0x40
  // PAGE_GUARD          = 0x100
  // PAGE_NOCACHE        = 0x200
  // PAGE_WRITECOMBINE   = 0x400
  uint32_t base = xbox_protect & 0xFF;
  if (base & 0x40) return xe::memory::PageAccess::kExecuteReadWrite;
  if (base & 0x20) return xe::memory::PageAccess::kExecuteRead;
  if (base & 0x10) return xe::memory::PageAccess::kExecuteRead;
  if (base & 0x04) return xe::memory::PageAccess::kReadWrite;
  if (base & 0x02) return xe::memory::PageAccess::kReadOnly;
  return xe::memory::PageAccess::kNoAccess;
}

void RegisterMemoryExports() {

  // ═══════════════════════════════════════════════════════════════════════════
  // Virtual memory
  // ═══════════════════════════════════════════════════════════════════════════

  // NtAllocateVirtualMemory (183)
  RegisterExport(183, [](uint32_t* args) -> uint32_t {
    // NTSTATUS NtAllocateVirtualMemory(
    //   HANDLE ProcessHandle,        // args[0] — usually 0xFFFFFFFF (current)
    //   PVOID *BaseAddress,          // args[1] — in/out guest ptr
    //   ULONG_PTR ZeroBits,          // args[2]
    //   PSIZE_T RegionSize,          // args[3] — in/out guest ptr
    //   ULONG AllocationType,        // args[4]
    //   ULONG Protect)               // args[5]
    uint32_t base_addr_ptr = args[1];
    uint32_t region_size_ptr = args[3];
    uint32_t alloc_type = args[4];
    uint32_t protect = args[5];

    uint32_t base_addr = base_addr_ptr ? GR32(base_addr_ptr) : 0;
    uint32_t region_size = region_size_ptr ? GR32(region_size_ptr) : 0;

    XELOGI("NtAllocateVirtualMemory: base=0x{:08X} size=0x{:X} type=0x{:X} prot=0x{:X}",
           base_addr, region_size, alloc_type, protect);

    if (region_size == 0) return STATUS_INVALID_PARAMETER;

    region_size = AlignUp(region_size, kPageSize);

    // If no base specified, use bump allocator
    if (base_addr == 0) {
      base_addr = AlignUp(g_virtual_alloc_ptr, kLargePageSize);
      g_virtual_alloc_ptr = base_addr + region_size;
    }

    // Commit the pages
    auto access = MapProtect(protect);
    void* host_ptr = xe::memory::TranslateVirtual(base_addr);
    if (!xe::memory::Commit(host_ptr, region_size, access)) {
      XELOGW("NtAllocateVirtualMemory: Commit failed for 0x{:08X} ({} bytes)",
             base_addr, region_size);
      return STATUS_NO_MEMORY;
    }

    // MEM_COMMIT | MEM_RESERVE typically — zero the memory
    if (alloc_type & 0x1000) {  // MEM_COMMIT
      memset(host_ptr, 0, region_size);
    }

    // Write back allocated address and size
    if (base_addr_ptr) GW32(base_addr_ptr, base_addr);
    if (region_size_ptr) GW32(region_size_ptr, region_size);

    XELOGI("NtAllocateVirtualMemory: allocated 0x{:08X} ({} bytes)",
           base_addr, region_size);
    return STATUS_SUCCESS;
  });

  // NtFreeVirtualMemory (199)
  RegisterExport(199, [](uint32_t* args) -> uint32_t {
    uint32_t base_addr_ptr = args[1];
    uint32_t region_size_ptr = args[3];
    uint32_t free_type = args[4];

    uint32_t base_addr = base_addr_ptr ? GR32(base_addr_ptr) : 0;
    uint32_t region_size = region_size_ptr ? GR32(region_size_ptr) : 0;

    XELOGI("NtFreeVirtualMemory: base=0x{:08X} size=0x{:X} type=0x{:X}",
           base_addr, region_size, free_type);

    if (base_addr == 0) return STATUS_INVALID_PARAMETER;

    void* host_ptr = xe::memory::TranslateVirtual(base_addr);

    if (free_type & 0x8000) {  // MEM_RELEASE
      xe::memory::Release(host_ptr, region_size ? region_size : kPageSize);
    } else if (free_type & 0x4000) {  // MEM_DECOMMIT
      xe::memory::Decommit(host_ptr, region_size);
    }

    return STATUS_SUCCESS;
  });

  // NtProtectVirtualMemory (203)
  RegisterExport(203, [](uint32_t* args) -> uint32_t {
    uint32_t base_addr_ptr = args[1];
    uint32_t region_size_ptr = args[3];
    uint32_t new_protect = args[4];
    uint32_t old_protect_ptr = args[5];

    uint32_t base_addr = base_addr_ptr ? GR32(base_addr_ptr) : 0;
    uint32_t region_size = region_size_ptr ? GR32(region_size_ptr) : 0;

    XELOGI("NtProtectVirtualMemory: base=0x{:08X} size=0x{:X} prot=0x{:X}",
           base_addr, region_size, new_protect);

    if (base_addr && region_size) {
      xe::memory::Protect(xe::memory::TranslateVirtual(base_addr),
                          region_size, MapProtect(new_protect));
    }

    // Return previous protection (always RW for simplicity)
    if (old_protect_ptr) GW32(old_protect_ptr, 0x04); // PAGE_READWRITE

    return STATUS_SUCCESS;
  });

  // NtQueryVirtualMemory (207)
  RegisterExport(207, [](uint32_t* args) -> uint32_t {
    uint32_t base_addr = args[1];
    uint32_t mem_info_ptr = args[2];
    uint32_t mem_info_size = args[3];
    uint32_t return_length_ptr = args[4];

    XELOGI("NtQueryVirtualMemory: addr=0x{:08X}", base_addr);

    // Fill MEMORY_BASIC_INFORMATION
    if (mem_info_ptr && mem_info_size >= 28) {
      auto* info = static_cast<uint8_t*>(
          xe::memory::TranslateVirtual(mem_info_ptr));
      memset(info, 0, 28);
      // BaseAddress
      GW32(mem_info_ptr + 0, base_addr & ~0xFFF);
      // AllocationBase
      GW32(mem_info_ptr + 4, base_addr & ~0xFFFF);
      // AllocationProtect
      GW32(mem_info_ptr + 8, 0x04);  // PAGE_READWRITE
      // RegionSize
      GW32(mem_info_ptr + 12, kLargePageSize);
      // State
      GW32(mem_info_ptr + 16, 0x1000);  // MEM_COMMIT
      // Protect
      GW32(mem_info_ptr + 20, 0x04);  // PAGE_READWRITE
      // Type
      GW32(mem_info_ptr + 24, 0x20000);  // MEM_PRIVATE
    }

    if (return_length_ptr) GW32(return_length_ptr, 28);
    return STATUS_SUCCESS;
  });

  // NtFlushVirtualMemory (200)
  RegisterExport(200, [](uint32_t* args) -> uint32_t {
    XELOGI("NtFlushVirtualMemory");
    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Physical memory (MmXxx)
  // ═══════════════════════════════════════════════════════════════════════════

  // MmAllocatePhysicalMemory (164) — simple form
  RegisterExport(164, [](uint32_t* args) -> uint32_t {
    uint32_t type = args[0];
    uint32_t size = args[1];
    XELOGI("MmAllocatePhysicalMemory: type={} size=0x{:X}", type, size);

    size = AlignUp(size, kPageSize);
    uint32_t addr = AlignUp(g_physical_alloc_ptr, kLargePageSize);
    g_physical_alloc_ptr = addr + size;

    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       xe::memory::PageAccess::kReadWrite);
    memset(xe::memory::TranslateVirtual(addr), 0, size);

    XELOGI("MmAllocatePhysicalMemory: -> 0x{:08X}", addr);
    return addr;
  });

  // MmAllocatePhysicalMemoryEx (165) — extended form
  RegisterExport(165, [](uint32_t* args) -> uint32_t {
    uint32_t type = args[0];
    uint32_t size = args[1];
    uint32_t protect = args[2];
    uint32_t min_addr = args[3];
    uint32_t max_addr = args[4];
    uint32_t alignment = args[5];

    XELOGI("MmAllocatePhysicalMemoryEx: size=0x{:X} prot=0x{:X} align=0x{:X}",
           size, protect, alignment);

    size = AlignUp(size, kPageSize);
    if (alignment < kPageSize) alignment = kPageSize;
    uint32_t addr = AlignUp(g_physical_alloc_ptr, alignment);
    g_physical_alloc_ptr = addr + size;

    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       MapProtect(protect));
    memset(xe::memory::TranslateVirtual(addr), 0, size);

    return addr;
  });

  // MmFreePhysicalMemory (167)
  RegisterExport(167, [](uint32_t* args) -> uint32_t {
    uint32_t type = args[0];
    uint32_t addr = args[1];
    XELOGI("MmFreePhysicalMemory: addr=0x{:08X}", addr);
    // Just decommit — we don't track sizes in the simple allocator
    xe::memory::Decommit(xe::memory::TranslateVirtual(addr), kLargePageSize);
    return 0;
  });

  // MmQueryAddressProtect (171)
  RegisterExport(171, [](uint32_t* args) -> uint32_t {
    XELOGI("MmQueryAddressProtect: addr=0x{:08X}", args[0]);
    return 0x04;  // PAGE_READWRITE
  });

  // MmSetAddressProtect (173)
  RegisterExport(173, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    uint32_t size = args[1];
    uint32_t protect = args[2];
    XELOGI("MmSetAddressProtect: addr=0x{:08X} size=0x{:X} prot=0x{:X}",
           addr, size, protect);
    if (size > 0) {
      xe::memory::Protect(xe::memory::TranslateVirtual(addr), size,
                          MapProtect(protect));
    }
    return 0;
  });

  // MmGetPhysicalAddress (169)
  RegisterExport(169, [](uint32_t* args) -> uint32_t {
    // On Xbox 360, physical = virtual for most purposes
    uint32_t vaddr = args[0];
    XELOGI("MmGetPhysicalAddress: 0x{:08X}", vaddr);
    return vaddr;  // Identity map
  });

  // MmMapIoSpace (170)
  RegisterExport(170, [](uint32_t* args) -> uint32_t {
    uint32_t phys_addr = args[0];
    uint32_t size = args[1];
    uint32_t protect = args[2];
    XELOGI("MmMapIoSpace: phys=0x{:08X} size=0x{:X}", phys_addr, size);
    // Identity mapping — just commit the pages
    xe::memory::Commit(xe::memory::TranslateVirtual(phys_addr), size,
                       xe::memory::PageAccess::kReadWrite);
    return phys_addr;
  });

  // MmUnmapIoSpace (174)
  RegisterExport(174, [](uint32_t* args) -> uint32_t {
    XELOGI("MmUnmapIoSpace: addr=0x{:08X}", args[0]);
    return 0;
  });

  // MmIsAddressValid (— ordinal 172)
  RegisterExport(172, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    // Addresses below 4GB in our reserved region are always "valid"
    return (addr < 0xFFFF0000) ? 1 : 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Pool memory (ExAllocatePool)
  // ═══════════════════════════════════════════════════════════════════════════

  // ExAllocatePoolWithTag (61)
  RegisterExport(61, [](uint32_t* args) -> uint32_t {
    uint32_t size = args[0];
    uint32_t tag = args[1];
    XELOGI("ExAllocatePoolWithTag: size=0x{:X} tag=0x{:08X}", size, tag);

    size = AlignUp(size, 16);
    uint32_t addr = AlignUp(g_virtual_alloc_ptr, 16);
    g_virtual_alloc_ptr = addr + size;

    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       xe::memory::PageAccess::kReadWrite);
    memset(xe::memory::TranslateVirtual(addr), 0, size);

    return addr;
  });

  // ExFreePool (63)
  RegisterExport(63, [](uint32_t* args) -> uint32_t {
    XELOGI("ExFreePool: addr=0x{:08X}", args[0]);
    // No-op in bump allocator
    return 0;
  });

  // ExAllocatePool (60) — simplified
  RegisterExport(60, [](uint32_t* args) -> uint32_t {
    uint32_t size = args[0];
    size = AlignUp(size, 16);
    uint32_t addr = AlignUp(g_virtual_alloc_ptr, 16);
    g_virtual_alloc_ptr = addr + size;
    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       xe::memory::PageAccess::kReadWrite);
    memset(xe::memory::TranslateVirtual(addr), 0, size);
    return addr;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Memory copy / zero
  // ═══════════════════════════════════════════════════════════════════════════

  // MmCopyMemory — not a real xboxkrnl export but sometimes patched in
  // RtlCopyMemory (362) — memcpy
  RegisterExport(362, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    uint32_t src = args[1];
    uint32_t length = args[2];
    memcpy(xe::memory::TranslateVirtual(dest),
           xe::memory::TranslateVirtual(src), length);
    return 0;
  });

  // RtlMoveMemory (— ordinal 383)
  RegisterExport(383, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    uint32_t src = args[1];
    uint32_t length = args[2];
    memmove(xe::memory::TranslateVirtual(dest),
            xe::memory::TranslateVirtual(src), length);
    return 0;
  });

  // RtlZeroMemory (384)
  RegisterExport(384, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    uint32_t length = args[1];
    memset(xe::memory::TranslateVirtual(dest), 0, length);
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Cache / misc
  // ═══════════════════════════════════════════════════════════════════════════

  // KeFlushCacheRange — already in module (119)

  // NtAllocateEncryptedMemory (— ordinal 178)
  RegisterExport(178, [](uint32_t* args) -> uint32_t {
    XELOGI("NtAllocateEncryptedMemory: size=0x{:X}", args[1]);
    // Just allocate normally
    uint32_t size = AlignUp(args[1], kPageSize);
    uint32_t addr = AlignUp(g_virtual_alloc_ptr, kPageSize);
    g_virtual_alloc_ptr = addr + size;
    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       xe::memory::PageAccess::kReadWrite);
    return addr;
  });

  XELOGI("Registered xboxkrnl memory exports");
}

}  // namespace xe::kernel::xboxkrnl
