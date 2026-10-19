// Draw calls and state application. State lives in the Xbox arrays (render_state(), texture_state())
// because game code writes them directly; it is translated to D3D9 at each draw (with a cache of what
// was last sent).
#include "d3d/draw.h"

#include "d3d/device.h"
#include "d3d/dispatch.h"
#include "d3d/host.h"
#include "d3d/pshader.h"
#include "d3d/pushbuf.h"
#include "d3d/state.h"
#include "d3d/vshader.h"
#include "fov.h"
#include "perf.h"

#include <wincodec.h>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace xd3d {

IDirect3DTexture9 *rt_texture_for(const PixelContainer *tex, uint32_t draw_serial, bool linear); // device.cpp
uint32_t g_draw_serial; // increments per draw (render-target snapshots are shared within a draw)
uint32_t backbuffer_data();
uint32_t depthbuffer_data();
// --debug-frame N[-M]: true while frame N (or any frame up to M) is being built
bool debug_frame_active()
{
    uint32_t f = g_frame_count + 1;
    if (!g_cfg.debug_frame) return false;
    return g_cfg.debug_frame_end ? (f >= g_cfg.debug_frame && f <= g_cfg.debug_frame_end) : f == g_cfg.debug_frame;
}

// ---- enum conversions ----

D3DCMPFUNC conv_cmp(uint32_t v) { return (v >= 0x200 && v <= 0x207) ? (D3DCMPFUNC)(v - 0x200 + 1) : D3DCMP_ALWAYS; }

D3DBLEND conv_blend(uint32_t v)
{
    switch (v) {
    case 0: return D3DBLEND_ZERO;
    case 1: return D3DBLEND_ONE;
    case 0x300: return D3DBLEND_SRCCOLOR;
    case 0x301: return D3DBLEND_INVSRCCOLOR;
    case 0x302: return D3DBLEND_SRCALPHA;
    case 0x303: return D3DBLEND_INVSRCALPHA;
    case 0x304: return D3DBLEND_DESTALPHA;
    case 0x305: return D3DBLEND_INVDESTALPHA;
    case 0x306: return D3DBLEND_DESTCOLOR;
    case 0x307: return D3DBLEND_INVDESTCOLOR;
    case 0x308: return D3DBLEND_SRCALPHASAT;
    case 0x8001: case 0x8003: return D3DBLEND_BLENDFACTOR;
    case 0x8002: case 0x8004: return D3DBLEND_INVBLENDFACTOR;
    default: return D3DBLEND_ONE;
    }
}

D3DBLENDOP conv_blendop(uint32_t v)
{
    switch (v) {
    case 0x800A: return D3DBLENDOP_SUBTRACT;
    case 0x800B: return D3DBLENDOP_REVSUBTRACT;
    case 0x8007: return D3DBLENDOP_MIN;
    case 0x8008: return D3DBLENDOP_MAX;
    default: return D3DBLENDOP_ADD;
    }
}

D3DSTENCILOP conv_stencilop(uint32_t v)
{
    switch (v) {
    case 0: return D3DSTENCILOP_ZERO;
    case 0x1E01: return D3DSTENCILOP_REPLACE;
    case 0x1E02: return D3DSTENCILOP_INCRSAT;
    case 0x1E03: return D3DSTENCILOP_DECRSAT;
    case 0x150A: return D3DSTENCILOP_INVERT;
    case 0x8507: return D3DSTENCILOP_INCR;
    case 0x8508: return D3DSTENCILOP_DECR;
    default: return D3DSTENCILOP_KEEP;
    }
}

D3DTEXTUREOP conv_texop(uint32_t v)
{
    static const D3DTEXTUREOP map[] = {
        D3DTOP_DISABLE, D3DTOP_DISABLE, D3DTOP_SELECTARG1, D3DTOP_SELECTARG2, D3DTOP_MODULATE, D3DTOP_MODULATE2X,
        D3DTOP_MODULATE4X, D3DTOP_ADD, D3DTOP_ADDSIGNED, D3DTOP_ADDSIGNED2X, D3DTOP_SUBTRACT, D3DTOP_ADDSMOOTH,
        D3DTOP_BLENDDIFFUSEALPHA, D3DTOP_BLENDCURRENTALPHA, D3DTOP_BLENDTEXTUREALPHA, D3DTOP_BLENDFACTORALPHA,
        D3DTOP_BLENDTEXTUREALPHAPM, D3DTOP_PREMODULATE, D3DTOP_MODULATEALPHA_ADDCOLOR, D3DTOP_MODULATECOLOR_ADDALPHA,
        D3DTOP_MODULATEINVALPHA_ADDCOLOR, D3DTOP_MODULATEINVCOLOR_ADDALPHA, D3DTOP_DOTPRODUCT3, D3DTOP_MULTIPLYADD,
        D3DTOP_LERP, D3DTOP_BUMPENVMAP, D3DTOP_BUMPENVMAPLUMINANCE};
    return v < sizeof map / sizeof map[0] ? map[v] : D3DTOP_MODULATE;
}

D3DTEXTUREADDRESS conv_address(uint32_t v)
{
    switch (v) {
    case 2: return D3DTADDRESS_MIRROR;
    case 3: case 5: return D3DTADDRESS_CLAMP;
    case 4: return D3DTADDRESS_BORDER;
    default: return D3DTADDRESS_WRAP;
    }
}

DWORD conv_colorwrite(uint32_t v)
{
    DWORD r = 0;
    if (v & 0x00010000) r |= D3DCOLORWRITEENABLE_RED;
    if (v & 0x00000100) r |= D3DCOLORWRITEENABLE_GREEN;
    if (v & 0x00000001) r |= D3DCOLORWRITEENABLE_BLUE;
    if (v & 0x01000000) r |= D3DCOLORWRITEENABLE_ALPHA;
    return r;
}

static D3DCULL conv_cull(uint32_t cull)
{
    if (cull == 0) return D3DCULL_NONE;
    return cull == 0x900 ? D3DCULL_CW : D3DCULL_CCW;
}

// ---- defaults (what the original CreateDevice sets up) ----

static float as_float(uint32_t v)
{
    float f;
    memcpy(&f, &v, 4);
    return f;
}

static uint32_t f2u(float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return v;
}

void init_default_states()
{
    uint32_t *rs = render_state();
    rs[XRS_ZFUNC] = 0x203;
    rs[XRS_ALPHAFUNC] = 0x207;
    rs[XRS_SRCBLEND] = 1;
    rs[XRS_DESTBLEND] = 0;
    rs[XRS_ZWRITEENABLE] = 1;
    rs[XRS_SHADEMODE] = 0x1D01;
    rs[XRS_COLORWRITEENABLE] = 0x01010101;
    rs[XRS_STENCILZFAIL] = rs[XRS_STENCILPASS] = rs[XRS_STENCILFAIL] = 0x1E00;
    rs[XRS_STENCILFUNC] = 0x207;
    rs[XRS_STENCILMASK] = rs[XRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[XRS_BLENDOP] = 0x8006;
    rs[XRS_FOGEND] = f2u(1.0f);
    rs[XRS_FOGDENSITY] = f2u(1.0f);
    rs[XRS_LIGHTING] = 1;
    rs[XRS_LOCALVIEWER] = 1;
    rs[XRS_COLORVERTEX] = 1;
    rs[XRS_SPECULARMATERIALSOURCE] = 2;
    rs[XRS_DIFFUSEMATERIALSOURCE] = 1;
    rs[XRS_POINTSIZE] = f2u(1.0f);
    rs[XRS_FILLMODE] = rs[XRS_BACKFILLMODE] = 0x1B02;
    rs[XRS_ZENABLE] = 1;
    rs[XRS_FRONTFACE] = 0x900;
    rs[XRS_CULLMODE] = 0x901;
    rs[XRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs[XRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    for (int s = 0; s < 4; s++) {
        uint32_t *ts = texture_state(s);
        ts[0] = ts[1] = ts[2] = 1;   // ADDRESSU/V/W = WRAP
        ts[3] = ts[4] = 2;           // MAG/MIN = LINEAR
        ts[5] = 0;                   // MIPFILTER = NONE
        ts[8] = 1;                   // MAXANISOTROPY
        ts[12] = s == 0 ? 4 : 1;     // COLOROP: MODULATE / DISABLE
        ts[14] = 2;                  // COLORARG1 = TEXTURE
        ts[15] = 1;                  // COLORARG2 = CURRENT
        ts[16] = s == 0 ? 2 : 1;     // ALPHAOP: SELECTARG1 / DISABLE
        ts[18] = 2;                  // ALPHAARG1 = TEXTURE
        ts[19] = 1;                  // ALPHAARG2 = CURRENT
        ts[20] = 1;                  // RESULTARG = CURRENT
        ts[28] = (uint32_t)s;        // TEXCOORDINDEX
    }
}

// ---- state application ----

namespace {

uint32_t g_rs_sent[XRS_MAX];
bool g_rs_valid[XRS_MAX];
uint32_t g_ts_sent[4][32];
bool g_ts_valid[4][32];
IDirect3DBaseTexture9 *g_tex_sent[4];

template <typename F> void on_change(uint32_t state, F apply)
{
    uint32_t v = render_state()[state];
    if (g_rs_valid[state] && g_rs_sent[state] == v) return;
    g_rs_valid[state] = true;
    g_rs_sent[state] = v;
    apply(v);
}

void apply_render_states(IDirect3DDevice9 *d)
{
    auto set = [&](D3DRENDERSTATETYPE s, DWORD v) { d->SetRenderState(s, v); };
    on_change(XRS_ZENABLE, [&](uint32_t v) { set(D3DRS_ZENABLE, v); });
    on_change(XRS_ZFUNC, [&](uint32_t v) { set(D3DRS_ZFUNC, conv_cmp(v)); });
    on_change(XRS_ZWRITEENABLE, [&](uint32_t v) { set(D3DRS_ZWRITEENABLE, v != 0); });
    on_change(XRS_ALPHATESTENABLE, [&](uint32_t v) { set(D3DRS_ALPHATESTENABLE, v != 0); });
    on_change(XRS_ALPHAFUNC, [&](uint32_t v) { set(D3DRS_ALPHAFUNC, conv_cmp(v)); });
    on_change(XRS_ALPHAREF, [&](uint32_t v) { set(D3DRS_ALPHAREF, v & 0xFF); });
    on_change(XRS_ALPHABLENDENABLE, [&](uint32_t v) { set(D3DRS_ALPHABLENDENABLE, v != 0); });
    on_change(XRS_SRCBLEND, [&](uint32_t v) { set(D3DRS_SRCBLEND, conv_blend(v)); });
    on_change(XRS_DESTBLEND, [&](uint32_t v) { set(D3DRS_DESTBLEND, conv_blend(v)); });
    on_change(XRS_BLENDOP, [&](uint32_t v) { set(D3DRS_BLENDOP, conv_blendop(v)); });
    on_change(XRS_BLENDCOLOR, [&](uint32_t v) { set(D3DRS_BLENDFACTOR, v); });
    on_change(XRS_DITHERENABLE, [&](uint32_t v) { set(D3DRS_DITHERENABLE, v != 0); });
    on_change(XRS_SHADEMODE, [&](uint32_t v) { set(D3DRS_SHADEMODE, v == 0x1D00 ? D3DSHADE_FLAT : D3DSHADE_GOURAUD); });
    on_change(XRS_COLORWRITEENABLE, [&](uint32_t v) { set(D3DRS_COLORWRITEENABLE, conv_colorwrite(v)); });
    on_change(XRS_STENCILENABLE, [&](uint32_t v) { set(D3DRS_STENCILENABLE, v != 0); });
    on_change(XRS_STENCILFUNC, [&](uint32_t v) { set(D3DRS_STENCILFUNC, conv_cmp(v)); });
    on_change(XRS_STENCILREF, [&](uint32_t v) { set(D3DRS_STENCILREF, v); });
    on_change(XRS_STENCILMASK, [&](uint32_t v) { set(D3DRS_STENCILMASK, v); });
    on_change(XRS_STENCILWRITEMASK, [&](uint32_t v) { set(D3DRS_STENCILWRITEMASK, v); });
    on_change(XRS_STENCILFAIL, [&](uint32_t v) { set(D3DRS_STENCILFAIL, conv_stencilop(v)); });
    on_change(XRS_STENCILZFAIL, [&](uint32_t v) { set(D3DRS_STENCILZFAIL, conv_stencilop(v)); });
    on_change(XRS_STENCILPASS, [&](uint32_t v) { set(D3DRS_STENCILPASS, conv_stencilop(v)); });
    on_change(XRS_CULLMODE, [&](uint32_t v) { set(D3DRS_CULLMODE, conv_cull(v)); });
    on_change(XRS_FILLMODE, [&](uint32_t v) {
        set(D3DRS_FILLMODE, v == 0x1B00 ? D3DFILL_POINT : v == 0x1B01 ? D3DFILL_WIREFRAME : D3DFILL_SOLID);
    });
    on_change(XRS_FOGENABLE, [&](uint32_t v) { set(D3DRS_FOGENABLE, v != 0); });
    on_change(XRS_FOGSTART, [&](uint32_t v) { set(D3DRS_FOGSTART, v); });
    on_change(XRS_FOGEND, [&](uint32_t v) { set(D3DRS_FOGEND, v); });
    on_change(XRS_FOGDENSITY, [&](uint32_t v) { set(D3DRS_FOGDENSITY, v); });
    on_change(XRS_FOGCOLOR, [&](uint32_t v) { set(D3DRS_FOGCOLOR, v); });
    on_change(XRS_RANGEFOGENABLE, [&](uint32_t v) { set(D3DRS_RANGEFOGENABLE, v != 0); });
    on_change(XRS_LIGHTING, [&](uint32_t v) { set(D3DRS_LIGHTING, v != 0); });
    on_change(XRS_SPECULARENABLE, [&](uint32_t v) { set(D3DRS_SPECULARENABLE, v != 0); });
    on_change(XRS_LOCALVIEWER, [&](uint32_t v) { set(D3DRS_LOCALVIEWER, v != 0); });
    on_change(XRS_COLORVERTEX, [&](uint32_t v) { set(D3DRS_COLORVERTEX, v != 0); });
    on_change(XRS_SPECULARMATERIALSOURCE, [&](uint32_t v) { set(D3DRS_SPECULARMATERIALSOURCE, v); });
    on_change(XRS_DIFFUSEMATERIALSOURCE, [&](uint32_t v) { set(D3DRS_DIFFUSEMATERIALSOURCE, v); });
    on_change(XRS_AMBIENTMATERIALSOURCE, [&](uint32_t v) { set(D3DRS_AMBIENTMATERIALSOURCE, v); });
    on_change(XRS_EMISSIVEMATERIALSOURCE, [&](uint32_t v) { set(D3DRS_EMISSIVEMATERIALSOURCE, v); });
    on_change(XRS_AMBIENT, [&](uint32_t v) { set(D3DRS_AMBIENT, v); });
    on_change(XRS_NORMALIZENORMALS, [&](uint32_t v) { set(D3DRS_NORMALIZENORMALS, v != 0); });
    on_change(XRS_TEXTUREFACTOR, [&](uint32_t v) { set(D3DRS_TEXTUREFACTOR, v); });
    on_change(XRS_POINTSIZE, [&](uint32_t v) { set(D3DRS_POINTSIZE, v); });
}

template <typename F> void ts_on_change(int s, int t, F apply)
{
    uint32_t v = texture_state(s)[t];
    if (g_ts_valid[s][t] && g_ts_sent[s][t] == v) return;
    g_ts_valid[s][t] = true;
    g_ts_sent[s][t] = v;
    apply(v);
}

void apply_texture_stages(IDirect3DDevice9 *d, bool fixed_function_pixels)
{
    for (int s = 0; s < 4; s++) {
        IDirect3DBaseTexture9 *t = nullptr;
        if (Resource *r = g_st.textures[s]) {
            const uint32_t *ts = texture_state(s); // MAGFILTER 3, MINFILTER 4: 2 = LINEAR and up
            t = rt_texture_for((const PixelContainer *)r, g_draw_serial, ts[3] >= 2 || ts[4] >= 2);
            if (debug_frame_active() && (r->Data == backbuffer_data() || r->Data == depthbuffer_data() || t)) {
                uint32_t w, h, dd, l;
                pixel_container_dims((PixelContainer *)r, &w, &h, &dd, &l);
                logf("[rt] frame %u: stage %d samples %s (texture %p data 0x%08x %ux%u fmt 0x%x)", g_frame_count.load(), s,
                     r->Data == backbuffer_data() ? "the BACK BUFFER" : r->Data == depthbuffer_data() ? "the DEPTH BUFFER" : "a render target",
                     r, r->Data, w, h, (((PixelContainer *)r)->Format >> FMT_FORMAT_SHIFT) & 0xFF);
            }
            if (!t) t = host_texture(r, g_st.palettes[s]);
        }
        if (t != g_tex_sent[s]) {
            d->SetTexture(s, t);
            g_tex_sent[s] = t;
        }
        ts_on_change(s, 0, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_ADDRESSU, conv_address(v)); });
        ts_on_change(s, 1, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_ADDRESSV, conv_address(v)); });
        ts_on_change(s, 2, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_ADDRESSW, conv_address(v)); });
        ts_on_change(s, 3, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_MAGFILTER, v > 3 ? D3DTEXF_LINEAR : v); });
        ts_on_change(s, 4, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_MINFILTER, v > 3 ? D3DTEXF_LINEAR : v); });
        ts_on_change(s, 5, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_MIPFILTER, v > 2 ? D3DTEXF_LINEAR : v); });
        ts_on_change(s, 6, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_MIPMAPLODBIAS, v); });
        ts_on_change(s, 7, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, v); });
        ts_on_change(s, 8, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, v ? v : 1); });
        ts_on_change(s, 29, [&](uint32_t v) { d->SetSamplerState(s, D3DSAMP_BORDERCOLOR, v); });
        if (!fixed_function_pixels) continue;
        ts_on_change(s, 12, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_COLOROP, conv_texop(v)); });
        ts_on_change(s, 13, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_COLORARG0, v); });
        ts_on_change(s, 14, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_COLORARG1, v); });
        ts_on_change(s, 15, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_COLORARG2, v); });
        ts_on_change(s, 16, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_ALPHAOP, conv_texop(v)); });
        ts_on_change(s, 17, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_ALPHAARG0, v); });
        ts_on_change(s, 18, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_ALPHAARG1, v); });
        ts_on_change(s, 19, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_ALPHAARG2, v); });
        ts_on_change(s, 20, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_RESULTARG, v); });
        ts_on_change(s, 21, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, v); });
        ts_on_change(s, 28, [&](uint32_t v) { d->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, v); });
    }
}

// A pixel shader that failed to translate: approximate with "texture stage 0 modulated by diffuse".
void apply_pixel_shader_fallback(IDirect3DDevice9 *d)
{
    static bool noted;
    if (!noted) {
        noted = true;
        logf("[d3d] note: untranslatable pixel shader drawn as texture x diffuse");
    }
    bool tex = g_tex_sent[0] != nullptr;
    d->SetTextureStageState(0, D3DTSS_COLOROP, tex ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, tex ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    // the stage-state cache no longer matches what the device has
    for (int s = 0; s < 2; s++)
        for (int t = 12; t < 21; t++) g_ts_valid[s][t] = false;
}

uint32_t fvf_stride(uint32_t fvf)
{
    uint32_t n = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ: n = 12; break;
    case D3DFVF_XYZRHW: n = 16; break;
    case D3DFVF_XYZB1: n = 16; break;
    case D3DFVF_XYZB2: n = 20; break;
    case D3DFVF_XYZB3: n = 24; break;
    case D3DFVF_XYZB4: n = 28; break;
    case D3DFVF_XYZB5: n = 32; break;
    }
    if (fvf & D3DFVF_NORMAL) n += 12;
    if (fvf & D3DFVF_PSIZE) n += 4;
    if (fvf & D3DFVF_DIFFUSE) n += 4;
    if (fvf & D3DFVF_SPECULAR) n += 4;
    uint32_t tc = (fvf >> 8) & 0xF;
    for (uint32_t i = 0; i < tc; i++) {
        static const uint32_t sizes[4] = {8, 12, 16, 4};
        n += sizes[(fvf >> (16 + 2 * i)) & 3];
    }
    return n;
}

struct Prim {
    D3DPRIMITIVETYPE type;
    uint32_t count;     // primitives
    bool quads;         // QUADLIST: needs index expansion
};

Prim conv_prim(uint32_t xprim, uint32_t verts)
{
    switch (xprim) {
    case XPT_POINTLIST: return {D3DPT_POINTLIST, verts, false};
    case XPT_LINELIST: return {D3DPT_LINELIST, verts / 2, false};
    case XPT_LINELOOP: case XPT_LINESTRIP: return {D3DPT_LINESTRIP, verts > 1 ? verts - 1 : 0, false};
    case XPT_TRIANGLELIST: return {D3DPT_TRIANGLELIST, verts / 3, false};
    case XPT_TRIANGLESTRIP: case XPT_QUADSTRIP: return {D3DPT_TRIANGLESTRIP, verts > 2 ? verts - 2 : 0, false};
    case XPT_TRIANGLEFAN: case XPT_POLYGON: return {D3DPT_TRIANGLEFAN, verts > 2 ? verts - 2 : 0, false};
    case XPT_QUADLIST: return {D3DPT_TRIANGLELIST, verts / 4 * 2, true};
    default: return {D3DPT_TRIANGLELIST, 0, false};
    }
}

std::vector<uint16_t> g_quad_indices;
std::vector<uint16_t> g_tmp_indices;

const uint16_t *quad_indices(uint32_t quads)
{
    if (g_quad_indices.size() < quads * 6) {
        g_quad_indices.resize(quads * 6);
        for (uint32_t q = 0; q < quads; q++) {
            uint16_t b = (uint16_t)(q * 4);
            uint16_t *p = &g_quad_indices[q * 6];
            p[0] = b; p[1] = b + 1; p[2] = b + 2; p[3] = b; p[4] = b + 2; p[5] = b + 3;
        }
    }
    return g_quad_indices.data();
}

// ---- programmable vertex path: Xbox elements -> one float4 per register ----

// Decode one Xbox D3DVSDT_* element to float4 (missing components default to 0,0,0,1).
void decode_element(uint8_t t, const uint8_t *p, float *o)
{
    o[0] = o[1] = o[2] = 0.0f;
    o[3] = 1.0f;
    uint32_t n = t >> 4;
    auto sext = [](uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); };
    switch (t & 0xF) {
    case 0x2: // FLOATn (0x72 = FLOAT2H: x, y, w)
        if (t == 0x72) {
            memcpy(o, p, 8);
            memcpy(&o[3], p + 8, 4);
        } else if (n >= 1 && n <= 4) {
            memcpy(o, p, n * 4);
        }
        break;
    case 0x0: // D3DCOLOR (ARGB dword) -> RGBA
        if (t == 0x40) {
            uint32_t c;
            memcpy(&c, p, 4);
            o[0] = ((c >> 16) & 255) / 255.0f;
            o[1] = ((c >> 8) & 255) / 255.0f;
            o[2] = (c & 255) / 255.0f;
            o[3] = (c >> 24) / 255.0f;
        }
        break;
    case 0x5: // SHORTn
        for (uint32_t i = 0; i < n; i++) o[i] = (float)((const int16_t *)p)[i];
        break;
    case 0x1: // NORMSHORTn
        for (uint32_t i = 0; i < n; i++) o[i] = ((const int16_t *)p)[i] / 32767.0f;
        break;
    case 0x4: // PBYTEn (unsigned normalized)
        for (uint32_t i = 0; i < n; i++) o[i] = p[i] / 255.0f;
        break;
    case 0x6: { // NORMPACKED3: 11:11:10 signed normalized
        uint32_t v;
        memcpy(&v, p, 4);
        o[0] = sext(v & 0x7FF, 11) / 1023.0f;
        o[1] = sext((v >> 11) & 0x7FF, 11) / 1023.0f;
        o[2] = sext(v >> 22, 10) / 511.0f;
        break;
    }
    }
}

// Decode `count` elements of one type, `sstride` bytes apart, into float4s `ostride` floats apart.
void decode_run(uint8_t t, const uint8_t *p, uint32_t sstride, float *o, uint32_t ostride, uint32_t count)
{
    switch (t) {
    case 0x12: // FLOAT1
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) {
            o[0] = *(const float *)p;
            o[1] = o[2] = 0.0f;
            o[3] = 1.0f;
        }
        return;
    case 0x22: // FLOAT2
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) {
            const float *f = (const float *)p;
            o[0] = f[0];
            o[1] = f[1];
            o[2] = 0.0f;
            o[3] = 1.0f;
        }
        return;
    case 0x32: // FLOAT3
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) {
            const float *f = (const float *)p;
            o[0] = f[0];
            o[1] = f[1];
            o[2] = f[2];
            o[3] = 1.0f;
        }
        return;
    case 0x42: // FLOAT4
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) {
            const float *f = (const float *)p;
            o[0] = f[0];
            o[1] = f[1];
            o[2] = f[2];
            o[3] = f[3];
        }
        return;
    case 0x40: // D3DCOLOR (ARGB) -> RGBA
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) {
            uint32_t c = *(const uint32_t *)p;
            constexpr float k = 1.0f / 255.0f;
            o[0] = ((c >> 16) & 255) * k;
            o[1] = ((c >> 8) & 255) * k;
            o[2] = (c & 255) * k;
            o[3] = (c >> 24) * k;
        }
        return;
    case 0x16: // NORMPACKED3: 11:11:10 signed normalized
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) {
            uint32_t c = *(const uint32_t *)p;
            o[0] = ((int32_t)(c << 21) >> 21) * (1.0f / 1023.0f);
            o[1] = ((int32_t)(c << 10) >> 21) * (1.0f / 1023.0f);
            o[2] = ((int32_t)c >> 22) * (1.0f / 511.0f);
            o[3] = 1.0f;
        }
        return;
    default:
        for (uint32_t v = 0; v < count; v++, p += sstride, o += ostride) decode_element(t, p, o);
    }
}

std::vector<float> g_conv; // only grows: a resize per draw zero-filled it every time
std::unordered_map<const VertexShader *, IDirect3DVertexDeclaration9 *> g_decls;
IDirect3DVertexDeclaration9 *g_decl_imm16; // immediate mode: all 16 attributes

IDirect3DVertexDeclaration9 *decl_for(const VertexShader *vs)
{
    auto it = g_decls.find(vs);
    if (it != g_decls.end()) return it->second;
    D3DVERTEXELEMENT9 el[33];
    uint32_t n = 0, seen = 0;
    for (uint32_t i = 0; i < vs->num_elements; i++) {
        uint8_t reg = vs->elements[i].reg;
        if (seen & (1u << reg)) continue;
        seen |= 1u << reg;
        el[n] = {0, (WORD)(16 * i), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, reg};
        n++;
    }
    el[n] = D3DDECL_END();
    IDirect3DVertexDeclaration9 *d = nullptr;
    if (FAILED(host3d::dev()->CreateVertexDeclaration(el, &d))) logf("[d3d] CreateVertexDeclaration failed for shader %p", vs);
    g_decls[vs] = d;
    return d;
}

IDirect3DVertexDeclaration9 *decl_imm16()
{
    if (!g_decl_imm16) {
        D3DVERTEXELEMENT9 el[17];
        for (BYTE i = 0; i < 16; i++) el[i] = {0, (WORD)(16 * i), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, i};
        el[16] = D3DDECL_END();
        host3d::dev()->CreateVertexDeclaration(el, &g_decl_imm16);
    }
    return g_decl_imm16;
}

// Immediate-mode vertices hold all 16 attribute slots as float4 (256 bytes), but D3D9 vertex strides go up to
// 255: Intel's driver overran its vertex buffer with 256 (a laptop crashed at the start of the first level in
// DrawPrimitiveUP). Draws send only the slots the game wrote or the shader declares, with a declaration per set.
std::unordered_map<uint32_t, IDirect3DVertexDeclaration9 *> g_decl_imm;
std::vector<float> g_imm_pack;

IDirect3DVertexDeclaration9 *decl_imm(uint32_t mask)
{
    IDirect3DVertexDeclaration9 *&d = g_decl_imm[mask];
    if (!d) {
        D3DVERTEXELEMENT9 el[17];
        BYTE n = 0;
        for (BYTE i = 0; i < 16; i++)
            if (mask & (1u << i)) {
                el[n] = {0, (WORD)(16 * n), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, i};
                n++;
            }
        el[n] = D3DDECL_END();
        if (FAILED(host3d::dev()->CreateVertexDeclaration(el, &d))) logf("[d3d] CreateVertexDeclaration failed for immediate mask 0x%x", mask);
    }
    return d;
}

const uint8_t *stream_data(uint32_t stream);

// Convert vertices [first, first+count) of the current streams (or user data for stream 0) for `vs`.
// Returns the stride of the converted vertices (16 bytes per declaration element).
uint32_t convert_vertices(const VertexShader *vs, uint32_t first, uint32_t count, const uint8_t *up, uint32_t up_stride)
{
    perf::Scope perf_scope(perf::kConvert);
    uint32_t ne = vs->num_elements, stride = 16 * ne;
    if (g_conv.size() < (size_t)count * ne * 4) g_conv.resize((size_t)count * ne * 4);
    for (uint32_t e = 0; e < ne; e++) {
        const VertexElement &el = vs->elements[e];
        const uint8_t *base;
        uint32_t s_stride;
        if (up && el.stream == 0) {
            base = up;
            s_stride = up_stride;
        } else {
            base = stream_data(el.stream);
            s_stride = g_st.streams[el.stream].stride ? g_st.streams[el.stream].stride : vs->stream_stride[el.stream];
        }
        float *o = &g_conv[e * 4];
        if (!base) {
            for (uint32_t v = 0; v < count; v++, o += ne * 4) { o[0] = o[1] = o[2] = 0; o[3] = 1; }
            continue;
        }
        decode_run(el.xtype, base + (size_t)first * s_stride + el.offset, s_stride, o, ne * 4, count);
    }
    return stride;
}

// Linear (pitch) textures are addressed with texel coordinates on the NV2A; D3D9 wants 0..1.
void linear_texture_scale(int stage, float *sx, float *sy)
{
    *sx = *sy = 1.0f;
    Resource *r = g_st.textures[stage];
    if (!r) return;
    auto *pc = (PixelContainer *)r;
    if (resource_type(r) != RTYPE_TEXTURE && resource_type(r) != RTYPE_SURFACE) return;
    if (!pc->Size) return; // swizzled: normalized coordinates
    uint32_t w, h, dd, l;
    pixel_container_dims(pc, &w, &h, &dd, &l);
    *sx = 1.0f / (float)w;
    *sy = 1.0f / (float)h;
}

// All 200 registers on every draw (~1.3 ms a frame in busy scenes). Sending only the registers that changed
// since the last upload broke the picture: the device then held values that were never sent to those
// registers (seen with GetVertexShaderConstantF), not explained by the shaders' def constants; not solved yet.
void upload_vs_constants(IDirect3DDevice9 *d)
{
    d->SetVertexShaderConstantF(0, &g_st.vs_constants[0][0], 192);
    float k[36] = {};
    host_viewport_transform(k, k + 4);
    for (int s = 0; s < 4; s++) {
        linear_texture_scale(s, &k[8 + s * 4], &k[9 + s * 4]);
        k[10 + s * 4] = k[11 + s * 4] = 1.0f;
    }
    // c198 fog start/end/density, c199 fog-mode weights (linear, exp, exp2, none)
    const uint32_t *rs = render_state();
    memcpy(&k[24], &rs[XRS_FOGSTART], 4);
    memcpy(&k[25], &rs[XRS_FOGEND], 4);
    memcpy(&k[26], &rs[XRS_FOGDENSITY], 4);
    switch (rs[XRS_FOGTABLEMODE]) {
    case D3DFOG_LINEAR: k[28] = 1.0f; break;
    case D3DFOG_EXP: k[29] = 1.0f; break;
    case D3DFOG_EXP2: k[30] = 1.0f; break;
    default: k[31] = 1.0f; break;
    }
    k[32] = k[33] = fov_viewmodel_scale(); // c200: the first-person gun keeps the game's FOV
    k[34] = k[35] = 1.0f;
    d->SetVertexShaderConstantF(192, k, 9);
}

// Fixed-function path: express the same scaling as a texture transform (when the game sets none).
void apply_fvf_texture_scale(IDirect3DDevice9 *d)
{
    for (int s = 0; s < 4; s++) {
        float sx, sy;
        linear_texture_scale(s, &sx, &sy);
        bool scaled = sx != 1.0f || sy != 1.0f;
        if (texture_state(s)[21] != 0) continue; // game uses its own texture transform
        D3DMATRIX m = {};
        m._11 = sx;
        m._22 = sy;
        m._33 = m._44 = 1.0f;
        d->SetTransform((D3DTRANSFORMSTATETYPE)(D3DTS_TEXTURE0 + s), &m);
        d->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, scaled ? D3DTTFF_COUNT2 : D3DTTFF_DISABLE);
    }
}

enum class Path { None, Fvf, Prog };
struct DrawCtx {
    Path path = Path::None;
    uint32_t fvf = 0;
    VertexShader *vs = nullptr;
};

// --debug-draws N: describe the first N draws (vertex data, constants, key states).
std::atomic<int> g_debug_draws{-1};

// The game code above a draw: return addresses into the XBE's game code found on the stack (values right after a
// call instruction), nearest first. A heuristic, like the profiler's: stale values happen.
std::string game_callers(const uint32_t *sp)
{
    const uint32_t *top = (const uint32_t *)((NT_TIB *)NtCurrentTeb())->StackBase;
    std::string out;
    int found = 0;
    for (const uint32_t *p = sp; p < top && p < sp + 0x800 && found < 5; p++) {
        uint32_t v = *p;
        if (v < 0x00011005 || v >= 0x00269000) continue; // game code (.text and the libraries before D3D)
        const uint8_t *c = (const uint8_t *)(uintptr_t)v;
        bool call = c[-5] == 0xE8 || (c[-2] == 0xFF && (c[-1] & 0x38) == 0x10) || (c[-3] == 0xFF && (c[-2] & 0x38) == 0x10) ||
                    (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10);
        if (!call) continue;
        out += (found++ ? " < " : "") + sym_name(v);
    }
    return out;
}

void debug_draw(const char *what, const DrawCtx &ctx, uint32_t prim, uint32_t count, const float *verts, uint32_t stride_floats)
{
    if (g_debug_draws < 0) g_debug_draws = (int)g_cfg.debug_draws;
    bool this_frame = debug_frame_active();
    if (g_debug_draws <= 0 && !this_frame) return;
    if (!this_frame) g_debug_draws--;
    // frame ranges trace the 2D/full-screen draws only (fades, post passes, HUD), not the world geometry
    if (this_frame && g_cfg.debug_frame_end && !strcmp(what, "DrawIndexedVertices")) return;
    if (this_frame && g_cfg.debug_frame_end) logf("[draw] --- frame %u", g_frame_count.load() + 1);
    const uint32_t *rs = render_state();
    logf("[draw] %s prim %u count %u, %s 0x%x, ps 0x%x, tex0 %p", what, prim, count, ctx.path == Path::Prog ? "vs" : "fvf",
         g_st.vertex_shader, g_st.pixel_shader, g_st.textures[0]);
    if (this_frame) logf("[draw]   from %s", game_callers((const uint32_t *)_AddressOfReturnAddress()).c_str());
    logf("[draw]   z %u func 0x%x write %u | blend %u 0x%x/0x%x op 0x%x | atest %u func 0x%x ref %u | cull 0x%x | cwrite 0x%x | stencil %u",
         rs[XRS_ZENABLE], rs[XRS_ZFUNC], rs[XRS_ZWRITEENABLE], rs[XRS_ALPHABLENDENABLE], rs[XRS_SRCBLEND], rs[XRS_DESTBLEND],
         rs[XRS_BLENDOP], rs[XRS_ALPHATESTENABLE], rs[XRS_ALPHAFUNC], rs[XRS_ALPHAREF], rs[XRS_CULLMODE], rs[XRS_COLORWRITEENABLE],
         rs[XRS_STENCILENABLE]);
    if (g_st.pixel_shader) { // the combiner program as it is in render states 0-56
        uint32_t n = rs[53] & 0xFF;
        logf("[draw]   psh: combiners 0x%x texmodes 0x%x final 0x%08x 0x%08x c0 0x%08x c1 0x%08x fc0 0x%08x fc1 0x%08x", rs[53],
             rs[XRS_PSTEXTUREMODES], rs[8], rs[9], rs[10], rs[18], rs[43], rs[44]);
        for (uint32_t i = 0; i < n && i < 8; i++)
            logf("[draw]   psh stage %u: rgb in 0x%08x out 0x%08x | alpha in 0x%08x out 0x%08x", i, rs[34 + i], rs[45 + i], rs[i], rs[26 + i]);
    }
    if (!g_st.pixel_shader)
        for (int s = 0; s < 2; s++) {
            const uint32_t *ts = texture_state(s);
            logf("[draw]   stage %d: color op %u args 0x%x 0x%x | alpha op %u args 0x%x 0x%x | tfactor 0x%08x", s, ts[12], ts[14], ts[15],
                 ts[16], ts[18], ts[19], rs[XRS_TEXTUREFACTOR]);
        }
    float vp[8];
    viewport_transform(vp, vp + 4);
    logf("[draw]   viewport scale %g %g %g offset %g %g %g; c58 %g %g %g %g; c59 %g %g %g %g", vp[0], vp[1], vp[2], vp[4], vp[5],
         vp[6], g_st.vs_constants[58][0], g_st.vs_constants[58][1], g_st.vs_constants[58][2], g_st.vs_constants[58][3],
         g_st.vs_constants[59][0], g_st.vs_constants[59][1], g_st.vs_constants[59][2], g_st.vs_constants[59][3]);
    if (ctx.path == Path::Prog) {
        for (int c : {96, 97, 190, 191, 0, 1, 2, 3, 10, 11, 12, 13, 18, 21, 22, 23, 24, 25})
            logf("[draw]   c%d (slot %d) = %g %g %g %g", c - 96, c, g_st.vs_constants[c][0], g_st.vs_constants[c][1],
                 g_st.vs_constants[c][2], g_st.vs_constants[c][3]);
    }
    if (verts && stride_floats == 64) // immediate mode: position and texture coordinates (attributes 0, 9-12)
        for (uint32_t v = 0; v < count && v < 4; v++) {
            const float *a = verts + v * 64;
            logf("[draw]   v%u: pos %g %g %g %g | t0 %g %g %g %g | t1 %g %g %g %g | t2 %g %g %g %g", v, a[0], a[1], a[2], a[3], a[36],
                 a[37], a[38], a[39], a[40], a[41], a[42], a[43], a[44], a[45], a[46], a[47]);
        }
    else if (verts)
        for (uint32_t v = 0; v < count && v < 3; v++) {
            std::string s;
            for (uint32_t f = 0; f < stride_floats && f < 16; f++) {
                char b[24];
                snprintf(b, sizeof b, "%s%g", (f && f % 4 == 0) ? " | " : (f ? " " : ""), verts[v * stride_floats + f]);
                s += b;
            }
            logf("[draw]   v%u: %s", v, s.c_str());
        }
}

// Set up state for a draw. Returns false (and counts a skipped draw) if it cannot be drawn.
// The shaders and vertex input last sent to the device. Drivers do not all filter repeats, and Intel's spent
// ~2 ms a frame on them. Fullscreen passes change these and restore them with a state block, so these stay valid.
IDirect3DPixelShader9 *g_ps_sent = (IDirect3DPixelShader9 *)~0u;
IDirect3DVertexShader9 *g_vs_sent = (IDirect3DVertexShader9 *)~0u;
IDirect3DVertexDeclaration9 *g_decl_sent; // null after SetFVF
DWORD g_fvf_sent = ~0u;                   // ~0 after SetVertexDeclaration
DWORD g_fog_table_sent = ~0u;

void set_ps(IDirect3DDevice9 *d, IDirect3DPixelShader9 *ps)
{
    if (ps != g_ps_sent) d->SetPixelShader(g_ps_sent = ps);
}

void set_vs(IDirect3DDevice9 *d, IDirect3DVertexShader9 *vs)
{
    if (vs != g_vs_sent) d->SetVertexShader(g_vs_sent = vs);
}

void set_fvf(IDirect3DDevice9 *d, DWORD fvf)
{
    if (fvf == g_fvf_sent && !g_decl_sent) return;
    d->SetFVF(g_fvf_sent = fvf);
    g_decl_sent = nullptr;
}

void set_decl(IDirect3DDevice9 *d, IDirect3DVertexDeclaration9 *decl)
{
    if (decl == g_decl_sent) return;
    d->SetVertexDeclaration(g_decl_sent = decl);
    g_fvf_sent = ~0u;
}

} // namespace

// After a fullscreen pass: set everything again at the next draw instead of trusting the state block's restore
// (a laptop with Intel graphics crashed in a vertex copy inside D3D9 after the cache was added).
void forget_sent_shaders()
{
    g_ps_sent = (IDirect3DPixelShader9 *)~0u;
    g_vs_sent = (IDirect3DVertexShader9 *)~0u;
    g_decl_sent = nullptr;
    g_fvf_sent = ~0u;
}

void forget_sent_state()
{
    forget_sent_shaders();
    memset(g_rs_valid, 0, sizeof g_rs_valid);
    memset(g_ts_valid, 0, sizeof g_ts_valid);
    for (IDirect3DBaseTexture9 *&t : g_tex_sent) t = nullptr; // Reset unbinds every texture
    g_fog_table_sent = ~0u;
}

namespace {

bool begin_draw(DrawCtx &ctx)
{
    IDirect3DDevice9 *d = host3d::dev();
    if (!d) return false;
    g_draw_serial++;
    apply_render_states(d);
    bool ps = g_st.pixel_shader != 0;
    apply_texture_stages(d, !ps); // may copy a render target (outside the scene) first
    host3d::begin_scene();
    IDirect3DPixelShader9 *hps = ps ? ps_host_shader() : nullptr;
    set_ps(d, hps);
    if (hps) upload_ps_constants(d);
    else if (ps) apply_pixel_shader_fallback(d);
    uint32_t h = g_st.vertex_shader;
    if (vs_is_fvf(h)) {
        if (h) {
            set_vs(d, nullptr);
            set_fvf(d, h);
            apply_fvf_texture_scale(d);
            ctx.path = Path::Fvf;
            ctx.fvf = h;
        }
    } else if (VertexShader *vs = vs_from_handle(h)) {
        if (IDirect3DVertexShader9 *hv = vs_host_shader(vs)) {
            set_vs(d, hv);
            upload_vs_constants(d);
            ctx.path = Path::Prog;
            ctx.vs = vs;
        }
    }
    if (ctx.path == Path::None) {
        g_st.draws_skipped++;
        return false;
    }
    // Vertex shaders compute the fog factor themselves (oFog -> table function): D3D9 vertex fog.
    DWORD fog_table = ctx.path == Path::Prog ? D3DFOG_NONE : render_state()[XRS_FOGTABLEMODE];
    if (fog_table != g_fog_table_sent) {
        d->SetRenderState(D3DRS_FOGTABLEMODE, fog_table);
        g_fog_table_sent = fog_table;
    }
    g_st.draws++;
    return true;
}

void draw_up(uint32_t xprim, uint32_t verts, const void *data, uint32_t stride)
{
    Prim p = conv_prim(xprim, verts);
    if (!p.count) return;
    perf::add_draw(verts, verts * stride);
    IDirect3DDevice9 *d = host3d::dev();
    if (p.quads)
        d->DrawIndexedPrimitiveUP(p.type, 0, verts, p.count, quad_indices(verts / 4), D3DFMT_INDEX16, data, stride);
    else
        d->DrawPrimitiveUP(p.type, p.count, data, stride);
}

void draw_indexed_up(uint32_t xprim, uint32_t nidx, const uint16_t *idx, const void *verts, uint32_t stride)
{
    if (!nidx) return;
    uint32_t mn = 0xFFFF, mx = 0;
    for (uint32_t i = 0; i < nidx; i++) {
        if (idx[i] < mn) mn = idx[i];
        if (idx[i] > mx) mx = idx[i];
    }
    Prim p = conv_prim(xprim, nidx);
    if (!p.count) return;
    perf::add_draw(mx - mn + 1, (mx - mn + 1) * stride);
    if (p.quads) {
        g_tmp_indices.resize(p.count * 3);
        for (uint32_t q = 0; q < nidx / 4; q++) {
            const uint16_t *s = idx + q * 4;
            uint16_t *o = &g_tmp_indices[q * 6];
            o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[0]; o[4] = s[2]; o[5] = s[3];
        }
        idx = g_tmp_indices.data();
    }
    host3d::dev()->DrawIndexedPrimitiveUP(p.type, mn, mx - mn + 1, p.count, idx, D3DFMT_INDEX16, verts, stride);
}

const uint8_t *stream_data(uint32_t stream)
{
    VertexBuffer *vb = g_st.streams[stream].vb;
    return vb ? (const uint8_t *)phys_to_virt(vb->Data) : nullptr;
}

} // namespace

// Pre-transformed (XYZRHW) vertices carry Xbox screen pixels, but host render targets are scale_x/y() times
// larger: copy vertices [first, first+count) with x/y scaled. Returns a pointer usable with the original
// indices (the copy offset by -first vertices); other paths get `data` back unchanged.
std::vector<uint8_t> g_rhw;
const uint8_t *scale_rhw(const DrawCtx &ctx, const void *data, uint32_t first, uint32_t count, uint32_t stride)
{
    auto *src = (const uint8_t *)data;
    float sx = target_scale_x(), sy = target_scale_y();
    if (ctx.path != Path::Fvf || (ctx.fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZRHW || (sx == 1 && sy == 1) || !src) return src;
    perf::Scope perf_scope(perf::kConvert);
    g_rhw.assign(src + (size_t)first * stride, src + (size_t)(first + count) * stride);
    for (uint32_t v = 0; v < count; v++) {
        float *p = (float *)&g_rhw[(size_t)v * stride];
        p[0] *= sx;
        p[1] *= sy;
    }
    return g_rhw.data() - (size_t)first * stride;
}

// ---- HLE draw functions ----

void __stdcall D3DDevice_DrawVerticesUP(uint32_t PrimitiveType, uint32_t VertexCount, const void *pVertexStreamZeroData,
                                        uint32_t VertexStreamZeroStride)
{
    DTRACE("%u, %u, %p, %u", PrimitiveType, VertexCount, pVertexStreamZeroData, VertexStreamZeroStride);
    pb_flush();
    perf::Scope perf_scope(perf::kDraw);
    DrawCtx ctx;
    if (!begin_draw(ctx)) return;
    if (ctx.path == Path::Prog) {
        uint32_t stride = convert_vertices(ctx.vs, 0, VertexCount, (const uint8_t *)pVertexStreamZeroData, VertexStreamZeroStride);
        debug_draw("DrawVerticesUP", ctx, PrimitiveType, VertexCount, g_conv.data(), stride / 4);
        set_decl(host3d::dev(), decl_for(ctx.vs));
        draw_up(PrimitiveType, VertexCount, g_conv.data(), stride);
        return;
    }
    debug_draw("DrawVerticesUP", ctx, PrimitiveType, VertexCount, (const float *)pVertexStreamZeroData, VertexStreamZeroStride / 4);
    draw_up(PrimitiveType, VertexCount, scale_rhw(ctx, pVertexStreamZeroData, 0, VertexCount, VertexStreamZeroStride),
            VertexStreamZeroStride);
}

void __stdcall D3DDevice_DrawVertices(uint32_t PrimitiveType, uint32_t StartVertex, uint32_t VertexCount)
{
    DTRACE("%u, %u, %u", PrimitiveType, StartVertex, VertexCount);
    pb_flush();
    perf::Scope perf_scope(perf::kDraw);
    DrawCtx ctx;
    if (!begin_draw(ctx)) return;
    if (ctx.path == Path::Prog) {
        uint32_t stride = convert_vertices(ctx.vs, StartVertex, VertexCount, nullptr, 0);
        debug_draw("DrawVertices", ctx, PrimitiveType, VertexCount, g_conv.data(), stride / 4);
        set_decl(host3d::dev(), decl_for(ctx.vs));
        draw_up(PrimitiveType, VertexCount, g_conv.data(), stride);
        return;
    }
    debug_draw("DrawVertices", ctx, PrimitiveType, VertexCount, nullptr, 0);
    const uint8_t *base = stream_data(0);
    uint32_t stride = g_st.streams[0].stride;
    if (!base || !stride) return;
    draw_up(PrimitiveType, VertexCount, scale_rhw(ctx, base, StartVertex, VertexCount, stride) + (size_t)StartVertex * stride, stride);
}

// --debug-frame: for indexed draws whose declaration has a packed normal (NORMPACKED3), write indices,
// positions and the raw normal dwords to a CSV, to check the normal decoding against the geometry.
void dump_packed_normal_geometry(const VertexShader *vs, uint32_t prim, uint32_t count, const uint16_t *idx, uint32_t mn, uint32_t mx)
{
    const VertexElement *pos = nullptr, *nrm = nullptr;
    for (uint32_t i = 0; i < vs->num_elements; i++) {
        if (vs->elements[i].reg == 0) pos = &vs->elements[i];
        if (vs->elements[i].xtype == 0x16) nrm = &vs->elements[i];
    }
    if (!pos || !nrm || pos->xtype != 0x32 || g_cfg.screenshot_dir.empty()) return;
    char name[64];
    snprintf(name, sizeof name, "\\f%u_d%u_geom.csv", g_frame_count.load() + 1, g_draw_serial);
    FILE *f = _wfopen((g_cfg.screenshot_dir + widen(name)).c_str(), L"w");
    if (!f) return;
    fprintf(f, "prim,%u\n", prim);
    for (uint32_t i = 0; i < count && i < 3000; i++) fprintf(f, "i,%u\n", idx[i]);
    auto data = [&](const VertexElement *e) {
        const uint8_t *b = stream_data(e->stream);
        uint32_t st = g_st.streams[e->stream].stride ? g_st.streams[e->stream].stride : vs->stream_stride[e->stream];
        return std::make_pair(b, st);
    };
    auto [pb, ps] = data(pos);
    auto [nb, ns] = data(nrm);
    if (pb && nb)
        for (uint32_t v = mn; v <= mx && v < mn + 4000; v++) {
            float p[3];
            uint32_t n;
            memcpy(p, pb + (size_t)v * ps + pos->offset, 12);
            memcpy(&n, nb + (size_t)v * ns + nrm->offset, 4);
            fprintf(f, "v,%u,%g,%g,%g,0x%08x\n", v, p[0], p[1], p[2], n);
        }
    fclose(f);
}

void __stdcall D3DDevice_DrawIndexedVertices(uint32_t PrimitiveType, uint32_t VertexCount, const uint16_t *pIndexData)
{
    DTRACE("%u, %u, %p", PrimitiveType, VertexCount, pIndexData);
    pb_flush();
    perf::Scope perf_scope(perf::kDraw);
    DrawCtx ctx;
    if (!begin_draw(ctx) || !pIndexData || !VertexCount) return;
    if (ctx.path == Path::Prog) {
        uint32_t mn = 0xFFFF, mx = 0;
        for (uint32_t i = 0; i < VertexCount; i++) {
            if (pIndexData[i] < mn) mn = pIndexData[i];
            if (pIndexData[i] > mx) mx = pIndexData[i];
        }
        uint32_t stride = convert_vertices(ctx.vs, mn, mx - mn + 1, nullptr, 0);
        debug_draw("DrawIndexedVertices", ctx, PrimitiveType, VertexCount, g_conv.data(), stride / 4);
        if (debug_frame_active() && !g_cfg.debug_frame_end) // single debug frames only (ranges stay fast)
            dump_packed_normal_geometry(ctx.vs, PrimitiveType, VertexCount, pIndexData, mn, mx);
        set_decl(host3d::dev(), decl_for(ctx.vs));
        // indices refer to vertex 0: point the data "mn vertices before" the converted block
        draw_indexed_up(PrimitiveType, VertexCount, pIndexData, (const uint8_t *)g_conv.data() - (size_t)mn * stride, stride);
        return;
    }
    const uint8_t *base = stream_data(0);
    uint32_t stride = g_st.streams[0].stride;
    if (!base || !stride) return;
    if (ctx.path == Path::Fvf && (ctx.fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        uint32_t mn = 0xFFFF, mx = 0;
        for (uint32_t i = 0; i < VertexCount; i++) {
            mn = pIndexData[i] < mn ? pIndexData[i] : mn;
            mx = pIndexData[i] > mx ? pIndexData[i] : mx;
        }
        base = scale_rhw(ctx, base, mn, mx - mn + 1, stride);
    }
    draw_indexed_up(PrimitiveType, VertexCount, pIndexData, base, stride);
}

// ---- push-buffer draws (immediate mode / inline arrays) ----

void draw_immediate(uint32_t prim, const float *verts, uint32_t count, uint32_t attr_mask)
{
    perf::Scope perf_scope(perf::kDraw);
    DrawCtx ctx;
    if (!begin_draw(ctx)) return;
    if (ctx.path == Path::Prog) { // the 16 attribute slots are exactly the shader's v0-v15
        debug_draw("immediate", ctx, prim, count, verts, 64);
        uint32_t mask = (attr_mask | vs_input_mask(ctx.vs) | 1u) & 0xFFFF, n = 0;
        uint8_t slot[16];
        for (uint32_t i = 0; i < 16; i++)
            if (mask & (1u << i)) slot[n++] = (uint8_t)i;
        if (n == 16) { // every slot: no smaller layout (not seen so far)
            KNOTE_ONCE("immediate-mode draw with all 16 attributes (256-byte vertices)");
            set_decl(host3d::dev(), decl_imm16());
            draw_up(prim, count, verts, 256);
            return;
        }
        KNOTE_ONCE("immediate-mode draws send %u of 16 attributes (mask 0x%x, %u-byte vertices)", n, mask, n * 16);
        if (g_imm_pack.size() < (size_t)count * n * 4) g_imm_pack.resize((size_t)count * n * 4);
        float *o = g_imm_pack.data();
        for (uint32_t v = 0; v < count; v++)
            for (uint32_t k = 0; k < n; k++, o += 4) memcpy(o, verts + v * 64 + slot[k] * 4, 16);
        set_decl(host3d::dev(), decl_imm(mask));
        draw_up(prim, count, g_imm_pack.data(), n * 16);
        return;
    }
    uint32_t fvf = ctx.fvf;
    // Rebuild the vertices in the current FVF layout from the 16 attribute slots
    // (0 position, 2 normal, 3 diffuse, 4 specular, 9.. texture coordinates).
    uint32_t stride = fvf_stride(fvf);
    std::vector<uint8_t> buf(stride * count);
    for (uint32_t v = 0; v < count; v++) {
        const float(*a)[4] = (const float(*)[4])(verts + v * 64);
        uint8_t *o = &buf[v * stride];
        auto putf = [&](float f) { memcpy(o, &f, 4); o += 4; };
        auto putc = [&](const float *c) {
            auto b = [](float x) { int i = (int)(x * 255.0f + 0.5f); return (uint32_t)(i < 0 ? 0 : i > 255 ? 255 : i); };
            uint32_t col = b(c[3]) << 24 | b(c[0]) << 16 | b(c[1]) << 8 | b(c[2]);
            memcpy(o, &col, 4);
            o += 4;
        };
        switch (fvf & D3DFVF_POSITION_MASK) {
        case D3DFVF_XYZRHW: // Xbox screen pixels -> render scale
            putf(a[0][0] * target_scale_x()); putf(a[0][1] * target_scale_y()); putf(a[0][2]); putf(a[0][3]);
            break;
        default: putf(a[0][0]); putf(a[0][1]); putf(a[0][2]); break;
        }
        if (fvf & D3DFVF_NORMAL) { putf(a[2][0]); putf(a[2][1]); putf(a[2][2]); }
        if (fvf & D3DFVF_DIFFUSE) putc(a[3]);
        if (fvf & D3DFVF_SPECULAR) putc(a[4]);
        uint32_t tc = (fvf >> 8) & 0xF;
        for (uint32_t i = 0; i < tc; i++) {
            static const uint32_t n[4] = {2, 3, 4, 1};
            uint32_t k = n[(fvf >> (16 + 2 * i)) & 3];
            for (uint32_t c = 0; c < k; c++) putf(a[9 + i][c]);
        }
    }
    draw_up(prim, count, buf.data(), stride);
}

void draw_inline_array(uint32_t prim, const uint32_t *data, uint32_t dwords)
{
    perf::Scope perf_scope(perf::kDraw);
    DrawCtx ctx;
    if (!begin_draw(ctx)) return;
    if (ctx.path == Path::Prog) { // inline data uses the stream-0 layout of the declaration
        uint32_t in_stride = ctx.vs->stream_stride[0];
        if (!in_stride) return;
        uint32_t count = dwords * 4 / in_stride;
        uint32_t stride = convert_vertices(ctx.vs, 0, count, (const uint8_t *)data, in_stride);
        set_decl(host3d::dev(), decl_for(ctx.vs));
        draw_up(prim, count, g_conv.data(), stride);
        return;
    }
    uint32_t stride = fvf_stride(ctx.fvf);
    if (!stride) return;
    draw_up(prim, dwords * 4 / stride, scale_rhw(ctx, data, 0, dwords * 4 / stride, stride), stride);
}

// ---- screenshots ----

void screenshot_now(const char *tag)
{
    if (g_cfg.screenshot_dir.empty() || !host3d::dev()) return;
    CreateDirectoryW(g_cfg.screenshot_dir.c_str(), nullptr);
    std::wstring path = g_cfg.screenshot_dir + L"\\" + widen(tag) + L".png";
    bool ok = save_screenshot(path.c_str());
    logf("screenshot %s: %s", narrow(path).c_str(), ok ? "saved" : "FAILED");
}

bool save_screenshot(const wchar_t *path)
{
    IDirect3DDevice9 *d = host3d::dev();
    if (!d) return false;
    IDirect3DSurface9 *bb = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return false;
    RECT frame, pic; // the picture as shown in the window, without black bars
    host3d::frame_layout(&frame, &pic);
    bool ok = save_surface_png(bb, path, false, &pic);
    bb->Release();
    return ok;
}

// A render target surface (32-bit) as PNG: colour (opaque), or with alpha_as_gray its alpha channel;
// only the `crop` rectangle when given.
bool save_surface_png(IDirect3DSurface9 *src, const wchar_t *path, bool alpha_as_gray, const RECT *crop)
{
    IDirect3DDevice9 *d = host3d::dev();
    IDirect3DSurface9 *sys = nullptr;
    D3DSURFACE_DESC desc;
    src->GetDesc(&desc);
    D3DFORMAT format = desc.Format;
    UINT full_w = desc.Width, full_h = desc.Height;
    if (crop) {
        desc.Width = crop->right - crop->left;
        desc.Height = crop->bottom - crop->top;
    }
    bool ok = false;
    if (SUCCEEDED(d->CreateOffscreenPlainSurface(full_w, full_h, format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
        SUCCEEDED(d->GetRenderTargetData(src, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, crop, D3DLOCK_READONLY))) {
            std::vector<uint8_t> px(desc.Width * desc.Height * 4);
            for (UINT y = 0; y < desc.Height; y++) {
                memcpy(&px[y * desc.Width * 4], (uint8_t *)lr.pBits + y * lr.Pitch, desc.Width * 4);
                for (UINT x = 0; x < desc.Width; x++) {
                    uint8_t *p = &px[(y * desc.Width + x) * 4];
                    if (alpha_as_gray) p[0] = p[1] = p[2] = p[3];
                    p[3] = 0xFF;
                }
            }
            sys->UnlockRect();
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            IWICImagingFactory *f = nullptr;
            IWICStream *st = nullptr;
            IWICBitmapEncoder *enc = nullptr;
            IWICBitmapFrameEncode *fr = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))) &&
                SUCCEEDED(f->CreateStream(&st)) && SUCCEEDED(st->InitializeFromFilename(path, GENERIC_WRITE)) &&
                SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                SUCCEEDED(enc->Initialize(st, WICBitmapEncoderNoCache)) && SUCCEEDED(enc->CreateNewFrame(&fr, nullptr)) &&
                SUCCEEDED(fr->Initialize(nullptr)) && SUCCEEDED(fr->SetSize(desc.Width, desc.Height))) {
                WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA;
                fr->SetPixelFormat(&pf);
                ok = SUCCEEDED(fr->WritePixels(desc.Height, desc.Width * 4, (UINT)px.size(), px.data())) &&
                     SUCCEEDED(fr->Commit()) && SUCCEEDED(enc->Commit());
            }
            if (fr) fr->Release();
            if (enc) enc->Release();
            if (st) st->Release();
            if (f) f->Release();
        }
    }
    if (sys) sys->Release();
    return ok;
}

// ---- table ----

#define H(addr, fn, params, ret) {addr, #fn, (void *)&fn, params, ret}

static const Hle kTable[] = {
    H(0x00269c90, D3DDevice_DrawVerticesUP, "s s s s", false),
    H(0x00269e40, D3DDevice_DrawVertices, "s s s", false),
    H(0x00269f10, D3DDevice_DrawIndexedVertices, "s s s", false),
};

const Hle *hle_draw_table(size_t *count)
{
    *count = sizeof kTable / sizeof kTable[0];
    return kTable;
}

} // namespace xd3d
