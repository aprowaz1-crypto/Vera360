/**
 * Vera360 — Xenia Edge
 * POSIX threading implementation (pthreads)
 */

#include "xenia/base/threading.h"

#include <cerrno>
#include <cstring>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <sys/prctl.h>
#endif

namespace xe::threading {

void Sleep(uint32_t milliseconds) {
  struct timespec ts;
  ts.tv_sec  = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000L;
  nanosleep(&ts, nullptr);
}

void NanoSleep(uint64_t nanoseconds) {
  struct timespec ts;
  ts.tv_sec  = nanoseconds / 1000000000ULL;
  ts.tv_nsec = nanoseconds % 1000000000ULL;
  nanosleep(&ts, nullptr);
}

void MaybeYield() {
  sched_yield();
}

void SetCurrentThreadName(const std::string& name) {
#if defined(__ANDROID__)
  prctl(PR_SET_NAME, name.c_str());
#else
  pthread_setname_np(pthread_self(), name.c_str());
#endif
}

bool SetThreadAffinity(uint64_t mask) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  for (int i = 0; i < 64; ++i) {
    if (mask & (1ULL << i)) {
      CPU_SET(i, &cpuset);
    }
  }
  return sched_setaffinity(0, sizeof(cpuset), &cpuset) == 0;
}

uint64_t GetCurrentThreadId() {
  return static_cast<uint64_t>(gettid());
}

// ── Thread ──────────────────────────────────────────────────────────────────

struct Thread::Impl {
  pthread_t handle = 0;
  bool joinable = false;
};

Thread::Thread() : impl_(std::make_unique<Impl>()) {}
Thread::~Thread() {
  if (impl_ && impl_->joinable) {
    pthread_detach(impl_->handle);
  }
}

Thread::Thread(Thread&&) noexcept = default;
Thread& Thread::operator=(Thread&&) noexcept = default;

std::unique_ptr<Thread> Thread::Create(EntryPoint entry, const std::string& name) {
  auto t = std::unique_ptr<Thread>(new Thread());
  
  struct ThreadArg {
    EntryPoint func;
    std::string thread_name;
  };
  
  auto* arg = new ThreadArg{std::move(entry), name};
  
  int rc = pthread_create(&t->impl_->handle, nullptr,
    [](void* p) -> void* {
      auto* a = static_cast<ThreadArg*>(p);
      if (!a->thread_name.empty()) {
        SetCurrentThreadName(a->thread_name);
      }
      a->func();
      delete a;
      return nullptr;
    }, arg);
  
  if (rc != 0) {
    delete arg;
    return nullptr;
  }
  
  t->impl_->joinable = true;
  return t;
}

void Thread::Join() {
  if (impl_->joinable) {
    pthread_join(impl_->handle, nullptr);
    impl_->joinable = false;
  }
}

void Thread::Detach() {
  if (impl_->joinable) {
    pthread_detach(impl_->handle);
    impl_->joinable = false;
  }
}

bool Thread::IsJoinable() const { return impl_->joinable; }

// ── Mutex ───────────────────────────────────────────────────────────────────

struct Mutex::Impl {
  pthread_mutex_t handle;
};

Mutex::Mutex() : impl_(std::make_unique<Impl>()) {
  pthread_mutex_init(&impl_->handle, nullptr);
}

Mutex::~Mutex() {
  pthread_mutex_destroy(&impl_->handle);
}

void Mutex::Lock() { pthread_mutex_lock(&impl_->handle); }
void Mutex::Unlock() { pthread_mutex_unlock(&impl_->handle); }
bool Mutex::TryLock() { return pthread_mutex_trylock(&impl_->handle) == 0; }

// ── ConditionVariable ───────────────────────────────────────────────────────

struct ConditionVariable::Impl {
  pthread_cond_t handle;
};

ConditionVariable::ConditionVariable() : impl_(std::make_unique<Impl>()) {
  pthread_cond_init(&impl_->handle, nullptr);
}

ConditionVariable::~ConditionVariable() {
  pthread_cond_destroy(&impl_->handle);
}

void ConditionVariable::Wait(Mutex& mutex) {
  pthread_cond_wait(&impl_->handle, &reinterpret_cast<pthread_mutex_t&>(*mutex.impl_));
}

bool ConditionVariable::WaitFor(Mutex& mutex, uint32_t timeout_ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec  += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000L;
  }
  return pthread_cond_timedwait(&impl_->handle,
    &reinterpret_cast<pthread_mutex_t&>(*mutex.impl_), &ts) == 0;
}

void ConditionVariable::NotifyOne() { pthread_cond_signal(&impl_->handle); }
void ConditionVariable::NotifyAll() { pthread_cond_broadcast(&impl_->handle); }

// ── Semaphore ───────────────────────────────────────────────────────────────

struct Semaphore::Impl {
  sem_t handle;
};

Semaphore::Semaphore(int32_t initial_count) : impl_(std::make_unique<Impl>()) {
  sem_init(&impl_->handle, 0, initial_count);
}

Semaphore::~Semaphore() {
  sem_destroy(&impl_->handle);
}

void Semaphore::Acquire() { sem_wait(&impl_->handle); }
bool Semaphore::TryAcquire() { return sem_trywait(&impl_->handle) == 0; }
void Semaphore::Release(int32_t count) {
  for (int32_t i = 0; i < count; ++i) {
    sem_post(&impl_->handle);
  }
}

}  // namespace xe::threading
