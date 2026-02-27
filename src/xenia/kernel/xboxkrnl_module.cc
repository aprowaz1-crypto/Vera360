/**
 * Vera360 — Xenia Edge
 * xboxkrnl.exe shim — main module
 *
 * Implements high-level kernel exports (HLE) for the Xbox 360 kernel.
 * Each export is a thunk that intercepts the guest call and provides
 * the emulated behaviour on Android/Linux.
 *
 * xboxkrnl ordinals: https://free60.org/System_Software/Kernel/
 * Status codes follow NT convention (0 = success, 0xC000xxxx = error).
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/clock.h"
#include <unordered_map>
#include <functional>
#include <cstring>
#include <ctime>

namespace xe::kernel::xboxkrnl {

// ─────────────────────────────────────────────────────────────────────────────
// Export dispatch infrastructure
// ─────────────────────────────────────────────────────────────────────────────
using ExportThunk = std::function<uint32_t(uint32_t* args)>;
static std::unordered_map<uint32_t, ExportThunk> g_exports;

void RegisterExport(uint32_t ordinal, ExportThunk thunk) {
  g_exports[ordinal] = std::move(thunk);
}

uint32_t Dispatch(uint32_t ordinal, uint32_t* args) {
  auto it = g_exports.find(ordinal);
  if (it != g_exports.end()) {
    return it->second(args);
  }
  XELOGW("Unimplemented xboxkrnl export: ordinal={}", ordinal);
  return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations for sub-module registration
// ─────────────────────────────────────────────────────────────────────────────
extern void RegisterThreadingExports();
extern void RegisterMemoryExports();
extern void RegisterIoExports();

// ─────────────────────────────────────────────────────────────────────────────
// NT status codes
// ─────────────────────────────────────────────────────────────────────────────
constexpr uint32_t X_STATUS_SUCCESS           = 0x00000000;
constexpr uint32_t X_STATUS_ABANDONED_WAIT_0  = 0x00000080;
constexpr uint32_t X_STATUS_TIMEOUT           = 0x00000102;
constexpr uint32_t X_STATUS_PENDING           = 0x00000103;
constexpr uint32_t X_STATUS_BUFFER_OVERFLOW   = 0x80000005;
constexpr uint32_t X_STATUS_UNSUCCESSFUL      = 0xC0000001;
constexpr uint32_t X_STATUS_NOT_IMPLEMENTED   = 0xC0000002;
constexpr uint32_t X_STATUS_INVALID_HANDLE    = 0xC0000008;
constexpr uint32_t X_STATUS_INVALID_PARAMETER = 0xC000000D;
constexpr uint32_t X_STATUS_NO_MEMORY         = 0xC0000017;
constexpr uint32_t X_STATUS_ACCESS_DENIED     = 0xC0000022;
constexpr uint32_t X_STATUS_OBJECT_NAME_NOT_FOUND = 0xC0000034;
constexpr uint32_t X_STATUS_OBJECT_NAME_COLLISION  = 0xC0000035;

// ─────────────────────────────────────────────────────────────────────────────
// Guest-host helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Read a big-endian uint32 from guest memory
static inline uint32_t GuestRead32(uint32_t guest_addr) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(guest_addr));
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | p[3];
}

/// Write a big-endian uint32 to guest memory
static inline void GuestWrite32(uint32_t guest_addr, uint32_t value) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(guest_addr));
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

/// Read a little-endian uint32 from guest memory (for some APIs)
static inline uint32_t GuestRead32LE(uint32_t guest_addr) {
  uint32_t v;
  memcpy(&v, xe::memory::TranslateVirtual(guest_addr), 4);
  return v;
}

static inline void GuestWrite32LE(uint32_t guest_addr, uint32_t value) {
  memcpy(xe::memory::TranslateVirtual(guest_addr), &value, 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// RegisterAllExports — called from Emulator::InitKernel
// ─────────────────────────────────────────────────────────────────────────────
void RegisterAllExports() {
  // ═══════════════════════════════════════════════════════════════════════════
  // Process / Module
  // ═══════════════════════════════════════════════════════════════════════════

  // XexGetModuleHandle (327)
  RegisterExport(327, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    auto* mod = state ? state->GetExecutableModule() : nullptr;
    uint32_t handle = mod ? mod->handle() : 0x80010000;
    XELOGI("XexGetModuleHandle -> 0x{:08X}", handle);
    return handle;
  });

  // XexGetModuleSection (326)
  RegisterExport(326, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t section_name_ptr = args[1];
    uint32_t data_ptr_out = args[2];
    uint32_t size_out = args[3];
    XELOGI("XexGetModuleSection: handle=0x{:08X}", handle);
    // Write dummy values — real impl would scan PE sections
    if (data_ptr_out) GuestWrite32(data_ptr_out, 0x82000000);
    if (size_out) GuestWrite32(size_out, 0x1000);
    return X_STATUS_SUCCESS;
  });

  // XexLoadImage (408)
  RegisterExport(408, [](uint32_t* args) -> uint32_t {
    uint32_t path_ptr = args[0];
    uint32_t flags = args[1];
    uint32_t ver_min = args[2];
    uint32_t handle_out = args[3];
    XELOGI("XexLoadImage: flags=0x{:08X}", flags);
    if (handle_out) GuestWrite32(handle_out, 0x80020000);
    return X_STATUS_SUCCESS;
  });

  // XexUnloadImage (409)
  RegisterExport(409, [](uint32_t* args) -> uint32_t {
    XELOGI("XexUnloadImage: handle=0x{:08X}", args[0]);
    return X_STATUS_SUCCESS;
  });

  // XexGetProcedureAddress (407)
  RegisterExport(407, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t ordinal = args[1];
    uint32_t addr_out = args[2];
    XELOGI("XexGetProcedureAddress: handle=0x{:08X}, ordinal={}", handle, ordinal);
    if (addr_out) GuestWrite32(addr_out, 0);
    return X_STATUS_NOT_IMPLEMENTED;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Configuration
  // ═══════════════════════════════════════════════════════════════════════════

  // ExGetXConfigSetting (404)
  RegisterExport(404, [](uint32_t* args) -> uint32_t {
    uint32_t category = args[0];
    uint32_t setting = args[1];
    uint32_t buffer_ptr = args[2];
    uint32_t buffer_size = args[3];
    uint32_t required_size_ptr = args[4];
    XELOGI("ExGetXConfigSetting(cat={}, set={})", category, setting);

    // Return common config values
    switch (category) {
      case 0x0003:  // XCONFIG_USER_CATEGORY
        switch (setting) {
          case 0x0001:  // Language
            if (buffer_ptr && buffer_size >= 4) GuestWrite32(buffer_ptr, 1); // English
            if (required_size_ptr) GuestWrite32(required_size_ptr, 4);
            return X_STATUS_SUCCESS;
          case 0x0002:  // Video flags
            if (buffer_ptr && buffer_size >= 4) GuestWrite32(buffer_ptr, 0x00040000); // 1080p
            if (required_size_ptr) GuestWrite32(required_size_ptr, 4);
            return X_STATUS_SUCCESS;
          case 0x0003:  // Audio flags
            if (buffer_ptr && buffer_size >= 4) GuestWrite32(buffer_ptr, 0x00010000); // Stereo
            if (required_size_ptr) GuestWrite32(required_size_ptr, 4);
            return X_STATUS_SUCCESS;
        }
        break;
      case 0x000B:  // XCONFIG_SECURED_CATEGORY
        switch (setting) {
          case 0x0002:  // MAC address
            if (buffer_ptr && buffer_size >= 6) {
              auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(buffer_ptr));
              memset(p, 0, 6);
              p[0] = 0x00; p[1] = 0x11; p[2] = 0x22;  // Fake MAC
              p[3] = 0x33; p[4] = 0x44; p[5] = 0x55;
            }
            if (required_size_ptr) GuestWrite32(required_size_ptr, 6);
            return X_STATUS_SUCCESS;
          case 0x0003:  // AV region
            if (buffer_ptr && buffer_size >= 4) GuestWrite32(buffer_ptr, 0x00001000); // NTSC-U
            if (required_size_ptr) GuestWrite32(required_size_ptr, 4);
            return X_STATUS_SUCCESS;
        }
        break;
    }
    return X_STATUS_INVALID_PARAMETER;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Time
  // ═══════════════════════════════════════════════════════════════════════════

  // KeQueryPerformanceCounter (18) → returns 64-bit counter via guest pointer
  RegisterExport(18, [](uint32_t* args) -> uint32_t {
    uint64_t ts = xe::Clock::QueryGuestTickCount();
    // Xbox 360 returns result in a LARGE_INTEGER* at args[0]
    if (args[0]) {
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(args[0]));
      // Big-endian: high 32 first
      p[0] = uint8_t(ts >> 56); p[1] = uint8_t(ts >> 48);
      p[2] = uint8_t(ts >> 40); p[3] = uint8_t(ts >> 32);
      p[4] = uint8_t(ts >> 24); p[5] = uint8_t(ts >> 16);
      p[6] = uint8_t(ts >> 8);  p[7] = uint8_t(ts);
    }
    return static_cast<uint32_t>(ts & 0xFFFFFFFF);
  });

  // KeQueryPerformanceFrequency (19)
  RegisterExport(19, [](uint32_t* args) -> uint32_t {
    // Xbox 360 timebase = 50 MHz
    constexpr uint32_t kTimebaseFreq = 50000000;
    if (args[0]) GuestWrite32(args[0], kTimebaseFreq);
    return kTimebaseFreq;
  });

  // KeQuerySystemTime (154) → fills FILETIME* at args[0]
  RegisterExport(154, [](uint32_t* args) -> uint32_t {
    // Windows FILETIME: 100ns intervals since 1601-01-01
    // We approximate with current time
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    // Epoch offset from 1601 to 1970 in 100ns units
    constexpr uint64_t kEpochDelta = 116444736000000000ULL;
    uint64_t ft = kEpochDelta +
                  static_cast<uint64_t>(ts.tv_sec) * 10000000ULL +
                  static_cast<uint64_t>(ts.tv_nsec) / 100;
    if (args[0]) {
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(args[0]));
      // FILETIME in big-endian
      for (int i = 7; i >= 0; --i) {
        p[7 - i] = uint8_t(ft >> (i * 8));
      }
    }
    return X_STATUS_SUCCESS;
  });

  // KeGetCurrentProcessType (124)
  RegisterExport(124, [](uint32_t* args) -> uint32_t {
    return 2;  // Title process (2), system = 1
  });

  // KeSetCurrentProcessType (125)
  RegisterExport(125, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetCurrentProcessType({})", args[0]);
    return X_STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Object management
  // ═══════════════════════════════════════════════════════════════════════════

  // ObReferenceObjectByHandle (345 — real Xbox 360 ordinal)
  RegisterExport(345, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t object_type = args[1];
    uint32_t object_ptr_out = args[2];
    XELOGI("ObReferenceObjectByHandle: handle=0x{:08X}", handle);
    auto* state = KernelState::shared();
    auto* obj = state ? state->GetObject(handle) : nullptr;
    if (!obj) return X_STATUS_INVALID_HANDLE;
    obj->Retain();
    if (object_ptr_out) GuestWrite32(object_ptr_out, handle);
    return X_STATUS_SUCCESS;
  });

  // ObDereferenceObject (316 — real Xbox 360 ordinal)
  RegisterExport(316, [](uint32_t* args) -> uint32_t {
    // args[0] = object pointer (we treat as handle for simplicity)
    XELOGI("ObDereferenceObject");
    return X_STATUS_SUCCESS;
  });

  // NtClose (184)
  RegisterExport(184, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    XELOGI("NtClose: handle=0x{:08X}", handle);
    auto* state = KernelState::shared();
    if (state) state->UnregisterObject(handle);
    return X_STATUS_SUCCESS;
  });

  // NtDuplicateObject (196)
  RegisterExport(196, [](uint32_t* args) -> uint32_t {
    uint32_t src_handle = args[0];
    uint32_t options = args[1];
    uint32_t out_handle_ptr = args[2];
    XELOGI("NtDuplicateObject: src=0x{:08X}", src_handle);
    // Clone handle in kernel state
    auto* state = KernelState::shared();
    auto* obj = state ? state->GetObject(src_handle) : nullptr;
    if (!obj) return X_STATUS_INVALID_HANDLE;
    uint32_t new_handle = state->AllocateHandle();
    state->RegisterObject(new_handle, obj);
    obj->Retain();
    if (out_handle_ptr) GuestWrite32(out_handle_ptr, new_handle);
    return X_STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Debug / String
  // ═══════════════════════════════════════════════════════════════════════════

  // DbgPrint (349)
  RegisterExport(349, [](uint32_t* args) -> uint32_t {
    // args[0] = format string pointer
    if (args[0]) {
      auto* str = static_cast<const char*>(xe::memory::TranslateVirtual(args[0]));
      XELOGI("[DbgPrint] {}", str);
    }
    return X_STATUS_SUCCESS;
  });

  // RtlInitAnsiString (374)
  RegisterExport(374, [](uint32_t* args) -> uint32_t {
    uint32_t dest_ptr = args[0];  // ANSI_STRING*
    uint32_t src_ptr = args[1];   // const char*
    if (dest_ptr && src_ptr) {
      auto* src = static_cast<const char*>(xe::memory::TranslateVirtual(src_ptr));
      uint16_t len = static_cast<uint16_t>(strlen(src));
      auto* dest = static_cast<uint8_t*>(xe::memory::TranslateVirtual(dest_ptr));
      // ANSI_STRING: {USHORT Length, USHORT MaxLength, PCHAR Buffer}
      // Big-endian shorts
      dest[0] = uint8_t(len >> 8); dest[1] = uint8_t(len);
      uint16_t max_len = len + 1;
      dest[2] = uint8_t(max_len >> 8); dest[3] = uint8_t(max_len);
      // Store buffer pointer (big-endian)
      GuestWrite32(dest_ptr + 4, src_ptr);
    }
    return 0;  // void function
  });

  // RtlInitUnicodeString (375)
  RegisterExport(375, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlInitUnicodeString");
    // Similar structure but with wchar_t
    return 0;
  });

  // RtlFreeAnsiString (370)
  RegisterExport(370, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlFreeAnsiString");
    return 0;
  });

  // RtlFreeUnicodeString (371)
  RegisterExport(371, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlFreeUnicodeString");
    return 0;
  });

  // RtlUnicodeStringToAnsiString (381)
  RegisterExport(381, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlUnicodeStringToAnsiString");
    return X_STATUS_SUCCESS;
  });

  // RtlMultiByteToUnicodeN (379)
  RegisterExport(379, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlMultiByteToUnicodeN");
    return X_STATUS_SUCCESS;
  });

  // RtlUnicodeToMultiByteN (382)
  RegisterExport(382, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlUnicodeToMultiByteN");
    return X_STATUS_SUCCESS;
  });

  // RtlCompareMemory (364)
  RegisterExport(364, [](uint32_t* args) -> uint32_t {
    uint32_t src1 = args[0];
    uint32_t src2 = args[1];
    uint32_t length = args[2];
    auto* p1 = static_cast<uint8_t*>(xe::memory::TranslateVirtual(src1));
    auto* p2 = static_cast<uint8_t*>(xe::memory::TranslateVirtual(src2));
    uint32_t matching = 0;
    for (uint32_t i = 0; i < length; ++i) {
      if (p1[i] != p2[i]) break;
      matching++;
    }
    return matching;
  });

  // RtlCompareMemoryUlong (365)
  RegisterExport(365, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlCompareMemoryUlong");
    return 0;
  });

  // RtlFillMemoryUlong (369)
  RegisterExport(369, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    uint32_t length = args[1];
    uint32_t pattern = args[2];
    auto* p = static_cast<uint32_t*>(xe::memory::TranslateVirtual(dest));
    uint32_t count = length / 4;
    for (uint32_t i = 0; i < count; ++i) p[i] = pattern;
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // TLS — Real implementations in xboxkrnl_threading.cc (ordinals 340-343)
  // Legacy ordinals 155/156/159/160 forward to the real KernelState TLS
  // ═══════════════════════════════════════════════════════════════════════════

  // KeTlsAlloc (155) — forward to real implementation
  RegisterExport(155, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    if (!state) return 0xFFFFFFFF;
    return state->AllocateTLS();
  });

  // KeTlsFree (156) — forward to real implementation
  RegisterExport(156, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    if (state) state->FreeTLS(args[0]);
    return 1;
  });

  // KeTlsGetValue (159) — forward to real implementation
  RegisterExport(159, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    if (!state) return 0;
    auto* thread = state->GetCurrentThread();
    uint32_t tid = thread ? thread->thread_id() : 0;
    return static_cast<uint32_t>(state->GetTLSValue(tid, args[0]));
  });

  // KeTlsSetValue (160) — forward to real implementation
  RegisterExport(160, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    if (!state) return 0;
    auto* thread = state->GetCurrentThread();
    uint32_t tid = thread ? thread->thread_id() : 0;
    state->SetTLSValue(tid, args[0], args[1]);
    return 1;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Critical sections / Mutexes
  // ═══════════════════════════════════════════════════════════════════════════

  // RtlInitializeCriticalSection (367)
  RegisterExport(367, [](uint32_t* args) -> uint32_t {
    uint32_t cs_ptr = args[0];
    XELOGI("RtlInitializeCriticalSection(0x{:08X})", cs_ptr);
    // Zero out the 28-byte structure
    if (cs_ptr) memset(xe::memory::TranslateVirtual(cs_ptr), 0, 28);
    return X_STATUS_SUCCESS;
  });

  // RtlInitializeCriticalSectionAndSpinCount (368)
  RegisterExport(368, [](uint32_t* args) -> uint32_t {
    uint32_t cs_ptr = args[0];
    uint32_t spin = args[1];
    XELOGI("RtlInitializeCriticalSectionAndSpinCount(0x{:08X}, {})", cs_ptr, spin);
    if (cs_ptr) memset(xe::memory::TranslateVirtual(cs_ptr), 0, 28);
    return X_STATUS_SUCCESS;
  });

  // RtlEnterCriticalSection (363)
  RegisterExport(363, [](uint32_t* args) -> uint32_t {
    // For single-threaded emulation, always succeeds
    uint32_t cs_ptr = args[0];
    if (cs_ptr) {
      // Increment recursion count (offset 8, big-endian)
      uint32_t rc = GuestRead32(cs_ptr + 8);
      GuestWrite32(cs_ptr + 8, rc + 1);
    }
    return X_STATUS_SUCCESS;
  });

  // RtlLeaveCriticalSection (373)
  RegisterExport(373, [](uint32_t* args) -> uint32_t {
    uint32_t cs_ptr = args[0];
    if (cs_ptr) {
      uint32_t rc = GuestRead32(cs_ptr + 8);
      if (rc > 0) GuestWrite32(cs_ptr + 8, rc - 1);
    }
    return X_STATUS_SUCCESS;
  });

  // RtlTryEnterCriticalSection (380)
  RegisterExport(380, [](uint32_t* args) -> uint32_t {
    uint32_t cs_ptr = args[0];
    if (cs_ptr) {
      uint32_t rc = GuestRead32(cs_ptr + 8);
      GuestWrite32(cs_ptr + 8, rc + 1);
    }
    return 1;  // TRUE = acquired
  });

  // RtlDeleteCriticalSection (366)
  RegisterExport(366, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlDeleteCriticalSection");
    return X_STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Interlocked operations
  // ═══════════════════════════════════════════════════════════════════════════

  // InterlockedIncrement (38)
  RegisterExport(38, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    uint32_t val = GuestRead32(addr);
    val++;
    GuestWrite32(addr, val);
    return val;
  });

  // InterlockedDecrement (37)
  RegisterExport(37, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    uint32_t val = GuestRead32(addr);
    val--;
    GuestWrite32(addr, val);
    return val;
  });

  // InterlockedCompareExchange (36)
  RegisterExport(36, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    uint32_t exchange = args[1];
    uint32_t comparand = args[2];
    uint32_t val = GuestRead32(addr);
    if (val == comparand) {
      GuestWrite32(addr, exchange);
    }
    return val;  // Original value
  });

  // InterlockedExchange (39)
  RegisterExport(39, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    uint32_t new_val = args[1];
    uint32_t old_val = GuestRead32(addr);
    GuestWrite32(addr, new_val);
    return old_val;
  });

  // InterlockedExchangeAdd (40)
  RegisterExport(40, [](uint32_t* args) -> uint32_t {
    uint32_t addr = args[0];
    uint32_t addend = args[1];
    uint32_t old_val = GuestRead32(addr);
    GuestWrite32(addr, old_val + addend);
    return old_val;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Error codes
  // ═══════════════════════════════════════════════════════════════════════════

  // RtlNtStatusToDosError (377)
  RegisterExport(377, [](uint32_t* args) -> uint32_t {
    uint32_t status = args[0];
    // Simplified mapping
    if (status == 0) return 0;  // ERROR_SUCCESS
    if (status == X_STATUS_NO_MEMORY) return 8;    // ERROR_NOT_ENOUGH_MEMORY
    if (status == X_STATUS_ACCESS_DENIED) return 5; // ERROR_ACCESS_DENIED
    if (status == X_STATUS_INVALID_PARAMETER) return 87; // ERROR_INVALID_PARAMETER
    if (status == X_STATUS_OBJECT_NAME_NOT_FOUND) return 2; // ERROR_FILE_NOT_FOUND
    return 317;  // ERROR_MR_MID_NOT_FOUND
  });

  // RtlRaiseException (376)
  RegisterExport(376, [](uint32_t* args) -> uint32_t {
    // Should never happen in working code; log and continue
    XELOGW("RtlRaiseException called!");
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Misc kernel
  // ═══════════════════════════════════════════════════════════════════════════

  // KeBugCheck (109)
  RegisterExport(109, [](uint32_t* args) -> uint32_t {
    XELOGE("*** KeBugCheck: code=0x{:08X} ***", args[0]);
    return 0;
  });

  // KeBugCheckEx (110)
  RegisterExport(110, [](uint32_t* args) -> uint32_t {
    XELOGE("*** KeBugCheckEx: code=0x{:08X}, p1=0x{:08X}, p2=0x{:08X}, p3=0x{:08X}, p4=0x{:08X} ***",
           args[0], args[1], args[2], args[3], args[4]);
    return 0;
  });

  // KeRaiseIrqlToDpcLevel (137)
  RegisterExport(137, [](uint32_t* args) -> uint32_t {
    return 0;  // Old IRQL = PASSIVE_LEVEL
  });

  // KfLowerIrql (161)
  RegisterExport(161, [](uint32_t* args) -> uint32_t {
    return 0;  // No-op on emulator
  });

  // KfRaiseIrql (162)
  RegisterExport(162, [](uint32_t* args) -> uint32_t {
    return 0;  // Old IRQL
  });

  // KeEnableFpuExceptions (118)
  RegisterExport(118, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // KeFlushCacheRange (119)
  RegisterExport(119, [](uint32_t* args) -> uint32_t {
    // Would flush CPU cache — no-op on ARMv8 (handles coherency)
    return X_STATUS_SUCCESS;
  });

  // KeInsertQueueDpc (131)
  RegisterExport(131, [](uint32_t* args) -> uint32_t {
    XELOGI("KeInsertQueueDpc");
    return 1;  // TRUE
  });

  // KeRemoveQueueDpc (142)
  RegisterExport(142, [](uint32_t* args) -> uint32_t {
    return 1;  // TRUE
  });

  // KeInitializeDpc (127)
  RegisterExport(127, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // KeInitializeTimerEx (130)
  RegisterExport(130, [](uint32_t* args) -> uint32_t {
    XELOGI("KeInitializeTimerEx");
    return 0;
  });

  // KeSetTimer (149)
  RegisterExport(149, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetTimer");
    return 0;  // FALSE = was not already in the queue
  });

  // KeSetTimerEx (150)
  RegisterExport(150, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetTimerEx");
    return 0;
  });

  // KeCancelTimer (111)
  RegisterExport(111, [](uint32_t* args) -> uint32_t {
    return 0;  // FALSE
  });

  // KeInitializeEvent (128)
  RegisterExport(128, [](uint32_t* args) -> uint32_t {
    XELOGI("KeInitializeEvent");
    return 0;
  });

  // NtCreateMutant (189)
  RegisterExport(189, [](uint32_t* args) -> uint32_t {
    uint32_t handle_out = args[0];
    if (handle_out) {
      auto* state = KernelState::shared();
      uint32_t h = state ? state->AllocateHandle() : 0x110;
      GuestWrite32(handle_out, h);
    }
    XELOGI("NtCreateMutant");
    return X_STATUS_SUCCESS;
  });

  // NtReleaseMutant (211)
  RegisterExport(211, [](uint32_t* args) -> uint32_t {
    XELOGI("NtReleaseMutant");
    return 1;  // Previous count
  });

  // NtCreateSemaphore (192)
  RegisterExport(192, [](uint32_t* args) -> uint32_t {
    uint32_t handle_out = args[0];
    if (handle_out) {
      auto* state = KernelState::shared();
      uint32_t h = state ? state->AllocateHandle() : 0x120;
      GuestWrite32(handle_out, h);
    }
    XELOGI("NtCreateSemaphore");
    return X_STATUS_SUCCESS;
  });

  // NtReleaseSemaphore (213)
  RegisterExport(213, [](uint32_t* args) -> uint32_t {
    XELOGI("NtReleaseSemaphore");
    return X_STATUS_SUCCESS;
  });

  // NtCreateTimer (193)
  RegisterExport(193, [](uint32_t* args) -> uint32_t {
    uint32_t handle_out = args[0];
    if (handle_out) {
      auto* state = KernelState::shared();
      uint32_t h = state ? state->AllocateHandle() : 0x130;
      GuestWrite32(handle_out, h);
    }
    XELOGI("NtCreateTimer");
    return X_STATUS_SUCCESS;
  });

  // NtSetTimerEx (221)
  RegisterExport(221, [](uint32_t* args) -> uint32_t {
    XELOGI("NtSetTimerEx");
    return X_STATUS_SUCCESS;
  });

  // NtCancelTimer (181)
  RegisterExport(181, [](uint32_t* args) -> uint32_t {
    return X_STATUS_SUCCESS;
  });

  // NtWaitForSingleObjectEx (226)
  RegisterExport(226, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t alertable = args[1];
    uint32_t timeout_ptr = args[2];
    XELOGI("NtWaitForSingleObjectEx: handle=0x{:08X}", handle);
    return X_STATUS_SUCCESS;  // WAIT_OBJECT_0
  });

  // NtWaitForMultipleObjectsEx (227)
  RegisterExport(227, [](uint32_t* args) -> uint32_t {
    XELOGI("NtWaitForMultipleObjectsEx");
    return X_STATUS_SUCCESS;
  });

  // NtSignalAndWaitForSingleObjectEx (222)
  RegisterExport(222, [](uint32_t* args) -> uint32_t {
    XELOGI("NtSignalAndWaitForSingleObjectEx");
    return X_STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Misc Rtl / Ex
  // ═══════════════════════════════════════════════════════════════════════════

  // RtlImageXexHeaderField (372)
  RegisterExport(372, [](uint32_t* args) -> uint32_t {
    uint32_t xex_header_ptr = args[0];
    uint32_t field_dword = args[1];
    XELOGI("RtlImageXexHeaderField: field=0x{:08X}", field_dword);
    return 0;
  });

  // ExRegisterTitleTerminateNotification (410)
  RegisterExport(410, [](uint32_t* args) -> uint32_t {
    XELOGI("ExRegisterTitleTerminateNotification");
    return X_STATUS_SUCCESS;
  });

  // ExTerminateThread (73)
  RegisterExport(73, [](uint32_t* args) -> uint32_t {
    uint32_t exit_code = args[0];
    XELOGI("ExTerminateThread: exit_code={}", exit_code);
    auto* state = KernelState::shared();
    auto* thread = state ? state->GetCurrentThread() : nullptr;
    if (thread) state->TerminateThread(thread, exit_code);
    return X_STATUS_SUCCESS;
  });

  // HalReturnToFirmware (40)  — ordinal 40 is overloaded, kernel uses 271
  RegisterExport(271, [](uint32_t* args) -> uint32_t {
    XELOGI("HalReturnToFirmware: reason={}", args[0]);
    return 0;
  });

  // RtlSleep (unused but sometimes called)
  RegisterExport(378, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Missing critical exports for game boot
  // ═══════════════════════════════════════════════════════════════════════════

  // RtlFillMemory (367) — fills memory with a byte value
  RegisterExport(367, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    uint32_t length = args[1];
    uint32_t fill = args[2] & 0xFF;
    if (dest && length) {
      memset(xe::memory::TranslateVirtual(dest), fill, length);
    }
    return 0;
  });

  // NtResumeThread (197)
  RegisterExport(197, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t suspend_count_ptr = args[1];
    XELOGI("NtResumeThread: handle=0x{:08X}", handle);
    auto* state = KernelState::shared();
    if (!state) return X_STATUS_INVALID_HANDLE;
    // Find thread by handle in thread list
    XThread* thread = nullptr;
    for (auto* t : state->GetAllThreads()) {
      if (t->handle() == handle) { thread = t; break; }
    }
    if (!thread) return X_STATUS_INVALID_HANDLE;
    uint32_t prev = thread->suspend_count();
    thread->Resume();
    if (suspend_count_ptr) GuestWrite32(suspend_count_ptr, prev);
    return X_STATUS_SUCCESS;
  });

  // NtSuspendThread (220)
  RegisterExport(220, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t suspend_count_ptr = args[1];
    XELOGI("NtSuspendThread: handle=0x{:08X}", handle);
    auto* state = KernelState::shared();
    if (!state) return X_STATUS_INVALID_HANDLE;
    XThread* thread = nullptr;
    for (auto* t : state->GetAllThreads()) {
      if (t->handle() == handle) { thread = t; break; }
    }
    if (!thread) return X_STATUS_INVALID_HANDLE;
    uint32_t prev = thread->suspend_count();
    thread->Suspend();
    if (suspend_count_ptr) GuestWrite32(suspend_count_ptr, prev);
    return X_STATUS_SUCCESS;
  });

  // RtlUnwind (391) — SEH unwinding, stub
  RegisterExport(391, [](uint32_t* args) -> uint32_t {
    XELOGI("RtlUnwind: stub");
    return 0;
  });

  // sprintf-family CRT exports (common xboxkrnl ordinals)
  // _snprintf (ordinal 410)
  RegisterExport(410, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    if (dest) {
      *static_cast<uint8_t*>(xe::memory::TranslateVirtual(dest)) = 0;
    }
    return 0;
  });

  // sprintf (ordinal 411)
  RegisterExport(411, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    if (dest) {
      *static_cast<uint8_t*>(xe::memory::TranslateVirtual(dest)) = 0;
    }
    return 0;
  });

  // _vsnprintf (ordinal 412)
  RegisterExport(412, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    if (dest) {
      *static_cast<uint8_t*>(xe::memory::TranslateVirtual(dest)) = 0;
    }
    return 0;
  });

  // vsprintf (ordinal 413)
  RegisterExport(413, [](uint32_t* args) -> uint32_t {
    uint32_t dest = args[0];
    if (dest) {
      *static_cast<uint8_t*>(xe::memory::TranslateVirtual(dest)) = 0;
    }
    return 0;
  });

  // _vscprintf (ordinal 414) — returns length of formatted string
  RegisterExport(414, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // NtQueryInformationThread (210) — basic stub
  RegisterExport(210, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueryInformationThread: stub");
    return X_STATUS_SUCCESS;
  });

  // KeQueryPerformanceCounter (18) — already registered but re-registering
  // ensures we have the proper real implementation
  // (Already at correct ordinal)

  // ExRegisterTitleTerminateNotification (410) — conflict with _snprintf
  // Real ordinal is 420
  RegisterExport(420, [](uint32_t* args) -> uint32_t {
    XELOGI("ExRegisterTitleTerminateNotification: stub");
    return X_STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Register all sub-modules
  // ═══════════════════════════════════════════════════════════════════════════
  RegisterThreadingExports();
  RegisterMemoryExports();
  RegisterIoExports();

  XELOGI("Registered xboxkrnl exports ({} total)", g_exports.size());
}

}  // namespace xe::kernel::xboxkrnl
