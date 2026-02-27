/**
 * Vera360 — Xenia Edge
 * POSIX threading primitives (replaces Win32 CRITICAL_SECTION, CreateThread, etc.)
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xe::threading {

/// High-resolution sleep
void Sleep(uint32_t milliseconds);
void NanoSleep(uint64_t nanoseconds);

/// Yield current thread time-slice
void MaybeYield();

/// Set thread name (visible in debugger / logcat)
void SetCurrentThreadName(const std::string& name);

/// Set thread affinity to specific core(s)
bool SetThreadAffinity(uint64_t mask);

/// Get current thread ID
uint64_t GetCurrentThreadId();

/// Simple RAII thread wrapper
class Thread {
 public:
  using EntryPoint = std::function<void()>;

  static std::unique_ptr<Thread> Create(EntryPoint entry, const std::string& name = "");
  ~Thread();

  void Join();
  void Detach();
  bool IsJoinable() const;

  Thread(const Thread&) = delete;
  Thread& operator=(const Thread&) = delete;
  Thread(Thread&&) noexcept;
  Thread& operator=(Thread&&) noexcept;

 private:
  Thread();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Mutex (non-recursive)
class Mutex {
 public:
  Mutex();
  ~Mutex();
  void Lock();
  void Unlock();
  bool TryLock();

  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;

 private:
  friend class ConditionVariable;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// RAII lock guard
class LockGuard {
 public:
  explicit LockGuard(Mutex& m) : mutex_(m) { mutex_.Lock(); }
  ~LockGuard() { mutex_.Unlock(); }
 private:
  Mutex& mutex_;
};

/// Condition variable
class ConditionVariable {
 public:
  ConditionVariable();
  ~ConditionVariable();
  void Wait(Mutex& mutex);
  bool WaitFor(Mutex& mutex, uint32_t timeout_ms);
  void NotifyOne();
  void NotifyAll();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

/// Semaphore
class Semaphore {
 public:
  explicit Semaphore(int32_t initial_count);
  ~Semaphore();
  void Acquire();
  bool TryAcquire();
  void Release(int32_t count = 1);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace xe::threading
