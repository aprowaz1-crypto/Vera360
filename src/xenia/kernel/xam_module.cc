/**
 * Vera360 — Xenia Edge
 * xam.xex shim — main module
 *
 * XAM (Xbox Application Model) provides higher-level system services:
 * user profiles, content management, networking, UI, etc.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/logging.h"
#include <functional>
#include <unordered_map>

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

void RegisterAllExports();

}  // namespace xe::kernel::xam
