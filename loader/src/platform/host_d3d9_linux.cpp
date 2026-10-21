// Host Direct3D 9 Graphics Implementation for Linux x86 via DXVK-Native and SDL2 (Phase 4)
#include "d3d/host.h"
#include "host_sdl.h"

#include <d3d9.h>
#include <wsi/native_sdl2.h>
#include <SDL2/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace host3d {
namespace {

IDirect3D9 *g_d3d = nullptr;
IDirect3DDevice9 *g_dev = nullptr;
D3DPRESENT_PARAMETERS g_pp{};
bool g_intz = false;
bool g_in_scene = false;
int g_bb_w = 1280;
int g_bb_h = 720;
int g_frame_w = 1280;
int g_frame_h = 720;

} // namespace

bool open_window(int width, int height, int aspect_w, int aspect_h)
{
    if (width <= 0) width = 1280;
    if (height <= 0) height = 720;
    if (aspect_w <= 0) aspect_w = 16;
    if (aspect_h <= 0) aspect_h = 9;

    g_bb_w = width;
    g_bb_h = height;

    bool hidden = (std::getenv("BLACK_HEADLESS") != nullptr);
    return host_sdl::open_window(width, height, aspect_w, aspect_h, hidden);
}

HWND window()
{
    SDL_Window *win = host_sdl::window();
    return win ? dxvk::wsi::toHwnd(win) : nullptr;
}

void set_title(const char *text)
{
    host_sdl::set_title(text);
}

bool window_closed()
{
    return host_sdl::window_closed();
}

void frame_layout(RECT *frame, RECT *picture)
{
    int win_w = 0, win_h = 0;
    if (host_sdl::window()) {
        SDL_GetWindowSize(host_sdl::window(), &win_w, &win_h);
    }
    if (win_w <= 0) win_w = 1280;
    if (win_h <= 0) win_h = 720;

    int fw = std::clamp(win_w, 320, g_bb_w);
    int fh = std::clamp(win_h, 180, g_bb_h);

    g_frame_w = fw;
    g_frame_h = fh;

    if (frame) {
        frame->left = 0;
        frame->top = 0;
        frame->right = fw;
        frame->bottom = fh;
    }

    if (picture) {
        int pw = fw;
        int ph = fw * 9 / 16;
        if (ph > fh) {
            ph = fh;
            pw = fh * 16 / 9;
        }

        int x = (fw - pw) / 2;
        int y = (fh - ph) / 2;
        picture->left = x;
        picture->top = y;
        picture->right = x + pw;
        picture->bottom = y + ph;
    }
}

bool mouse_captured()
{
    return host_sdl::mouse_captured();
}

void request_capture(bool on)
{
    host_sdl::request_capture(on);
}

void set_fullscreen(bool on)
{
    host_sdl::set_fullscreen(on);
}

bool fullscreen()
{
    return host_sdl::is_fullscreen();
}

int mouse_buttons()
{
    return host_sdl::mouse_buttons();
}

void mouse_take(int *dx, int *dy, int *wheel)
{
    host_sdl::mouse_take(dx, dy, wheel);
}

bool create_device()
{
    if (g_dev) return true;

    // Ensure DXVK WSI driver is configured to use SDL2
    setenv("DXVK_WSI_DRIVER", "SDL2", 1);

    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!g_d3d) {
        std::fprintf(stderr, "[host3d] Direct3DCreate9 failed on DXVK-Native\n");
        return false;
    }

    HWND hwnd = window();
    if (!hwnd) {
        std::fprintf(stderr, "[host3d] Window handle is null, cannot create D3D9 device\n");
        return false;
    }

    int max_w = std::max(1920, g_bb_w);
    int max_h = std::max(1080, g_bb_h);
    int num_displays = SDL_GetNumVideoDisplays();
    for (int i = 0; i < num_displays; ++i) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(i, &dm) == 0) {
            if (dm.w > max_w) max_w = dm.w;
            if (dm.h > max_h) max_h = dm.h;
        }
    }
    g_bb_w = max_w;
    g_bb_h = max_h;

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = g_bb_w;
    pp.BackBufferHeight = g_bb_h;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
    g_pp = pp;

    HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &pp, &g_dev);
    if (FAILED(hr)) {
        std::fprintf(stderr, "[host3d] Hardware VP failed (0x%08x), retrying software vertex processing\n", (unsigned int)hr);
        flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
        hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &pp, &g_dev);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[host3d] CreateDevice failed on DXVK-Native (0x%08x)\n", (unsigned int)hr);
            return false;
        }
    }

    D3DADAPTER_IDENTIFIER9 ident{};
    if (SUCCEEDED(g_d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident))) {
        std::printf("[host3d] Direct3D 9 via DXVK Vulkan device created: '%s' (%dx%d)\n",
                    ident.Description, g_bb_w, g_bb_h);
    }

    g_intz = SUCCEEDED(g_d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                                D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FMT_INTZ));
    std::printf("[host3d] INTZ depth texture support: %s\n", g_intz ? "YES" : "NO");

    return true;
}

IDirect3DDevice9 *dev()
{
    return g_dev;
}

bool intz()
{
    return g_intz;
}

void begin_scene()
{
    if (!g_in_scene && g_dev) {
        g_dev->BeginScene();
        g_in_scene = true;
    }
}

void end_scene()
{
    if (g_in_scene && g_dev) {
        g_dev->EndScene();
        g_in_scene = false;
    }
}

HRESULT present()
{
    if (!g_dev) return D3D_OK;
    begin_scene();
    g_dev->EndScene();
    g_in_scene = false;

    RECT src{0, 0, g_frame_w ? g_frame_w : g_bb_w, g_frame_h ? g_frame_h : g_bb_h};
    HRESULT hr = g_dev->Present(&src, nullptr, nullptr, nullptr);
    if (FAILED(hr)) {
        hr = g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    }
    return hr;
}

bool reset_device()
{
    end_scene();
    if (!g_dev) return false;
    D3DPRESENT_PARAMETERS pp = g_pp;
    HRESULT hr = g_dev->Reset(&pp);
    return SUCCEEDED(hr);
}

} // namespace host3d
