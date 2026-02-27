/**
 * Vera360 — Xenia Edge
 * XThread — emulated Xbox 360 thread
 */
#pragma once

#include "xenia/kernel/xobject.h"
#include <cstdint>
#include <string>

namespace xe::kernel {

class XThread : public XObject {
 public:
  XThread(KernelState* state, uint32_t stack_size, uint32_t entry_point,
          uint32_t param, bool suspended)
      : XObject(state, Type::kThread),
        stack_size_(stack_size),
        entry_point_(entry_point),
        parameter_(param),
        suspended_(suspended) {}
  ~XThread() override = default;

  uint32_t entry_point() const { return entry_point_; }
  uint32_t parameter() const { return parameter_; }
  uint32_t stack_size() const { return stack_size_; }
  uint32_t thread_id() const { return thread_id_; }
  void set_thread_id(uint32_t id) { thread_id_ = id; }
  
  bool is_suspended() const { return suspended_; }
  void Resume() { suspended_ = false; }
  void Suspend() { suspended_ = true; }
  void Terminate(uint32_t exit_code) { terminated_ = true; exit_code_ = exit_code; }
  bool is_terminated() const { return terminated_; }
  uint32_t exit_code() const { return exit_code_; }

  void set_name(const std::string& n) { name_ = n; }
  const std::string& name() const { return name_; }

 private:
  uint32_t stack_size_;
  uint32_t entry_point_;
  uint32_t parameter_;
  uint32_t thread_id_ = 0;
  bool suspended_;
  bool terminated_ = false;
  uint32_t exit_code_ = 0;
  std::string name_;
};

}  // namespace xe::kernel
