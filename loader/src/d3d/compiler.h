// The HLSL compiler (d3dcompiler_47, or an older one), loaded at run time (d3d/compiler.cpp).
#pragma once

#include <d3dcompiler.h>

namespace xd3d {

bool compiler_load(); // false: no d3dcompiler found (the port cannot draw)
// D3DCompile through the loaded compiler; E_NOINTERFACE without one.
HRESULT compile(LPCVOID src, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines, ID3DInclude *include, LPCSTR entry,
                LPCSTR target, UINT flags1, UINT flags2, ID3DBlob **code, ID3DBlob **errors);

} // namespace xd3d
