/**
 * Vera360 — Xenia Edge
 * xboxkrnl threading shim — KeCreateThread, KeWaitFor*, etc.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xthread.h"
#include "xenia/base/logging.h"
#include <functional>

namespace xe::kernel::xboxkrnl {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

void RegisterThreadingExports() {
  // ExCreateThread (ordinal 74)
  RegisterExport(74, [](uint32_t* args) -> uint32_t {
    uint32_t handle_ptr = args[0];
    uint32_t stack_size = args[1];
    uint32_t entry_point = args[3];
    uint32_t param = args[4];
    bool suspended = (args[5] & 1) != 0;

    XELOGI("ExCreateThread: entry=0x{:08X}, stack=0x{:X}, param=0x{:X}",
           entry_point, stack_size, param);

    auto* state = KernelState::shared();
    auto* thread = state->CreateThread(
        stack_size ? stack_size : (64 * 1024),
        entry_point, param, suspended);

    return thread ? 0 : 0xC000000D;  // STATUS_INVALID_PARAMETER
  });

  // KeResumeThread (ordinal 145)
  RegisterExport(145, [](uint32_t* args) -> uint32_t {
    XELOGI("KeResumeThread");
    return 0;
  });

  // KeSuspendThread (ordinal 152)
  RegisterExport(152, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSuspendThread");
    return 0;
  });

  // KeWaitForSingleObject (ordinal 158)
  RegisterExport(158, [](uint32_t* args) -> uint32_t {
    XELOGI("KeWaitForSingleObject");
    return 0;  // WAIT_OBJECT_0
  });

  // KeWaitForMultipleObjects (ordinal 157)
  RegisterExport(157, [](uint32_t* args) -> uint32_t {
    XELOGI("KeWaitForMultipleObjects");
    return 0;
  });

  // KeSetEvent (ordinal 148)
  RegisterExport(148, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetEvent");
    return 0;
  });

  // KeResetEvent (ordinal 144)
  RegisterExport(144, [](uint32_t* args) -> uint32_t {
    XELOGI("KeResetEvent");
    return 0;
  });

  // NtCreateEvent (ordinal 185)
  RegisterExport(185, [](uint32_t* args) -> uint32_t {
    XELOGI("NtCreateEvent");
    return 0;
  });

  // KeInitializeSemaphore (ordinal 129)
  RegisterExport(129, [](uint32_t* args) -> uint32_t {
    XELOGI("KeInitializeSemaphore");
    return 0;
  });

  // KeReleaseSemaphore (ordinal 143)
  RegisterExport(143, [](uint32_t* args) -> uint32_t {
    XELOGI("KeReleaseSemaphore");
    return 1;  // Previous count
  });

  // KeDelayExecutionThread (ordinal 116)
  RegisterExport(116, [](uint32_t* args) -> uint32_t {
    XELOGI("KeDelayExecutionThread");
    return 0;
  });

  // KeGetCurrentProcessType (ordinal 124)
  RegisterExport(124, [](uint32_t* args) -> uint32_t {
    return 1;  // System process
  });

  XELOGI("Registered xboxkrnl threading exports");
}

}  // namespace xe::kernel::xboxkrnl
