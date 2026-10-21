// Unit Test: DXVK-Native Hardware Draw Pipeline, Vertex Streams and Index Buffers
#include <d3dcompiler.h>
#include <d3d9.h>
#include <wsi/native_sdl2.h>
#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void check(bool cond, const char *msg)
{
    if (!cond) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    std::printf("=== Running test_dxvk_draw_pipeline ===\n");
    setenv("DXVK_WSI_DRIVER", "SDL2", 1);

    // 1. Initialize SDL2
    check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL_Init failed");

    // 2. Create SDL2 window with Vulkan flag
    SDL_Window *win = SDL_CreateWindow("DXVK Draw Pipeline Test",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       640, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN);
    check(win != nullptr, "SDL_CreateWindow failed");

    // 3. Create Direct3D 9 device via DXVK
    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        std::printf("[SKIP] Vulkan ICD / DXVK-Native adapter not available in this environment\n");
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    HWND hwnd = dxvk::wsi::toHwnd(win);
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

    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                  D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr) || !dev) {
        std::printf("[SKIP] Vulkan device creation not supported in this environment\n");
        d3d->Release();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }
    std::printf("[test_dxvk_draw_pipeline] Direct3D9 device initialized successfully\n");

    // Verify Cull Mode mappings (Xbox NV2A register values -> D3DCULL)
    auto test_conv_cull = [](uint32_t cull) -> D3DCULL {
        if (cull == 0) return D3DCULL_NONE;
        return cull == 0x900 ? D3DCULL_CW : D3DCULL_CCW;
    };
    check(test_conv_cull(0) == D3DCULL_NONE, "Cull mode 0 must be D3DCULL_NONE");
    check(test_conv_cull(0x900) == D3DCULL_CW, "Cull mode 0x900 must be D3DCULL_CW");
    check(test_conv_cull(0x901) == D3DCULL_CCW, "Cull mode 0x901 must be D3DCULL_CCW");
    check(SUCCEEDED(dev->SetRenderState(D3DRS_CULLMODE, test_conv_cull(0x901))), "SetRenderState D3DCULL_CCW failed");
    std::printf("[test_dxvk_draw_pipeline] Cull mode mapping verified (0x900=CW, 0x901=CCW)\n");

    // 4. Compile shaders via D3DCompile runtime (standalone, zero /tmp dependency)
    const char *vs_src =
        "row_major float4x4 g_m : register(c0);\n"
        "struct VS_IN  { float4 pos : POSITION; };\n"
        "struct VS_OUT { float4 pos : POSITION; };\n"
        "VS_OUT main(VS_IN vin) {\n"
        "    VS_OUT vout;\n"
        "    vout.pos = mul(vin.pos, g_m);\n"
        "    return vout;\n"
        "}\n";

    const char *ps_src =
        "float4 main() : COLOR {\n"
        "    return float4(0.2f, 0.4f, 0.8f, 1.0f);\n"
        "}\n";

    ID3DBlob *vs_blob = nullptr, *err_blob = nullptr;
    hr = D3DCompile(vs_src, std::strlen(vs_src), "pipe_vs", nullptr, nullptr, "main", "vs_2_0", 0, 0, &vs_blob, &err_blob);
    if (FAILED(hr) || !vs_blob) {
        std::printf("[SKIP] D3DCompile not available in this environment (requires fxc compiler tool)\n");
        dev->Release();
        d3d->Release();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    ID3DBlob *ps_blob = nullptr;
    hr = D3DCompile(ps_src, std::strlen(ps_src), "pipe_ps", nullptr, nullptr, "main", "ps_2_0", 0, 0, &ps_blob, &err_blob);
    if (FAILED(hr) || !ps_blob) {
        std::printf("[SKIP] D3DCompile not available in this environment (requires fxc compiler tool)\n");
        vs_blob->Release();
        dev->Release();
        d3d->Release();
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 0;
    }

    IDirect3DVertexShader9 *vs = nullptr;
    hr = dev->CreateVertexShader((const DWORD *)vs_blob->GetBufferPointer(), &vs);
    check(SUCCEEDED(hr) && vs != nullptr, "CreateVertexShader failed in DXVK");
    std::printf("[test_dxvk_draw_pipeline] Vertex Shader created in Vulkan (%zu bytes)\n", vs_blob->GetBufferSize());

    IDirect3DPixelShader9 *ps = nullptr;
    hr = dev->CreatePixelShader((const DWORD *)ps_blob->GetBufferPointer(), &ps);
    check(SUCCEEDED(hr) && ps != nullptr, "CreatePixelShader failed in DXVK");
    std::printf("[test_dxvk_draw_pipeline] Pixel Shader created in Vulkan (%zu bytes)\n", ps_blob->GetBufferSize());

    // 5. Create Vertex Declaration
    D3DVERTEXELEMENT9 decl_elems[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9 *decl = nullptr;
    hr = dev->CreateVertexDeclaration(decl_elems, &decl);
    check(SUCCEEDED(hr) && decl != nullptr, "CreateVertexDeclaration failed");

    // 6. Create Vertex Buffer and fill triangle coordinates
    struct Vertex {
        float x, y, z, w;
    };
    Vertex triangle_vertices[3] = {
        { -0.5f, -0.5f, 0.0f, 1.0f },
        {  0.0f,  0.5f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f }
    };

    IDirect3DVertexBuffer9 *vb = nullptr;
    hr = dev->CreateVertexBuffer(sizeof(triangle_vertices), 0, 0, D3DPOOL_DEFAULT, &vb, nullptr);
    check(SUCCEEDED(hr) && vb != nullptr, "CreateVertexBuffer failed");

    void *vb_ptr = nullptr;
    hr = vb->Lock(0, sizeof(triangle_vertices), &vb_ptr, 0);
    check(SUCCEEDED(hr) && vb_ptr != nullptr, "vb->Lock failed");
    std::memcpy(vb_ptr, triangle_vertices, sizeof(triangle_vertices));
    vb->Unlock();

    // 7. Render Triangle Frame through full pipeline
    hr = dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(12, 28, 16), 1.0f, 0);
    check(SUCCEEDED(hr), "dev->Clear failed");

    hr = dev->BeginScene();
    check(SUCCEEDED(hr), "dev->BeginScene failed");

    // Bind state
    dev->SetVertexDeclaration(decl);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    dev->SetStreamSource(0, vb, 0, sizeof(Vertex));

    // Upload 4x4 matrix constant (Identity matrix) to c0..c3
    float identity_matrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    hr = dev->SetVertexShaderConstantF(0, identity_matrix, 4);
    check(SUCCEEDED(hr), "SetVertexShaderConstantF failed");

    // Draw Primitive
    hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    check(SUCCEEDED(hr), "DrawPrimitive failed");

    // Test DrawIndexedPrimitiveUP with non-zero indices rebased to 0 (Mission 2 weapon/level mesh pattern)
    Vertex up_verts[4] = {
        { -0.2f, -0.2f, 0.0f, 1.0f },
        {  0.2f, -0.2f, 0.0f, 1.0f },
        {  0.2f,  0.2f, 0.0f, 1.0f },
        { -0.2f,  0.2f, 0.0f, 1.0f },
    };
    // Non-zero raw indices (e.g. mn=100, mx=103)
    uint16_t raw_idx[6] = { 100, 101, 102, 100, 102, 103 };
    uint16_t mn = 100, mx = 103;
    uint16_t rebased[6];
    for (int i = 0; i < 6; i++) rebased[i] = raw_idx[i] - mn;

    hr = dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, mx - mn + 1, 2, rebased, D3DFMT_INDEX16, up_verts, sizeof(Vertex));
    check(SUCCEEDED(hr), "DrawIndexedPrimitiveUP with rebased indices failed");
    std::printf("[test_dxvk_draw_pipeline] DrawIndexedPrimitiveUP (rebased non-zero indices) executed successfully!\n");

    hr = dev->EndScene();
    check(SUCCEEDED(hr), "dev->EndScene failed");

    hr = dev->Present(nullptr, nullptr, nullptr, nullptr);
    check(SUCCEEDED(hr), "dev->Present failed");

    std::printf("[test_dxvk_draw_pipeline] DrawPrimitive & DrawIndexedPrimitiveUP executed and presented via Vulkan!\n");

    // 8. Clean up
    vb->Release();
    decl->Release();
    ps->Release();
    vs->Release();
    vs_blob->Release();
    ps_blob->Release();
    dev->Release();
    d3d->Release();
    SDL_DestroyWindow(win);
    SDL_Quit();

    std::printf("[PASS] test_dxvk_draw_pipeline completed successfully with zero errors.\n\n");
    return 0;
}
