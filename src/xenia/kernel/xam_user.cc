/**
 * Vera360 — Xenia Edge
 * XAM user/profile shim
 */

#include "xenia/base/logging.h"
#include <functional>
#include <cstdint>

namespace xe::kernel::xam {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

void RegisterUserExports() {
  // XamUserGetSigninState (528)
  RegisterExport(528, [](uint32_t* args) -> uint32_t {
    // Return signed in (1) for user 0
    uint32_t user_index = args[0];
    XELOGI("XamUserGetSigninState(user={})", user_index);
    return user_index == 0 ? 1 : 0;  // 1 = signed in locally
  });

  // XamUserGetSigninInfo (557)
  RegisterExport(557, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserGetSigninInfo");
    return 0;
  });

  // XamUserGetName (530)
  RegisterExport(530, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserGetName");
    // Would write "XeniaPlayer" to buffer at args[1]
    return 0;
  });

  // XamUserReadProfileSettings (566)
  RegisterExport(566, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserReadProfileSettings");
    return 0;
  });

  // XamShowSigninUI (1)
  RegisterExport(1, [](uint32_t* args) -> uint32_t {
    XELOGI("XamShowSigninUI");
    return 0;
  });

  // XamUserCheckPrivilege (550)
  RegisterExport(550, [](uint32_t* args) -> uint32_t {
    XELOGI("XamUserCheckPrivilege");
    return 0;  // Has privilege
  });

  XELOGI("Registered xam user exports");
}

}  // namespace xe::kernel::xam
