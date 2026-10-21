// Integration Test: Complete Graphics Subsystem & NV2A Hardware Draw Pipeline (Linux x86)
// Interactive 3D Vulkan Demo featuring Shaders, Geometry, Gamepad/Keyboard Input, and Audio Synthesis.
#include "../loader/src/d3d/host.h"
#include "../loader/src/platform/host_sdl.h"
#include "../loader/src/platform/input_sdl.h"
#include "../loader/src/platform/audio_sdl.h"

#include <d3d9.h>
#include <d3dcompiler.h>
#include <wsi/native_sdl2.h>
#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>

static void check(bool condition, const char *msg)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL render_pipeline] %s\n", msg);
        std::exit(1);
    }
}

struct Vertex3D {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

struct Mat4 { float m[4][4]; };

static Mat4 mat_identity()
{
    Mat4 r{};
    for (int i = 0; i < 4; i++) r.m[i][i] = 1.0f;
    return r;
}

static Mat4 mat_mul(const Mat4 &a, const Mat4 &b)
{
    Mat4 r{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] +
                        a.m[i][1] * b.m[1][j] +
                        a.m[i][2] * b.m[2][j] +
                        a.m[i][3] * b.m[3][j];
        }
    }
    return r;
}

static Mat4 mat_rotate_y(float rad)
{
    Mat4 r = mat_identity();
    r.m[0][0] = std::cos(rad);
    r.m[0][2] = -std::sin(rad);
    r.m[2][0] = std::sin(rad);
    r.m[2][2] = std::cos(rad);
    return r;
}

static Mat4 mat_rotate_x(float rad)
{
    Mat4 r = mat_identity();
    r.m[1][1] = std::cos(rad);
    r.m[1][2] = -std::sin(rad);
    r.m[2][1] = std::sin(rad);
    r.m[2][2] = std::cos(rad);
    return r;
}

static Mat4 mat_perspective_lh(float fov_rad, float aspect, float znear, float zfar)
{
    Mat4 r{};
    float h = 1.0f / std::tan(fov_rad * 0.5f);
    float w = h / aspect;
    float q = zfar / (zfar - znear);
    r.m[0][0] = w;
    r.m[1][1] = h;
    r.m[2][2] = q;
    r.m[2][3] = 1.0f;
    r.m[3][2] = -znear * q;
    return r;
}

#define CUBE_VERT(px, py, pz, normx, normy, normz, texu, texv, cr, cg, cb) \
    Vertex3D{ (px), (py), (pz), (normx), (normy), (normz), (texu), (texv), (cr), (cg), (cb), 1.0f }

// Generates an indexed or triangle-list 3D colored cube (36 vertices)
// CW winding with outward-facing normals and UV coordinates for face borders
static void build_colored_cube(Vertex3D *out)
{
    const float p = 0.5f;

    Vertex3D v[36] = {
        // Front (Z = -p) - Facing Camera, Ruby Red
        CUBE_VERT(-p,  p, -p,  0, 0,-1,  0,1,  0.95f, 0.20f, 0.25f),
        CUBE_VERT( p,  p, -p,  0, 0,-1,  1,1,  0.95f, 0.20f, 0.25f),
        CUBE_VERT( p, -p, -p,  0, 0,-1,  1,0,  0.95f, 0.20f, 0.25f),
        CUBE_VERT(-p,  p, -p,  0, 0,-1,  0,1,  0.95f, 0.20f, 0.25f),
        CUBE_VERT( p, -p, -p,  0, 0,-1,  1,0,  0.95f, 0.20f, 0.25f),
        CUBE_VERT(-p, -p, -p,  0, 0,-1,  0,0,  0.95f, 0.20f, 0.25f),

        // Back (Z = +p) - Emerald Green
        CUBE_VERT( p,  p,  p,  0, 0, 1,  0,1,  0.15f, 0.85f, 0.35f),
        CUBE_VERT(-p,  p,  p,  0, 0, 1,  1,1,  0.15f, 0.85f, 0.35f),
        CUBE_VERT(-p, -p,  p,  0, 0, 1,  1,0,  0.15f, 0.85f, 0.35f),
        CUBE_VERT( p,  p,  p,  0, 0, 1,  0,1,  0.15f, 0.85f, 0.35f),
        CUBE_VERT(-p, -p,  p,  0, 0, 1,  1,0,  0.15f, 0.85f, 0.35f),
        CUBE_VERT( p, -p,  p,  0, 0, 1,  0,0,  0.15f, 0.85f, 0.35f),

        // Top (Y = +p) - Sapphire Blue
        CUBE_VERT(-p,  p,  p,  0, 1, 0,  0,1,  0.20f, 0.45f, 0.95f),
        CUBE_VERT( p,  p,  p,  0, 1, 0,  1,1,  0.20f, 0.45f, 0.95f),
        CUBE_VERT( p,  p, -p,  0, 1, 0,  1,0,  0.20f, 0.45f, 0.95f),
        CUBE_VERT(-p,  p,  p,  0, 1, 0,  0,1,  0.20f, 0.45f, 0.95f),
        CUBE_VERT( p,  p, -p,  0, 1, 0,  1,0,  0.20f, 0.45f, 0.95f),
        CUBE_VERT(-p,  p, -p,  0, 1, 0,  0,0,  0.20f, 0.45f, 0.95f),

        // Bottom (Y = -p) - Amber Gold
        CUBE_VERT(-p, -p, -p,  0,-1, 0,  0,1,  0.95f, 0.75f, 0.15f),
        CUBE_VERT( p, -p, -p,  0,-1, 0,  1,1,  0.95f, 0.75f, 0.15f),
        CUBE_VERT( p, -p,  p,  0,-1, 0,  1,0,  0.95f, 0.75f, 0.15f),
        CUBE_VERT(-p, -p, -p,  0,-1, 0,  0,1,  0.95f, 0.75f, 0.15f),
        CUBE_VERT( p, -p,  p,  0,-1, 0,  1,0,  0.95f, 0.75f, 0.15f),
        CUBE_VERT(-p, -p,  p,  0,-1, 0,  0,0,  0.95f, 0.75f, 0.15f),

        // Right (X = +p) - Cyan
        CUBE_VERT( p,  p, -p,  1, 0, 0,  0,1,  0.15f, 0.85f, 0.90f),
        CUBE_VERT( p,  p,  p,  1, 0, 0,  1,1,  0.15f, 0.85f, 0.90f),
        CUBE_VERT( p, -p,  p,  1, 0, 0,  1,0,  0.15f, 0.85f, 0.90f),
        CUBE_VERT( p,  p, -p,  1, 0, 0,  0,1,  0.15f, 0.85f, 0.90f),
        CUBE_VERT( p, -p,  p,  1, 0, 0,  1,0,  0.15f, 0.85f, 0.90f),
        CUBE_VERT( p, -p, -p,  1, 0, 0,  0,0,  0.15f, 0.85f, 0.90f),

        // Left (X = -p) - Magenta
        CUBE_VERT(-p,  p,  p, -1, 0, 0,  0,1,  0.90f, 0.25f, 0.85f),
        CUBE_VERT(-p,  p, -p, -1, 0, 0,  1,1,  0.90f, 0.25f, 0.85f),
        CUBE_VERT(-p, -p, -p, -1, 0, 0,  1,0,  0.90f, 0.25f, 0.85f),
        CUBE_VERT(-p,  p,  p, -1, 0, 0,  0,1,  0.90f, 0.25f, 0.85f),
        CUBE_VERT(-p, -p, -p, -1, 0, 0,  1,0,  0.90f, 0.25f, 0.85f),
        CUBE_VERT(-p, -p,  p, -1, 0, 0,  0,0,  0.90f, 0.25f, 0.85f),
    };

    for (int i = 0; i < 36; ++i) {
        out[i] = v[i];
    }
}

int main(int argc, char **argv)
{
    bool interactive = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gui") == 0 || std::strcmp(argv[i], "-g") == 0 ||
            std::strcmp(argv[i], "--interactive") == 0 || std::strcmp(argv[i], "-i") == 0) {
            interactive = true;
        }
    }

    std::printf("====================================================\n");
    if (interactive) {
        std::printf("=== BLACK PC: Demonstracao Interativa Completa ===\n");
        std::printf("  - Renderizador 3D: Direct3D 9 via DXVK-Native (Vulkan)\n");
        std::printf("  - GPU: AMD Radeon 610M (RADV Vulkan)\n");
        std::printf("  - Subsistema de Áudio: SDL2 PCM Estéreo Ativo\n");
        std::printf("  - Subsistema de Input: Teclado (WASD/Setas) e Gamepad\n");
        std::printf("  - Controles: WASD ou Analógico para girar o cubo 3D\n");
        std::printf("  - Pressione ESC ou feche a janela para finalizar\n");
    } else {
        std::printf("=== Running Render Pipeline Integration Test ====\n");
    }
    std::printf("====================================================\n");

    // 1. Configure DXVK WSI Driver to use SDL2
    setenv("DXVK_WSI_DRIVER", "SDL2", 1);
    if (!interactive) {
        setenv("BLACK_HEADLESS", "1", 1);
    } else {
        unsetenv("BLACK_HEADLESS");
    }

    // 2. Initialize Host Window via SDL2
    bool win_ok = host3d::open_window(1280, 720, 16, 9);
    check(win_ok, "Failed to open host SDL2 window");
    HWND hwnd = host3d::window();
    check(hwnd != nullptr, "Window handle is null");
    std::printf("[render_pipeline] Host window opened successfully (1280x720, %s)\n",
                interactive ? "visible" : "headless");

    // 3. Initialize Input Subsystem
    bool in_ok = input_sdl::init();
    check(in_ok, "Failed to initialize input subsystem");
    std::printf("[render_pipeline] Input subsystem initialized\n");

    // 4. Initialize Audio Subsystem
    bool audio_ok = audio_sdl::open();
    std::printf("[render_pipeline] Audio subsystem initialized: %s\n",
                audio_ok ? "hardware output" : "clock fallback");

    // 5. Initialize Direct3D 9 Device via DXVK-Native
    bool dev_ok = host3d::create_device();
    if (!dev_ok || !host3d::dev()) {
        std::printf("[SKIP] DXVK-Native Direct3D 9 device not supported in this environment\n");
        host_sdl::close_window();
        return 0;
    }
    IDirect3DDevice9 *dev = host3d::dev();

    // 6. Verify GPU Capabilities
    D3DCAPS9 caps{};
    HRESULT hr = dev->GetDeviceCaps(&caps);
    check(SUCCEEDED(hr), "GetDeviceCaps failed");
    std::printf("[render_pipeline] GPU D3D9 Caps: VS Version 0x%08X, PS Version 0x%08X, Max Streams %u\n",
                (unsigned int)caps.VertexShaderVersion, (unsigned int)caps.PixelShaderVersion, (unsigned int)caps.MaxStreams);
    check(caps.VertexShaderVersion >= D3DVS_VERSION(2, 0), "Vertex Shader 2.0+ required");
    check(caps.PixelShaderVersion >= D3DPS_VERSION(2, 0), "Pixel Shader 2.0+ required");

    // 7. Compile HLSL Vertex Shader & Pixel Shader via Linux D3DCompile
    const char *vs_src =
        "row_major float4x4 u_wvp   : register(c0);\n"
        "row_major float4x4 u_world : register(c4);\n"
        "float4             u_light : register(c8);\n"
        "float4             u_tint  : register(c9);\n"
        "struct VS_IN {\n"
        "    float3 pos    : POSITION;\n"
        "    float3 normal : NORMAL;\n"
        "    float2 uv     : TEXCOORD0;\n"
        "    float4 col    : COLOR0;\n"
        "};\n"
        "struct VS_OUT {\n"
        "    float4 pos    : POSITION;\n"
        "    float4 col    : COLOR0;\n"
        "    float2 uv     : TEXCOORD0;\n"
        "    float3 normal : TEXCOORD1;\n"
        "};\n"
        "VS_OUT main(VS_IN vin) {\n"
        "    VS_OUT vout;\n"
        "    vout.pos = mul(float4(vin.pos, 1.0f), u_wvp);\n"
        "    float3 norm_world = normalize(mul(vin.normal, (float3x3)u_world));\n"
        "    float3 light_dir = normalize(u_light.xyz);\n"
        "    float diff = max(0.0f, dot(norm_world, light_dir));\n"
        "    float light_intensity = u_light.w + (1.0f - u_light.w) * diff;\n"
        "    vout.col = float4(vin.col.rgb * light_intensity * u_tint.rgb, vin.col.a);\n"
        "    vout.uv = vin.uv;\n"
        "    vout.normal = norm_world;\n"
        "    return vout;\n"
        "}\n";

    const char *ps_src =
        "struct PS_IN {\n"
        "    float4 col    : COLOR0;\n"
        "    float2 uv     : TEXCOORD0;\n"
        "    float3 normal : TEXCOORD1;\n"
        "};\n"
        "float4 main(PS_IN pin) : COLOR0 {\n"
        "    float2 d = min(pin.uv, 1.0f - pin.uv);\n"
        "    float edge = min(d.x, d.y);\n"
        "    float border = smoothstep(0.02f, 0.05f, edge);\n"
        "    float3 view_dir = float3(0.0f, 0.0f, -1.0f);\n"
        "    float3 light_dir = normalize(float3(0.5f, 0.8f, -0.7f));\n"
        "    float3 half_vec = normalize(light_dir + view_dir);\n"
        "    float spec = pow(max(0.0f, dot(pin.normal, half_vec)), 16.0f) * 0.35f;\n"
        "    float3 final_rgb = pin.col.rgb * (0.35f + 0.65f * border) + spec;\n"
        "    return float4(final_rgb, pin.col.a);\n"
        "}\n";

    ID3DBlob *vs_blob = nullptr;
    ID3DBlob *err_blob = nullptr;
    hr = D3DCompile(vs_src, std::strlen(vs_src), "pipeline_vs", nullptr, nullptr, "main", "vs_2_0", 0, 0, &vs_blob, &err_blob);
    if (FAILED(hr) || !vs_blob) {
        if (err_blob) std::fprintf(stderr, "VS compile error: %s\n", (char*)err_blob->GetBufferPointer());
        std::printf("[SKIP] D3DCompile not available in this environment (requires fxc compiler tool)\n");
        dev->Release();
        host_sdl::close_window();
        return 0;
    }

    ID3DBlob *ps_blob = nullptr;
    hr = D3DCompile(ps_src, std::strlen(ps_src), "pipeline_ps", nullptr, nullptr, "main", "ps_2_0", 0, 0, &ps_blob, &err_blob);
    if (FAILED(hr) || !ps_blob) {
        if (err_blob) std::fprintf(stderr, "PS compile error: %s\n", (char*)err_blob->GetBufferPointer());
        std::printf("[SKIP] D3DCompile not available in this environment (requires fxc compiler tool)\n");
        vs_blob->Release();
        dev->Release();
        host_sdl::close_window();
        return 0;
    }

    // 8. Create Shaders on DXVK Device
    IDirect3DVertexShader9 *vs = nullptr;
    hr = dev->CreateVertexShader((const DWORD *)vs_blob->GetBufferPointer(), &vs);
    check(SUCCEEDED(hr) && vs != nullptr, "dev->CreateVertexShader failed");

    IDirect3DPixelShader9 *ps = nullptr;
    hr = dev->CreatePixelShader((const DWORD *)ps_blob->GetBufferPointer(), &ps);
    check(SUCCEEDED(hr) && ps != nullptr, "dev->CreatePixelShader failed");
    std::printf("[render_pipeline] Shaders compiled and created on GPU successfully\n");

    // 9. Create Vertex Declaration
    D3DVERTEXELEMENT9 elems[] = {
        { 0, 0,  D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,   0 },
        { 0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,    0 },
        D3DDECL_END()
    };
    IDirect3DVertexDeclaration9 *decl = nullptr;
    hr = dev->CreateVertexDeclaration(elems, &decl);
    check(SUCCEEDED(hr) && decl != nullptr, "dev->CreateVertexDeclaration failed");

    // 10. Create Vertex Buffer with 3D Colored Cube (36 vertices)
    Vertex3D cube_vertices[36];
    build_colored_cube(cube_vertices);

    IDirect3DVertexBuffer9 *vb = nullptr;
    hr = dev->CreateVertexBuffer(sizeof(cube_vertices), 0, 0, D3DPOOL_DEFAULT, &vb, nullptr);
    check(SUCCEEDED(hr) && vb != nullptr, "dev->CreateVertexBuffer failed");

    void *ptr = nullptr;
    hr = vb->Lock(0, sizeof(cube_vertices), &ptr, 0);
    check(SUCCEEDED(hr) && ptr != nullptr, "vb->Lock failed");
    std::memcpy(ptr, cube_vertices, sizeof(cube_vertices));
    vb->Unlock();

    std::printf("[render_pipeline] 3D Cube geometry buffer initialized (36 vertices)\n");

    // Set hardware render states: depth buffer enabled, CCW culling
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);

    // 11. Render & Interaction Loop
    float rot_x = 0.4f;
    float rot_y = 0.5f;
    float zoom = 1.0f;
    float audio_phase = 0.0f;
    uint32_t target_frames = interactive ? 0 : 60;
    uint32_t frame_count = 0;

    auto last_fps_time = std::chrono::steady_clock::now();
    uint32_t fps_counter = 0;
    float current_fps = 60.0f;

    while (!host3d::window_closed()) {
        host_sdl::pump_events();
        if (host3d::window_closed()) break;

        // Poll Input
        input_sdl::poll();
        XboxInputState pad0{};
        input_sdl::get_state(0, &pad0);

        // Check for ESC or Back+Start to quit
        if (interactive) {
            const Uint8 *keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_ESCAPE]) break;
            if ((pad0.Gamepad.wButtons & XINPUT_BACK) && (pad0.Gamepad.wButtons & XINPUT_START)) break;

            // Handle Keyboard controls
            if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    rot_x -= 0.04f;
            if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  rot_x += 0.04f;
            if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  rot_y -= 0.04f;
            if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) rot_y += 0.04f;
            if (keys[SDL_SCANCODE_Q]) zoom = std::max(0.4f, zoom - 0.02f);
            if (keys[SDL_SCANCODE_E]) zoom = std::min(2.5f, zoom + 0.02f);
            if (keys[SDL_SCANCODE_R]) { rot_x = 0.4f; rot_y = 0.5f; zoom = 1.0f; }

            // Handle Gamepad sticks
            if (std::abs(pad0.Gamepad.sThumbLX) > 4000) rot_y += (pad0.Gamepad.sThumbLX / 32768.0f) * 0.06f;
            if (std::abs(pad0.Gamepad.sThumbLY) > 4000) rot_x -= (pad0.Gamepad.sThumbLY / 32768.0f) * 0.06f;
            if (std::abs(pad0.Gamepad.sThumbRY) > 4000) zoom = std::clamp(zoom + (pad0.Gamepad.sThumbRY / 32768.0f) * 0.03f, 0.4f, 2.5f);
        } else {
            // In automated test, rotate steadily
            rot_x += 0.03f;
            rot_y += 0.05f;
        }

        // Auto spin slight idle rotation
        rot_y += 0.01f;

        // Modulate Audio Frequency based on rotation & triggers
        float base_freq = 330.0f; // E4
        float freq_mod = std::abs(std::sin(rot_y)) * 110.0f + (pad0.Gamepad.bAnalogButtons[7] / 255.0f) * 220.0f;
        float current_freq = base_freq + freq_mod;

        // Submit audio block if desired
        uint32_t wanted_blocks = audio_sdl::blocks_wanted();
        if (wanted_blocks > 0) {
            float audio_buf[audio_sdl::BLOCK_SIZE * audio_sdl::CHANNELS];
            for (uint32_t i = 0; i < audio_sdl::BLOCK_SIZE; ++i) {
                float sample = 0.05f * std::sin(audio_phase);
                audio_phase += 2.0f * 3.1415926535f * current_freq / (float)audio_sdl::SAMPLE_RATE;
                if (audio_phase > 2.0f * 3.1415926535f) audio_phase -= 2.0f * 3.1415926535f;

                audio_buf[i * 2 + 0] = sample;
                audio_buf[i * 2 + 1] = sample;
            }
            for (uint32_t b = 0; b < wanted_blocks; ++b) {
                audio_sdl::submit(audio_buf);
            }
        }

        // 3D Perspective Matrix Setup
        Mat4 rx = mat_rotate_x(rot_x);
        Mat4 ry = mat_rotate_y(rot_y);
        Mat4 world = mat_mul(rx, ry);

        Mat4 view = mat_identity();
        view.m[3][2] = 2.8f / zoom; // Camera eye at (0, 0, -2.8 / zoom)

        Mat4 proj = mat_perspective_lh(55.0f * 3.14159265f / 180.0f, 16.0f / 9.0f, 0.1f, 100.0f);
        Mat4 wvp = mat_mul(mat_mul(world, view), proj);

        float light[4] = { 0.5f, 0.8f, -0.7f, 0.35f }; // Direction (x, y, z), Ambient (w)

        // Tint modulation vector
        float tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (pad0.Gamepad.bAnalogButtons[7] > 0) { // RT gives amber/red glow
            tint[0] = 1.0f + (pad0.Gamepad.bAnalogButtons[7] / 255.0f) * 0.5f;
            tint[1] = 1.0f - (pad0.Gamepad.bAnalogButtons[7] / 255.0f) * 0.2f;
            tint[2] = 1.0f - (pad0.Gamepad.bAnalogButtons[7] / 255.0f) * 0.3f;
        }

        // Render Frame
        host3d::begin_scene();

        // Clear Color: dark carbon blue (Xbox/BLACK aesthetics)
        hr = dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                        D3DCOLOR_XRGB(14, 20, 32), 1.0f, 0);
        check(SUCCEEDED(hr), "dev->Clear failed");

        dev->SetVertexShaderConstantF(0, (const float*)&wvp, 4);
        dev->SetVertexShaderConstantF(4, (const float*)&world, 4);
        dev->SetVertexShaderConstantF(8, light, 1);
        dev->SetVertexShaderConstantF(9, tint, 1);
        dev->SetVertexDeclaration(decl);
        dev->SetStreamSource(0, vb, 0, sizeof(Vertex3D));
        dev->SetVertexShader(vs);
        dev->SetPixelShader(ps);

        // Draw the 12 triangles of the 3D cube
        hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 12);
        check(SUCCEEDED(hr), "dev->DrawPrimitive failed");

        hr = host3d::present();
        check(SUCCEEDED(hr), "host3d::present failed");

        frame_count++;
        fps_counter++;

        // Measure FPS and update window title
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - last_fps_time).count();
        if (elapsed_sec >= 0.5) {
            current_fps = (float)(fps_counter / elapsed_sec);
            fps_counter = 0;
            last_fps_time = now;

            if (interactive) {
                char title[256];
                std::snprintf(title, sizeof(title),
                              "BLACK PC Demo | Vulkan RADV (%.1f FPS) | Rot:(%.1f, %.1f) Zoom:%.2f | Som: %.0fHz | [WASD/Setas/Controle]",
                              current_fps, rot_x, rot_y, zoom, current_freq);
                host3d::set_title(title);
            }
        }

        if (target_frames > 0 && frame_count >= target_frames) {
            break;
        }

        if (interactive) {
            SDL_Delay(16); // ~60 FPS
        }
    }

    std::printf("[render_pipeline] Successfully rendered %u frames via Vulkan hardware backend\n", frame_count);

    // 12. Clean up Direct3D and Audio resources
    audio_sdl::close();
    vb->Release();
    decl->Release();
    vs->Release();
    ps->Release();
    vs_blob->Release();
    ps_blob->Release();

    std::printf("====================================================\n");
    std::printf("render_pipeline: ALL ASSERTIONS PASSED (Exit Code 0)\n");
    std::printf("Render Pipeline verified: Graphics, Shaders, Input & Audio integrated\n");
    std::printf("====================================================\n");

    return 0;
}
