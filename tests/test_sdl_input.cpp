// Unit Test: SDL2 Gamepad, Keyboard and Mouse Input for Linux x86
#include "../loader/src/platform/input_sdl.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" short GetAsyncKeyState(int vk);

static void check(bool condition, const char *msg)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    std::printf("=== Running test_sdl_input ===\n");

    // 1. Initialize input system
    bool ok = input_sdl::init();
    check(ok, "input_sdl::init() failed");

    // 2. Poll inputs
    input_sdl::poll();

    // 3. User 0 must always be connected (fallback to keyboard/mouse)
    XboxInputState state0{};
    bool conn0 = input_sdl::get_state(0, &state0);
    check(conn0, "User 0 should be connected by default (KB/M fallback)");
    std::printf("[test_sdl_input] User 0 initial packet: %u\n", state0.dwPacketNumber);

    // 4. Test synthetic controller state injection for User 1
    XboxGamepadState synth{};
    synth.wButtons = XINPUT_DPAD_UP | XINPUT_START;
    synth.bAnalogButtons[0] = 255; // A button
    synth.bAnalogButtons[6] = 130; // Left trigger
    synth.bAnalogButtons[7] = 240; // Right trigger
    synth.sThumbLX = 12345;
    synth.sThumbLY = -14567;
    synth.sThumbRX = 23456;
    synth.sThumbRY = 31000;

    input_sdl::inject_synthetic_state(1, synth);

    XboxInputState state1{};
    bool conn1 = input_sdl::get_state(1, &state1);
    check(conn1, "User 1 should be connected after synthetic state injection");
    check(state1.dwPacketNumber >= 1, "Packet number must be >= 1 after injection");

    // Assert exact field values
    check(state1.Gamepad.wButtons == (XINPUT_DPAD_UP | XINPUT_START), "wButtons mismatch");
    check(state1.Gamepad.bAnalogButtons[0] == 255, "A button analog mismatch");
    check(state1.Gamepad.bAnalogButtons[6] == 130, "Left trigger mismatch");
    check(state1.Gamepad.bAnalogButtons[7] == 240, "Right trigger mismatch");
    check(state1.Gamepad.sThumbLX == 12345, "sThumbLX mismatch");
    check(state1.Gamepad.sThumbLY == -14567, "sThumbLY mismatch");
    check(state1.Gamepad.sThumbRX == 23456, "sThumbRX mismatch");
    check(state1.Gamepad.sThumbRY == 31000, "sThumbRY mismatch");

    std::printf("[test_sdl_input] Synthetic controller state verified: wButtons=0x%04x, LT=%u, RT=%u, LX=%d, LY=%d\n",
                state1.Gamepad.wButtons,
                state1.Gamepad.bAnalogButtons[6],
                state1.Gamepad.bAnalogButtons[7],
                state1.Gamepad.sThumbLX,
                state1.Gamepad.sThumbLY);

    // 5. Test packet number incrementing on subsequent injection
    uint32_t prev_pkt = state1.dwPacketNumber;
    synth.wButtons |= XINPUT_DPAD_DOWN;
    input_sdl::inject_synthetic_state(1, synth);
    input_sdl::get_state(1, &state1);
    check(state1.dwPacketNumber == prev_pkt + 1, "Packet number should increment when state changes");
    check((state1.Gamepad.wButtons & XINPUT_DPAD_DOWN) != 0, "New button flag missing");

    // 6. Test mouselook delta reset behavior
    float yaw = 99.0f, pitch = 99.0f;
    input_sdl::get_mouselook_delta(&yaw, &pitch);
    // Since no mouse motion occurred, deltas should reset to 0
    check(yaw == 0.0f, "Initial mouselook yaw should be 0.0f");
    check(pitch == 0.0f, "Initial mouselook pitch should be 0.0f");

    // 7. Test rumble invocation (safe no-op if no physical controller attached)
    input_sdl::set_rumble(0, 32000, 32000);
    input_sdl::set_rumble(1, 0, 0);

    // 8. Test extended virtual keys in GetAsyncKeyState (F1-F12, NumPad, Home/End, etc.)
    check(GetAsyncKeyState(0x70 /* VK_F1 */) == 0, "VK_F1 should query without crash");
    check(GetAsyncKeyState(0x7B /* VK_F12 */) == 0, "VK_F12 should query without crash");
    check(GetAsyncKeyState(0x60 /* VK_NUMPAD0 */) == 0, "VK_NUMPAD0 should query without crash");
    check(GetAsyncKeyState(0x69 /* VK_NUMPAD9 */) == 0, "VK_NUMPAD9 should query without crash");
    check(GetAsyncKeyState(0x24 /* VK_HOME */) == 0, "VK_HOME should query without crash");
    check(GetAsyncKeyState(0x23 /* VK_END */) == 0, "VK_END should query without crash");
    check(GetAsyncKeyState(0x21 /* VK_PRIOR PageUp */) == 0, "VK_PRIOR should query without crash");
    check(GetAsyncKeyState(0x22 /* VK_NEXT PageDown */) == 0, "VK_NEXT should query without crash");
    check(GetAsyncKeyState(0x14 /* VK_CAPITAL */) == 0, "VK_CAPITAL should query without crash");
    check(GetAsyncKeyState(0x6A /* VK_MULTIPLY */) == 0, "VK_MULTIPLY should query without crash");

    // 9. Shutdown input system cleanly
    input_sdl::shutdown();

    std::printf("[PASS] test_sdl_input completed successfully with zero errors.\n\n");
    return 0;
}
