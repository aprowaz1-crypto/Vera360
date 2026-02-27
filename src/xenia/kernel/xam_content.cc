/**
 * Vera360 — Xenia Edge
 * XAM content management shim
 */

#include "xenia/base/logging.h"
#include <functional>
#include <cstdint>

namespace xe::kernel::xam {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

void RegisterContentExports() {
  // XamContentCreateEnumerator (574)
  RegisterExport(574, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentCreateEnumerator");
    return 0;
  });

  // XamContentCreate (575)
  RegisterExport(575, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentCreate");
    return 0;
  });

  // XamContentClose (576)
  RegisterExport(576, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentClose");
    return 0;
  });

  // XamContentGetLicenseMask (579)
  RegisterExport(579, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentGetLicenseMask");
    return 0;
  });

  // XamEnumerate (20)
  RegisterExport(20, [](uint32_t* args) -> uint32_t {
    XELOGI("XamEnumerate");
    return 1;  // No more items
  });

  XELOGI("Registered xam content exports");
}

}  // namespace xe::kernel::xam
