/**
 * Vera360 — Xenia Edge
 * XObject — base class for all Xbox 360 kernel objects
 */
#pragma once

#include <cstdint>
#include <atomic>
#include <string>

namespace xe::kernel {

class KernelState;

/// Base type for all emulated Xbox 360 kernel objects
class XObject {
 public:
  enum class Type : uint8_t {
    kThread,
    kModule,
    kEvent,
    kMutant,
    kSemaphore,
    kTimer,
    kFile,
    kNotificationListener,
  };

  XObject(KernelState* state, Type type)
      : kernel_state_(state), type_(type) {}
  virtual ~XObject() = default;

  Type type() const { return type_; }
  uint32_t handle() const { return handle_; }
  void set_handle(uint32_t h) { handle_ = h; }

  uint32_t Retain() { return ++ref_count_; }
  uint32_t Release() {
    auto r = --ref_count_;
    if (r == 0) delete this;
    return r;
  }

 protected:
  KernelState* kernel_state_;
  Type type_;
  uint32_t handle_ = 0;
  std::atomic<uint32_t> ref_count_{1};
};

}  // namespace xe::kernel
