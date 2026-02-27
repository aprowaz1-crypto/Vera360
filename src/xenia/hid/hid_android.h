/**
 * Vera360 — Xenia Edge
 * HID Android — input from Android touch overlay + gamepads
 */
#pragma once

#include <cstdint>

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

/// Initialize the HID system
bool Initialize();
/// Shutdown the HID system
void Shutdown();

/// Set button state from JNI/touch overlay
void SetButton(int pad, uint16_t button, bool pressed);
/// Set full button mask at once (replaces all buttons)
void SetButtonsRaw(int pad, uint16_t buttons);
/// Set analog stick from JNI/touch overlay
void SetAnalog(int pad, bool left, float x, float y);
/// Set trigger value from JNI
void SetTrigger(int pad, bool left, float value);
/// Get current pad state (used by kernel XInput shim)
GamepadState GetState(int pad);

}  // namespace xe::hid
