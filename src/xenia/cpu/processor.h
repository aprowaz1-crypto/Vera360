/**
 * Vera360 — Xenia Edge
 * CPU Processor — manages PPC emulation via interpreter and ARM64 JIT
 */
#pragma once

#include "xenia/cpu/backend/arm64/arm64_backend.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace xe::cpu::frontend { class PPCInterpreter; }

namespace xe::cpu {

/// Per-thread CPU state (represents one Xbox 360 hardware thread)
struct ThreadState {
  // PPC General Purpose Registers (r0-r31), 64-bit each
  uint64_t gpr[32] = {};

  // PPC Special Purpose Registers
  uint64_t lr = 0;    // Link Register
  uint64_t ctr = 0;   // Count Register
  uint64_t xer = 0;   // Integer Exception Register

  // Condition Register (8 x 4-bit fields)
  uint32_t cr = 0;

  // PPC Floating Point Registers (f0-f31)
  double fpr[32] = {};

  // VMX128 Vector Registers (v0-v127), 128-bit each
  alignas(16) uint8_t vmx[128][16] = {};

  // Program Counter
  uint32_t pc = 0;

  // Thread ID (Xbox 360 has 6 hardware threads: 3 cores × 2 threads)
  uint32_t thread_id = 0;

  // Reservation (for lwarx/stwcx atomic ops)
  uint32_t reserve_address = 0;
  bool reserve_valid = false;

  // Running flag — set to false to stop execution
  bool running = true;
};

/// HLE kernel export callback: (thread_state, ordinal)
using KernelDispatchFn = std::function<void(ThreadState*, uint32_t)>;

/// Execution mode
enum class ExecMode : uint8_t {
  kInterpreter = 0,
  kJIT,
};

class Processor {
 public:
  Processor();
  ~Processor();

  bool Initialize(uint8_t* guest_base = nullptr,
                  ExecMode mode = ExecMode::kInterpreter);
  void Shutdown();

  /// Set the kernel HLE dispatch (called for sc instructions / thunks)
  void SetKernelDispatch(KernelDispatchFn fn);

  /// Register an HLE thunk at guest_addr for given ordinal
  void RegisterThunk(uint32_t guest_addr, uint32_t ordinal);

  /// Create a new thread state
  ThreadState* CreateThreadState(uint32_t thread_id);

  /// Execute guest code starting at address on given thread
  void Execute(ThreadState* thread, uint32_t start_address);

  /// Execute a bounded number of instructions (interp only) — returns count
  uint64_t ExecuteBounded(ThreadState* thread, uint32_t start_address,
                          uint64_t max_instructions);

  /// Step one instruction (for debugging)
  void Step(ThreadState* thread);

  backend::arm64::ARM64Backend* GetBackend() { return backend_.get(); }
  frontend::PPCInterpreter* GetInterpreter() { return interpreter_.get(); }
  ExecMode exec_mode() const { return exec_mode_; }

 private:
  ExecMode exec_mode_ = ExecMode::kInterpreter;
  uint8_t* guest_base_ = nullptr;
  std::unique_ptr<backend::arm64::ARM64Backend> backend_;
  std::unique_ptr<frontend::PPCInterpreter> interpreter_;
  std::vector<std::unique_ptr<ThreadState>> thread_states_;
  KernelDispatchFn kernel_dispatch_;
};

}  // namespace xe::cpu
