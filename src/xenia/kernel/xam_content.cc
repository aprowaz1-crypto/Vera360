/**
 * Vera360 — Xenia Edge
 * XAM content management shim
 *
 * Handles content enumeration, creation, deletion for save games,
 * DLC, title updates, and marketplace content.
 */

#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include <functional>
#include <cstdint>
#include <cstring>

namespace xe::kernel::xam {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

static inline void GW32(uint32_t addr, uint32_t v) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}

static constexpr uint32_t X_ERROR_SUCCESS    = 0;
static constexpr uint32_t X_ERROR_NOT_FOUND  = 1168;
static constexpr uint32_t X_ERROR_NO_MORE_FILES = 18;
static constexpr uint32_t X_ERROR_IO_PENDING = 997;

// Content types
static constexpr uint32_t XCONTENTTYPE_SAVEDGAME    = 0x00000001;
static constexpr uint32_t XCONTENTTYPE_MARKETPLACE  = 0x00000002;
static constexpr uint32_t XCONTENTTYPE_PUBLISHER     = 0x00000003;
static constexpr uint32_t XCONTENTTYPE_INSTALLED_GAME= 0x00060000;

void RegisterContentExports() {

  // ═══════════════════════════════════════════════════════════════════════════
  // Content enumeration
  // ═══════════════════════════════════════════════════════════════════════════

  // XamContentCreateEnumerator (574)
  RegisterExport(574, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t device_id = args[1];
    uint32_t content_type = args[2];
    uint32_t content_flags = args[3];
    uint32_t items_per_enum = args[4];
    uint32_t buffer_size_ptr = args[5];
    uint32_t handle_out = args[6];

    XELOGI("XamContentCreateEnumerator: user={} type=0x{:08X} items={}",
           user_index, content_type, items_per_enum);

    // Set required buffer size (XCONTENT_DATA = 0x244 per item)
    if (buffer_size_ptr) {
      uint32_t count = items_per_enum > 0 ? items_per_enum : 16;
      GW32(buffer_size_ptr, count * 0x244);
    }

    // Return enumerator handle
    if (handle_out) GW32(handle_out, 0x2000);

    return X_ERROR_SUCCESS;
  });

  // XamEnumerate (20)
  RegisterExport(20, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t buffer_ptr = args[1];
    uint32_t buffer_size = args[2];
    uint32_t items_returned_ptr = args[3];
    uint32_t overlapped_ptr = args[4];

    XELOGI("XamEnumerate: handle=0x{:08X}", handle);

    // Return 0 items — no content found
    if (items_returned_ptr) GW32(items_returned_ptr, 0);

    if (overlapped_ptr) {
      GW32(overlapped_ptr, X_ERROR_NO_MORE_FILES);
      GW32(overlapped_ptr + 4, 0);
    }

    return X_ERROR_NO_MORE_FILES;
  });

  // XamContentClose (576)
  RegisterExport(576, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentClose: handle=0x{:08X}", args[0]);
    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Content creation / management
  // ═══════════════════════════════════════════════════════════════════════════

  // XamContentCreate (575)
  RegisterExport(575, [](uint32_t* args) -> uint32_t {
    uint32_t user_index = args[0];
    uint32_t root_name_ptr = args[1];
    uint32_t content_data_ptr = args[2];
    uint32_t flags = args[3];
    uint32_t disposition_ptr = args[4];
    uint32_t license_mask_ptr = args[5];
    uint32_t cache_size = args[6];
    uint32_t content_size = args[7];
    uint32_t overlapped_ptr = args[8];

    XELOGI("XamContentCreate: user={} flags=0x{:08X}", user_index, flags);

    // Set disposition to "opened existing" or "created new"
    if (disposition_ptr) GW32(disposition_ptr, 1); // CREATED_NEW

    if (overlapped_ptr) {
      GW32(overlapped_ptr, X_ERROR_SUCCESS);
    }

    return X_ERROR_SUCCESS;
  });

  // XamContentCreateEx (— ordinal 585)
  RegisterExport(585, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentCreateEx");
    return X_ERROR_SUCCESS;
  });

  // XamContentDelete (— ordinal 581)
  RegisterExport(581, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentDelete");
    return X_ERROR_SUCCESS;
  });

  // XamContentFlush (— ordinal 582)
  RegisterExport(582, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // License
  // ═══════════════════════════════════════════════════════════════════════════

  // XamContentGetLicenseMask (579)
  RegisterExport(579, [](uint32_t* args) -> uint32_t {
    uint32_t mask_ptr = args[0];
    uint32_t overlapped_ptr = args[1];

    XELOGI("XamContentGetLicenseMask");

    // Return full license (all bits set)
    if (mask_ptr) GW32(mask_ptr, 0xFFFFFFFF);

    if (overlapped_ptr) {
      GW32(overlapped_ptr, X_ERROR_SUCCESS);
    }

    return X_ERROR_SUCCESS;
  });

  // XamContentGetThumbnail (— ordinal 583)
  RegisterExport(583, [](uint32_t* args) -> uint32_t {
    XELOGI("XamContentGetThumbnail");
    return X_ERROR_NOT_FOUND;
  });

  // XamContentSetThumbnail (584)
  RegisterExport(584, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Overlapped I/O helpers
  // ═══════════════════════════════════════════════════════════════════════════

  // XamGetOverlappedResult (5)
  RegisterExport(5, [](uint32_t* args) -> uint32_t {
    uint32_t overlapped_ptr = args[0];
    uint32_t result_ptr = args[1];
    uint32_t wait = args[2];

    XELOGI("XamGetOverlappedResult");

    // Return the error stored in overlapped
    if (overlapped_ptr && result_ptr) {
      uint32_t status = 0;
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(overlapped_ptr));
      status = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8)  | p[3];
      GW32(result_ptr, status);
    }

    return X_ERROR_SUCCESS;
  });

  // XamGetOverlappedExtendedError (6)
  RegisterExport(6, [](uint32_t* args) -> uint32_t {
    return X_ERROR_SUCCESS;
  });

  XELOGI("Registered xam content exports");
}

}  // namespace xe::kernel::xam
