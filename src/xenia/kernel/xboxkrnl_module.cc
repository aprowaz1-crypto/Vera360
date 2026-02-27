/**
 * Vera360 — Xenia Edge
 * xboxkrnl.exe shim — main module
 *
 * Implements high-level kernel exports (HLE) for the Xbox 360 kernel.
 * Each export is a thunk that intercepts the guest call and provides
 * the emulated behaviour on Android/Linux.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/logging.h"
#include <unordered_map>
#include <functional>

namespace xe::kernel::xboxkrnl {

/// Kernel export thunk signature
using ExportThunk = std::function<uint32_t(uint32_t* args)>;

static std::unordered_map<uint32_t, ExportThunk> g_exports;

/// Register a kernel export
void RegisterExport(uint32_t ordinal, ExportThunk thunk) {
  g_exports[ordinal] = std::move(thunk);
}

/// Dispatch a kernel call by ordinal
uint32_t Dispatch(uint32_t ordinal, uint32_t* args) {
  auto it = g_exports.find(ordinal);
  if (it != g_exports.end()) {
    return it->second(args);
  }
  XELOGW("Unimplemented xboxkrnl export: ordinal={}", ordinal);
  return 0;
}

/// Register all xboxkrnl exports
void RegisterAllExports() {
  // --- Process/Module ---
  RegisterExport(327, [](uint32_t* args) -> uint32_t {  // XexGetModuleHandle
    XELOGI("XexGetModuleHandle");
    return 0x80000000;  // fake handle
  });

  RegisterExport(404, [](uint32_t* args) -> uint32_t {  // ExGetXConfigSetting
    XELOGI("ExGetXConfigSetting(cat={}, setting={})", args[0], args[1]);
    return 0;
  });

  // --- Time ---
  RegisterExport(18, [](uint32_t* args) -> uint32_t {  // KeQueryPerformanceCounter
    XELOGI("KeQueryPerformanceCounter");
    return 0;
  });
  
  RegisterExport(154, [](uint32_t* args) -> uint32_t {  // KeQuerySystemTime
    XELOGI("KeQuerySystemTime");
    return 0;
  });

  // --- Debug ---
  RegisterExport(349, [](uint32_t* args) -> uint32_t {  // DbgPrint
    XELOGI("DbgPrint called");
    return 0;  // STATUS_SUCCESS
  });
  
  RegisterExport(374, [](uint32_t* args) -> uint32_t {  // RtlInitAnsiString
    XELOGI("RtlInitAnsiString");
    return 0;
  });

  XELOGI("Registered xboxkrnl exports ({} total)", g_exports.size());
}

}  // namespace xe::kernel::xboxkrnl
