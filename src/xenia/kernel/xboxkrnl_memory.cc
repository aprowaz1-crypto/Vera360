/**
 * Vera360 — Xenia Edge
 * xboxkrnl memory shim — NtAllocateVirtualMemory, NtFreeVirtualMemory, etc.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"

namespace xe::kernel::xboxkrnl {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

void RegisterMemoryExports() {
  // NtAllocateVirtualMemory (ordinal 183)
  RegisterExport(183, [](uint32_t* args) -> uint32_t {
    uint32_t base_addr = args[1];
    uint32_t region_size = args[3];
    uint32_t alloc_type = args[4];
    uint32_t protect = args[5];
    
    XELOGI("NtAllocateVirtualMemory: base=0x{:08X}, size=0x{:X}, type=0x{:X}, protect=0x{:X}",
           base_addr, region_size, alloc_type, protect);

    // Map Xbox protect flags to POSIX-style
    xe::memory::PageAccess pf = xe::memory::PageAccess::kReadWrite;
    if (protect & 0x40) {  // PAGE_EXECUTE_READWRITE
      pf = xe::memory::PageAccess::kExecuteReadWrite;
    } else if (protect & 0x10) {  // PAGE_EXECUTE
      pf = xe::memory::PageAccess::kExecuteRead;
    }

    void* ptr = xe::memory::Commit(
        xe::memory::TranslateVirtual(base_addr),
        region_size, pf) ? xe::memory::TranslateVirtual(base_addr) : nullptr;

    return ptr ? 0 : 0xC0000017;  // STATUS_NO_MEMORY
  });

  // NtFreeVirtualMemory (ordinal 199)
  RegisterExport(199, [](uint32_t* args) -> uint32_t {
    uint32_t base_addr = args[1];
    uint32_t region_size = args[3];
    uint32_t free_type = args[4];

    XELOGI("NtFreeVirtualMemory: base=0x{:08X}, size=0x{:X}, type=0x{:X}",
           base_addr, region_size, free_type);

    if (free_type & 0x8000) {  // MEM_RELEASE
      xe::memory::Release(xe::memory::TranslateVirtual(base_addr), region_size);
    } else {
      xe::memory::Decommit(xe::memory::TranslateVirtual(base_addr), region_size);
    }
    return 0;
  });

  // NtQueryVirtualMemory (ordinal 207)
  RegisterExport(207, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueryVirtualMemory: addr=0x{:08X}", args[1]);
    return 0;
  });

  // MmAllocatePhysicalMemoryEx (ordinal 164)
  RegisterExport(164, [](uint32_t* args) -> uint32_t {
    uint32_t size = args[1];
    XELOGI("MmAllocatePhysicalMemoryEx: size=0x{:X}", size);
    // Allocate from high guest addresses (0xA0000000+)
    static uint32_t phys_ptr = 0xA0000000;
    uint32_t addr = phys_ptr;
    phys_ptr += (size + 0xFFF) & ~0xFFF;
    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       xe::memory::PageAccess::kReadWrite);
    return addr;
  });

  // MmFreePhysicalMemory (ordinal 167)
  RegisterExport(167, [](uint32_t* args) -> uint32_t {
    XELOGI("MmFreePhysicalMemory");
    return 0;
  });

  XELOGI("Registered xboxkrnl memory exports");
}

}  // namespace xe::kernel::xboxkrnl
