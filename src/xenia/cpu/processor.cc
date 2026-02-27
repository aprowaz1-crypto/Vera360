/**
 * Vera360 — Xenia Edge
 * CPU Processor implementation
 */

#include "xenia/cpu/processor.h"
#include "xenia/base/logging.h"

namespace xe::cpu {

Processor::Processor() = default;
Processor::~Processor() { Shutdown(); }

bool Processor::Initialize() {
  backend_ = std::make_unique<backend::arm64::ARM64Backend>();
  if (!backend_->Initialize()) {
    XELOGE("Failed to initialize ARM64 backend");
    return false;
  }
  XELOGI("CPU Processor initialized (ARM64 JIT)");
  return true;
}

void Processor::Shutdown() {
  if (backend_) {
    backend_->Shutdown();
    backend_.reset();
  }
  thread_states_.clear();
}

ThreadState* Processor::CreateThreadState(uint32_t thread_id) {
  auto ts = std::make_unique<ThreadState>();
  ts->thread_id = thread_id;
  // Set initial stack pointer (PPC r1)
  // Xbox 360 stack grows down from 0x70000000 region
  ts->gpr[1] = 0x70000000 - (thread_id * 0x100000);  // 1MB per thread stack
  
  ThreadState* ptr = ts.get();
  thread_states_.push_back(std::move(ts));
  
  XELOGI("Created thread state #{} (SP=0x{:08X})", thread_id, ptr->gpr[1]);
  return ptr;
}

void Processor::Execute(ThreadState* thread, uint32_t start_address) {
  thread->pc = start_address;
  backend_->Execute(start_address, thread);
}

void Processor::Step(ThreadState* thread) {
  // Single-step: compile just the current instruction
  backend_->Execute(thread->pc, thread);
  thread->pc += 4;
}

}  // namespace xe::cpu
