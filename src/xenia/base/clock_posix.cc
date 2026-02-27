/**
 * Vera360 — Xenia Edge
 * High-resolution clock (POSIX clock_gettime)
 */

#include "xenia/base/clock.h"

#include <time.h>

namespace xe {

uint64_t Clock::QueryHostTickCount() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t Clock::QueryHostTickFrequency() {
  return 1000000000ULL;  // nanosecond resolution
}

double Clock::QueryHostSeconds() {
  return static_cast<double>(QueryHostTickCount()) / 1e9;
}

uint64_t Clock::QueryHostUptimeMillis() {
  return QueryHostTickCount() / 1000000ULL;
}

// ── Guest clock (Xbox 360 timebase: ~49.875 MHz) ───────────────────────────

namespace {
constexpr uint64_t kGuestTickRate = 49875000ULL;  // Xbox 360 CPU timebase
uint64_t g_guest_tick_offset = 0;
double g_guest_time_scalar = 1.0;
bool g_guest_paused = false;
uint64_t g_guest_pause_tick = 0;
}

void Clock::SetGuestTimeScalar(double scalar) {
  g_guest_time_scalar = scalar;
}

double Clock::GetGuestTimeScalar() {
  return g_guest_time_scalar;
}

uint64_t Clock::QueryGuestTickCount() {
  if (g_guest_paused) return g_guest_pause_tick;
  uint64_t host_ns = QueryHostTickCount();
  double guest_ticks = static_cast<double>(host_ns) *
                       (static_cast<double>(kGuestTickRate) / 1e9) *
                       g_guest_time_scalar;
  return static_cast<uint64_t>(guest_ticks) + g_guest_tick_offset;
}

uint64_t Clock::QueryGuestTickFrequency() {
  return kGuestTickRate;
}

void Clock::PauseGuest() {
  if (!g_guest_paused) {
    g_guest_pause_tick = QueryGuestTickCount();
    g_guest_paused = true;
  }
}

void Clock::ResumeGuest() {
  g_guest_paused = false;
}

}  // namespace xe
