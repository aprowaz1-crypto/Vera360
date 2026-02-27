/**
 * Vera360 — Xenia Edge
 * xam.xex shim — main module
 *
 * XAM (Xbox Application Model) provides higher-level system services:
 * user profiles, content management, networking, UI, achievements, etc.
 *
 * XAM ordinals are separate from xboxkrnl ordinals.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include <functional>
#include <unordered_map>
#include <cstring>

namespace xe::kernel::xam {

using ExportThunk = std::function<uint32_t(uint32_t*)>;
static std::unordered_map<uint32_t, ExportThunk> g_xam_exports;

void RegisterExport(uint32_t ordinal, ExportThunk thunk) {
  g_xam_exports[ordinal] = std::move(thunk);
}

uint32_t Dispatch(uint32_t ordinal, uint32_t* args) {
  auto it = g_xam_exports.find(ordinal);
  if (it != g_xam_exports.end()) {
    return it->second(args);
  }
  XELOGW("Unimplemented xam export: ordinal={}", ordinal);
  return 0;
}

// ── Forward declarations ─────────────────────────────────────────────────────
extern void RegisterUserExports();
extern void RegisterContentExports();

// ── Guest memory helpers ─────────────────────────────────────────────────────
static inline void GW32(uint32_t addr, uint32_t v) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

// ── Status codes ─────────────────────────────────────────────────────────────
static constexpr uint32_t X_ERROR_SUCCESS        = 0;
static constexpr uint32_t X_ERROR_ACCESS_DENIED  = 5;
static constexpr uint32_t X_ERROR_INVALID_PARAMETER = 87;
static constexpr uint32_t X_ERROR_FUNCTION_FAILED= 1627;
static constexpr uint32_t X_ERROR_NOT_FOUND      = 1168;
static constexpr uint32_t X_ERROR_NO_MORE_FILES  = 18;

void RegisterAllExports() {

  // ═══════════════════════════════════════════════════════════════════════════
  // Notification / Listener
  // ═══════════════════════════════════════════════════════════════════════════

  // XNotifyCreateListener (68)
  RegisterExport(68, [](uint32_t* args) -> uint32_t {
    XELOGI("XNotifyCreateListener: area=0x{:016X}", (uint64_t(args[0]) << 32) | args[1]);
    auto* state = KernelState::shared();
    return state ? state->AllocateHandle() : 0x300;
  });

  // XNotifyGetNext (69)
  RegisterExport(69, [](uint32_t* args) -> uint32_t {
    // No pending notifications
    return 0;  // FALSE
  });

  // XNotifyPositionUI (70)
  RegisterExport(70, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XNotifyDelayUI (— ordinal 71)
  RegisterExport(71, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Overlay / UI
  // ═══════════════════════════════════════════════════════════════════════════

  // XamShowMessageBoxUI (85)
  RegisterExport(85, [](uint32_t* args) -> uint32_t {
    XELOGI("XamShowMessageBoxUI");
    // Return "button 0 pressed"
    uint32_t result_ptr = args[7];
    if (result_ptr) GW32(result_ptr, 0);
    return X_ERROR_SUCCESS;
  });

  // XamShowKeyboardUI (— ordinal 87)
  RegisterExport(87, [](uint32_t* args) -> uint32_t {
    XELOGI("XamShowKeyboardUI");
    return X_ERROR_SUCCESS;
  });

  // XamShowGamerCardUI (88)
  RegisterExport(88, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XamShowNuiTroubleShooterUI (— some games)
  RegisterExport(90, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XamTaskShouldExit (91)
  RegisterExport(91, [](uint32_t* args) -> uint32_t {
    return 0;  // FALSE — keep running
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Achievements
  // ═══════════════════════════════════════════════════════════════════════════

  // XamUserWriteAchievements (— ordinal 564)
  RegisterExport(564, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserWriteAchievements: user={} count={}", args[0], args[1]);
    return X_ERROR_SUCCESS;
  });

  // XamUserCreateAchievementEnumerator (— ordinal 563)
  RegisterExport(563, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserCreateAchievementEnumerator");
    return X_ERROR_NOT_FOUND;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Networking (stubs — no actual XBL)
  // ═══════════════════════════════════════════════════════════════════════════

  // XNetGetTitleXnAddr (— ordinal 73)
  RegisterExport(73, [](uint32_t* args) -> uint32_t {
    XELOGI("XNetGetTitleXnAddr");
    return 0;  // XNET_GET_XNADDR_NONE
  });

  // XNetGetEthernetLinkStatus (— ordinal 74)
  RegisterExport(74, [](uint32_t* args) -> uint32_t {
    return 0;  // No Ethernet link
  });

  // XOnlineGetNatType (— ordinal 651)
  RegisterExport(651, [](uint32_t* args) -> uint32_t {
    return 1;  // XONLINE_NAT_OPEN
  });

  // XNetStartup (— ordinal 51)
  RegisterExport(51, [](uint32_t* args) -> uint32_t {
    XELOGI("XNetStartup");
    return X_ERROR_SUCCESS;
  });

  // XNetCleanup (52)
  RegisterExport(52, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XLiveInitialize (— ordinal 5000)
  RegisterExport(5000, [](uint32_t* args) -> uint32_t {
    XELOGI("XLiveInitialize");
    return X_ERROR_SUCCESS;
  });

  // XLiveInput (— ordinal 5001)
  RegisterExport(5001, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XLiveRender (5002)
  RegisterExport(5002, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XLiveUninitialize (5003)
  RegisterExport(5003, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Locale / language
  // ═══════════════════════════════════════════════════════════════════════════

  // XGetLanguage (— ordinal 400)
  RegisterExport(400, [](uint32_t* args) -> uint32_t {
    return 1;  // English
  });

  // XGetLocale (401)
  RegisterExport(401, [](uint32_t* args) -> uint32_t {
    return 1;  // English (US)
  });

  // XamGetLocale (402)
  RegisterExport(402, [](uint32_t* args) -> uint32_t {
    return 1;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Storage / save game
  // ═══════════════════════════════════════════════════════════════════════════

  // XamContentGetDeviceData (— ordinal 577)
  RegisterExport(577, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentGetDeviceData");
    return X_ERROR_NOT_FOUND;
  });

  // XamContentGetDeviceName (578)
  RegisterExport(578, [](uint32_t* args) -> uint32_t {
    return X_ERROR_NOT_FOUND;
  });

  // XamContentResolve (— ordinal 580)
  RegisterExport(580, [](uint32_t* args) -> uint32_t {
    return X_ERROR_NOT_FOUND;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // System / misc
  // ═══════════════════════════════════════════════════════════════════════════

  // XamGetSystemVersion (— ordinal 480)
  RegisterExport(480, [](uint32_t* args) -> uint32_t {
    // Return a reasonable dashboard version (2.0.17559.0)
    return 0x0200448F;  // Major=2, Minor=0, Build=17559
  });

  // XamLoaderLaunchTitle (— ordinal 15)
  RegisterExport(15, [](uint32_t* args) -> uint32_t {
    XELOGI("XamLoaderLaunchTitle");
    return X_ERROR_SUCCESS;
  });

  // XamLoaderTerminateTitle (16)
  RegisterExport(16, [](uint32_t* args) -> uint32_t {
    XELOGI("XamLoaderTerminateTitle");
    return X_ERROR_SUCCESS;
  });

  // XamLoaderGetLaunchDataSize (— ordinal 17)
  RegisterExport(17, [](uint32_t* args) -> uint32_t {
    uint32_t size_ptr = args[0];
    if (size_ptr) GW32(size_ptr, 0);
    return X_ERROR_NOT_FOUND;
  });

  // XamLoaderGetLaunchData (18)
  RegisterExport(18, [](uint32_t* args) -> uint32_t {
    return X_ERROR_NOT_FOUND;
  });

  // XamAlloc (— ordinal 490)
  RegisterExport(490, [](uint32_t* args) -> uint32_t {
    uint32_t flags = args[0];
    uint32_t size = args[1];
    uint32_t out_ptr = args[2];
    XELOGI("XamAlloc: size={}", size);
    // Use a simple bump allocator
    static uint32_t xam_heap = 0x30000000;
    uint32_t addr = xam_heap;
    xam_heap += (size + 15) & ~15;
    xe::memory::Commit(xe::memory::TranslateVirtual(addr), size,
                       xe::memory::PageAccess::kReadWrite);
    if (out_ptr) GW32(out_ptr, addr);
    return X_ERROR_SUCCESS;
  });

  // XamFree (491)
  RegisterExport(491, [](uint32_t* args) -> uint32_t {
    // No-op
    return X_ERROR_SUCCESS;
  });

  // XamInputGetCapabilities (— ordinal 310)
  RegisterExport(310, [](uint32_t* args) -> uint32_t {
    XELOGI("XamInputGetCapabilities: user={}", args[0]);
    return X_ERROR_FUNCTION_FAILED;  // No controller
  });

  // XamInputGetState (311)
  RegisterExport(311, [](uint32_t* args) -> uint32_t {
    return X_ERROR_FUNCTION_FAILED;
  });

  // XamInputSetState (312)
  RegisterExport(312, [](uint32_t* args) -> uint32_t {
    return X_ERROR_FUNCTION_FAILED;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Register sub-modules
  // ═══════════════════════════════════════════════════════════════════════════
  RegisterUserExports();
  RegisterContentExports();

  XELOGI("Registered xam exports ({} total)", g_xam_exports.size());
}

}  // namespace xe::kernel::xam
