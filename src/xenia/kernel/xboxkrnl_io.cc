/**
 * Vera360 — Xenia Edge
 * xboxkrnl I/O shim — NtCreateFile, NtReadFile, NtWriteFile, etc.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/logging.h"
#include <functional>

namespace xe::kernel::xboxkrnl {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

void RegisterIoExports() {
  // NtCreateFile (ordinal 186)
  RegisterExport(186, [](uint32_t* args) -> uint32_t {
    XELOGI("NtCreateFile");
    return 0xC0000034;  // STATUS_OBJECT_NAME_NOT_FOUND (placeholder)
  });

  // NtOpenFile (ordinal 202)
  RegisterExport(202, [](uint32_t* args) -> uint32_t {
    XELOGI("NtOpenFile");
    return 0xC0000034;
  });

  // NtReadFile (ordinal 209)
  RegisterExport(209, [](uint32_t* args) -> uint32_t {
    XELOGI("NtReadFile");
    return 0;
  });

  // NtWriteFile (ordinal 225)
  RegisterExport(225, [](uint32_t* args) -> uint32_t {
    XELOGI("NtWriteFile");
    return 0;
  });

  // NtClose (ordinal 184)
  RegisterExport(184, [](uint32_t* args) -> uint32_t {
    XELOGI("NtClose: handle=0x{:08X}", args[0]);
    return 0;
  });

  // NtQueryInformationFile (ordinal 206)
  RegisterExport(206, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueryInformationFile");
    return 0;
  });

  // NtSetInformationFile (ordinal 218)
  RegisterExport(218, [](uint32_t* args) -> uint32_t {
    XELOGI("NtSetInformationFile");
    return 0;
  });

  // NtQueryDirectoryFile (ordinal 205)
  RegisterExport(205, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueryDirectoryFile");
    return 0xC0000034;
  });

  // NtQueryFullAttributesFile (ordinal 208)
  RegisterExport(208, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueryFullAttributesFile");
    return 0xC0000034;
  });

  XELOGI("Registered xboxkrnl I/O exports");
}

}  // namespace xe::kernel::xboxkrnl
