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
  void TerminateThread(XThread* thread, uint32_t exit_code);

  /// Module management
  XModule* LoadModule(const std::string& path);
  XModule* GetModule(const std::string& name);
  XModule* GetExecutableModule() const { return exe_module_; }
  void SetExecutableModule(XModule* module) { exe_module_ = module; }

  /// TLS
  uint32_t AllocateTLS();

 private:
  static KernelState* shared_instance_;

  std::mutex object_mutex_;
  uint32_t next_handle_ = 0x100;
  std::unordered_map<uint32_t, XObject*> objects_;

  std::vector<XThread*> threads_;
  XModule* exe_module_ = nullptr;
};

}  // namespace xe::kernel
