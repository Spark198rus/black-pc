// Xbox pixel shaders on the host. The combiner program lives in render states 0-56 plus PSTextureModes
// (136): SetPixelShader copies the D3DPIXELSHADERDEF there, SetPixelShaderConstant and game code patch it.
#pragma once

#include <d3d9.h>

#include <cstdint>

namespace xd3d {

struct PixelShaderDef;

// Host translation of the program currently in the render states (cached by content), or null.
IDirect3DPixelShader9 *ps_host_shader();
// c0-c17 combiner constants, c18 fog color, c19-c22 bump-env matrices.
void upload_ps_constants(IDirect3DDevice9 *d);
// What the library's SetPixelShaderConstant does: pack to ARGB8 and route through the shader's
// PSC0/PSC1/final-combiner mappings into the PSCONSTANT render states.
void ps_set_constant(const PixelShaderDef *def, uint32_t reg, const float *v);

} // namespace xd3d
