/**
 * Vera360 — Xenia Edge
 * Clock interface
 */
#pragma once

#include <cstdint>

namespace xe {

class Clock {
 public:
  // Host clock (real time)
  static uint64_t QueryHostTickCount();
  static uint64_t QueryHostTickFrequency();
  static double   QueryHostSeconds();
  static uint64_t QueryHostUptimeMillis();

  // Guest clock (Xbox 360 timebase)
  static void     SetGuestTimeScalar(double scalar);
  static double   GetGuestTimeScalar();
  static uint64_t QueryGuestTickCount();
  static uint64_t QueryGuestTickFrequency();
  static void     PauseGuest();
  static void     ResumeGuest();
};

}  // namespace xe
