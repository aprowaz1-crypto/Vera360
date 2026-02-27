/**
 * Vera360 — Math helpers (ARM NEON aware)
 */
#pragma once

#include <arm_neon.h>
#include <cmath>
#include <cstdint>

namespace xe::math {

inline uint32_t NextPowerOfTwo(uint32_t v) {
  v--;
  v |= v >> 1;  v |= v >> 2;
  v |= v >> 4;  v |= v >> 8;
  v |= v >> 16;
  return v + 1;
}

inline bool IsPowerOfTwo(uint32_t v) {
  return v && !(v & (v - 1));
}

inline uint32_t AlignUp(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

inline uint32_t AlignDown(uint32_t value, uint32_t alignment) {
  return value & ~(alignment - 1);
}

inline uint32_t CountLeadingZeros(uint32_t v) {
  return v ? __builtin_clz(v) : 32;
}

inline uint32_t CountTrailingZeros(uint32_t v) {
  return v ? __builtin_ctz(v) : 32;
}

inline uint32_t PopCount(uint32_t v) {
  return __builtin_popcount(v);
}

// ── NEON vector helpers ─────────────────────────────────────────────
inline float32x4_t Vec4Load(const float* p) {
  return vld1q_f32(p);
}

inline void Vec4Store(float* p, float32x4_t v) {
  vst1q_f32(p, v);
}

inline float32x4_t Vec4Add(float32x4_t a, float32x4_t b) {
  return vaddq_f32(a, b);
}

inline float32x4_t Vec4Mul(float32x4_t a, float32x4_t b) {
  return vmulq_f32(a, b);
}

}  // namespace xe::math
