// Platform SDL2 Gamepad, Keyboard and Mouse Input for Linux x86 (Phase 3)
#pragma once

#include <cstdint>

#pragma pack(push, 1)
// Xbox Controller Gamepad structure (XPP / XInput format)
struct XboxGamepadState {
    uint16_t wButtons;
    uint8_t  bAnalogButtons[8]; // A, B, X, Y, BLACK, WHITE, LTRIGGER, RTRIGGER (0..255)
    int16_t  sThumbLX;          // -32768..32767
    int16_t  sThumbLY;
    int16_t  sThumbRX;
    int16_t  sThumbRY;
};

struct XboxInputState {
    uint32_t         dwPacketNumber;
    XboxGamepadState Gamepad;
};
#pragma pack(pop)

// Button Bitmasks (matches Xbox XINPUT_GAMEPAD_* defines)
constexpr uint16_t XINPUT_DPAD_UP        = 0x0001;
constexpr uint16_t XINPUT_DPAD_DOWN      = 0x0002;
constexpr uint16_t XINPUT_DPAD_LEFT      = 0x0004;
constexpr uint16_t XINPUT_DPAD_RIGHT     = 0x0008;
constexpr uint16_t XINPUT_START          = 0x0010;
constexpr uint16_t XINPUT_BACK           = 0x0020;
constexpr uint16_t XINPUT_LEFT_THUMB     = 0x0040;
constexpr uint16_t XINPUT_RIGHT_THUMB    = 0x0080;

namespace input_sdl {

bool init();
void shutdown();

// Poll hardware inputs (game controllers, keyboard and mouse)
void poll();

// Query current Xbox gamepad state for player index (0..3)
// Returns true if device is connected or emulated, false if disconnected.
bool get_state(uint32_t user_index, XboxInputState *out_state);

// Returns true if a physical gamepad or synthetic test override is present on slot
bool has_controller(uint32_t user_index);

// Set vibration / rumble for game controller
void set_rumble(uint32_t user_index, uint16_t left_motor, uint16_t right_motor);

// Mouse look pending deltas in degrees (for mouselook integration)
void get_mouselook_delta(float *out_yaw_deg, float *out_pitch_deg);

// Injects synthetic controller state (used for automated unit testing)
void inject_synthetic_state(uint32_t user_index, const XboxGamepadState &state);

} // namespace input_sdl
