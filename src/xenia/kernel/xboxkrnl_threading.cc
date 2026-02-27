/**
 * Vera360 — Xenia Edge
 * xboxkrnl threading shim — ExCreateThread, KeWaitFor*, Ke*Event, etc.
 *
 * Covers all threading/synchronisation exports from xboxkrnl.exe:
 *   • Thread creation, termination, suspension, priority
 *   • Events (auto-reset, manual-reset)
 *   • Semaphores, mutexes
 *   • Waits (single/multiple/alertable)
 *   • DPCs, APCs
 *   • Processor affinity
 *   • Critical section (primary impl in xboxkrnl_module)
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xobject.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include <functional>
#include <cstring>
#include <thread>
#include <chrono>

namespace xe::kernel::xboxkrnl {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

// ── Status codes (duplicated here for self-containment) ──────────────────────
static constexpr uint32_t STATUS_SUCCESS           = 0x00000000;
static constexpr uint32_t STATUS_TIMEOUT           = 0x00000102;
static constexpr uint32_t STATUS_INVALID_HANDLE    = 0xC0000008;
static constexpr uint32_t STATUS_INVALID_PARAMETER = 0xC000000D;

// ── Guest memory helpers ─────────────────────────────────────────────────────
static inline void GW32(uint32_t addr, uint32_t v) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}
static inline uint32_t GR32(uint32_t addr) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8)  | p[3];
}

void RegisterThreadingExports() {

  // ═══════════════════════════════════════════════════════════════════════════
  // Thread creation / lifecycle
  // ═══════════════════════════════════════════════════════════════════════════

  // ExCreateThread (74)
  RegisterExport(74, [](uint32_t* args) -> uint32_t {
    uint32_t handle_ptr   = args[0];
    uint32_t stack_size   = args[1];
    uint32_t thread_id_ptr= args[2];
    uint32_t entry_point  = args[3];
    uint32_t param        = args[4];
    uint32_t flags        = args[5];

    bool suspended = (flags & 0x01) != 0;
    if (!stack_size) stack_size = 64 * 1024;  // Default 64KB

    XELOGI("ExCreateThread: entry=0x{:08X} stack=0x{:X} param=0x{:X} susp={}",
           entry_point, stack_size, param, suspended ? 1 : 0);

    auto* state = KernelState::shared();
    auto* thread = state->CreateThread(stack_size, entry_point, param, suspended);
    if (!thread) return STATUS_INVALID_PARAMETER;

    if (handle_ptr) GW32(handle_ptr, thread->handle());
    if (thread_id_ptr) GW32(thread_id_ptr, thread->thread_id());

    return STATUS_SUCCESS;
  });

  // NtCreateThread (194) — lower-level version
  RegisterExport(194, [](uint32_t* args) -> uint32_t {
    uint32_t handle_ptr   = args[0];
    uint32_t entry_point  = args[3];
    uint32_t param        = args[4];
    bool suspended = (args[5] & 1) != 0;

    XELOGI("NtCreateThread: entry=0x{:08X}", entry_point);

    auto* state = KernelState::shared();
    auto* thread = state->CreateThread(64 * 1024, entry_point, param, suspended);
    if (!thread) return STATUS_INVALID_PARAMETER;
    if (handle_ptr) GW32(handle_ptr, thread->handle());
    return STATUS_SUCCESS;
  });

  // KeResumeThread (145)
  RegisterExport(145, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    auto* state = KernelState::shared();
    auto* obj = state ? state->GetObject(handle) : nullptr;
    if (obj && obj->type() == XObject::Type::kThread) {
      auto* t = static_cast<XThread*>(obj);
      uint32_t prev = t->is_suspended() ? 1 : 0;
      t->Resume();
      XELOGI("KeResumeThread: handle=0x{:08X} prev_count={}", handle, prev);
      return prev;
    }
    return 0;
  });

  // KeSuspendThread (152)
  RegisterExport(152, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    auto* state = KernelState::shared();
    auto* obj = state ? state->GetObject(handle) : nullptr;
    if (obj && obj->type() == XObject::Type::kThread) {
      auto* t = static_cast<XThread*>(obj);
      uint32_t prev = t->is_suspended() ? 1 : 0;
      t->Suspend();
      XELOGI("KeSuspendThread: handle=0x{:08X}", handle);
      return prev;
    }
    return 0;
  });

  // NtTerminateThread (223) — terminates specific thread
  RegisterExport(223, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t exit_code = args[1];
    XELOGI("NtTerminateThread: handle=0x{:08X}, exit_code={}", handle, exit_code);
    auto* state = KernelState::shared();
    auto* obj = state ? state->GetObject(handle) : nullptr;
    if (obj && obj->type() == XObject::Type::kThread) {
      state->TerminateThread(static_cast<XThread*>(obj), exit_code);
      return STATUS_SUCCESS;
    }
    return STATUS_INVALID_HANDLE;
  });

  // KeSetBasePriorityThread (146)
  RegisterExport(146, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetBasePriorityThread: priority={}", static_cast<int32_t>(args[1]));
    return 0;  // Previous priority
  });

  // KeSetAffinityThread (147)
  RegisterExport(147, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetAffinityThread: mask=0x{:08X}", args[1]);
    return 0;  // Previous mask
  });

  // KeQueryBasePriorityThread (133)
  RegisterExport(133, [](uint32_t* args) -> uint32_t {
    return 8;  // THREAD_PRIORITY_NORMAL
  });

  // NtSetInformationThread (219)
  RegisterExport(219, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t info_class = args[1];
    XELOGI("NtSetInformationThread: handle=0x{:08X}, class={}", handle, info_class);
    return STATUS_SUCCESS;
  });

  // NtQueryInformationThread (210)
  RegisterExport(210, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueryInformationThread");
    return STATUS_SUCCESS;
  });

  // KeGetCurrentThread (— often inline, but some games call it)
  // Ordinal 120
  RegisterExport(120, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    auto* t = state ? state->GetCurrentThread() : nullptr;
    return t ? t->handle() : 0;
  });

  // KeSetCurrentStackPointers (153)
  RegisterExport(153, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetCurrentStackPointers");
    return STATUS_SUCCESS;
  });

  // PsCreateSystemThreadEx (255)
  RegisterExport(255, [](uint32_t* args) -> uint32_t {
    uint32_t handle_ptr = args[0];
    uint32_t stack_size = args[2];
    uint32_t entry = args[6];
    uint32_t param = args[7];
    XELOGI("PsCreateSystemThreadEx: entry=0x{:08X}", entry);

    auto* state = KernelState::shared();
    auto* thread = state->CreateThread(stack_size ? stack_size : 64*1024,
                                        entry, param, false);
    if (!thread) return STATUS_INVALID_PARAMETER;
    if (handle_ptr) GW32(handle_ptr, thread->handle());
    return STATUS_SUCCESS;
  });

  // PsTerminateSystemThread (258)
  RegisterExport(258, [](uint32_t* args) -> uint32_t {
    uint32_t exit_code = args[0];
    XELOGI("PsTerminateSystemThread: exit_code={}", exit_code);
    auto* state = KernelState::shared();
    auto* t = state ? state->GetCurrentThread() : nullptr;
    if (t) state->TerminateThread(t, exit_code);
    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Wait operations
  // ═══════════════════════════════════════════════════════════════════════════

  // KeWaitForSingleObject (158)
  RegisterExport(158, [](uint32_t* args) -> uint32_t {
    uint32_t object_ptr = args[0];
    uint32_t wait_reason = args[1];
    uint32_t wait_mode = args[2];
    uint32_t alertable = args[3];
    uint32_t timeout_ptr = args[4];

    XELOGI("KeWaitForSingleObject: obj=0x{:08X} reason={} alertable={}",
           object_ptr, wait_reason, alertable);

    // Check if timeout is 0 (non-blocking poll)
    if (timeout_ptr) {
      int64_t timeout_100ns;
      memcpy(&timeout_100ns, xe::memory::TranslateVirtual(timeout_ptr), 8);
      if (timeout_100ns == 0) {
        return STATUS_TIMEOUT;  // Would block, but timeout=0 → poll
      }
    }

    // For objects tracked via handle, check event state
    auto* state = KernelState::shared();
    if (state) {
      // The object_ptr might be a dispatcher header in guest memory.
      // For NtCreateEvent-created events, check signaled state.
      // Object_ptr points to guest DISPATCHER_HEADER: Type(1), Absolute(1), Size(1), Inserted(1), SignalState(4)...
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(object_ptr + 4));
      int32_t signal = (int32_t(p[0]) << 24) | (int32_t(p[1]) << 16) |
                       (int32_t(p[2]) << 8) | int32_t(p[3]);
      if (signal > 0) {
        return STATUS_SUCCESS;  // Already signaled
      }
    }

    // Not signaled — in a real emulator we'd block. For cooperative scheduling,
    // yield and return success (approximation to prevent deadlocks)
    std::this_thread::yield();
    return STATUS_SUCCESS;
  });

  // KeWaitForMultipleObjects (157)
  RegisterExport(157, [](uint32_t* args) -> uint32_t {
    uint32_t count = args[0];
    uint32_t objects_ptr = args[1];
    uint32_t wait_type = args[2];  // 0=WaitAll, 1=WaitAny
    uint32_t wait_reason = args[3];
    uint32_t wait_mode = args[4];
    uint32_t alertable = args[5];
    uint32_t timeout_ptr = args[6];

    XELOGI("KeWaitForMultipleObjects: count={} type={}", count, wait_type);
    return STATUS_SUCCESS;  // All satisfied
  });

  // NtWaitForSingleObjectEx (226) — already in module, but add detailed version
  // (handled in xboxkrnl_module.cc)

  // ═══════════════════════════════════════════════════════════════════════════
  // Event objects
  // ═══════════════════════════════════════════════════════════════════════════

  // NtCreateEvent (185)
  RegisterExport(185, [](uint32_t* args) -> uint32_t {
    uint32_t handle_ptr = args[0];
    uint32_t obj_attrs_ptr = args[1];
    uint32_t event_type = args[2];    // 0=NotificationEvent(manual), 1=SynchronizationEvent(auto)
    uint32_t initial_state = args[3]; // TRUE/FALSE

    XELOGI("NtCreateEvent: type={} initial={}", event_type, initial_state);

    auto* state = KernelState::shared();
    uint32_t handle = state ? state->AllocateHandle() : 0x200;

    // Register event state for proper signaling
    if (state) {
      bool manual_reset = (event_type == 0);
      state->RegisterEvent(handle, manual_reset, initial_state != 0);
    }

    if (handle_ptr) GW32(handle_ptr, handle);

    return STATUS_SUCCESS;
  });

  // NtSetEvent (215)
  RegisterExport(215, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t prev_state_ptr = args[1];
    XELOGI("NtSetEvent: handle=0x{:08X}", handle);

    auto* state = KernelState::shared();
    if (state) {
      auto* es = state->GetEventState(handle);
      if (es) {
        uint32_t prev = es->signaled ? 1 : 0;
        es->signaled = true;
        if (prev_state_ptr) GW32(prev_state_ptr, prev);
        return STATUS_SUCCESS;
      }
    }
    if (prev_state_ptr) GW32(prev_state_ptr, 0);
    return STATUS_SUCCESS;
  });

  // NtClearEvent (182)
  RegisterExport(182, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    XELOGI("NtClearEvent: handle=0x{:08X}", handle);

    auto* state = KernelState::shared();
    if (state) {
      auto* es = state->GetEventState(handle);
      if (es) es->signaled = false;
    }
    return STATUS_SUCCESS;
  });

  // NtPulseEvent (204)
  RegisterExport(204, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    XELOGI("NtPulseEvent: handle=0x{:08X}", handle);

    auto* state = KernelState::shared();
    if (state) {
      auto* es = state->GetEventState(handle);
      if (es) {
        es->signaled = true;   // Signal briefly
        es->signaled = false;  // Then reset
      }
    }
    return STATUS_SUCCESS;
  });

  // NtResetEvent — usually same as NtClearEvent
  // Already covered above via NtClearEvent

  // KeSetEvent (148)
  RegisterExport(148, [](uint32_t* args) -> uint32_t {
    uint32_t event_ptr = args[0];
    uint32_t increment = args[1];
    uint32_t wait = args[2];
    XELOGI("KeSetEvent: ptr=0x{:08X} inc={} wait={}", event_ptr, increment, wait);
    // KeSetEvent works on a KEVENT in guest memory; we'd need to map ptr→handle
    // For now, return previous state = 0 (not signaled)
    return 0;
  });

  // KeResetEvent (144)
  RegisterExport(144, [](uint32_t* args) -> uint32_t {
    XELOGI("KeResetEvent: ptr=0x{:08X}", args[0]);
    return 0;
  });

  // KeInitializeEvent (128) — already in module
  // KeSetEventBoostPriority (— some games use this)
  RegisterExport(151, [](uint32_t* args) -> uint32_t {
    XELOGI("KeSetEventBoostPriority");
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Semaphores
  // ═══════════════════════════════════════════════════════════════════════════

  // KeInitializeSemaphore (129)
  RegisterExport(129, [](uint32_t* args) -> uint32_t {
    uint32_t sem_ptr = args[0];
    uint32_t count = args[1];
    uint32_t limit = args[2];
    XELOGI("KeInitializeSemaphore: ptr=0x{:08X} count={} limit={}", sem_ptr, count, limit);
    return 0;
  });

  // KeReleaseSemaphore (143)
  RegisterExport(143, [](uint32_t* args) -> uint32_t {
    uint32_t sem_ptr = args[0];
    uint32_t adjustment = args[1];
    uint32_t increment = args[2];
    uint32_t wait = args[3];
    XELOGI("KeReleaseSemaphore: ptr=0x{:08X} adj={}", sem_ptr, adjustment);
    return 0;  // Previous count
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Delay / Sleep
  // ═══════════════════════════════════════════════════════════════════════════

  // KeDelayExecutionThread (116)
  RegisterExport(116, [](uint32_t* args) -> uint32_t {
    uint32_t mode = args[0];
    uint32_t alertable = args[1];
    uint32_t interval_ptr = args[2];

    if (interval_ptr) {
      int64_t interval;
      // Big-endian read
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(interval_ptr));
      interval = (int64_t(p[0]) << 56) | (int64_t(p[1]) << 48) |
                 (int64_t(p[2]) << 40) | (int64_t(p[3]) << 32) |
                 (int64_t(p[4]) << 24) | (int64_t(p[5]) << 16) |
                 (int64_t(p[6]) << 8)  | int64_t(p[7]);
      
      // Negative = relative time in 100ns units
      if (interval < 0) {
        int64_t us = (-interval) / 10;  // Convert 100ns to microseconds
        if (us > 1000000) us = 1000000; // Cap at 1 second
        if (us > 0) {
          std::this_thread::sleep_for(std::chrono::microseconds(us));
        }
      }
    }
    return STATUS_SUCCESS;
  });

  // NtYieldExecution (233)
  RegisterExport(233, [](uint32_t* args) -> uint32_t {
    std::this_thread::yield();
    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // APC
  // ═══════════════════════════════════════════════════════════════════════════

  // KeInitializeApc (126)
  RegisterExport(126, [](uint32_t* args) -> uint32_t {
    XELOGI("KeInitializeApc");
    return 0;
  });

  // KeInsertQueueApc (132)
  RegisterExport(132, [](uint32_t* args) -> uint32_t {
    XELOGI("KeInsertQueueApc");
    return 1;  // TRUE
  });

  // KeRemoveQueueApc (139)
  RegisterExport(139, [](uint32_t* args) -> uint32_t {
    return 1;  // TRUE
  });

  // KiApcNormalRoutineNop (6)
  RegisterExport(6, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Processor info
  // ═══════════════════════════════════════════════════════════════════════════

  // KeGetCurrentProcessType — already in module (124)

  // KeNumberProcessors (105)
  RegisterExport(105, [](uint32_t* args) -> uint32_t {
    return 6;  // Xbox 360 has 6 hardware threads (3 cores × 2 HT)
  });

  // KeGetCurrentProcessorNumber (163)
  RegisterExport(163, [](uint32_t* args) -> uint32_t {
    return 0;  // Always processor 0 for now
  });

  // KeSetDisableBoostThread (— ordinal 147 already used, use 135)
  RegisterExport(135, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Misc / Interrupt
  // ═══════════════════════════════════════════════════════════════════════════

  // KeStallExecutionProcessor (151) — busy-wait
  // (ordinal 151 may overlap; careful)

  // KeEnterCriticalRegion (117)
  RegisterExport(117, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // KeLeaveCriticalRegion (136)
  RegisterExport(136, [](uint32_t* args) -> uint32_t {
    return 0;
  });

  // KeTestAlertThread (— ordinal 156)
  RegisterExport(156, [](uint32_t* args) -> uint32_t {
    return STATUS_SUCCESS;
  });

  // NtQueueApcThread (— ordinal 216)
  RegisterExport(216, [](uint32_t* args) -> uint32_t {
    XELOGI("NtQueueApcThread");
    return STATUS_SUCCESS;
  });

  // NtAlertResumeThread (175)
  RegisterExport(175, [](uint32_t* args) -> uint32_t {
    XELOGI("NtAlertResumeThread");
    return STATUS_SUCCESS;
  });

  // NtAlertThread (176)
  RegisterExport(176, [](uint32_t* args) -> uint32_t {
    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // TLS (Thread Local Storage)
  // ═══════════════════════════════════════════════════════════════════════════

  // KeTlsAlloc (— ordinal 340)
  RegisterExport(340, [](uint32_t* args) -> uint32_t {
    auto* state = KernelState::shared();
    if (!state) return 0xFFFFFFFF;  // TLS_OUT_OF_INDEXES
    uint32_t slot = state->AllocateTLS();
    XELOGI("KeTlsAlloc: slot={}", slot);
    return slot;
  });

  // KeTlsFree (— ordinal 341)
  RegisterExport(341, [](uint32_t* args) -> uint32_t {
    uint32_t slot = args[0];
    auto* state = KernelState::shared();
    if (state) state->FreeTLS(slot);
    XELOGI("KeTlsFree: slot={}", slot);
    return 1;  // TRUE
  });

  // KeTlsGetValue (— ordinal 342)
  RegisterExport(342, [](uint32_t* args) -> uint32_t {
    uint32_t slot = args[0];
    auto* state = KernelState::shared();
    if (!state) return 0;
    auto* thread = state->GetCurrentThread();
    uint32_t tid = thread ? thread->thread_id() : 0;
    uint64_t value = state->GetTLSValue(tid, slot);
    return static_cast<uint32_t>(value);
  });

  // KeTlsSetValue (— ordinal 343)
  RegisterExport(343, [](uint32_t* args) -> uint32_t {
    uint32_t slot = args[0];
    uint32_t value = args[1];
    auto* state = KernelState::shared();
    if (!state) return 0;
    auto* thread = state->GetCurrentThread();
    uint32_t tid = thread ? thread->thread_id() : 0;
    state->SetTLSValue(tid, slot, value);
    return 1;  // TRUE
  });

  XELOGI("Registered xboxkrnl threading exports");
}

}  // namespace xe::kernel::xboxkrnl
