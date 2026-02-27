/**
 * Vera360 — Xenia Edge
 * Input System — translates Xbox 360 XInput calls to Android HID
 */

#include "xenia/base/logging.h"
#include <cstdint>

namespace xe::hid {

/// Xbox 360 gamepad state (XINPUT_GAMEPAD-like) — must match hid_android.cc
struct GamepadState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
};

extern GamepadState GetState(int pad);

/// Called from kernel shim when game calls XInputGetState
uint32_t XInputGetState(uint32_t user_index, void* out_state) {
  if (user_index > 3) return 0x048F;  // ERROR_DEVICE_NOT_CONNECTED
  
  auto state = GetState(static_cast<int>(user_index));
  
  // For user_index == 0, always connected (touch overlay)
  // Others: only if a Bluetooth gamepad is connected
  if (user_index > 0) {
    return 0x048F;  // TODO: detect BT gamepads
  }

  // Write state to guest memory
  // The caller is responsible for byte-swapping to big-endian
  if (out_state) {
    auto* p = reinterpret_cast<uint8_t*>(out_state);
    // XINPUT_GAMEPAD layout (big-endian):
    // +0: uint16 buttons
    // +2: uint8 left_trigger
    // +3: uint8 right_trigger
    // +4: int16 thumb_lx
    // +6: int16 thumb_ly
    // +8: int16 thumb_rx
    // +10: int16 thumb_ry
    auto bswap16 = [](uint16_t v) -> uint16_t {
      return __builtin_bswap16(v);
    };
    auto bswap16s = [](int16_t v) -> int16_t {
      return static_cast<int16_t>(__builtin_bswap16(static_cast<uint16_t>(v)));
    };
    
    *reinterpret_cast<uint16_t*>(p + 0) = bswap16(state.buttons);
    p[2] = state.left_trigger;
    p[3] = state.right_trigger;
    *reinterpret_cast<int16_t*>(p + 4) = bswap16s(state.thumb_lx);
    *reinterpret_cast<int16_t*>(p + 6) = bswap16s(state.thumb_ly);
    *reinterpret_cast<int16_t*>(p + 8) = bswap16s(state.thumb_rx);
    *reinterpret_cast<int16_t*>(p + 10) = bswap16s(state.thumb_ry);
  }
  
  return 0;  // ERROR_SUCCESS
}

/// Called from kernel shim when game calls XInputSetState (vibration)
uint32_t XInputSetState(uint32_t user_index, void* vibration) {
  if (user_index > 0) return 0x048F;
  // TODO: Android vibrator API or gamepad haptics
  XELOGD("XInputSetState: vibration for pad {}", user_index);
  return 0;
}

}  // namespace xe::hid
