// Platform SDL2 Window and Display Host for Linux x86 (Phase 3)
#pragma once

#include <cstdint>

struct SDL_Window;

namespace host_sdl {

// Open SDL2 window.
// width/height: desired client area (0 = default 1280x720 or screen fit).
// aspect_w/aspect_h: target aspect ratio (e.g. 16:9 or 4:3).
// hidden: if true, window is created hidden (useful for automated testing/CI).
bool open_window(int width = 1280, int height = 720, int aspect_w = 16, int aspect_h = 9, bool hidden = false);
void close_window();

SDL_Window *window();
void set_title(const char *text);

// Computes current frame bounds and centered picture rectangle preserving aspect ratio.
void frame_layout(int *frame_w, int *frame_h, int *pic_w, int *pic_h);

// Window state
bool is_open();
bool window_closed();

// Fullscreen management
void set_fullscreen(bool on);
bool is_fullscreen();

// Mouse capture management
bool mouse_captured();
void request_capture(bool on);
void mouse_take(int *dx, int *dy, int *wheel);
int mouse_buttons(); // bitmask: bit 0 = left, bit 1 = right, bit 2 = middle

// Process pending SDL2 events (call once per frame)
void pump_events();

} // namespace host_sdl
