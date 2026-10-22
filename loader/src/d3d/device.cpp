// HLE replacements for the Xbox D3D8 device API the game calls. Each function keeps the Xbox-side data
// the game reads directly (device object fields, render/texture state arrays) up to date and drives the
// host Direct3D 9 device.
#include "d3d/device.h"

#include "d3d/dispatch.h"
#include "d3d/draw.h"
#include "d3d/host.h"
#include "d3d/nv2a_psh.h"
#include "d3d/pshader.h"
#include "perf.h"
#include "d3d/pushbuf.h"
#include "d3d/state.h"
#include "d3d/vshader.h"
#include "kernel/kexports.h"
#include "xthread.h"

#include <d3dcompiler.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

std::atomic<uint32_t> g_frame_count;

namespace xd3d {

uint8_t *g_dev;
uint32_t *g_push_base;
uint32_t g_push_dwords;
int g_width = 640, g_height = 480;
HleState g_st;

namespace {

constexpr uint32_t PUSH_BYTES = 512 * 1024;
uint32_t g_gpu_time;
std::atomic<bool> g_shot_requested; // request_screenshot: taken at the next Swap
std::mutex g_shot_lock;
std::string g_shot_tag;
Surface *g_backbuffer, *g_depthbuffer;
Viewport g_requested_viewport = {0, 0, 640, 480, 0.0f, 1.0f}; // as the game last set it (before clamping)
IDirect3DSurface9 *g_host_backbuffer, *g_host_depth;

// Render targets live in host textures keyed by (memory address, width, height), so every Xbox surface or
// texture header over the same memory shares one host texture. That includes the game's back buffer
// (presented at Swap) and the second surface object BLACK creates over the back buffer memory for its
// post-processing composite. Depth buffers are host depth surfaces keyed the same way.
struct HostRT {
    IDirect3DTexture9 *tex = nullptr;
    uint32_t w = 0, h = 0;   // host size (Xbox size x render scale)
    uint32_t xw = 0, xh = 0; // Xbox size
    float sx = 0, sy = 0;    // the scale it was made with (1 for the small post-processing buffers)
    uint32_t epoch = 0;      // times bound for drawing (its contents may have changed since)
    IDirect3DTexture9 *smooth = nullptr; // box-filtered copy for the game's blur taps (scaled small buffers)
    uint32_t smooth_epoch = ~0u, smooth_serial = ~0u;
};
uint64_t mem_key(uint32_t data, uint32_t w, uint32_t h) { return ((uint64_t)data << 24) | ((uint64_t)(w & 0xFFF) << 12) | (h & 0xFFF); }
std::unordered_map<uint64_t, HostRT> g_rts;
struct HostZ {
    IDirect3DTexture9 *tex = nullptr;  // INTZ, so the game can sample its depth buffer (when supported)
    IDirect3DSurface9 *surf = nullptr;
};
std::unordered_map<uint64_t, HostZ> g_zs;
std::unordered_map<uint64_t, IDirect3DTexture9 *> g_zviews; // depth buffers as the game reads them as colour textures
IDirect3DPixelShader9 *g_zview_ps, *g_zfill_ps;
// Which interpretation last wrote a piece of memory. BLACK renders downsampled scene depth as colour into a
// surface and then binds the same memory as the depth buffer of its low-resolution passes.
enum class Owner : uint8_t { None, Color, Depth };
std::unordered_map<uint64_t, Owner> g_owner;
IDirect3DTexture9 *g_cur_rt; // host texture currently bound as render target
std::unordered_map<uint64_t, IDirect3DTexture9 *> g_snapshots; // copies for draws that sample their own target

template <typename T> T &dev_field(uint32_t off) { return *(T *)(g_dev + off); }

// NTSC SDTV modes (standard AV pack). Flags: 0x20 interlaced, 0x100 10x11 pixel aspect ratio. RenderWare's
// driver insists on an exact match of size, bit depth and these flags with what the game asks for
// (640x480, 0x120).
const DisplayMode kModes[] = {
    {640, 480, 60, 0x120, XFMT_LIN_A8R8G8B8},
    {640, 480, 60, 0x120, XFMT_LIN_X8R8G8B8},
    {640, 480, 60, 0x120, XFMT_LIN_R5G6B5},
    {640, 480, 60, 0x120, XFMT_LIN_X1R5G5B5},
    {640, 480, 60, 0x020, XFMT_LIN_A8R8G8B8},
    {640, 480, 60, 0x020, XFMT_LIN_X8R8G8B8},
    {640, 480, 60, 0x020, XFMT_LIN_R5G6B5},
    {640, 480, 60, 0x020, XFMT_LIN_X1R5G5B5},
    {720, 480, 60, 0x020, XFMT_LIN_A8R8G8B8},
    {720, 480, 60, 0x020, XFMT_LIN_X8R8G8B8},
    {720, 480, 60, 0x020, XFMT_LIN_R5G6B5},
    {720, 480, 60, 0x020, XFMT_LIN_X1R5G5B5},
};

Surface *make_surface(uint32_t width, uint32_t height, uint32_t xfmt)
{
    const FormatInfo &fi = format_info(xfmt);
    uint32_t pitch = ((width * fi.bpp / 8) + 63) & ~63u;
    auto *s = (Surface *)xk::ExAllocatePoolWithTag(sizeof(Surface), 'fruS');
    memset(s, 0, sizeof *s);
    void *mem = xk::MmAllocateContiguousMemoryEx(pitch * height, 0, 0xFFFFFFFF, 0x1000, PAGE_READWRITE);
    s->Common = 1 | (RTYPE_SURFACE << COMMON_TYPE_SHIFT) | COMMON_D3DCREATED | COMMON_VIDEOMEMORY;
    s->Data = xk::MmGetPhysicalAddress(mem);
    s->Format = 1 | (xfmt << FMT_FORMAT_SHIFT) | (2u << FMT_DIMENSION_SHIFT) | (1u << FMT_MIPMAP_SHIFT);
    s->Size = (width - 1) | ((height - 1) << SIZE_HEIGHT_SHIFT) | ((pitch / 64 - 1) << SIZE_PITCH_SHIFT);
    return s;
}

void surface_size(const PixelContainer *s, uint32_t *w, uint32_t *h)
{
    uint32_t d, l;
    pixel_container_dims(s, w, h, &d, &l);
}

} // namespace

// Render resolution scale (--scale): host render targets and depth buffers are this many times the Xbox
// size; keys and everything the game sees stay in Xbox pixels. In widescreen the game draws a 16:9 picture
// into its 4:3 buffers (anamorphic), so the host targets are widened by 4/3 as well.
float g_scale_x, g_scale_y; // set at CreateDevice (setup_render_scale), changed at run time by change_render_scale
int g_native_w, g_native_h; // the picture size the native render size was made for
std::atomic<bool> g_render_request{false}; // the menu changed the render-size settings
std::atomic<bool> g_force_rebuild{false};  // re-create the host targets even at the same render size
D3DRECT g_scissor;          // the game's scissor rectangle (Xbox pixels) and whether it is on, to re-apply after a rescale
bool g_scissor_on;

// The render size: --resolution (WxH, H, native) or --scale. x and y are separate factors, so any size works;
// in widescreen a picture with square pixels has x = 4/3 y. Kept within Direct3D 9's 8192 texture limit.
void setup_render_scale(int pic_w, int pic_h)
{
    float aspect = !g_cfg.widescreen ? 1.0f : (g_cfg.ultrawide ? 7.0f / 4.0f : 4.0f / 3.0f);
    float w = (float)g_width, h = (float)g_height;
    float sx = g_cfg.scale * aspect, sy = g_cfg.scale;
    if (g_cfg.render_native && pic_w > 0 && pic_h > 0) {
        sx = pic_w * g_cfg.render_factor / w;
        sy = pic_h * g_cfg.render_factor / h;
    } else if (g_cfg.render_h > 0) {
        sy = g_cfg.render_h / h;
        sx = g_cfg.render_w > 0 ? g_cfg.render_w / w : sy * aspect;
    }
    g_scale_x = std::clamp(sx, 0.5f, 8192.0f / w);
    g_scale_y = std::clamp(sy, 0.5f, 8192.0f / h);
}

float scale_x()
{
    if (!g_scale_x) setup_render_scale(0, 0);
    return g_scale_x;
}
float scale_y()
{
    if (!g_scale_y) setup_render_scale(0, 0);
    return g_scale_y;
}
uint32_t host_w(uint32_t xbox_w) { return (uint32_t)lroundf(xbox_w * scale_x()); }
uint32_t host_h(uint32_t xbox_h) { return (uint32_t)lroundf(xbox_h * scale_y()); }

// The game's small post-processing buffers (glow, light cones, blur: 320x240, 160x120, 80x60, 64x32): targets of
// at most half the back buffer in both directions. Its blur samples them with offsets in their own texels, so at
// 3-4x the size the taps skipped texels and showed a faint grid. By default they stay at the render scale and
// are box-filtered over one Xbox texel before a linear-filtered draw samples them (smoothed_rt); with "Effects at
// original resolution" (g_cfg.effects_native) they keep their Xbox size instead (softer, blockier, like the Xbox).
bool small_buffer(uint32_t xw, uint32_t xh) { return xw * 2 <= (uint32_t)g_width && xh * 2 <= (uint32_t)g_height; }
bool native_size(uint32_t xw, uint32_t xh) { return g_cfg.effects_native && small_buffer(xw, xh); }

// The scale of the bound render target (viewport, scissor, clear rectangles, pre-transformed vertices).
float g_cur_sx, g_cur_sy;
float target_scale_x() { return g_cur_sx ? g_cur_sx : scale_x(); }
float target_scale_y() { return g_cur_sy ? g_cur_sy : scale_y(); }

namespace {

// An Xbox pixel rectangle on the bound host render target (edges rounded, so adjacent rectangles still meet).
RECT host_rect(LONG x1, LONG y1, LONG x2, LONG y2)
{
    float sx = target_scale_x(), sy = target_scale_y();
    return {lroundf(x1 * sx), lroundf(y1 * sy), lroundf(x2 * sx), lroundf(y2 * sy)};
}

HostRT &host_rt(const PixelContainer *s)
{
    uint32_t w, h;
    surface_size(s, &w, &h);
    HostRT &rt = g_rts[mem_key(s->Data, w, h)];
    if (!rt.tex) {
        rt.xw = w;
        rt.xh = h;
        bool native = native_size(w, h);
        rt.sx = native ? 1.0f : scale_x();
        rt.sy = native ? 1.0f : scale_y();
        w = native ? w : host_w(w);
        h = native ? h : host_h(h);
        host3d::dev()->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt.tex, nullptr);
        rt.w = w;
        rt.h = h;
        IDirect3DSurface9 *surf = nullptr;
        if (rt.tex && SUCCEEDED(rt.tex->GetSurfaceLevel(0, &surf))) {
            host3d::dev()->ColorFill(surf, nullptr, 0);
            surf->Release();
        }
        logf("[d3d] render target texture %ux%u for surface data 0x%08x", w, h, s->Data);
    }
    return rt;
}

// Depth surface for an Xbox depth buffer; at least as large as the render target (a D3D9 rule).
// rt_w/rt_h and the key are in Xbox pixels; the surface is created at the render scale.
HostZ &host_z(const PixelContainer *z, uint32_t rt_w, uint32_t rt_h, uint64_t *key)
{
    uint32_t w, h;
    surface_size(z, &w, &h);
    if (w < rt_w) w = rt_w;
    if (h < rt_h) h = rt_h;
    *key = mem_key(z->Data, w, h);
    HostZ &hz = g_zs[*key];
    if (!native_size(w, h)) { // the same size as the render targets it pairs with
        w = host_w(w);
        h = host_h(h);
    }
    if (!hz.surf) {
        if (host3d::intz() &&
            SUCCEEDED(host3d::dev()->CreateTexture(w, h, 1, D3DUSAGE_DEPTHSTENCIL, host3d::FMT_INTZ, D3DPOOL_DEFAULT, &hz.tex, nullptr)))
            hz.tex->GetSurfaceLevel(0, &hz.surf);
        else
            host3d::dev()->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &hz.surf, nullptr);
        logf("[d3d] depth surface %ux%u for surface data 0x%08x%s", w, h, z->Data, hz.tex ? " (INTZ)" : "");
    }
    return hz;
}

IDirect3DPixelShader9 *compile_ps(const char *src, const char *name, const char *profile = "ps_2_0")
{
    IDirect3DPixelShader9 *ps = nullptr;
    ID3DBlob *code = nullptr, *errors = nullptr;
    if (SUCCEEDED(D3DCompile(src, strlen(src), name, nullptr, nullptr, "main", profile, 0, 0, &code, &errors)))
        host3d::dev()->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps);
    else
        logf("[d3d] %s shader: %s", name, errors ? (const char *)errors->GetBufferPointer() : "?");
    if (code) code->Release();
    if (errors) errors->Release();
    return ps;
}

// Draw a full-screen quad with `ps` sampling `src` into (color, depth); restores the game's device state.
// w, h: the size of `color` (the quad covers it, or only `dst` when given); linear: filter `src` (for
// scaling), else point-sample.
void fullscreen_pass(IDirect3DPixelShader9 *ps, IDirect3DBaseTexture9 *src, IDirect3DSurface9 *color, IDirect3DSurface9 *depth,
                     uint32_t w, uint32_t h, bool write_depth, const float *c0 = nullptr, bool linear = false,
                     const RECT *dst = nullptr, IDirect3DBaseTexture9 *src2 = nullptr)
{
    IDirect3DDevice9 *d = host3d::dev();
    IDirect3DStateBlock9 *sb = nullptr;
    d->CreateStateBlock(D3DSBT_ALL, &sb);
    IDirect3DSurface9 *old_rt = nullptr, *old_z = nullptr;
    d->GetRenderTarget(0, &old_rt);
    d->GetDepthStencilSurface(&old_z);
    d->SetRenderTarget(0, color);
    d->SetDepthStencilSurface(depth);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(ps);
    if (c0) d->SetPixelShaderConstantF(0, c0, 1);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    d->SetTexture(0, src);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    if (src2) { // a lookup texture: filtered between entries, clamped
        d->SetTexture(1, src2);
        d->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    for (D3DRENDERSTATETYPE rs : {D3DRS_ALPHABLENDENABLE, D3DRS_ALPHATESTENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_SCISSORTESTENABLE})
        d->SetRenderState(rs, FALSE);
    d->SetRenderState(D3DRS_ZENABLE, write_depth);
    d->SetRenderState(D3DRS_ZWRITEENABLE, write_depth);
    d->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, write_depth ? 0 : 0xF);
    RECT r = dst ? *dst : RECT{0, 0, (LONG)w, (LONG)h};
    float x0 = r.left - 0.5f, y0 = r.top - 0.5f, x1 = r.right - 0.5f, y1 = r.bottom - 0.5f;
    struct V {
        float x, y, z, rhw, u, v;
    } q[4] = {{x0, y0, 0, 1, 0, 0}, {x1, y0, 0, 1, 1, 0}, {x0, y1, 0, 1, 0, 1}, {x1, y1, 0, 1, 1, 1}};
    host3d::begin_scene();
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
    d->SetRenderTarget(0, old_rt);
    d->SetDepthStencilSurface(old_z);
    if (sb) {
        sb->Apply(); // viewport, shaders, textures and states as the game left them
        sb->Release();
    }
    forget_sent_shaders();
    if (old_rt) old_rt->Release();
    if (old_z) old_z->Release();
}

IDirect3DTexture9 *scratch_rt(uint32_t w, uint32_t h);

// The inverse of the depth view: colour bytes written by the game become depth values (Z = A:R:G).
const char kZFillPS[] = "sampler2D c : register(s0);\n"
                        "float4 main(float2 uv : TEXCOORD0, out float depth : DEPTH) : COLOR0 {\n"
                        "  float4 t = floor(tex2D(c, uv) * 255.0 + 0.5);\n"
                        "  depth = (t.a * 65536.0 + t.r * 256.0 + t.g) / 16777215.0;\n"
                        "  return 0;\n"
                        "}\n";

void color_to_depth(IDirect3DTexture9 *color, HostZ &hz, uint32_t w, uint32_t h)
{
    if (!g_zfill_ps) g_zfill_ps = compile_ps(kZFillPS, "zfill");
    IDirect3DTexture9 *dummy = scratch_rt(w, h);
    if (!g_zfill_ps || !dummy) return;
    IDirect3DSurface9 *ds = nullptr;
    dummy->GetSurfaceLevel(0, &ds);
    fullscreen_pass(g_zfill_ps, color, ds, hz.surf, w, h, true);
    ds->Release();
}

// The game reads a Z24S8 depth buffer through an A8R8G8B8 texture header over the same memory: each texel
// is the dword (Z << 8) | stencil, so A = Z[23:16], R = Z[15:8], G = Z[7:0], B = stencil. Rebuild those bytes
// from the INTZ depth (BLACK's depth-of-field and reload blur masks come from this). The min() matters: at
// depth 1.0, 16777215 + 0.5 is not a float and rounds to 16777216, which turned the cleared sky into 0xFF0000
// (0.9961, nearer than distant walls) and cut the far light cones, which are depth-tested against a copy.
const char kZViewPS[] = "sampler2D z : register(s0);\n"
                        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
                        "  float v = min(floor(saturate(tex2D(z, uv).r) * 16777215.0 + 0.5), 16777215.0);\n"
                        "  float hi = floor(v / 65536.0), rest = v - hi * 65536.0, mid = floor(rest / 256.0), lo = rest - mid * 256.0;\n"
                        "  return float4(mid, lo, 0, hi) / 255.0;\n"
                        "}\n";

IDirect3DTexture9 *depth_view(uint64_t key, const HostZ &z, uint32_t w, uint32_t h) // w, h: Xbox pixels
{
    if (!native_size(w, h)) { // the size of the depth surface
        w = host_w(w);
        h = host_h(h);
    }
    if (!g_zview_ps) g_zview_ps = compile_ps(kZViewPS, "zview");
    IDirect3DTexture9 *&view = g_zviews[key];
    if (!view) host3d::dev()->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &view, nullptr);
    if (!g_zview_ps || !view) return nullptr;
    IDirect3DSurface9 *target = nullptr;
    view->GetSurfaceLevel(0, &target);
    fullscreen_pass(g_zview_ps, z.tex, target, nullptr, w, h, false);
    target->Release();
    return view;
}

// Copy the game's back buffer to the window through the game's gamma ramp, which the Xbox applied in the
// video DAC: BLACK's ramp builder (FUN_001762B0) computes ramp[i] = (i/255)^1.2 * (brightness + 1) * 2.2 *
// tint * fade (the menu brightness, a colour tint and the fade factor, FUN_0015A8A0 = setFade), so the ramp
// carries the screen fades and doubles the framebuffer brightness at the default setting. Then the user's
// gain and gamma (F5-F8).
const char kGammaPS[] = "sampler2D f : register(s0);\n"
                        "sampler2D ramp : register(s1);\n" // 256x1: R/G/B = the red/green/blue ramps
                        "float4 g : register(c0);\n"         // x = 1/gamma, y = gain
                        "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
                        "  float3 i = saturate(tex2D(f, uv).rgb) * (255.0 / 256.0) + 0.5 / 256.0;\n"
                        "  float3 c = float3(tex2D(ramp, float2(i.r, 0.5)).r, tex2D(ramp, float2(i.g, 0.5)).g, tex2D(ramp, float2(i.b, 0.5)).b);\n"
                        "  return float4(pow(saturate(c * g.y), g.xxx), 1);\n"
                        "}\n";
IDirect3DPixelShader9 *g_gamma_ps;
IDirect3DTexture9 *g_ramp_tex;

// Anti-aliasing: an edge-directed post filter on the final picture (after the gamma ramp, at render
// resolution, before the scale to the window), written for this project after the published FXAA idea.
// MSAA does not fit this game: its depth-of-field and blur passes read the depth buffer back as a texture
// (not possible for multisampled depth in Direct3D 9) and it re-reads its own render targets.
// Per pixel: luma contrast against the 4 neighbours decides whether there is an edge; the 3x3 second
// derivatives pick horizontal or vertical; the edge is walked both ways (fixed steps, branch-free for
// ps_2_a) to where the luma leaves the edge's average; the pixel is then sampled shifted across the edge in
// proportion to its position along it (plus a sub-pixel term for isolated pixels).
const char kFxaaPS[] =
    "sampler2D s : register(s0);\n"
    "float4 rcp : register(c0);\n" // x = 1/width, y = 1/height
    "float L(float2 uv) { return dot(tex2D(s, uv).rgb, float3(0.299, 0.587, 0.114)); }\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
    "  float3 cM = tex2D(s, uv).rgb;\n"
    "  float lM = dot(cM, float3(0.299, 0.587, 0.114));\n"
    "  float lN = L(uv + float2(0, -rcp.y)), lS = L(uv + float2(0, rcp.y));\n"
    "  float lW = L(uv + float2(-rcp.x, 0)), lE = L(uv + float2(rcp.x, 0));\n"
    "  float lMin = min(lM, min(min(lN, lS), min(lW, lE))), lMax = max(lM, max(max(lN, lS), max(lW, lE)));\n"
    "  float range = lMax - lMin;\n"
    "  float lNW = L(uv + float2(-rcp.x, -rcp.y)), lNE = L(uv + float2(rcp.x, -rcp.y));\n"
    "  float lSW = L(uv + float2(-rcp.x, rcp.y)), lSE = L(uv + float2(rcp.x, rcp.y));\n"
    "  float edgeH = abs(lNW + lSW - 2 * lW) + 2 * abs(lN + lS - 2 * lM) + abs(lNE + lSE - 2 * lE);\n"
    "  float edgeV = abs(lNW + lNE - 2 * lN) + 2 * abs(lW + lE - 2 * lM) + abs(lSW + lSE - 2 * lS);\n"
    "  bool horz = edgeH >= edgeV;\n"
    "  float l1 = horz ? lN : lW, l2 = horz ? lS : lE;\n"
    "  float g1 = l1 - lM, g2 = l2 - lM;\n"
    "  bool steep1 = abs(g1) >= abs(g2);\n"
    "  float gScaled = 0.25 * max(abs(g1), abs(g2));\n"
    "  float stepLen = horz ? rcp.y : rcp.x;\n"
    "  stepLen = steep1 ? -stepLen : stepLen;\n"
    "  float lAvg = 0.5 * ((steep1 ? l1 : l2) + lM);\n"
    "  float2 cuv = uv + (horz ? float2(0, stepLen * 0.5) : float2(stepLen * 0.5, 0));\n"
    "  float2 off = horz ? float2(rcp.x, 0) : float2(0, rcp.y);\n"
    "  float2 uv1 = cuv - off, uv2 = cuv + off;\n"
    "  float e1 = L(uv1) - lAvg, e2 = L(uv2) - lAvg;\n"
    "  bool done1 = abs(e1) >= gScaled, done2 = abs(e2) >= gScaled;\n"
    "  const float Q[7] = {1, 1, 1.5, 2, 2, 4, 8};\n"
    "  [unroll] for (int i = 0; i < 7; i++) {\n"
    "    float2 n1 = uv1 - off * Q[i], n2 = uv2 + off * Q[i];\n"
    "    float t1 = L(n1) - lAvg, t2 = L(n2) - lAvg;\n"
    "    uv1 = done1 ? uv1 : n1; e1 = done1 ? e1 : t1; done1 = done1 || abs(e1) >= gScaled;\n"
    "    uv2 = done2 ? uv2 : n2; e2 = done2 ? e2 : t2; done2 = done2 || abs(e2) >= gScaled;\n"
    "  }\n"
    "  float d1 = horz ? uv.x - uv1.x : uv.y - uv1.y, d2 = horz ? uv2.x - uv.x : uv2.y - uv.y;\n"
    "  bool dir1 = d1 < d2;\n"
    "  float pixOff = 0.5 - min(d1, d2) / (d1 + d2);\n"
    "  bool correct = ((dir1 ? e1 : e2) < 0.0) != (lM < lAvg);\n"
    "  float fOff = correct ? pixOff : 0.0;\n"
    "  float lA = (2 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) / 12.0;\n"
    "  float sp = saturate(abs(lA - lM) / max(range, 1e-4));\n"
    "  sp = (-2 * sp + 3) * sp * sp;\n"
    "  fOff = max(fOff, sp * sp * 0.75);\n"
    "  float2 fuv = uv + (horz ? float2(0, fOff * stepLen) : float2(fOff * stepLen, 0));\n"
    "  float3 c = tex2D(s, fuv).rgb;\n"
    "  return float4(range < max(0.0312, lMax * 0.125) ? cM : c, 1);\n" // no edge: leave the pixel alone
    "}\n";
IDirect3DPixelShader9 *g_fxaa_ps;
IDirect3DTexture9 *g_aa_tex[3]; // render-resolution intermediates: after the ramp, after the filter, seams filled

IDirect3DTexture9 *aa_target(int i, uint32_t w, uint32_t h)
{
    if (!g_aa_tex[i])
        host3d::dev()->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_aa_tex[i], nullptr);
    return g_aa_tex[i];
}

// Seam fill: destructible walls are built from chunks with their own matrices; where they meet, hairline gaps
// (sub-pixel at the Xbox's 640x480, so rarely visible there) show the background along the whole seam at 3-4x
// the size, flickering as the view moves. A pixel whose depth is well behind both of its neighbours 1 or 2
// pixels away (horizontally or vertically) while those two lie at nearly the same depth (one surface) is
// such a gap: it takes their average colour. Silhouettes do not match (their two sides differ in depth).
// u = 1 - depth grows with nearness (about 1/distance with the game's infinite far plane).
const char kSeamPS[] = "sampler2D col : register(s0);\n"
                       "sampler2D dep : register(s1);\n"
                       "float4 px : register(c0);\n"
                       "float u(float2 t) { return 1.0 - tex2D(dep, t).r; }\n"
                       "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
                       "  float4 c = tex2D(col, uv);\n"
                       "  float uc = u(uv);\n"
                       "  float4 sum = 0; float n = 0;\n"
                       "  float2 offs[4] = { float2(px.x, 0), float2(0, px.y), float2(2 * px.x, 0), float2(0, 2 * px.y) };\n"
                       "  for (int i = 0; i < 4; i++) {\n"
                       "    float ua = u(uv - offs[i]), ub = u(uv + offs[i]);\n"
                       "    float lo = min(ua, ub), hi = max(ua, ub);\n"
                       "    float w = step(hi - lo, 0.1 * hi) * step(uc, 0.75 * lo) * step(1e-6, lo) * step(n, 0.5);\n"
                       "    sum += w * (tex2D(col, uv - offs[i]) + tex2D(col, uv + offs[i]));\n"
                       "    n += 2 * w;\n"
                       "  }\n"
                       "  return n > 0 ? sum / n : c;\n"
                       "}\n";
IDirect3DPixelShader9 *g_seam_ps;
Surface *g_main_z; // the depth surface the game uses with its back buffer

// The ramp texture from the game's current gamma ramp (identity until the game has written one).
IDirect3DTexture9 *ramp_texture()
{
    static uint8_t shown[768];
    static bool valid, made;
    if (!g_ramp_tex && FAILED(host3d::dev()->CreateTexture(256, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_ramp_tex, nullptr)))
        return nullptr;
    uint8_t ramp[768];
    if (!valid && g_cfg.ramp)
        valid = dev_field<uint32_t>(DEV_GAMMA_DIRTY) || dev_field<uint32_t>(DEV_GAMMA_DIRTY + 4);
    if (valid) {
        uint32_t idx = dev_field<uint32_t>(DEV_GAMMA_INDEX) & 1;
        memcpy(ramp, g_dev + DEV_GAMMA_RAMP + idx * 0x300, sizeof ramp);
    } else {
        for (int i = 0; i < 256; i++) ramp[i] = ramp[256 + i] = ramp[512 + i] = (uint8_t)i;
    }
    if (made && !memcmp(ramp, shown, sizeof ramp)) return g_ramp_tex;
    D3DLOCKED_RECT lr;
    if (FAILED(g_ramp_tex->LockRect(0, &lr, nullptr, 0))) return nullptr;
    auto *px = (uint32_t *)lr.pBits;
    for (int i = 0; i < 256; i++) px[i] = 0xFF000000u | ramp[i] << 16 | ramp[256 + i] << 8 | ramp[512 + i];
    g_ramp_tex->UnlockRect(0);
    if (!made || ramp[255] != shown[255] || ramp[127] != shown[127]) {
        static int logged; // the first changes (the boot fades) show the ramp works; then quiet
        if (logged++ < 8)
            logf("[d3d] gamma ramp%s: 64 -> %u, 127 -> %u, 255 -> %u (R), frame %u", valid ? "" : " (identity, none from the game yet)",
                 ramp[64], ramp[127], ramp[255], g_frame_count.load());
    }
    memcpy(shown, ramp, sizeof ramp);
    made = true;
    return g_ramp_tex;
}

void present_backbuffer()
{
    static float shown_gamma = -1.0f, shown_gain = -1.0f, shown_sens = -1.0f;
    static int shown_aa = -1;
    if (shown_gamma != g_cfg.gamma || shown_gain != g_cfg.gain || shown_sens != g_cfg.mouse_sens || shown_aa != (int)g_cfg.aa) {
        if (shown_aa < 0 && g_cfg.background) host3d::set_title("[AUTOMATED TEST, MUTED] BLACK");
        shown_gamma = g_cfg.gamma;
        shown_gain = g_cfg.gain;
        shown_sens = g_cfg.mouse_sens;
        shown_aa = g_cfg.aa;
        logf("[d3d] mouse sensitivity %.2f, output gain %.1f, gamma %.1f, anti-aliasing %s", g_cfg.mouse_sens, g_cfg.gain,
             g_cfg.gamma, g_cfg.aa ? "on" : "off");
    }
    HostRT &bb = host_rt(g_backbuffer);
    IDirect3DSurface9 *s = nullptr;
    if (FAILED(bb.tex->GetSurfaceLevel(0, &s))) return;
    host3d::end_scene();
    if (!g_gamma_ps) g_gamma_ps = compile_ps(kGammaPS, "gamma");
    IDirect3DTexture9 *ramp = ramp_texture();
    // The frame (Xbox size x render scale) is filtered into the window-sized part of the host back buffer,
    // at the game's aspect with black bars when the window's shape differs (fullscreen, maximised).
    RECT frame, pic;
    host3d::frame_layout(&frame, &pic);
    if (pic.left > 0 || pic.top > 0 || pic.right < frame.right || pic.bottom < frame.bottom)
        host3d::dev()->ColorFill(g_host_backbuffer, &frame, 0);
    if (g_cfg.aa && !g_fxaa_ps) {
        g_fxaa_ps = compile_ps(kFxaaPS, "fxaa", "ps_2_a");
        if (!g_fxaa_ps) g_cfg.aa = false;
    }
    IDirect3DTexture9 *src = bb.tex; // the frame, with seams filled when that is on
    if (g_cfg.seam_fill && g_main_z) {
        static bool tried;
        if (!g_seam_ps && !tried) {
            tried = true;
            g_seam_ps = compile_ps(kSeamPS, "seamfill", "ps_2_a");
            if (!g_seam_ps) logf("[d3d] seam fill shader not available");
        }
        uint64_t zkey;
        HostZ &hz = host_z(g_main_z, bb.xw, bb.xh, &zkey);
        IDirect3DTexture9 *out = g_seam_ps && hz.tex ? aa_target(2, bb.w, bb.h) : nullptr;
        if (out) {
            IDirect3DSurface9 *so = nullptr;
            out->GetSurfaceLevel(0, &so);
            float px[4] = {1.0f / bb.w, 1.0f / bb.h, 0, 0};
            fullscreen_pass(g_seam_ps, bb.tex, so, nullptr, bb.w, bb.h, false, px, false, nullptr, hz.tex);
            so->Release();
            src = out;
        }
    }
    IDirect3DTexture9 *aa0 = g_cfg.aa ? aa_target(0, bb.w, bb.h) : nullptr, *aa1 = g_cfg.aa ? aa_target(1, bb.w, bb.h) : nullptr;
    if (g_gamma_ps && ramp && aa0 && aa1) {
        // ramp + gamma 1:1 into aa0, anti-alias 1:1 into aa1, then filter into the window
        float c[4] = {1.0f / g_cfg.gamma, g_cfg.gain, 0, 0};
        IDirect3DSurface9 *s0 = nullptr, *s1 = nullptr;
        aa0->GetSurfaceLevel(0, &s0);
        aa1->GetSurfaceLevel(0, &s1);
        fullscreen_pass(g_gamma_ps, src, s0, nullptr, bb.w, bb.h, false, c, false, nullptr, ramp);
        float r[4] = {1.0f / bb.w, 1.0f / bb.h, 0, 0};
        fullscreen_pass(g_fxaa_ps, aa0, s1, nullptr, bb.w, bb.h, false, r, true);
        host3d::dev()->StretchRect(s1, nullptr, g_host_backbuffer, &pic, D3DTEXF_LINEAR);
        s0->Release();
        s1->Release();
    } else if (g_gamma_ps && ramp) {
        float c[4] = {1.0f / g_cfg.gamma, g_cfg.gain, 0, 0};
        fullscreen_pass(g_gamma_ps, src, g_host_backbuffer, nullptr, (uint32_t)frame.right, (uint32_t)frame.bottom, false, c,
                        true, &pic, ramp);
    } else {
        host3d::dev()->StretchRect(s, nullptr, g_host_backbuffer, &pic, D3DTEXF_LINEAR);
    }
    s->Release();
}

// A colour target that is never sampled (for passes that only write depth).
IDirect3DTexture9 *scratch_rt(uint32_t w, uint32_t h)
{
    IDirect3DTexture9 *&t = g_snapshots[mem_key(1, w, h)];
    if (!t) host3d::dev()->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t, nullptr);
    return t;
}

void bind_render_target(Surface *rt, Surface *z)
{
    IDirect3DDevice9 *d = host3d::dev();
    HostRT &h = host_rt(rt);
    uint32_t rw, rh;
    surface_size(rt, &rw, &rh);
    IDirect3DSurface9 *zs = nullptr;
    if (z) {
        if (rt == g_backbuffer) g_main_z = z;
        uint64_t zkey;
        HostZ &hz = host_z(z, h.xw, h.xh, &zkey);
        uint32_t zw, zh;
        surface_size(z, &zw, &zh);
        if (native_size(std::max(zw, h.xw), std::max(zh, h.xh)) != native_size(h.xw, h.xh))
            KNOTE_ONCE("render target %ux%u and depth %ux%u at different scales: depth test misaligned", h.xw, h.xh, zw, zh);
        auto c = g_rts.find(zkey);
        if (g_owner[zkey] == Owner::Color && c != g_rts.end() && hz.tex) {
            color_to_depth(c->second.tex, hz, c->second.w, c->second.h); // memory last written as colour
            static int logged;
            if (logged++ < 10) logf("[d3d] depth buffer 0x%08x was written as colour: converted (%ux%u)", z->Data, c->second.w, c->second.h);
        }
        g_owner[zkey] = Owner::Depth;
        zs = hz.surf;
    }
    g_owner[mem_key(rt->Data, rw, rh)] = Owner::Color;
    h.epoch++;
    IDirect3DSurface9 *s = nullptr;
    h.tex->GetSurfaceLevel(0, &s);
    d->SetRenderTarget(0, s);
    s->Release();
    g_cur_rt = h.tex;
    d->SetDepthStencilSurface(zs);
    if (g_cur_sx != h.sx || g_cur_sy != h.sy) { // a target of another scale: the scissor follows (SetRenderTarget
        g_cur_sx = h.sx;                         // re-applies the viewport itself)
        g_cur_sy = h.sy;
        if (g_scissor_on) {
            RECT r = host_rect(g_scissor.x1, g_scissor.y1, g_scissor.x2, g_scissor.y2);
            d->SetScissorRect(&r);
        }
    }
}

// Copy a render target texture (outside the scene) into a same-size scratch texture.
IDirect3DTexture9 *snapshot_of(IDirect3DTexture9 *src)
{
    perf::Scope perf_scope(perf::kRtCopy);
    perf::add_rt_copy();
    D3DSURFACE_DESC desc;
    src->GetLevelDesc(0, &desc);
    IDirect3DTexture9 *&snap = g_snapshots[mem_key(0, desc.Width, desc.Height)];
    if (!snap) host3d::dev()->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &snap, nullptr);
    if (!snap) return src;
    IDirect3DSurface9 *a = nullptr, *b = nullptr;
    src->GetSurfaceLevel(0, &a);
    snap->GetSurfaceLevel(0, &b);
    host3d::end_scene();
    host3d::dev()->StretchRect(a, nullptr, b, nullptr, D3DTEXF_NONE);
    a->Release();
    b->Release();
    return snap;
}

// The game's viewport clamped to the current render target: what the host device uses. (The Xbox keeps
// the viewport across SetRenderTarget, and BLACK draws into 160x120 targets with a 640x480 viewport.)
D3DVIEWPORT9 effective_viewport()
{
    uint32_t rw = (uint32_t)g_width, rh = (uint32_t)g_height;
    if (g_st.render_target && g_st.render_target != g_backbuffer) surface_size(g_st.render_target, &rw, &rh);
    D3DVIEWPORT9 vp;
    vp.X = g_st.viewport.X < rw ? g_st.viewport.X : 0;
    vp.Y = g_st.viewport.Y < rh ? g_st.viewport.Y : 0;
    vp.Width = g_st.viewport.Width && vp.X + g_st.viewport.Width <= rw ? g_st.viewport.Width : rw - vp.X;
    vp.Height = g_st.viewport.Height && vp.Y + g_st.viewport.Height <= rh ? g_st.viewport.Height : rh - vp.Y;
    vp.MinZ = g_st.viewport.MinZ;
    vp.MaxZ = g_st.viewport.MaxZ;
    return vp;
}

void apply_viewport()
{
    D3DVIEWPORT9 vp = effective_viewport(); // Xbox pixels -> host render targets are scale_x/y() times larger
    RECT r = host_rect(vp.X, vp.Y, vp.X + vp.Width, vp.Y + vp.Height);
    vp.X = r.left;
    vp.Y = r.top;
    vp.Width = r.right - r.left;
    vp.Height = r.bottom - r.top;
    host3d::dev()->SetViewport(&vp);
}

// Vertical blank emulation (60 Hz). On the Xbox the GPU interrupt signals the device's vblank KEVENT
// (+0x1DBC, waited on by D3DDevice_BlockUntilVerticalBlank and game threads) and calls the callback set
// with SetVerticalBlankCallback (+0x1DB8) with a D3DVBLANKDATA {VBlank, Swap, Flags}.
std::atomic<uint32_t> g_vblank_count, g_swap_count;
HANDLE g_vblank_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr); // wakes pace_swap at each vblank

// The emulated vblank follows the display's when the display refreshes at about 60 Hz: DwmFlush returns after
// the desktop composition that shows the last Present, and composition runs at the display's vblank. A
// free-running 59.94 Hz timer drifts against the display (59.95 Hz on the user's), and while the phase sat
// near the composition deadline, frames were shown a refresh late or skipped: stutter now and then with no
// slow frames in the performance recordings. When DwmFlush returns early (nothing presented yet, window
// minimised) the tick falls back to the timer. Other refresh rates, --background and --timer-vblank use the
// timer only.
using DwmFlushFn = HRESULT(WINAPI *)();
DwmFlushFn display_sync()
{
    if (g_cfg.timer_vblank || (g_cfg.background && !g_cfg.display_vblank)) return nullptr;
    DEVMODEW dm = {};
    dm.dmSize = sizeof dm;
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) || dm.dmDisplayFrequency < 59 || dm.dmDisplayFrequency > 61) return nullptr;
    HMODULE m = LoadLibraryW(L"dwmapi.dll");
    return m ? (DwmFlushFn)GetProcAddress(m, "DwmFlush") : nullptr;
}

void __stdcall vblank_thread(void *)
{
    xthread_current()->base_priority = 15;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    timeBeginPeriod(1);
    LARGE_INTEGER f, last, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&last);
    const int64_t period = f.QuadPart * 1001 / 60000; // 59.94 Hz
    DwmFlushFn flush = display_sync();
    logf("[d3d] vblank: %s", flush ? "follows the display (DwmFlush at ~60 Hz)" : "59.94 Hz timer");
    uint32_t from_display = 0;
    LARGE_INTEGER minute = last;
    for (;;) {
        bool synced = false;
        if (flush && SUCCEEDED(flush())) {
            QueryPerformanceCounter(&now);
            synced = now.QuadPart - last.QuadPart > period / 2;
        }
        if (!synced) {
            int64_t due = last.QuadPart + period;
            for (;;) {
                QueryPerformanceCounter(&now);
                int64_t left_ms = (due - now.QuadPart) * 1000 / f.QuadPart;
                if (left_ms <= 0) break;
                Sleep(left_ms > 2 ? (DWORD)(left_ms - 1) : 0);
            }
            // keep the timer's cadence (no drift from late wake-ups) unless it fell more than a frame behind
            now.QuadPart = now.QuadPart - due < period ? due : now.QuadPart;
        }
        last = now;
        uint32_t vb = ++g_vblank_count;
        SetEvent(g_vblank_wake);
        from_display += synced;
        if (flush && vb % 3600 == 0) {
            logf("[d3d] vblank: %u of the last 3600 from the display, %.3f Hz", from_display,
                 3600.0 * f.QuadPart / (double)(now.QuadPart - minute.QuadPart));
            from_display = 0;
            minute = now;
        }
        ksync_signal_object(g_dev + DEV_VBLANKEVENT);
        if (auto cb = *(void(__cdecl **)(uint32_t *))(g_dev + DEV_VBLANKCALLBACK)) {
            uint32_t data[3] = {vb, g_swap_count.load(), 0};
            uint8_t old = xthread_raise_irql(2);
            cb(data);
            xthread_lower_irql(old);
        }
    }
}

} // namespace

// Host render target texture for a texture the game samples (same memory and size), or null. A draw that
// samples the target it renders into (BLACK's final colour pass reads the back buffer) gets a copy; one
// copy per draw is shared by all stages.
// Four bilinear taps a quarter Xbox texel off the centre: an average over about one Xbox texel of a buffer
// rendered at the render scale (c0.xy = 0.25 / Xbox size, in texture coordinates).
const char kBoxPS[] = "sampler2D s : register(s0);\n"
                      "float4 o : register(c0);\n"
                      "float4 main(float2 uv : TEXCOORD0) : COLOR0 {\n"
                      "  return 0.25 * (tex2D(s, uv + o.xy) + tex2D(s, uv - o.xy) + tex2D(s, uv + float2(o.x, -o.y)) +\n"
                      "                 tex2D(s, uv + float2(-o.x, o.y)));\n"
                      "}\n";
IDirect3DPixelShader9 *g_box_ps;

IDirect3DTexture9 *smoothed_rt(HostRT &h, uint32_t draw_serial)
{
    bool bound = h.tex == g_cur_rt; // sampled while being drawn into: its contents change with every draw
    if (h.smooth && h.smooth_epoch == h.epoch && (!bound || h.smooth_serial == draw_serial)) return h.smooth;
    static bool tried;
    if (!g_box_ps && !tried) {
        tried = true;
        g_box_ps = compile_ps(kBoxPS, "box");
    }
    if (!g_box_ps) return h.tex;
    if (!h.smooth) host3d::dev()->CreateTexture(h.w, h.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &h.smooth, nullptr);
    if (!h.smooth) return h.tex;
    IDirect3DSurface9 *s = nullptr;
    h.smooth->GetSurfaceLevel(0, &s);
    float c[4] = {0.25f / h.xw, 0.25f / h.xh, 0, 0};
    host3d::end_scene();
    fullscreen_pass(g_box_ps, h.tex, s, nullptr, h.w, h.h, false, c, true);
    s->Release();
    h.smooth_epoch = h.epoch;
    h.smooth_serial = draw_serial;
    return h.smooth;
}

IDirect3DTexture9 *rt_texture_for(const PixelContainer *tex, uint32_t draw_serial, bool linear)
{
    uint32_t w, h;
    surface_size(tex, &w, &h);
    uint64_t key = mem_key(tex->Data, w, h);
    auto it = g_rts.find(key);
    auto zi = g_zs.find(key);
    bool as_depth = zi != g_zs.end() && zi->second.tex && (it == g_rts.end() || g_owner[key] == Owner::Depth);
    if (as_depth) { // a depth buffer read as a colour texture
        static uint32_t view_serial = ~0u;
        static uint64_t view_key;
        static IDirect3DTexture9 *view;
        if (view_serial != draw_serial || view_key != key) {
            view = depth_view(key, zi->second, w, h);
            view_serial = draw_serial;
            view_key = key;
        }
        return view;
    }
    if (it == g_rts.end()) return nullptr;
    IDirect3DTexture9 *t = it->second.tex;
    if (linear && it->second.sx > 1.5f && small_buffer(it->second.xw, it->second.xh)) return smoothed_rt(it->second, draw_serial);
    if (t != g_cur_rt) return t;
    static uint32_t snap_serial = ~0u;
    static IDirect3DTexture9 *snap_src, *snap;
    if (snap_serial != draw_serial || snap_src != t) {
        snap = snapshot_of(t);
        snap_src = t;
        snap_serial = draw_serial;
    }
    return snap;
}

// End of a --debug-frame frame: every host render target and depth view as PNG (colour and alpha), so the
// steps of a post-processing chain can be inspected.
void dump_render_targets(uint32_t frame)
{
    if (g_cfg.screenshot_dir.empty()) return;
    CreateDirectoryW(g_cfg.screenshot_dir.c_str(), nullptr);
    auto save = [&](const char *kind, uint64_t key, IDirect3DTexture9 *t) {
        if (!t) return;
        IDirect3DSurface9 *s = nullptr;
        if (FAILED(t->GetSurfaceLevel(0, &s))) return;
        char name[96];
        snprintf(name, sizeof name, "f%u_%s_%08x_%ux%u", frame, kind, (uint32_t)(key >> 24), (uint32_t)(key >> 12) & 0xFFF,
                 (uint32_t)key & 0xFFF);
        std::wstring base = g_cfg.screenshot_dir + L"\\" + widen(name);
        save_surface_png(s, (base + L".png").c_str(), false);
        save_surface_png(s, (base + L"_alpha.png").c_str(), true);
        s->Release();
    };
    for (auto &kv : g_rts) save("rt", kv.first, kv.second.tex);
    for (auto &kv : g_zviews) save("zview", kv.first, kv.second);
    logf("[d3d] frame %u: %zu render targets and %zu depth views saved", frame, g_rts.size(), g_zviews.size());
}

bool is_backbuffer(const Surface *s) { return s == g_backbuffer; }
uint32_t backbuffer_data() { return g_backbuffer ? g_backbuffer->Data : 0; }
uint32_t depthbuffer_data() { return g_depthbuffer ? g_depthbuffer->Data : 0; }

void hle_flush() { pb_flush(); }

// ---- Direct3D ----

uint32_t __stdcall Direct3D_GetAdapterModeCount(uint32_t Adapter)
{
    DTRACE("%u", Adapter);
    return (uint32_t)(sizeof kModes / sizeof kModes[0]);
}

HRESULT __stdcall Direct3D_EnumAdapterModes(uint32_t Adapter, uint32_t Mode, DisplayMode *pMode)
{
    DTRACE("%u, %u, %p", Adapter, Mode, pMode);
    if (Mode >= sizeof kModes / sizeof kModes[0]) return D3DERR_INVALIDCALL;
    *pMode = kModes[Mode];
    return D3D_OK;
}

// Game code reads the device's copies directly: transforms at +0xC60 + State*0x40 (VIEW, PROJECTION,
// TEXTURE0-3, WORLD0-3) and the clipped viewport {X, Y, W, H, MinZ, MaxZ} at +0xEE0 (FUN_0016f770
// builds its view-projection from them).
constexpr uint32_t DEV_TRANSFORMS = 0x0C60, DEV_VIEWPORT = 0x0EE0;

HRESULT __stdcall Direct3D_CreateDevice(uint32_t Adapter, uint32_t DeviceType, uint32_t hFocusWindow, uint32_t BehaviorFlags,
                                        PresentParameters *pp, uint8_t **ppDevice)
{
    logf("[d3d] Direct3D_CreateDevice(adapter %u, type %u, flags 0x%x): %ux%u fmt 0x%x x%u, ms 0x%x, swap %u, depth %u fmt 0x%x, "
         "flags 0x%x, rate %u, interval 0x%x",
         Adapter, DeviceType, BehaviorFlags, pp->BackBufferWidth, pp->BackBufferHeight, pp->BackBufferFormat, pp->BackBufferCount,
         pp->MultiSampleType, pp->SwapEffect, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat, pp->Flags,
         pp->FullScreen_RefreshRateInHz, pp->FullScreen_PresentationInterval);
    g_width = pp->BackBufferWidth ? pp->BackBufferWidth : 640;
    g_height = pp->BackBufferHeight ? pp->BackBufferHeight : 480;
    // The window and the host back buffer are the screen-sized output; the game renders into host render
    // targets at Xbox size x render scale, filtered into the back buffer at Swap.
    if (!host3d::open_window(g_cfg.window_w, g_cfg.window_h,
                             g_cfg.widescreen ? (g_cfg.ultrawide ? 21 : 16) : 4,
                             g_cfg.widescreen ? 9 : 3))
        return D3DERR_NOTAVAILABLE;
    if (!host3d::create_device()) return D3DERR_NOTAVAILABLE;
    RECT frame, pic; // the picture's size in the window (for --resolution native)
    host3d::frame_layout(&frame, &pic);
    g_native_w = pic.right - pic.left;
    g_native_h = pic.bottom - pic.top;
    setup_render_scale(g_native_w, g_native_h);
    logf("[d3d] rendering at %ux%u (x%.4g by x%.4g the Xbox's %dx%d, %s), picture in the window %ldx%ld", host_w(g_width),
         host_h(g_height), scale_x(), scale_y(), g_width, g_height, !g_cfg.widescreen ? "4:3" : (g_cfg.ultrawide ? "ultrawide 21:9" : "widescreen 16:9"),
         pic.right - pic.left, pic.bottom - pic.top);
    host3d::dev()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_host_backbuffer);
    host3d::dev()->GetDepthStencilSurface(&g_host_depth);

    // The library's device object is static (g_Device at 0x279B50, up to D3D_g_pDevice); game code reads
    // some of its fields by absolute address, so use that storage.
    g_dev = (uint8_t *)(uintptr_t)addr_device_object();
    g_st.vs_constants = (float (*)[4])(uintptr_t)xbe_va(0x0027C2A8);
    memset(g_dev, 0, DEVICE_SIZE);
    g_push_base = (uint32_t *)VirtualAlloc(nullptr, PUSH_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_push_dwords = PUSH_BYTES / 4;
    dev_field<uint32_t *>(DEV_PUT) = g_push_base;
    dev_field<uint32_t *>(DEV_THRESHOLD) = g_push_base + g_push_dwords - 0x4000; // keep 64 KB headroom
    dev_field<float>(0x096C) = 1.0f; // back buffer scale x (read by GetBackBufferScale, run natively)
    dev_field<float>(0x0970) = 1.0f; // back buffer scale y
    *(uint32_t *)(uintptr_t)addr_push_segment() = 0x2000;

    g_backbuffer = make_surface(g_width, g_height, pp->BackBufferFormat ? pp->BackBufferFormat : XFMT_LIN_X8R8G8B8);
    if (pp->EnableAutoDepthStencil)
        g_depthbuffer = make_surface(g_width, g_height, pp->AutoDepthStencilFormat ? pp->AutoDepthStencilFormat : XFMT_LIN_D24S8);
    dev_field<Surface *>(DEV_RENDERTARGET) = g_backbuffer;
    dev_field<Surface *>(DEV_ZBUFFER) = g_depthbuffer;
    g_st.render_target = g_backbuffer;
    g_st.depth_stencil = g_depthbuffer;
    bind_render_target(g_backbuffer, g_depthbuffer);
    g_st.viewport = {0, 0, (uint32_t)g_width, (uint32_t)g_height, 0.0f, 1.0f};
    g_requested_viewport = g_st.viewport;
    update_viewport_constants();
    memcpy(g_dev + DEV_VIEWPORT, &g_st.viewport, sizeof(Viewport));
    for (uint32_t i = 0; i < 10; i++) { // the library starts every transform as identity
        D3DMATRIX m = {};
        m._11 = m._22 = m._33 = m._44 = 1.0f;
        g_st.transforms[i] = m;
        memcpy(g_dev + DEV_TRANSFORMS + i * 0x40, &m, 0x40);
    }
    *(uint8_t **)(uintptr_t)addr_g_pdevice() = g_dev;
    if (ppDevice) *ppDevice = g_dev;
    init_default_states();
    render_state()[XRS_PRESENTATIONINTERVAL] = pp->FullScreen_PresentationInterval;
    pb_init(g_push_base);
    kthread_create(nullptr, nullptr, 0, 0, (uint32_t)(uintptr_t)&vblank_thread, 0, 0, false, "vblank");
    logf("[d3d] device object %p, push buffer %p (%u KB), back buffer surface %p, depth surface %p", g_dev, g_push_base,
         PUSH_BYTES / 1024, g_backbuffer, g_depthbuffer);
    return D3D_OK;
}

HRESULT __stdcall D3DDevice_GetDisplayMode(DisplayMode *pMode)
{
    DTRACE("%p", pMode);
    *pMode = kModes[0];
    pMode->Width = (uint32_t)g_width;
    pMode->Height = (uint32_t)g_height;
    return D3D_OK;
}

// D3DFIELD_STATUS {Field (1 odd, 2 even, 3 progressive), VBlankCount}: interlaced fields alternate.
void __stdcall D3DDevice_GetDisplayFieldStatus(uint32_t *pFieldStatus)
{
    DTRACE("%p", pFieldStatus);
    uint32_t vb = g_vblank_count.load();
    pFieldStatus[0] = (vb & 1) ? 1 : 2;
    pFieldStatus[1] = vb;
}

uint32_t __stdcall D3DDevice_Release()
{
    DTRACE("");
    return 1;
}

// ---- push buffer ----

uint32_t *__stdcall D3D_MakeRequestedSpace(uint32_t MinimumSpace, uint32_t RequestedSpace)
{
    DTRACE("0x%x, 0x%x", MinimumSpace, RequestedSpace);
    pb_flush();
    pb_reset();
    return dev_field<uint32_t *>(DEV_PUT);
}

uint32_t *__stdcall D3DDevice_BeginPush(uint32_t Count)
{
    DTRACE("%u", Count);
    uint32_t *put = dev_field<uint32_t *>(DEV_PUT);
    if (put + Count + 16 >= dev_field<uint32_t *>(DEV_THRESHOLD)) {
        pb_flush();
        pb_reset();
        put = dev_field<uint32_t *>(DEV_PUT);
    }
    return put;
}

void __stdcall D3DDevice_EndPush(uint32_t *pPush)
{
    DTRACE("%p", pPush);
    dev_field<uint32_t *>(DEV_PUT) = pPush;
    pb_flush();
}

void __stdcall D3DDevice_End()
{
    DTRACE("");
    pb_flush();
    pb_end_primitive();
}

// ---- frame ----

HRESULT __stdcall D3DDevice_Clear(uint32_t Count, const D3DRECT *pRects, uint32_t Flags, uint32_t Color, float Z, uint32_t Stencil)
{
    DTRACE("%u, %p, 0x%x, 0x%08x, %f, %u", Count, pRects, Flags, Color, Z, Stencil);
    pb_flush();
    if (debug_frame_active()) logf("[rt] Clear(%u rects, flags 0x%x, color 0x%08x, z %g)", Count, Flags, Color, Z);
    DWORD f = 0;
    if (Flags & XCLEAR_TARGET) f |= D3DCLEAR_TARGET;
    if (Flags & XCLEAR_ZBUFFER) f |= D3DCLEAR_ZBUFFER;
    if (Flags & XCLEAR_STENCIL) f |= D3DCLEAR_STENCIL;
    host3d::begin_scene();
    std::vector<D3DRECT> rects; // Xbox pixels -> render scale
    for (uint32_t i = 0; pRects && i < Count; i++) {
        RECT r = host_rect(pRects[i].x1, pRects[i].y1, pRects[i].x2, pRects[i].y2);
        rects.push_back({r.left, r.top, r.right, r.bottom});
    }
    host3d::dev()->Clear(pRects ? Count : 0, pRects ? rects.data() : nullptr, f, Color, Z, Stencil);
    return D3D_OK;
}

// The Xbox flips at a vblank: D3DRS_PRESENTATIONINTERVAL ONE/TWO/THREE/FOUR = flip after that many
// vblanks since the last flip; the _OR_IMMEDIATE variants (bit 31) flip at once when the frame is late,
// the plain ones wait for the next vblank; IMMEDIATE alone does not wait.
void pace_swap()
{
    static uint32_t last_vb;
    uint32_t iv = render_state()[XRS_PRESENTATIONINTERVAL];
    uint32_t n;
    switch (iv & 0xF) {
    case 0: n = (iv & 0x80000000) ? 0 : 1; break; // DEFAULT = ONE
    case 2: n = g_cfg.fps == 60 ? 1 : 2; break; // BLACK asks for TWO_OR_IMMEDIATE (30 fps); its update uses frame time
    case 4: n = 3; break;
    case 8: n = 4; break;
    default: n = 1; break;
    }
    if (n) {
        uint32_t target = last_vb + n, now = g_vblank_count.load();
        if (now >= target && !(iv & 0x80000000)) target = now + 1;
        while (g_vblank_count.load() < target) WaitForSingleObject(g_vblank_wake, 4); // Sleep(1) polling woke 0-2 ms late
    }
    uint32_t now_vb = g_vblank_count.load();
    perf::pace_end(now_vb - last_vb, n ? n : 1);
    last_vb = now_vb;
}

// Debug (--memdump "[*]ADDR:SIZE:F1,F2"): save SIZE bytes at ADDR (or at the pointer stored at ADDR) at the
// end of frames F1, F2... as mem_<frame>.bin in the --screenshot dir, to diff game state between frames.
void memdump_frame(uint32_t frame)
{
    if (g_cfg.memdump.empty() || g_cfg.screenshot_dir.empty()) return;
    std::string spec = narrow(g_cfg.memdump);
    const char *q = spec.c_str();
    bool deref = *q == '*';
    if (deref) q++;
    char *end = nullptr;
    uint32_t addr = (uint32_t)strtoul(q, &end, 16), size = end && *end == ':' ? (uint32_t)strtoul(end + 1, &end, 16) : 0;
    if (!size || !end || *end != ':') return;
    bool hit = false;
    for (const char *f = end + 1; *f;) {
        if ((uint32_t)strtoul(f, (char **)&f, 10) == frame) hit = true;
        if (*f == ',') f++;
        else break;
    }
    if (!hit) return;
    if (deref) addr = *(uint32_t *)(uintptr_t)addr;
    CreateDirectoryW(g_cfg.screenshot_dir.c_str(), nullptr);
    wchar_t name[64];
    swprintf(name, 64, L"\\mem_%u.bin", frame);
    FILE *fp = nullptr;
    if (!_wfopen_s(&fp, (g_cfg.screenshot_dir + name).c_str(), L"wb")) {
        fwrite((const void *)(uintptr_t)addr, 1, size, fp);
        fclose(fp);
        logf("[debug] memdump frame %u: %u bytes at %08x", frame, size, addr);
    }
}

// Clips (--rawvideo FILE): from --shot-start seconds for --clip-seconds, the picture as shown in the window,
// as raw BGRX frames (one per Swap). The picture is shrunk on the GPU into a small target first so the read
// back stays cheap and the game keeps its frame rate; "[capture] first frame" in the log lines it up with --wav.
void capture_frame()
{
    static FILE *fp;
    static IDirect3DSurface9 *shrunk, *sys;
    static uint32_t frames;
    static bool done;
    static ULONGLONG t0 = GetTickCount64();
    if (g_cfg.rawvideo.empty() || done) return;
    ULONGLONG ms = GetTickCount64() - t0, from = g_cfg.shot_start * 1000ull;
    if (ms < from) return;
    if (ms > from + g_cfg.clip_seconds * 1000ull) {
        if (fp) fclose(fp);
        fp = nullptr;
        done = true;
        logf("[capture] %u frames written", frames);
        return;
    }
    RECT frame, pic;
    host3d::frame_layout(&frame, &pic);
    UINT w = (UINT)(pic.right - pic.left) & ~1u, h = (UINT)(pic.bottom - pic.top) & ~1u; // even sizes for video encoders
    IDirect3DDevice9 *d = host3d::dev();
    if (!shrunk) {
        if (FAILED(d->CreateRenderTarget(w, h, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &shrunk, nullptr)) ||
            FAILED(d->CreateOffscreenPlainSurface(w, h, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)) ||
            _wfopen_s(&fp, g_cfg.rawvideo.c_str(), L"wb")) {
            done = true;
            logf("[capture] could not start");
            return;
        }
        logf("[capture] %ux%u BGRX frames at %d fps to %s", w, h, g_cfg.fps, narrow(g_cfg.rawvideo).c_str());
    }
    RECT src = {pic.left, pic.top, pic.left + (LONG)w, pic.top + (LONG)h};
    d->StretchRect(g_host_backbuffer, &src, shrunk, nullptr, D3DTEXF_POINT);
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(d->GetRenderTargetData(shrunk, sys)) && SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
        for (UINT y = 0; y < h; y++) fwrite((const uint8_t *)lr.pBits + y * lr.Pitch, 4, w, fp);
        sys->UnlockRect();
        if (++frames == 1) logf("[capture] first frame");
    }
}

// Every host target (all of them in D3DPOOL_DEFAULT): the colour targets, depth buffers, depth views, snapshot
// copies and anti-aliasing intermediates are dropped and come back on demand (their contents for one frame are
// lost; the game redraws them every frame). Textures still bound for sampling stay alive in Direct3D until the
// next draw replaces them.
namespace {

void release_host_targets()
{
    host3d::end_scene();
    IDirect3DDevice9 *d = host3d::dev();
    d->SetRenderTarget(0, g_host_backbuffer);
    d->SetDepthStencilSurface(g_host_depth);
    for (auto &kv : g_rts) {
        if (kv.second.tex) kv.second.tex->Release();
        if (kv.second.smooth) kv.second.smooth->Release();
    }
    g_rts.clear();
    for (auto &kv : g_zs) {
        if (kv.second.surf) kv.second.surf->Release();
        if (kv.second.tex) kv.second.tex->Release();
    }
    g_zs.clear();
    for (auto &kv : g_zviews)
        if (kv.second) kv.second->Release();
    g_zviews.clear();
    for (auto &kv : g_snapshots)
        if (kv.second) kv.second->Release();
    g_snapshots.clear();
    for (IDirect3DTexture9 *&t : g_aa_tex)
        if (t) {
            t->Release();
            t = nullptr;
        }
    g_cur_rt = nullptr;
}

// The game's current target and depth buffer bound again, with the viewport and scissor.
void rebind_game_target()
{
    bind_render_target(g_st.render_target ? g_st.render_target : g_backbuffer, g_st.depth_stencil);
    apply_viewport();
    if (g_scissor_on) {
        RECT r = host_rect(g_scissor.x1, g_scissor.y1, g_scissor.x2, g_scissor.y2);
        host3d::dev()->SetScissorRect(&r);
        host3d::dev()->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    }
}

} // namespace

// Change the render size while running (between frames). Every host target was made at the old scale.
void change_render_scale(int pic_w, int pic_h)
{
    float ox = g_scale_x, oy = g_scale_y;
    setup_render_scale(pic_w, pic_h);
    if (fabsf(ox - g_scale_x) < 1e-4f && fabsf(oy - g_scale_y) < 1e-4f && !g_force_rebuild.exchange(false)) return;
    release_host_targets();
    rebind_game_target();
    logf("[d3d] render size changed to %ux%u (x%.4g by x%.4g)", host_w(g_width), host_h(g_height), g_scale_x, g_scale_y);
}

namespace {

// What the game set that goes to the device as it is and is not sent again at every draw (a Reset clears it).
const D3DTRANSFORMSTATETYPE kTransforms[10] = {D3DTS_VIEW,     D3DTS_PROJECTION,     D3DTS_TEXTURE0,       D3DTS_TEXTURE1,
                                               D3DTS_TEXTURE2, D3DTS_TEXTURE3,       D3DTS_WORLDMATRIX(0), D3DTS_WORLDMATRIX(1),
                                               D3DTS_WORLDMATRIX(2), D3DTS_WORLDMATRIX(3)};
D3DMATERIAL9 g_material;
bool g_material_set;
std::map<uint32_t, D3DLIGHT9> g_lights;
std::map<uint32_t, BOOL> g_lights_on;

// A Direct3D 9 device is lost when Windows takes the display from it: sleep and wake, the lock screen,
// Ctrl+Alt+Del, a UAC prompt. Present then fails and everything drawn is thrown away, so the picture stopped at the
// last frame (the game went on running). Once TestCooperativeLevel says the device can be reset, the host targets
// and the back buffer references are released, the device is Reset, and what the game set is sent again: the draws
// forget the states they sent, and the transforms, material, lights, target, viewport and scissor are set here.
// Game textures (D3DPOOL_MANAGED), shaders and vertex declarations survive a Reset.
bool g_device_lost;
ULONGLONG g_lost_since;
std::atomic<bool> g_reset_requested; // tests: press-file "reset" (the same path as a lost device)

void recover_device(bool force)
{
    IDirect3DDevice9 *d = host3d::dev();
    HRESULT hr = d->TestCooperativeLevel();
    if (hr == D3D_OK && force) {
        hr = D3DERR_DEVICENOTRESET;
        logf("[d3d] tests: resetting the device as if it had been lost");
    }
    if (hr == D3D_OK) {
        g_device_lost = false;
        return;
    }
    if (!g_device_lost) {
        g_device_lost = true;
        g_lost_since = GetTickCount64();
        logf("[d3d] the Direct3D device is lost (0x%08lx: sleep, lock screen...); drawing resumes when it can be reset", hr);
    }
    if (hr != D3DERR_DEVICENOTRESET) return; // still lost (D3DERR_DEVICELOST): ask again next frame
    release_host_targets();
    d->SetRenderTarget(0, g_host_backbuffer);
    d->SetDepthStencilSurface(g_host_depth);
    if (g_host_backbuffer) g_host_backbuffer->Release();
    if (g_host_depth) g_host_depth->Release();
    g_host_backbuffer = g_host_depth = nullptr;
    bool ok = host3d::reset_device();
    d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_host_backbuffer);
    d->GetDepthStencilSurface(&g_host_depth);
    if (!ok) return; // the next frame tries again
    forget_sent_state();
    for (int i = 0; i < 10; i++) d->SetTransform(kTransforms[i], &g_st.transforms[i]);
    if (g_material_set) d->SetMaterial(&g_material);
    for (auto &kv : g_lights) d->SetLight(kv.first, &kv.second);
    for (auto &kv : g_lights_on) d->LightEnable(kv.first, kv.second);
    rebind_game_target();
    g_device_lost = false;
    logf("[d3d] the Direct3D device was reset; drawing again after %.1f s", (GetTickCount64() - g_lost_since) / 1000.0);
}

} // namespace

void request_render_change() { g_render_request = true; }
void request_render_rebuild()
{
    g_force_rebuild = true;
    g_render_request = true;
}

// After each Swap: in native mode the render size follows the window once a resize has settled for 0.3 s.
void check_render_size()
{
    bool now = false;
    if (!g_cfg.rescale_at.empty()) { // tests: "S:factor,..." sets the render size to that factor x the window at S seconds
        static ULONGLONG t0 = GetTickCount64();
        static size_t next_item;
        std::string spec = narrow(g_cfg.rescale_at);
        size_t pos = 0;
        for (size_t i = 0; pos < spec.size(); i++) {
            size_t comma = spec.find(',', pos);
            std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (i == next_item && GetTickCount64() - t0 >= (ULONGLONG)(atof(item.c_str()) * 1000.0)) {
                size_t colon = item.find(':');
                g_cfg.render_factor = colon == std::string::npos ? 1.0f : (float)atof(item.c_str() + colon + 1);
                g_cfg.render_native = true;
                now = true;
                next_item++;
                break;
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    if (g_render_request.exchange(false)) { // PC SETTINGS chose a render size
        RECT frame, pic;
        host3d::frame_layout(&frame, &pic);
        g_native_w = pic.right - pic.left;
        g_native_h = pic.bottom - pic.top;
        change_render_scale(g_native_w, g_native_h);
        return;
    }
    if (!g_cfg.render_native) return;
    RECT frame, pic;
    host3d::frame_layout(&frame, &pic);
    int w = pic.right - pic.left, h = pic.bottom - pic.top;
    static int want_w, want_h;
    static ULONGLONG since;
    if (!now && w == g_native_w && h == g_native_h) return;
    if (w != want_w || h != want_h) {
        want_w = w;
        want_h = h;
        since = GetTickCount64();
    }
    if (!now && GetTickCount64() - since < 300) return;
    g_native_w = w;
    g_native_h = h;
    change_render_scale(w, h);
}

uint32_t __stdcall D3DDevice_Swap(uint32_t Flags)
{
    DTRACE("0x%x", Flags);
    pb_flush();
    perf::swap_begin();
    pace_swap();
    present_backbuffer();
    capture_frame();
    uint32_t frames = ++g_frame_count;
    memdump_frame(frames);
    if (g_cfg.debug_frame && frames == (g_cfg.debug_frame_end ? g_cfg.debug_frame_end : g_cfg.debug_frame)) {
        dump_render_targets(frames);
        char tag[32];
        snprintf(tag, sizeof tag, "f%u_screen", frames);
        screenshot_now(tag);
    }
    if (g_shot_requested.exchange(false)) { // tests: --press-file "shot:name"
        std::lock_guard<std::mutex> lk(g_shot_lock);
        screenshot_now(g_shot_tag.c_str());
    }
    static ULONGLONG t0 = GetTickCount64();
    static const bool periodic = !g_cfg.screenshot_dir.empty(); // --screenshot given
    if (periodic && (frames == 1 || frames % g_cfg.shot_every == 0) && GetTickCount64() - t0 >= g_cfg.shot_start * 1000ull) {
        char tag[32];
        snprintf(tag, sizeof tag, "frame_%05u", frames);
        screenshot_now(tag);
    }
    bool force = g_reset_requested.exchange(false);
    if (host3d::present() == D3DERR_DEVICELOST || g_device_lost || force) recover_device(force);
    check_render_size();
    perf::frame_end(host_w(g_width), host_h(g_height));
    dev_field<uint32_t>(DEV_SWAPCOUNT)++;
    g_swap_count++;
    if (frames % 60 == 0 || frames < 4)
        logf("[d3d] frame %u presented (%u draws, %u skipped since last report)", frames, g_st.draws, g_st.draws_skipped);
    if (frames % 60 == 0 || frames < 4) g_st.draws = g_st.draws_skipped = 0;
    if (auto cb = dev_field<void(__cdecl *)(void *)>(DEV_VBLANKCALLBACK)) {
        static std::atomic<bool> noted{false};
        if (!noted.exchange(true)) logf("[d3d] vblank callback %s is set by the game", sym_name((uint32_t)(uintptr_t)cb).c_str());
    }
    return 0;
}

// ---- render targets ----

void fit_viewport();

HRESULT __stdcall D3DDevice_SetRenderTarget(Surface *pRenderTarget, Surface *pNewZStencil)
{
    DTRACE("%p, %p", pRenderTarget, pNewZStencil);
    pb_flush();
    if (pRenderTarget && debug_frame_active()) {
        uint32_t w, h;
        surface_size(pRenderTarget, &w, &h);
        logf("[rt] frame %u: SetRenderTarget %p data 0x%08x %ux%u fmt 0x%x%s, z %p [common 0x%08x format 0x%08x size 0x%08x]",
             g_frame_count.load(), pRenderTarget, pRenderTarget->Data, w, h, (pRenderTarget->Format >> FMT_FORMAT_SHIFT) & 0xFF,
             pRenderTarget == g_backbuffer ? " (back buffer)" : "", pNewZStencil, pRenderTarget->Common, pRenderTarget->Format,
             pRenderTarget->Size);
    }
    if (pRenderTarget) {
        g_st.render_target = pRenderTarget;
        dev_field<Surface *>(DEV_RENDERTARGET) = pRenderTarget;
    }
    g_st.depth_stencil = pNewZStencil;
    dev_field<Surface *>(DEV_ZBUFFER) = pNewZStencil;
    bind_render_target(g_st.render_target ? g_st.render_target : g_backbuffer, pNewZStencil);
    fit_viewport();
    return D3D_OK;
}

Surface *__stdcall D3DDevice_GetRenderTarget2()
{
    DTRACE("");
    Surface *s = g_st.render_target;
    if (s) s->Common++;
    return s;
}

Surface *__stdcall D3DDevice_GetDepthStencilSurface2()
{
    DTRACE("");
    Surface *s = g_st.depth_stencil;
    if (s) s->Common++;
    return s;
}

// ---- transforms, viewport, lights ----

void __stdcall D3DDevice_SetTransform(uint32_t State, const D3DMATRIX *pMatrix)
{
    DTRACE("%u, %p", State, pMatrix);
    if (State >= 10) return;
    g_st.transforms[State] = *pMatrix;
    memcpy(g_dev + DEV_TRANSFORMS + State * 0x40, pMatrix, 0x40);
    host3d::dev()->SetTransform(kTransforms[State], pMatrix);
}

void __stdcall D3DDevice_SetViewport(const Viewport *pViewport)
{
    DTRACE("%p: %u,%u %ux%u z %f-%f", pViewport, pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height, pViewport->MinZ,
           pViewport->MaxZ);
    pb_flush();
    if (debug_frame_active())
        logf("[rt] SetViewport %u,%u %ux%u z %g-%g (render target %p)", pViewport->X, pViewport->Y, pViewport->Width, pViewport->Height,
             pViewport->MinZ, pViewport->MaxZ, g_st.render_target);
    g_requested_viewport = *pViewport;
    fit_viewport();
}

// Like the library's SetViewport (0x26ac00), clamp the requested viewport to the current render target
// before storing it and deriving the screen-space transform (c-38/c-37). This is also re-done when the
// render target changes: BLACK sets a 640x480 viewport once per frame and then draws its light shafts
// into a 160x120 buffer expecting the whole view there (with the 640x480 transform they came out 4x too
// large, anchored at the top-left). No library code was found that does this re-fit on the Xbox.
void fit_viewport()
{
    Viewport v = g_requested_viewport;
    uint32_t rw = (uint32_t)g_width, rh = (uint32_t)g_height;
    if (g_st.render_target && g_st.render_target != g_backbuffer) surface_size(g_st.render_target, &rw, &rh);
    if (v.X + v.Width >= rw) v.Width = v.X < rw ? rw - v.X : 0;
    if (v.Y + v.Height >= rh) v.Height = v.Y < rh ? rh - v.Y : 0;
    g_st.viewport = v;
    apply_viewport();
    update_viewport_constants();
    memcpy(g_dev + DEV_VIEWPORT, &g_st.viewport, sizeof(Viewport));
}

// NV2A screen-space viewport transform: screen = clip / w * scale + offset.
void viewport_transform(float *pScale, float *pOffset)
{
    const Viewport &v = g_st.viewport;
    float zscale = 16777215.0f; // D24 depth buffer
    float sx = g_dev ? dev_field<float>(DEV_SCREENOFFSET_X) : 0.0f, sy = g_dev ? dev_field<float>(DEV_SCREENOFFSET_Y) : 0.0f;
    pScale[0] = v.Width * 0.5f;
    pScale[1] = -(v.Height * 0.5f);
    pScale[2] = (v.MaxZ - v.MinZ) * zscale;
    pScale[3] = 1.0f;
    pOffset[0] = v.X + v.Width * 0.5f + sx;
    pOffset[1] = v.Y + v.Height * 0.5f + sy;
    pOffset[2] = v.MinZ * zscale;
    pOffset[3] = 0.0f;
}

// NV2A vertex programs output pixel coordinates of the render target. Translated shaders turn them back
// into clip space with this transform of the host viewport, so pixels land where they would on the Xbox
// even when the game's viewport is larger than the target.
void host_viewport_transform(float *pScale, float *pOffset)
{
    D3DVIEWPORT9 v = effective_viewport();
    float zscale = 16777215.0f;
    float sx = g_dev ? dev_field<float>(DEV_SCREENOFFSET_X) : 0.0f, sy = g_dev ? dev_field<float>(DEV_SCREENOFFSET_Y) : 0.0f;
    pScale[0] = v.Width * 0.5f;
    pScale[1] = -(v.Height * 0.5f);
    pScale[2] = (v.MaxZ - v.MinZ) * zscale;
    pScale[3] = 1.0f;
    pOffset[0] = v.X + v.Width * 0.5f + sx;
    pOffset[1] = v.Y + v.Height * 0.5f + sy;
    pOffset[2] = v.MinZ * zscale;
    pOffset[3] = 0.0f;
}

// The library keeps the transform in the reserved constants c-38 (scale) and c-37 (offset), which the
// screen-space epilogue of XDK-compiled vertex shaders reads.
void update_viewport_constants()
{
    viewport_transform(g_st.vs_constants[96 - 38], g_st.vs_constants[96 - 37]);
}

void __stdcall D3DDevice_GetViewportOffsetAndScale(float *pOffset, float *pScale)
{
    DTRACE("%p, %p", pOffset, pScale);
    viewport_transform(pScale, pOffset);
}

void __stdcall D3DDevice_SetScreenSpaceOffset(float x, float y)
{
    DTRACE("%f, %f", x, y);
    dev_field<float>(DEV_SCREENOFFSET_X) = x;
    dev_field<float>(DEV_SCREENOFFSET_Y) = y;
    update_viewport_constants();
}

// FUN_0026eaf0: writes the viewport transform into the push buffer (eax = push pointer, returns it
// advanced). Game code calls it after changing the screen-space offset fields directly.
uint32_t *__stdcall D3D_WriteViewportTransform(uint32_t *pPush)
{
    DTRACE("%p", pPush);
    update_viewport_constants();
    return pPush; // HLE applies the viewport directly; nothing to push
}

void __stdcall D3DDevice_SetMaterial(const D3DMATERIAL9 *pMaterial)
{
    DTRACE("%p", pMaterial);
    g_material = *pMaterial;
    g_material_set = true;
    host3d::dev()->SetMaterial(pMaterial);
}

void __stdcall D3DDevice_SetLight(uint32_t Index, const D3DLIGHT9 *pLight)
{
    DTRACE("%u, %p", Index, pLight);
    g_lights[Index] = *pLight;
    host3d::dev()->SetLight(Index, pLight);
}

void __stdcall D3DDevice_LightEnable(uint32_t Index, BOOL bEnable)
{
    DTRACE("%u, %d", Index, bEnable);
    g_lights_on[Index] = bEnable;
    host3d::dev()->LightEnable(Index, bEnable);
}

void __stdcall D3DDevice_SetScissors(uint32_t Count, BOOL Exclusive, const D3DRECT *pRects)
{
    DTRACE("%u, %d, %p", Count, Exclusive, pRects);
    pb_flush();
    IDirect3DDevice9 *d = host3d::dev();
    if (!Count || !pRects) {
        d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        g_scissor_on = false;
        return;
    }
    RECT r = host_rect(pRects[0].x1, pRects[0].y1, pRects[0].x2, pRects[0].y2);
    d->SetScissorRect(&r);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    g_scissor = pRects[0];
    g_scissor_on = true;
    if (Count > 1 || Exclusive) KNOTE_ONCE("SetScissors with %u rects / exclusive %d: only the first rect is used", Count, Exclusive);
}

// ---- textures and streams ----

void __stdcall D3DDevice_SetTexture(uint32_t Stage, Resource *pTexture)
{
    DTRACE("%u, %p", Stage, pTexture);
    if (Stage >= 4) return;
    pb_flush();
    g_st.textures[Stage] = pTexture;
    ((Resource **)(g_dev + DEV_TEXTURES))[Stage] = pTexture;
}

void __stdcall D3DDevice_SetPalette(uint32_t Stage, Palette *pPalette)
{
    DTRACE("%u, %p", Stage, pPalette);
    if (Stage < 4) g_st.palettes[Stage] = pPalette;
}

void __stdcall D3DDevice_SetStreamSource(uint32_t StreamNumber, VertexBuffer *pStreamData, uint32_t Stride)
{
    DTRACE("%u, %p, %u", StreamNumber, pStreamData, Stride);
    if (StreamNumber >= 16) return;
    pb_flush();
    g_st.streams[StreamNumber] = {pStreamData, Stride};
}

// ---- render / texture-stage states ----

void __stdcall D3DDevice_SetRenderStateNotInline(uint32_t State, uint32_t Value)
{
    DTRACE("%u, 0x%x", State, Value);
    pb_flush();
    if (State < XRS_MAX) render_state()[State] = Value;
}

void __stdcall D3DDevice_SetRenderState_Simple(uint32_t Method, uint32_t Value)
{
    // Inline code has already written D3D_g_RenderState; this only emitted the NV2A method.
    DTRACE("method 0x%x, 0x%x", Method, Value);
    pb_flush();
}

#define RS_SETTER(fn, state)                         \
    void __stdcall fn(uint32_t Value)                \
    {                                                \
        DTRACE("0x%x", Value);                       \
        pb_flush();                                  \
        render_state()[state] = Value;               \
    }
RS_SETTER(D3DDevice_SetRenderState_NormalizeNormals, XRS_NORMALIZENORMALS)
RS_SETTER(D3DDevice_SetRenderState_StencilEnable, XRS_STENCILENABLE)
RS_SETTER(D3DDevice_SetRenderState_StencilFail, XRS_STENCILFAIL)
RS_SETTER(D3DDevice_SetRenderState_TextureFactor, XRS_TEXTUREFACTOR)

void __stdcall D3DDevice_SetTextureStageStateNotInline(uint32_t Stage, uint32_t Type, uint32_t Value)
{
    DTRACE("%u, %u, 0x%x", Stage, Type, Value);
    pb_flush();
    if (Stage < 4 && Type < 32) texture_state(Stage)[Type] = Value;
}

// FUN_002709a0: lazy state flush of the original library (processes the dirty flags). HLE applies state
// at draw time, so there is nothing to do.
void __stdcall D3D_FlushDirtyState() { DTRACE(""); }

// FUN_002695f0 (esi = device): fog/vertex-shader state upload of the original library.
void __stdcall D3D_UpdateFogState() { DTRACE(""); }

// ---- vertex shaders ----

HRESULT __stdcall D3DDevice_CreateVertexShader(const uint32_t *pDeclaration, const uint32_t *pFunction, uint32_t *pHandle,
                                               uint32_t Usage)
{
    VertexShader *vs = vs_create(pDeclaration, pFunction, Usage);
    if (!vs) return D3DERR_INVALIDCALL;
    *pHandle = (uint32_t)(uintptr_t)vs | 1;
    DTRACE("%p, %p, %p, 0x%x) -> handle 0x%x, %u elements, %u instructions (", pDeclaration, pFunction, pHandle, Usage, *pHandle,
           vs->num_elements, vs->program_insts);
    return D3D_OK;
}

void __stdcall D3DDevice_DeleteVertexShader(uint32_t Handle)
{
    DTRACE("0x%x", Handle);
    if (g_st.vertex_shader == Handle) g_st.vertex_shader = 0;
    vs_delete(vs_from_handle(Handle));
}

void __stdcall D3DDevice_SetVertexShader(uint32_t Handle)
{
    DTRACE("0x%x", Handle);
    pb_flush();
    g_st.vertex_shader = Handle;
}

// FUN_002690c0 (eax = handle): direct vertex shader selection used by inline game code.
void __stdcall D3D_SelectVertexShader(uint32_t Handle)
{
    DTRACE("0x%x", Handle);
    pb_flush();
    g_st.vertex_shader = Handle & ~0x10u;
}

static void set_vs_constants(int slot, const float *data, uint32_t count)
{
    pb_flush();
    if (debug_frame_active() && slot <= 59 && slot + (int)count > 58)
        logf("[rt] vertex constants %d+%u written (screen-space slots 58/59): %g %g %g %g", slot, count, data[0], data[1], data[2], data[3]);
    for (uint32_t i = 0; i < count; i++) {
        int r = slot + (int)i;
        if (r >= 0 && r < 192) memcpy(g_st.vs_constants[r], data + 4 * i, 16);
    }
}

void __stdcall D3DDevice_SetVertexShaderConstant1(int Register, const float *pConstantData)
{
    DTRACE("%d, %p", Register, pConstantData);
    set_vs_constants(Register, pConstantData, 1);
}

void __stdcall D3DDevice_SetVertexShaderConstant1Fast(int Register, const float *pConstantData)
{
    DTRACE("%d, %p", Register, pConstantData);
    set_vs_constants(Register, pConstantData, 1);
}

void __stdcall D3DDevice_SetVertexShaderConstant4(int Register, const float *pConstantData)
{
    DTRACE("%d, %p", Register, pConstantData);
    set_vs_constants(Register, pConstantData, 4);
}

// The count is in DWORDs (0x2699e0 pushes that many with SET_TRANSFORM_CONSTANT); callers fill the shadow
// themselves. Treating it as a constant count overwrote 3x as many following slots with stray memory.
void __stdcall D3DDevice_SetVertexShaderConstantNotInlineFast(int Register, const float *pConstantData, uint32_t DwordCount)
{
    DTRACE("%d, %p, %u", Register, pConstantData, DwordCount);
    set_vs_constants(Register, pConstantData, DwordCount / 4);
}

// ---- pixel shaders ----

// The handle points at {RefCount, D3DOwned, D3DPIXELSHADERDEF *}. Like the library, copy the program into
// render states 0-56 and PSTextureModes; draws translate whatever is there.
const PixelShaderDef *ps_def(uint32_t handle) { return handle ? *(const PixelShaderDef **)(uintptr_t)(handle + 8) : nullptr; }

void __stdcall D3DDevice_SetPixelShader(uint32_t Handle)
{
    DTRACE("0x%x", Handle);
    pb_flush();
    g_st.pixel_shader = Handle;
    if (const PixelShaderDef *def = ps_def(Handle)) {
        memcpy(render_state(), def, 57 * 4);
        render_state()[XRS_PSTEXTUREMODES] = def->PSTextureModes;
    }
}

void __stdcall D3DDevice_DeletePixelShader(uint32_t Handle)
{
    DTRACE("0x%x", Handle);
    if (g_st.pixel_shader == Handle) g_st.pixel_shader = 0;
}

void __stdcall D3DDevice_SetPixelShaderConstant(uint32_t Register, const float *pConstantData, uint32_t ConstantCount)
{
    DTRACE("%u, %p, %u", Register, pConstantData, ConstantCount);
    pb_flush();
    const PixelShaderDef *def = ps_def(g_st.pixel_shader);
    for (uint32_t i = 0; i < ConstantCount && Register + i < 16; i++) {
        memcpy(g_st.ps_constants[Register + i], pConstantData + 4 * i, 16);
        if (def) ps_set_constant(def, Register + i, pConstantData + 4 * i);
    }
}

// ---- fences ----

uint32_t __stdcall D3D_SetFence(uint32_t Flags)
{
    DTRACE("0x%x", Flags);
    pb_flush();
    ++g_gpu_time;
    dev_field<uint32_t>(DEV_GPUTIME) = g_gpu_time;
    return g_gpu_time;
}

BOOL __stdcall D3DDevice_IsFencePending(uint32_t Fence)
{
    DTRACE("%u", Fence);
    return FALSE; // HLE work completes synchronously
}

void __stdcall D3D_BlockOnTime(uint32_t Time, BOOL MakeSpace) { DTRACE("%u, %d", Time, MakeSpace); }

// ---- table ----

#define H(addr, fn, params, ret) {addr, #fn, (void *)&fn, params, ret}
#define NATIVE(addr, name) {addr, name, nullptr, "", false}

static const Hle kTable[] = {
    H(0x00277580, Direct3D_GetAdapterModeCount, "s", true),
    H(0x00277640, Direct3D_EnumAdapterModes, "s s s", true),
    H(0x0026c620, Direct3D_CreateDevice, "s s s s s s", true),
    H(0x0026b5d0, D3DDevice_Release, "", true),
    H(0x0026a320, D3DDevice_GetDisplayMode, "s", true),
    H(0x0026b610, D3DDevice_GetDisplayFieldStatus, "s", false),
    H(0x0026cdc0, D3D_MakeRequestedSpace, "s eax", true),
    H(0x0026d110, D3DDevice_BeginPush, "s", true),
    H(0x0026d170, D3DDevice_EndPush, "s", false),
    H(0x0026a1f0, D3DDevice_End, "", false),
    H(0x00268880, D3DDevice_Clear, "s s s s s s", true),
    H(0x002773a0, D3DDevice_Swap, "s", true),
    H(0x0026a420, D3DDevice_SetRenderTarget, "s s", true),
    H(0x0026aa60, D3DDevice_GetRenderTarget2, "", true),
    H(0x0026aaa0, D3DDevice_GetDepthStencilSurface2, "", true),
    H(0x0026aae0, D3DDevice_SetTransform, "s s", false),
    H(0x0026ac00, D3DDevice_SetViewport, "s", false),
    H(0x0026e9c0, D3DDevice_GetViewportOffsetAndScale, "s s", false),
    H(0x0026ba50, D3DDevice_SetScreenSpaceOffset, "s s", false),
    H(0x0026eaf0, D3D_WriteViewportTransform, "eax", true),
    H(0x0026ae20, D3DDevice_SetMaterial, "s", false),
    H(0x0026ae50, D3DDevice_SetLight, "s s", true),
    H(0x0026b210, D3DDevice_LightEnable, "s s", true),
    H(0x0026b720, D3DDevice_SetScissors, "s s s", false),
    H(0x0026b370, D3DDevice_SetTexture, "s s", false),
    H(0x0026b530, D3DDevice_SetPalette, "s s", false),
    H(0x00268de0, D3DDevice_SetStreamSource, "s s s", false),
    H(0x0026e5e0, D3DDevice_SetRenderStateNotInline, "s s", false),
    H(0x0026e5b0, D3DDevice_SetRenderState_Simple, "ecx edx", false),
    H(0x0026dd60, D3DDevice_SetRenderState_NormalizeNormals, "s", false),
    H(0x0026de50, D3DDevice_SetRenderState_StencilEnable, "s", false),
    H(0x0026df00, D3DDevice_SetRenderState_StencilFail, "s", false),
    H(0x0026df80, D3DDevice_SetRenderState_TextureFactor, "s", false),
    H(0x0026e860, D3DDevice_SetTextureStageStateNotInline, "s s s", false),
    H(0x002709a0, D3D_FlushDirtyState, "", false),
    H(0x002695f0, D3D_UpdateFogState, "", false),
    H(0x00269480, D3DDevice_CreateVertexShader, "s s s s", true),
    H(0x002695d0, D3DDevice_DeleteVertexShader, "s", false),
    H(0x00269710, D3DDevice_SetVertexShader, "s", false),
    H(0x002690c0, D3D_SelectVertexShader, "eax", false),
    H(0x00269880, D3DDevice_SetVertexShaderConstant1, "ecx edx", false),
    H(0x002698e0, D3DDevice_SetVertexShaderConstant1Fast, "ecx edx", false),
    H(0x00269930, D3DDevice_SetVertexShaderConstant4, "ecx edx", false),
    H(0x002699e0, D3DDevice_SetVertexShaderConstantNotInlineFast, "ecx edx s", false),
    H(0x0026bd00, D3DDevice_SetPixelShader, "s", false),
    H(0x0026bce0, D3DDevice_DeletePixelShader, "s", false),
    H(0x0026bf10, D3DDevice_SetPixelShaderConstant, "ecx s eax", false),
    H(0x0026c920, D3D_SetFence, "s", true),
    H(0x0026d0f0, D3DDevice_IsFencePending, "s", true),
    H(0x0026cc10, D3D_BlockOnTime, "s s", false),
    NATIVE(0x00277570, "FUN_00277570 (returns 1)"),
    NATIVE(0x00277490, "GetBackBufferScale (reads device +0x96C/+0x970)"),
};

const Hle *hle_device_table(size_t *count)
{
    *count = sizeof kTable / sizeof kTable[0];
    return kTable;
}

void request_device_reset() { g_reset_requested = true; }

void request_screenshot(const char *tag)
{
    std::lock_guard<std::mutex> lk(g_shot_lock);
    g_shot_tag = tag;
    g_shot_requested = true;
}

} // namespace xd3d
