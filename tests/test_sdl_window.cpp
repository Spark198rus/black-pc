// Unit Test: SDL2 Window and Display Host for Linux x86
#include "../loader/src/platform/host_sdl.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void check(bool condition, const char *msg)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    std::printf("=== Running test_sdl_window ===\n");

    // 1. Open hidden window for testing (800x600, 16:9 aspect)
    bool ok = host_sdl::open_window(800, 600, 16, 9, /*hidden=*/true);
    check(ok, "host_sdl::open_window returned false");
    check(host_sdl::is_open(), "host_sdl::is_open returned false after creation");
    check(host_sdl::window() != nullptr, "host_sdl::window() is null");
    check(!host_sdl::window_closed(), "host_sdl::window_closed() is true initially");

    // 2. Test aspect ratio layout computation for 16:9
    int frame_w = 0, frame_h = 0, pic_w = 0, pic_h = 0;
    host_sdl::frame_layout(&frame_w, &frame_h, &pic_w, &pic_h);
    std::printf("[test_sdl_window] 16:9 layout: frame=%dx%d, pic=%dx%d\n", frame_w, frame_h, pic_w, pic_h);
    check(frame_w == 800, "Expected frame_w == 800");
    check(frame_h == 600, "Expected frame_h == 600");
    // With 800x600 and 16:9, width fits at 800, height should be 800 * 9 / 16 = 450
    check(pic_w == 800, "Expected pic_w == 800 for 16:9 in 800x600");
    check(pic_h == 450, "Expected pic_h == 450 for 16:9 in 800x600");

    // 3. Test window title
    const char *test_title = "BLACK Native Linux x86 Unit Test";
    host_sdl::set_title(test_title);
    const char *actual_title = SDL_GetWindowTitle(host_sdl::window());
    check(actual_title != nullptr, "SDL_GetWindowTitle returned null");
    check(std::strcmp(actual_title, test_title) == 0, "Window title mismatch");
    std::printf("[test_sdl_window] Window title verified: '%s'\n", actual_title);

    // 4. Test relative mouse capture request
    check(!host_sdl::mouse_captured(), "Mouse should not be captured initially");
    host_sdl::request_capture(true);
    check(host_sdl::mouse_captured(), "Mouse capture should be true after request_capture(true)");
    host_sdl::request_capture(false);
    check(!host_sdl::mouse_captured(), "Mouse capture should be false after request_capture(false)");

    // 5. Test event pumping
    host_sdl::pump_events();
    check(host_sdl::is_open(), "Window should still be open after pump_events");

    // 6. Test window closure
    host_sdl::close_window();
    check(!host_sdl::is_open(), "Window should report closed after close_window");
    check(host_sdl::window_closed(), "window_closed should report true after close_window");
    check(host_sdl::window() == nullptr, "Window handle should be null after close_window");

    // 7. Test 4:3 aspect ratio calculation
    ok = host_sdl::open_window(800, 600, 4, 3, /*hidden=*/true);
    check(ok, "Failed to reopen window with 4:3 aspect");
    host_sdl::frame_layout(&frame_w, &frame_h, &pic_w, &pic_h);
    std::printf("[test_sdl_window] 4:3 layout: frame=%dx%d, pic=%dx%d\n", frame_w, frame_h, pic_w, pic_h);
    // With 800x600 and 4:3: 800 * 3 / 4 = 600, which exactly matches 600!
    check(pic_w == 800, "Expected pic_w == 800 for 4:3 in 800x600");
    check(pic_h == 600, "Expected pic_h == 600 for 4:3 in 800x600");
    host_sdl::close_window();

    std::printf("[PASS] test_sdl_window completed successfully with zero errors.\n\n");
    return 0;
}
