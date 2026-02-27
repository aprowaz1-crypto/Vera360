/**
 * Vera360 — Xenia Edge
 * CPU Processor implementation — interpreter + JIT backend
 */

#include "xenia/cpu/processor.h"
#include "xenia/cpu/frontend/ppc_interpreter.h"
#include "xenia/base/logging.h"

namespace xe::cpu {

Processor::Processor() = default;
Processor::~Processor() { Shutdown(); }

bool Processor::Initialize(uint8_t* guest_base, ExecMode mode) {
  exec_mode_ = mode;
  guest_base_ = guest_base;

  // Always create the interpreter (used for fallback / debugging)
  interpreter_ = std::make_unique<frontend::PPCInterpreter>();
  if (guest_base_) {
    interpreter_->SetGuestBase(guest_base_);
  }

  if (mode == ExecMode::kJIT) {
    backend_ = std::make_unique<backend::arm64::ARM64Backend>();
    if (!backend_->Initialize()) {
      XELOGW("ARM64 JIT init failed — falling back to interpreter");
      exec_mode_ = ExecMode::kInterpreter;
    } else {
      XELOGI("CPU Processor initialized (ARM64 JIT + interpreter fallback)");
    }
  }

  if (exec_mode_ == ExecMode::kInterpreter) {
    XELOGI("CPU Processor initialized (pure interpreter)");
  }

  return true;
}

void Processor::Shutdown() {
  if (backend_) {
    backend_->Shutdown();
    backend_.reset();
  }
  interpreter_.reset();
  thread_states_.clear();
}

void Processor::SetKernelDispatch(KernelDispatchFn fn) {
  kernel_dispatch_ = std::move(fn);
  if (interpreter_) {
    interpreter_->SetHleDispatch(kernel_dispatch_);
  }
}

void Processor::RegisterThunk(uint32_t guest_addr, uint32_t ordinal) {
  if (interpreter_) {
    interpreter_->RegisterThunk(guest_addr, ordinal);
  }
}

ThreadState* Processor::CreateThreadState(uint32_t thread_id) {
  auto ts = std::make_unique<ThreadState>();
  ts->thread_id = thread_id;
  // Xbox 360 stack grows down from 0x70000000 region, 1MB per thread
  ts->gpr[1] = 0x70000000 - (thread_id * 0x100000);
  ts->running = true;
  
  ThreadState* ptr = ts.get();
  thread_states_.push_back(std::move(ts));
  
  XELOGI("Created thread state #{} (SP=0x{:08X})", thread_id,
         static_cast<uint32_t>(ptr->gpr[1]));
  return ptr;
}

void Processor::Execute(ThreadState* thread, uint32_t start_address) {
  thread->pc = start_address;
  thread->running = true;

  if (exec_mode_ == ExecMode::kJIT && backend_) {
    backend_->Execute(start_address, thread);
  } else if (interpreter_) {
    interpreter_->Run(thread, 0);  // Run until blr / halt
  }
}

uint64_t Processor::ExecuteBounded(ThreadState* thread, uint32_t start_address,
                                   uint64_t max_instructions) {
  thread->pc = start_address;
  thread->running = true;

  if (interpreter_) {
    return interpreter_->Run(thread, max_instructions);
  }
  return 0;
}

void Processor::Step(ThreadState* thread) {
  if (interpreter_) {
    interpreter_->Step(thread);
  } else if (backend_) {
    backend_->Execute(thread->pc, thread);
    thread->pc += 4;
  }
}

}  // namespace xe::cpu
