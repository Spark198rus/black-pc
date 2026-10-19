// Draw paths: immediate mode and inline arrays from the push buffer, plus the HLE Draw* functions.
#pragma once

#include "common.h"

#include <d3d9.h>

namespace xd3d {

// verts: count vertices of 16 attributes x float4; attr_mask: attributes written since SET_BEGIN_END.
void draw_immediate(uint32_t prim, const float *verts, uint32_t count, uint32_t attr_mask);
// dwords of vertex data in the current vertex format.
void draw_inline_array(uint32_t prim, const uint32_t *data, uint32_t dwords);

// The shaders / vertex input the draws remember having sent are unknown again (after a fullscreen pass).
void forget_sent_shaders();
// After a device Reset: Direct3D is back at its defaults, so every state, texture and shader the draws remember
// having sent is unknown again.
void forget_sent_state();

// Save the host back buffer as a PNG (used by --screenshot).
bool save_screenshot(const wchar_t *path);
// A 32-bit render target surface as PNG: colour, or with alpha_as_gray its alpha channel.
bool save_surface_png(IDirect3DSurface9 *src, const wchar_t *path, bool alpha_as_gray, const RECT *crop = nullptr);
// --screenshot <dir>: save "<dir>\<tag>.png" now (no-op without the option).
void screenshot_now(const char *tag);
void request_screenshot(const char *tag); // tests: "<dir>\<tag>.png" of the next whole frame (any thread)
void request_device_reset();              // tests: at the next Swap, Reset the device as after a device loss

} // namespace xd3d
