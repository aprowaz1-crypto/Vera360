/**
 * Vera360 — Xenia Edge
 * POSIX Memory Manager (mmap-based replacement for Win32 VirtualAlloc)
 *
 * Xbox 360 has a 4GB virtual address space. We emulate it with a single
 * large mmap reservation, then commit/decommit pages on demand using
 * mprotect + madvise.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace xe::memory {

/// Page protection flags (matching Xbox 360 / Win32 semantics)
enum class PageAccess : uint32_t {
  kNoAccess        = 0,
  kReadOnly        = 1 << 0,
  kReadWrite       = 1 << 1,
  kExecuteRead     = 1 << 2,
  kExecuteReadWrite= 1 << 3,
};

/// Result of a mapping operation
struct MappingResult {
  void* base = nullptr;
  size_t size = 0;
  bool success = false;
};

/// Initialize the guest memory system. Reserves the 4GB virtual space.
bool Initialize();

/// Tears down the guest memory system.
void Shutdown();

/// Returns the base of the 4GB guest virtual address space.
uint8_t* GetGuestBase();

/// Translate a guest address (0x00000000–0xFFFFFFFF) to host pointer.
inline void* TranslateVirtual(uint32_t guest_addr) {
  return GetGuestBase() + guest_addr;
}

/// Reserve a region without committing physical pages.
MappingResult Reserve(void* preferred_base, size_t size);

/// Commit pages in a previously reserved region (makes them R/W).
bool Commit(void* base, size_t size, PageAccess access);

/// Decommit pages (release physical backing, keep reservation).
bool Decommit(void* base, size_t size);

/// Change protection on committed pages.
bool Protect(void* base, size_t size, PageAccess access);

/// Release a region entirely (un-reserves).
bool Release(void* base, size_t size);

/// Allocate executable memory for JIT code caches.
void* AllocateExecutable(size_t size);

/// Free executable memory.
void FreeExecutable(void* base, size_t size);

/// Query how much physical memory is available on the device.
size_t QueryAvailablePhysicalMemory();

/// Query total system RAM.
size_t QueryTotalPhysicalMemory();

}  // namespace xe::memory
