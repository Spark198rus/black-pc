// Unit Test: Mouse Look Pipeline Verification for Linux x86 (Issue 2)
#include "../loader/src/platform/host_sdl.h"
#include "../loader/src/platform/input_sdl.h"
#include "../loader/src/input/mouselook.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

#include <atomic>

std::atomic<uint32_t> g_frame_count{1};

extern "C" {
extern float g_ml_pending_yaw, g_ml_pending_pitch;
}

static void check(bool condition, const char *msg)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    std::printf("=== Running test_mouselook_pipeline ===\n");

    // 1. Initialize SDL Window and Input Subsystems
    bool win_ok = host_sdl::open_window(640, 360, 16, 9, true); // hidden window
    check(win_ok, "host_sdl::open_window failed");

    bool in_ok = input_sdl::init();
    check(in_ok, "input_sdl::init failed");

    // 2. Check initial mouse state
    check(!host_sdl::mouse_captured(), "Mouse should not be captured initially");

    // 3. Request capture
    host_sdl::request_capture(true);
    host_sdl::pump_events();
    check(host_sdl::mouse_captured(), "Mouse capture should be active after request_capture(true)");

    // 4. Inject synthetic mouse motion events via SDL
    SDL_Event ev1{};
    ev1.type = SDL_MOUSEMOTION;
    ev1.motion.xrel = 42;
    ev1.motion.yrel = -18;
    SDL_PushEvent(&ev1);

    SDL_Event ev2{};
    ev2.type = SDL_MOUSEMOTION;
    ev2.motion.xrel = 8;
    ev2.motion.yrel = 5;
    SDL_PushEvent(&ev2);

    // 5. Run input_sdl::poll() BEFORE pump_events or mouse_take
    // CRITICAL REGRESSION TEST: input_sdl::poll() must NEVER consume or drain host_sdl mouse deltas!
    input_sdl::poll();

    // 6. Pump events in host_sdl
    host_sdl::pump_events();

    // 7. Another input_sdl::poll() to ensure it doesn't touch the accumulated motion
    input_sdl::poll();

    // 8. Retrieve mouse movement via host_sdl::mouse_take (same as xinput mouse_look)
    int dx = 0, dy = 0, wheel = 0;
    host_sdl::mouse_take(&dx, &dy, &wheel);

    check(dx == 50, "Total mouse dx should be 42 + 8 = 50");
    check(dy == -13, "Total mouse dy should be -18 + 5 = -13");
    std::printf("[test_mouselook] mouse_take returned: dx=%d, dy=%d (correct)\n", dx, dy);

    // 9. Second mouse_take must be 0 (properly consumed)
    int dx2 = 99, dy2 = 99;
    host_sdl::mouse_take(&dx2, &dy2, nullptr);
    check(dx2 == 0 && dy2 == 0, "Subsequent mouse_take must return 0");

    // 10. Test mouselook_set_frame calculation
    float sens = 2.0f;
    float k = 0.022f * sens;
    mouselook_set_frame(-dx * k, dy * k);

    float expected_yaw = -50.0f * k;
    float expected_pitch = -13.0f * k;

    check(std::fabs(g_ml_pending_yaw - expected_yaw) < 0.0001f, "g_ml_pending_yaw mismatch");
    check(std::fabs(g_ml_pending_pitch - expected_pitch) < 0.0001f, "g_ml_pending_pitch mismatch");
    std::printf("[test_mouselook] mouselook_set_frame verified: yaw=%.4f deg, pitch=%.4f deg\n",
                g_ml_pending_yaw, g_ml_pending_pitch);

    // 11. Test release capture
    host_sdl::request_capture(false);
    host_sdl::pump_events();
    check(!host_sdl::mouse_captured(), "Mouse should be uncaptured after request_capture(false)");

    // 12. Cleanup
    input_sdl::shutdown();
    host_sdl::close_window();

    std::printf("[PASS] test_mouselook_pipeline completed successfully with 0 errors!\n\n");
    return 0;
}
