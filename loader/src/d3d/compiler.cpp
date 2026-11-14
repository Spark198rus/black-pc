// The HLSL compiler, found at run time (Windows 8 and newer): Windows 8.1 and 10 include d3dcompiler_47.dll, Windows
// 8.0 does not, so linking it stopped the port before it started there. Loading it lets the port also take an older
// d3dcompiler (46, or 43 from the DirectX End-User Runtime of June 2010; the port compiles vs_3_0 / ps_3_0, which
// they all support) and say what is missing instead of Windows' "DLL not found".
#include "d3d/compiler.h"

#include "common.h"

namespace xd3d {
namespace {

pD3DCompile g_compile;
const char *g_name;

} // namespace

bool compiler_load()
{
    if (g_compile) return true;
    for (const char *name : {"d3dcompiler_47.dll", "d3dcompiler_46.dll", "d3dcompiler_43.dll"}) {
        HMODULE m = LoadLibraryA(name);
        if (!m) continue;
        if ((g_compile = (pD3DCompile)GetProcAddress(m, "D3DCompile"))) {
            g_name = name;
            logf("[d3d] HLSL compiler: %s", name);
            return true;
        }
    }
    return false;
}

HRESULT compile(LPCVOID src, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines, ID3DInclude *include, LPCSTR entry,
                LPCSTR target, UINT flags1, UINT flags2, ID3DBlob **code, ID3DBlob **errors)
{
    if (!g_compile && !compiler_load()) return E_NOINTERFACE;
    return g_compile(src, size, name, defines, include, entry, target, flags1, flags2, code, errors);
}

} // namespace xd3d
