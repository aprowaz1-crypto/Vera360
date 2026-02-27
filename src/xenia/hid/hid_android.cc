/**
 * Vera360 — Xenia Edge
 * HID Android — input from Android touch overlay + gamepads
 */

#include "xenia/base/logging.h"
#include <cstdint>
#include <array>
#include <mutex>

namespace xe::hid {

/// Xbox 360 gamepad state (XINPUT_GAMEPAD-like)
struct GamepadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
};

/// Button masks
namespace Button {
  constexpr uint16_t kDpadUp    = 0x0001;
  constexpr uint16_t kDpadDown  = 0x0002;
  constexpr uint16_t kDpadLeft  = 0x0004;
  constexpr uint16_t kDpadRight = 0x0008;
  constexpr uint16_t kStart     = 0x0010;
  constexpr uint16_t kBack      = 0x0020;
  constexpr uint16_t kLThumb    = 0x0040;
  constexpr uint16_t kRThumb    = 0x0080;
  constexpr uint16_t kLShoulder = 0x0100;
  constexpr uint16_t kRShoulder = 0x0200;
  constexpr uint16_t kGuide     = 0x0400;
  constexpr uint16_t kA         = 0x1000;
  constexpr uint16_t kB         = 0x2000;
  constexpr uint16_t kX         = 0x4000;
  constexpr uint16_t kY         = 0x8000;
}

/// Gamepad state for up to 4 controllers
static std::array<GamepadState, 4> g_gamepads;
static std::mutex g_gamepad_mutex;

/// Called from JNI/touch overlay to update button state
void SetButton(int pad, uint16_t button, bool pressed) {
  std::lock_guard<std::mutex> lock(g_gamepad_mutex);
  if (pad < 0 || pad >= 4) return;
  if (pressed) {
    g_gamepads[pad].buttons |= button;
  } else {
    g_gamepads[pad].buttons &= ~button;
  }
}

/// Called from JNI/touch overlay to set analog stick
void SetAnalog(int pad, bool left, float x, float y) {
  std::lock_guard<std::mutex> lock(g_gamepad_mutex);
  if (pad < 0 || pad >= 4) return;
  int16_t ix = static_cast<int16_t>(x * 32767.0f);
  int16_t iy = static_cast<int16_t>(y * 32767.0f);
  if (left) {
    g_gamepads[pad].thumb_lx = ix;
    g_gamepads[pad].thumb_ly = iy;
  } else {
    g_gamepads[pad].thumb_rx = ix;
    g_gamepads[pad].thumb_ry = iy;
  }
}

/// Called from JNI to set trigger value
void SetTrigger(int pad, bool left, float value) {
  std::lock_guard<std::mutex> lock(g_gamepad_mutex);
  if (pad < 0 || pad >= 4) return;
  uint8_t v = static_cast<uint8_t>(value * 255.0f);
  if (left) {
    g_gamepads[pad].left_trigger = v;
  } else {
    g_gamepads[pad].right_trigger = v;
  }
}

/// Get current pad state (used by kernel XInput shim)
GamepadState GetState(int pad) {
  std::lock_guard<std::mutex> lock(g_gamepad_mutex);
  if (pad < 0 || pad >= 4) return {};
  return g_gamepads[pad];
}

bool Initialize() {
  XELOGI("HID Android initialized (4 controllers)");
  return true;
}

void Shutdown() {
  XELOGI("HID Android shutdown");
}

}  // namespace xe::hid
