// HLE-side graphics state shared by the d3d_*.cpp files. Render and texture-stage states stay in the
// Xbox arrays (render_state()/texture_state()) because game code writes them directly; everything else
// the Xbox D3D keeps internally lives here.
#pragma once

#include "d3d/xd3d.h"

#include <d3d9.h>

namespace xd3d {

struct VertexElement {
    uint8_t stream, reg, xtype; // Xbox D3DVSDT_* type
    uint16_t offset;            // byte offset in the stream
};

// Vertex shader object handed to the game as (pointer | 1). FVF handles have bit 0 clear.
struct VertexShader {
    uint32_t magic; // 'XVSH'
    uint32_t usage;
    VertexElement elements[32];
    uint32_t num_elements;
    uint32_t stream_stride[16];   // implied by the declaration (when not given by SetStreamSource)
    uint32_t *program;            // NV2A microcode (4 dwords per instruction), copied
    uint32_t program_insts;
    uint32_t program_type;        // 0x2078 normal, 0x6378 read/write, 0x7778 state
    IDirect3DVertexShader9 *host_vs; // translated program, or null (not translated yet)
    bool translate_failed;
};
constexpr uint32_t VSH_MAGIC = 0x48535658;

inline bool vs_is_fvf(uint32_t handle) { return (handle & 1) == 0; }
inline VertexShader *vs_from_handle(uint32_t handle)
{
    auto *v = (VertexShader *)(uintptr_t)(handle & ~1u);
    return (v && v->magic == VSH_MAGIC) ? v : nullptr;
}

struct Stream {
    VertexBuffer *vb;
    uint32_t stride;
};

struct HleState {
    uint32_t vertex_shader = 0;       // handle as the game set it
    Stream streams[16] = {};
    Resource *textures[4] = {};
    Palette *palettes[4] = {};
    // Vertex shader constants, slots 0-191 (Xbox c-96..c95). Kept in the library's own shadow array at
    // 0x27C2A8, which SetVertexShaderConstant* fill (the register passed is already the slot).
    float (*vs_constants)[4] = (float (*)[4])(uintptr_t)0x0027C2A8;
    float ps_constants[16][4] = {};
    uint32_t pixel_shader = 0;        // handle as the game set it
    D3DMATRIX transforms[10] = {};    // Xbox D3DTS_*: VIEW 0, PROJECTION 1, TEXTURE0-3 2-5, WORLD 6-9
    Viewport viewport = {0, 0, 640, 480, 0.0f, 1.0f};
    Surface *render_target = nullptr;
    Surface *depth_stencil = nullptr;
    uint32_t draws = 0, draws_skipped = 0;
};
extern HleState g_st;

// ---- conversions (Xbox/NV2A enum values -> D3D9) ----
D3DCMPFUNC conv_cmp(uint32_t v);
D3DBLEND conv_blend(uint32_t v);
D3DBLENDOP conv_blendop(uint32_t v);
D3DSTENCILOP conv_stencilop(uint32_t v);
D3DTEXTUREOP conv_texop(uint32_t v);
D3DTEXTUREADDRESS conv_address(uint32_t v);
DWORD conv_colorwrite(uint32_t v);

// ---- textures (texture.cpp) ----
IDirect3DBaseTexture9 *host_texture(Resource *r, Palette *pal); // upload/cached host copy
void texture_mark_dirty(const Resource *r);
void texture_forget(const Resource *r);
uint32_t texture_level_offset(const PixelContainer *pc, uint32_t face, uint32_t level, uint32_t *pitch, uint32_t *w,
                              uint32_t *h);
void pixel_container_dims(const PixelContainer *pc, uint32_t *w, uint32_t *h, uint32_t *d, uint32_t *levels);

// ---- draws (draw.cpp) ----
void init_default_states(); // what the original CreateDevice puts in the state arrays
bool debug_frame_active();  // --debug-frame N: true while frame N is being built

// ---- device (device.cpp) ----
void viewport_transform(float *scale, float *offset); // NV2A screen = clip / w * scale + offset
void host_viewport_transform(float *scale, float *offset); // the same for the host viewport (undoing screen space)
void update_viewport_constants();                     // c-38 / c-37 = viewport scale / offset

} // namespace xd3d
