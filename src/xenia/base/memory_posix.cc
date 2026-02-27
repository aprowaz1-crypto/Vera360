/**
 * Vera360 — Xenia Edge
 * POSIX Memory Manager implementation (Linux/Android via mmap)
 *
 * This replaces ALL Win32 virtual memory APIs:
 *   VirtualAlloc   → mmap + mprotect
 *   VirtualFree    → munmap / madvise(MADV_DONTNEED)
 *   VirtualProtect → mprotect
 *   VirtualQuery   → parsing /proc/self/maps (if needed)
 */

#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

namespace xe::memory {

namespace {

/// 4GB guest address space — the full Xbox 360 virtual range
constexpr size_t kGuestSize = 4ULL * 1024 * 1024 * 1024;  // 0x100000000

/// Host pointer to the base of the guest reservation
uint8_t* g_guest_base = nullptr;

/// Convert our PageAccess enum to POSIX mprotect flags
int ToPosixProtection(PageAccess access) {
  switch (access) {
    case PageAccess::kNoAccess:
      return PROT_NONE;
    case PageAccess::kReadOnly:
      return PROT_READ;
    case PageAccess::kReadWrite:
      return PROT_READ | PROT_WRITE;
    case PageAccess::kExecuteRead:
      return PROT_READ | PROT_EXEC;
    case PageAccess::kExecuteReadWrite:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      return PROT_NONE;
  }
}

/// System page size (cached)
size_t g_page_size = 0;

size_t GetPageSize() {
  if (!g_page_size) {
    g_page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  }
  return g_page_size;
}

/// Round up to page boundary
size_t AlignToPage(size_t value) {
  size_t ps = GetPageSize();
  return (value + ps - 1) & ~(ps - 1);
}

}  // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────

bool Initialize() {
  if (g_guest_base) {
    XELOGE("Memory system already initialized");
    return false;
  }

  // Reserve 4GB with PROT_NONE — no physical pages yet
  void* base = mmap(
      nullptr,                           // let kernel choose address
      kGuestSize,
      PROT_NONE,                         // no access until committed
      MAP_PRIVATE | MAP_ANONYMOUS |
      MAP_NORESERVE,                     // don't count against commit charge
      -1,                                // no file backing
      0
  );

  if (base == MAP_FAILED) {
    XELOGE("Failed to reserve 4GB guest space: {} ({})", strerror(errno), errno);
    return false;
  }

  g_guest_base = static_cast<uint8_t*>(base);
  XELOGI("Guest memory reserved at {:p}, size 0x{:X}", base, kGuestSize);
  return true;
}

void Shutdown() {
  if (g_guest_base) {
    munmap(g_guest_base, kGuestSize);
    g_guest_base = nullptr;
    XELOGI("Guest memory released");
  }
}

uint8_t* GetGuestBase() {
  return g_guest_base;
}

MappingResult Reserve(void* preferred_base, size_t size) {
  size = AlignToPage(size);

  void* base = mmap(
      preferred_base,
      size,
      PROT_NONE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
      -1, 0
  );

  if (base == MAP_FAILED) {
    XELOGE("Reserve({}, 0x{:X}) failed: {}", preferred_base, size, strerror(errno));
    return {nullptr, 0, false};
  }

  return {base, size, true};
}

bool Commit(void* base, size_t size, PageAccess access) {
  size = AlignToPage(size);
  int prot = ToPosixProtection(access);

  // mprotect to make pages accessible — kernel allocates physical pages on first touch  
  if (mprotect(base, size, prot) != 0) {
    XELOGE("Commit mprotect({:p}, 0x{:X}) failed: {}", base, size, strerror(errno));
    return false;
  }

  // Force zero-fill by advising the kernel
  // (MADV_WILLNEED hints that we'll access these pages soon)
  madvise(base, size, MADV_WILLNEED);

  return true;
}

bool Decommit(void* base, size_t size) {
  size = AlignToPage(size);

  // Set to PROT_NONE so accesses fault
  if (mprotect(base, size, PROT_NONE) != 0) {
    XELOGE("Decommit mprotect({:p}, 0x{:X}) failed: {}", base, size, strerror(errno));
    return false;
  }

  // MADV_DONTNEED releases physical pages but keeps the VMA
  if (madvise(base, size, MADV_DONTNEED) != 0) {
    XELOGW("Decommit madvise DONTNEED failed (non-fatal): {}", strerror(errno));
  }

  return true;
}

bool Protect(void* base, size_t size, PageAccess access) {
  size = AlignToPage(size);
  int prot = ToPosixProtection(access);

  if (mprotect(base, size, prot) != 0) {
    XELOGE("Protect({:p}, 0x{:X}, {}) failed: {}", base, size,
           static_cast<uint32_t>(access), strerror(errno));
    return false;
  }

  return true;
}

bool Release(void* base, size_t size) {
  size = AlignToPage(size);

  if (munmap(base, size) != 0) {
    XELOGE("Release munmap({:p}, 0x{:X}) failed: {}", base, size, strerror(errno));
    return false;
  }

  return true;
}

void* AllocateExecutable(size_t size) {
  size = AlignToPage(size);

  void* mem = mmap(
      nullptr,
      size,
      PROT_READ | PROT_WRITE | PROT_EXEC,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1, 0
  );

  if (mem == MAP_FAILED) {
    XELOGE("AllocateExecutable(0x{:X}) failed: {}", size, strerror(errno));
    return nullptr;
  }

  return mem;
}

void FreeExecutable(void* base, size_t size) {
  if (base) {
    munmap(base, AlignToPage(size));
  }
}

size_t QueryAvailablePhysicalMemory() {
  std::ifstream meminfo("/proc/meminfo");
  std::string line;
  while (std::getline(meminfo, line)) {
    if (line.starts_with("MemAvailable:")) {
      // Format: "MemAvailable:    1234567 kB"
      size_t kb = 0;
      sscanf(line.c_str(), "MemAvailable: %zu", &kb);
      return kb * 1024;
    }
  }
  // Fallback: sysconf
  long pages = sysconf(_SC_AVPHYS_PAGES);
  long page_size = sysconf(_SC_PAGESIZE);
  return static_cast<size_t>(pages) * static_cast<size_t>(page_size);
}

size_t QueryTotalPhysicalMemory() {
  long pages = sysconf(_SC_PHYS_PAGES);
  long page_size = sysconf(_SC_PAGESIZE);
  return static_cast<size_t>(pages) * static_cast<size_t>(page_size);
}

}  // namespace xe::memory
