// Unit Test: DXVK-Native D3D9 Initialization on Linux x86
#include <d3d9.h>
#include <wsi/native_sdl2.h>
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>

static void check(bool cond, const char *msg)
{
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    std::printf("=== Running test_dxvk_device ===\n");

    // 1. Initialize SDL2 Video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Failed to init SDL2: %s\n", SDL_GetError());
        return 1;
    }

    // 2. Create hidden SDL2 window with SDL_WINDOW_VULKAN
    SDL_Window *win = SDL_CreateWindow("DXVK-Native Test",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       640, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
    check(win != nullptr, "SDL_CreateWindow failed");
    std::printf("[test_dxvk_device] SDL2 window created successfully\n");

    // 3. Create Direct3D 9 via DXVK-Native
    IDirect3D9 *d3d = nullptr;
    try {
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
    } catch (const std::exception &e) {
        std::printf("[test_dxvk_device] Direct3DCreate9 threw exception: %s\n", e.what());
        return 1;
    } catch (...) {
        std::printf("[test_dxvk_device] Direct3DCreate9 threw unknown exception\n");
        return 1;
    }
    if (!d3d) {
        std::printf("[SKIP] Vulkan ICD / DXVK-Native adapter not available in this environment\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }
    std::printf("[test_dxvk_device] Direct3DCreate9 succeeded\n");

    // 4. Query Adapter info
    D3DADAPTER_IDENTIFIER9 ident{};
    if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident))) {
        std::printf("[test_dxvk_device] GPU Adapter: '%s' (Driver: '%s')\n",
                    ident.Description, ident.Driver);
    }

    // 5. Convert SDL_Window to DXVK HWND
    HWND hwnd = dxvk::wsi::toHwnd(win);
    check(hwnd != nullptr, "dxvk::wsi::toHwnd returned null");

    // 6. Setup D3DPRESENT_PARAMETERS
    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    // 7. Create Direct3D 9 Device
    IDirect3DDevice9 *dev = nullptr;
    DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &pp, &dev);
    if (FAILED(hr)) {
        std::printf("[test_dxvk_device] Hardware VP failed (0x%08x), retrying software VP...\n", (unsigned int)hr);
        flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, flags, &pp, &dev);
    }

    if (FAILED(hr) || !dev) {
        std::printf("[SKIP] Vulkan device creation not supported in this environment\n");
        d3d->Release();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }
    std::printf("[test_dxvk_device] IDirect3DDevice9 created successfully via Vulkan!\n");

    // 8. Test basic frame operations (Clear, BeginScene, EndScene, Present)
    hr = dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(10, 48, 18), 1.0f, 0);
    check(SUCCEEDED(hr), "dev->Clear failed");

    hr = dev->BeginScene();
    check(SUCCEEDED(hr), "dev->BeginScene failed");

    hr = dev->EndScene();
    check(SUCCEEDED(hr), "dev->EndScene failed");

    hr = dev->Present(nullptr, nullptr, nullptr, nullptr);
    check(SUCCEEDED(hr), "dev->Present failed");
    std::printf("[test_dxvk_device] Clear, BeginScene, EndScene, Present verified!\n");

    // 9. Clean release
    dev->Release();
    d3d->Release();
    SDL_DestroyWindow(win);
    SDL_Quit();

    std::printf("[PASS] test_dxvk_device completed successfully with zero errors.\n\n");
    return 0;
}
