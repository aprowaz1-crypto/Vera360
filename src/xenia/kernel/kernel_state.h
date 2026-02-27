/**
 * Vera360 — Xenia Edge
 * Kernel State — holds the emulated Xbox 360 kernel state
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <string>
#include <vector>

namespace xe::kernel {

class XObject;
class XThread;
class XModule;

/**
 * Manages the state of the emulated Xbox 360 kernel.
 * Owns all kernel objects (threads, modules, mutants, events, etc.).
 */
class KernelState {
 public:
  KernelState();
  ~KernelState();

  static KernelState* shared();
  static void SetShared(KernelState* state);

  /// Object management
  uint32_t AllocateHandle();
  void RegisterObject(uint32_t handle, XObject* object);
  XObject* GetObject(uint32_t handle);
  void UnregisterObject(uint32_t handle);

  /// Thread management
  XThread* CreateThread(uint32_t stack_size, uint32_t entry_point,
                        uint32_t param, bool suspended);
  XThread* GetCurrentThread();
  void SetCurrentThread(XThread* thread);
  void TerminateThread(XThread* thread, uint32_t exit_code);
  const std::vector<XThread*>& GetAllThreads() const { return threads_; }
  size_t GetActiveThreadCount() const;
  size_t current_thread_index() const { return current_thread_idx_; }
  void set_current_thread_index(size_t idx) { current_thread_idx_ = idx; }

  /// Module management
  XModule* LoadModule(const std::string& path);
  XModule* GetModule(const std::string& name);
  XModule* GetExecutableModule() const { return exe_module_; }
  void SetExecutableModule(XModule* module) { exe_module_ = module; }

  /// TLS
  uint32_t AllocateTLS();
  void FreeTLS(uint32_t slot);
  void SetTLSValue(uint32_t thread_id, uint32_t slot, uint64_t value);
  uint64_t GetTLSValue(uint32_t thread_id, uint32_t slot);

  /// Event tracking for sync primitives
  struct EventState {
    bool signaled = false;
    bool manual_reset = false;  // true = NotificationEvent, false = SynchronizationEvent
  };
  void RegisterEvent(uint32_t handle, bool manual_reset, bool initial_state);
  EventState* GetEventState(uint32_t handle);

 private:
  static KernelState* shared_instance_;

  std::mutex object_mutex_;
  uint32_t next_handle_ = 0x100;
  std::unordered_map<uint32_t, XObject*> objects_;

  std::vector<XThread*> threads_;
  size_t current_thread_idx_ = 0;
  XThread* current_thread_ = nullptr;
  XModule* exe_module_ = nullptr;

  // TLS storage: thread_id -> (slot -> value)
  std::mutex tls_mutex_;
  uint32_t next_tls_slot_ = 1;
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint64_t>> tls_data_;

  // Event state tracking
  std::mutex event_mutex_;
  std::unordered_map<uint32_t, EventState> event_states_;
};

}  // namespace xe::kernel
