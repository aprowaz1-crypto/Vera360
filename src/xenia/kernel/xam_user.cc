/**
 * Vera360 — Xenia Edge
 * XAM user/profile shim
 *
 * Handles user sign-in state, profile settings, gamertags, achievements,
 * and various user-related XAM exports.
 */

#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include <functional>
#include <cstdint>
#include <cstring>

namespace xe::kernel::xam {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

// ── Guest memory helpers ─────────────────────────────────────────────────────
static inline void GW32(uint32_t addr, uint32_t v) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

static constexpr uint32_t X_ERROR_SUCCESS = 0;
static constexpr uint32_t X_ERROR_NO_SUCH_USER = 1008;
static constexpr uint32_t X_ERROR_NOT_LOGGED_ON = 1757;

/// Default gamertag
static const char* kDefaultGamertag = "Vera360Player";
static constexpr uint64_t kDefaultXuid = 0x0009000000000001ULL;

void RegisterUserExports() {

  // ═══════════════════════════════════════════════════════════════════════════
  // Sign-in state
  // ═══════════════════════════════════════════════════════════════════════════

  // XamUserGetSigninState (528)
  RegisterExport(528, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    XELOGI("XamUserGetSigninState(user={})", user_index);
    // Only user 0 is signed in
    // 0 = not signed in, 1 = signed in locally, 2 = signed in to Live
    return user_index == 0 ? 1 : 0;
  });

  // XamUserGetSigninInfo (557)
  RegisterExport(557, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t flags = args[1];
    uint32_t info_ptr = args[2];

    XELOGI("XamUserGetSigninInfo(user={}, flags=0x{:X})", user_index, flags);

    if (user_index != 0) return X_ERROR_NO_SUCH_USER;

    if (info_ptr) {
      auto* info = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
      memset(info, 0, 0x64);  // XUSER_SIGNIN_INFO size

      // XUID at offset 0 (8 bytes, BE)
      uint64_t xuid = kDefaultXuid;
      for (int i = 0; i < 8; ++i) {
        info[i] = uint8_t(xuid >> ((7-i)*8));
      }

      // SigninState at offset 8: 1 = local
      GW32(info_ptr + 8, 1);

      // GuestNumber at offset 12
      GW32(info_ptr + 12, 0);

      // Gamertag at offset 16 (16 wide chars)
      auto* gtag = info + 16;
      const char* name = kDefaultGamertag;
      for (int i = 0; name[i] && i < 15; ++i) {
        gtag[i*2] = 0;
        gtag[i*2+1] = static_cast<uint8_t>(name[i]);
      }
    }

    return X_ERROR_SUCCESS;
  });

  // XamUserGetName (530)
  RegisterExport(530, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t buffer_ptr = args[1];
    uint32_t buffer_size = args[2];

    XELOGI("XamUserGetName(user={})", user_index);

    if (user_index != 0) return X_ERROR_NO_SUCH_USER;

    if (buffer_ptr) {
      auto* buf = static_cast<char*>(xe::memory::TranslateVirtual(buffer_ptr));
      size_t max_len = buffer_size < 16 ? buffer_size : 16;
      strncpy(buf, kDefaultGamertag, max_len - 1);
      buf[max_len - 1] = '\0';
    }

    return X_ERROR_SUCCESS;
  });

  // XamUserGetXUID (529)
  RegisterExport(529, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t type = args[1];  // 1 = offline, 2 = online
    uint32_t xuid_ptr = args[2];

    XELOGI("XamUserGetXUID(user={}, type={})", user_index, type);

    if (user_index != 0) return X_ERROR_NO_SUCH_USER;

    if (xuid_ptr) {
      uint64_t xuid = kDefaultXuid;
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(xuid_ptr));
      for (int i = 0; i < 8; ++i) p[i] = uint8_t(xuid >> ((7-i)*8));
    }

    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Profile settings
  // ═══════════════════════════════════════════════════════════════════════════

  // XamUserReadProfileSettings (566)
  RegisterExport(566, [](uint32_t* args) -> uint32_t {
    uint32_t title_id = args[0];
    uint32_t user_index = args[1];
    uint32_t num_setting_ids = args[2];
    uint32_t setting_ids_ptr = args[3];
    uint32_t buffer_size_ptr = args[4];
    uint32_t buffer_ptr = args[5];
    uint32_t overlapped_ptr = args[6];

    XELOGI("XamUserReadProfileSettings: user={} count={}", user_index, num_setting_ids);

    // Return "no settings" — needs buffer_size = 0
    if (buffer_size_ptr) GW32(buffer_size_ptr, 0);

    // If overlapped, signal completion
    if (overlapped_ptr) {
      // XOVERLAPPED: {ULONG InternalLow, ULONG InternalHigh, ...}
      GW32(overlapped_ptr, X_ERROR_SUCCESS);
      GW32(overlapped_ptr + 4, 0);
    }

    return X_ERROR_SUCCESS;
  });

  // XamUserWriteProfileSettings (567)
  RegisterExport(567, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserWriteProfileSettings");
    return X_ERROR_SUCCESS;
  });

  // XamProfileCreate (— ordinal 540)
  RegisterExport(540, [](uint32_t* args) -> uint32_t {
    XELOGI("XamProfileCreate");
    return X_ERROR_SUCCESS;
  });

  // XamProfileFindAccount (541)
  RegisterExport(541, [](uint32_t* args) -> uint32_t {
    return X_ERROR_NO_SUCH_USER;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // UI / Signin overlay
  // ═══════════════════════════════════════════════════════════════════════════

  // XamShowSigninUI (1)
  RegisterExport(1, [](uint32_t* args) -> uint32_t {
    uint32_t requesting_pane = args[0];
    uint32_t flags = args[1];
    XELOGI("XamShowSigninUI: pane={} flags=0x{:X}", requesting_pane, flags);
    return X_ERROR_SUCCESS;
  });

  // XamShowAchievementsUI (— ordinal 86)
  RegisterExport(86, [](uint32_t* args) -> uint32_t {
    XELOGI("XamShowAchievementsUI");
    return X_ERROR_SUCCESS;
  });

  // XamShowFriendsUI (89)
  RegisterExport(89, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // XamShowDeviceSelectorUI (— ordinal 92)
  RegisterExport(92, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t content_type = args[1];
    uint32_t content_flags = args[2];
    uint32_t device_id_count = args[3];
    uint32_t device_id_ptr = args[4];
    uint32_t overlapped_ptr = args[5];

    XELOGI("XamShowDeviceSelectorUI: user={} type=0x{:08X}", user_index, content_type);

    // Return the "hard drive" device
    if (device_id_ptr) GW32(device_id_ptr, 1);

    if (overlapped_ptr) {
      GW32(overlapped_ptr, X_ERROR_SUCCESS);
    }

    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Privilege / Access
  // ═══════════════════════════════════════════════════════════════════════════

  // XamUserCheckPrivilege (550)
  RegisterExport(550, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t privilege_type = args[1];
    uint32_t result_ptr = args[2];
    XELOGI("XamUserCheckPrivilege: user={} priv={}", user_index, privilege_type);
    // Grant all privileges
    if (result_ptr) GW32(result_ptr, 1); // TRUE = has privilege
    return X_ERROR_SUCCESS;
  });

  // XamUserAreUsersFriends (551)
  RegisterExport(551, [](uint32_t* args) -> uint32_t {
    uint32_t result_ptr = args[2];
    if (result_ptr) GW32(result_ptr, 0);  // Not friends
    return X_ERROR_SUCCESS;
  });

  // XamUserIsGuest (— ordinal 533)
  RegisterExport(533, [](uint32_t* args) -> uint32_t {
    return 0;  // Not a guest
  });

  // XamUserGetMembershipTier (— ordinal 534)
  RegisterExport(534, [](uint32_t* args) -> uint32_t {
    return 6;  // Gold
  });

  XELOGI("Registered xam user exports");
}

}  // namespace xe::kernel::xam
