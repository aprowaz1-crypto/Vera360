/**
 * Vera360 — Xenia Edge
 * Kernel State implementation
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xobject.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/base/logging.h"

namespace xe::kernel {

KernelState* KernelState::shared_instance_ = nullptr;

KernelState::KernelState() = default;
KernelState::~KernelState() = default;

KernelState* KernelState::shared() { return shared_instance_; }
void KernelState::SetShared(KernelState* state) { shared_instance_ = state; }

uint32_t KernelState::AllocateHandle() {
  std::lock_guard<std::mutex> lock(object_mutex_);
  return next_handle_++;
}

void KernelState::RegisterObject(uint32_t handle, XObject* object) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  objects_[handle] = object;
}

XObject* KernelState::GetObject(uint32_t handle) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  auto it = objects_.find(handle);
  return it != objects_.end() ? it->second : nullptr;
}

void KernelState::UnregisterObject(uint32_t handle) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  objects_.erase(handle);
}

XThread* KernelState::CreateThread(uint32_t stack_size, uint32_t entry_point,
                                   uint32_t param, bool suspended) {
  auto* thread = new XThread(this, stack_size, entry_point, param, suspended);
  uint32_t handle = AllocateHandle();
  thread->set_handle(handle);
  RegisterObject(handle, thread);
  threads_.push_back(thread);
  XELOGI("Created thread: handle=0x{:08X}, entry=0x{:08X}", handle, entry_point);
  return thread;
}

XThread* KernelState::GetCurrentThread() {
  if (current_thread_) return current_thread_;
  return threads_.empty() ? nullptr : threads_.front();
}

void KernelState::SetCurrentThread(XThread* thread) {
  current_thread_ = thread;
}

void KernelState::TerminateThread(XThread* thread, uint32_t exit_code) {
  XELOGI("Terminating thread: handle=0x{:08X}, exit_code={}", thread->handle(), exit_code);
  thread->Terminate(exit_code);
}

size_t KernelState::GetActiveThreadCount() const {
  size_t count = 0;
  for (auto* t : threads_) {
    if (!t->is_terminated() && !t->is_suspended()) count++;
  }
  return count;
}

XModule* KernelState::LoadModule(const std::string& path) {
  auto* module = new XModule(this, path);
  uint32_t handle = AllocateHandle();
  module->set_handle(handle);
  RegisterObject(handle, module);
  XELOGI("Loaded module: {} (handle=0x{:08X})", path, handle);
  return module;
}

XModule* KernelState::GetModule(const std::string& name) {
  std::lock_guard<std::mutex> lock(object_mutex_);
  for (auto& [h, obj] : objects_) {
    if (obj->type() != XObject::Type::kModule) continue;
    auto* mod = static_cast<XModule*>(obj);
    if (mod->name() == name) return mod;
  }
  return nullptr;
}

uint32_t KernelState::AllocateTLS() {
  std::lock_guard<std::mutex> lock(tls_mutex_);
  return next_tls_slot_++;
}

void KernelState::FreeTLS(uint32_t slot) {
  std::lock_guard<std::mutex> lock(tls_mutex_);
  for (auto& [tid, slots] : tls_data_) {
    slots.erase(slot);
  }
}

void KernelState::SetTLSValue(uint32_t thread_id, uint32_t slot, uint64_t value) {
  std::lock_guard<std::mutex> lock(tls_mutex_);
  tls_data_[thread_id][slot] = value;
}

uint64_t KernelState::GetTLSValue(uint32_t thread_id, uint32_t slot) {
  std::lock_guard<std::mutex> lock(tls_mutex_);
  auto it = tls_data_.find(thread_id);
  if (it == tls_data_.end()) return 0;
  auto sit = it->second.find(slot);
  return sit != it->second.end() ? sit->second : 0;
}

void KernelState::RegisterEvent(uint32_t handle, bool manual_reset, bool initial_state) {
  std::lock_guard<std::mutex> lock(event_mutex_);
  EventState es;
  es.signaled = initial_state;
  es.manual_reset = manual_reset;
  event_states_[handle] = es;
}

KernelState::EventState* KernelState::GetEventState(uint32_t handle) {
  std::lock_guard<std::mutex> lock(event_mutex_);
  auto it = event_states_.find(handle);
  return it != event_states_.end() ? &it->second : nullptr;
}

}  // namespace xe::kernel
