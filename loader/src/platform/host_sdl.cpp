// Platform SDL2 Window and Display Host for Linux x86 (Phase 3)
#include "host_sdl.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace host_sdl {
namespace {

SDL_Window *g_window = nullptr;
int g_aspect_w = 16;
int g_aspect_h = 9;
bool g_fullscreen = false;
std::atomic<bool> g_captured{false};
std::atomic<bool> g_window_closed{false};
std::atomic<int> g_pending_capture{-1};

std::atomic<int> g_mouse_dx{0};
std::atomic<int> g_mouse_dy{0};
std::atomic<int> g_mouse_wheel{0};
std::atomic<int> g_mouse_buttons{0};

} // namespace

bool open_window(int width, int height, int aspect_w, int aspect_h, bool hidden)
{
    if (g_window) return true;

    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "host_sdl: Falha ao inicializar subsistema de vídeo SDL2: %s\n", SDL_GetError());
        return false;
    }

    if (width <= 0) width = 1280;
    if (height <= 0) height = 720;
    if (aspect_w > 0) g_aspect_w = aspect_w;
    if (aspect_h > 0) g_aspect_h = aspect_h;

    uint32_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;
    if (hidden) {
        flags |= SDL_WINDOW_HIDDEN;
    } else {
        flags |= SDL_WINDOW_SHOWN;
    }

    g_window = SDL_CreateWindow("BLACK (Xbox) - Native Linux x86",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width, height, flags);
    if (!g_window) {
        fprintf(stderr, "host_sdl: Falha ao criar janela SDL2: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetWindowMinimumSize(g_window, 640, 360);

    g_window_closed = false;
    if (g_fullscreen) {
        SDL_SetWindowFullscreen(g_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    printf("[host_sdl] Janela SDL2 criada com sucesso: %dx%d (Aspecto: %d:%d, %s, %s)\n",
           width, height, g_aspect_w, g_aspect_h, hidden ? "Oculta" : "Visível",
           g_fullscreen ? "Tela Cheia" : "Janela");
    return true;
}

void close_window()
{
    if (g_window) {
        if (g_captured) {
            request_capture(false);
        }
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    g_window_closed = true;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

SDL_Window *window()
{
    return g_window;
}

void set_title(const char *text)
{
    if (g_window && text) {
        SDL_SetWindowTitle(g_window, text);
    }
}

void frame_layout(int *frame_w, int *frame_h, int *pic_w, int *pic_h)
{
    if (!g_window) {
        if (frame_w) *frame_w = 1280;
        if (frame_h) *frame_h = 720;
        if (pic_w) *pic_w = 1280;
        if (pic_h) *pic_h = 720;
        return;
    }

    int fw = 0, fh = 0;
    SDL_GetWindowSize(g_window, &fw, &fh);
    if (fw <= 0) fw = 1280;
    if (fh <= 0) fh = 720;

    int pw = fw;
    int ph = fw * g_aspect_h / g_aspect_w;
    if (ph > fh) {
        ph = fh;
        pw = fh * g_aspect_w / g_aspect_h;
    }

    if (frame_w) *frame_w = fw;
    if (frame_h) *frame_h = fh;
    if (pic_w) *pic_w = pw;
    if (pic_h) *pic_h = ph;
}

bool is_open()
{
    return g_window != nullptr && !g_window_closed.load();
}

bool window_closed()
{
    return g_window_closed.load();
}

void set_fullscreen(bool on)
{
    if (!g_window) return;
    g_fullscreen = on;
    SDL_SetWindowFullscreen(g_window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

bool is_fullscreen()
{
    return g_fullscreen;
}

bool mouse_captured()
{
    return g_captured.load();
}

void request_capture(bool on)
{
    g_captured.store(on);
    g_pending_capture.store(on ? 1 : 0);
    if (SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    }
}

void mouse_take(int *dx, int *dy, int *wheel)
{
    if (dx) *dx = g_mouse_dx.exchange(0);
    if (dy) *dy = g_mouse_dy.exchange(0);
    if (wheel) *wheel = g_mouse_wheel.exchange(0);
}

int mouse_buttons()
{
    return g_captured.load() ? g_mouse_buttons.load() : 0;
}

void pump_events()
{
    if (!g_window) return;

    int pending = g_pending_capture.exchange(-1);
    if (pending != -1) {
        SDL_SetRelativeMouseMode(pending == 1 ? SDL_TRUE : SDL_FALSE);
        if (pending == 1) {
            g_mouse_dx.store(0);
            g_mouse_dy.store(0);
            g_mouse_wheel.store(0);
        }
    }

    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                g_window_closed = true;
                break;

            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE) {
                    g_window_closed = true;
                } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    if (g_captured.load()) {
                        request_capture(false);
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (g_captured.load()) {
                    g_mouse_dx.fetch_add(ev.motion.xrel);
                    g_mouse_dy.fetch_add(ev.motion.yrel);
                }
                break;

            case SDL_MOUSEWHEEL:
                g_mouse_wheel.fetch_add(ev.wheel.y);
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                bool down = (ev.type == SDL_MOUSEBUTTONDOWN);
                int bit = 0;
                if (ev.button.button == SDL_BUTTON_LEFT)   bit = 1 << 0;
                if (ev.button.button == SDL_BUTTON_RIGHT)  bit = 1 << 1;
                if (ev.button.button == SDL_BUTTON_MIDDLE) bit = 1 << 2;
                if (ev.button.button == SDL_BUTTON_X1)     bit = 1 << 3;
                if (ev.button.button == SDL_BUTTON_X2)     bit = 1 << 4;

                if (down) {
                    if (!g_captured.load()) {
                        request_capture(true);
                        g_mouse_dx.store(0);
                        g_mouse_dy.store(0);
                        g_mouse_wheel.store(0);
                    } else {
                        g_mouse_buttons.fetch_or(bit);
                    }
                } else {
                    g_mouse_buttons.fetch_and(~bit);
                }
                break;
            }

            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT)) {
                    set_fullscreen(!is_fullscreen());
                }
                break;

            default:
                break;
        }
    }
}

} // namespace host_sdl
