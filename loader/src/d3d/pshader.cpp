#include "d3d/pshader.h"

#include "d3d/host.h"
#include "perf.h"
#include "d3d/nv2a_psh.h"
#include "d3d/state.h"

#include <d3dcompiler.h>

#include <cstring>
#include <string>
#include <unordered_map>

namespace xd3d {
namespace {

std::unordered_map<std::string, IDirect3DPixelShader9 *> g_cache; // key: program dwords (no constants) + cube mask

uint32_t cube_mask()
{
    uint32_t m = 0;
    for (int s = 0; s < 4; s++) {
        Resource *r = g_st.textures[s];
        if (r && resource_type(r) == RTYPE_TEXTURE && (((PixelContainer *)r)->Format & FMT_CUBEMAP)) m |= 1u << s;
    }
    return m;
}

uint32_t pack_color(const float *v)
{
    auto c = [](float f) {
        f = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
        return (uint32_t)(f * 255.0f + 0.5f);
    };
    return (c(v[3]) << 24) | (c(v[0]) << 16) | (c(v[1]) << 8) | c(v[2]);
}

void unpack_color(uint32_t c, float *o)
{
    o[0] = ((c >> 16) & 255) / 255.0f;
    o[1] = ((c >> 8) & 255) / 255.0f;
    o[2] = (c & 255) / 255.0f;
    o[3] = (c >> 24) / 255.0f;
}

void log_def(const PixelShaderDef &d)
{
    logf("[psh] combiners 0x%x texmodes 0x%x dotmap 0x%x inputtex 0x%x compare 0x%x final 0x%08x 0x%08x", d.PSCombinerCount,
         d.PSTextureModes, d.PSDotMapping, d.PSInputTexture, d.PSCompareMode, d.PSFinalCombinerInputsABCD, d.PSFinalCombinerInputsEFG);
    for (uint32_t i = 0; i < (d.PSCombinerCount & 0xFF) && i < 8; i++)
        logf("[psh]   stage %u: rgb in 0x%08x out 0x%08x | alpha in 0x%08x out 0x%08x", i, d.PSRGBInputs[i], d.PSRGBOutputs[i],
             d.PSAlphaInputs[i], d.PSAlphaOutputs[i]);
}

} // namespace

IDirect3DPixelShader9 *ps_host_shader()
{
    const uint32_t *rs = render_state();
    PixelShaderDef def = {};
    memcpy(&def, rs, 57 * 4);
    def.PSTextureModes = rs[XRS_PSTEXTUREMODES];
    uint32_t cube = cube_mask();
    // key: everything except the constant values (10-25, 43, 44)
    uint32_t key[48];
    uint32_t n = 0;
    for (uint32_t i = 0; i < 57; i++)
        if (!(i >= 10 && i <= 25) && i != 43 && i != 44) key[n++] = rs[i];
    key[n++] = def.PSTextureModes;
    key[n++] = cube;
    static uint32_t last_key[48], last_n;
    static IDirect3DPixelShader9 *last_ps;
    if (n == last_n && !memcmp(key, last_key, n * 4)) return last_ps; // most draws keep the previous shader
    std::string k((const char *)key, n * 4);
    auto it = g_cache.find(k);
    if (it != g_cache.end()) {
        memcpy(last_key, key, n * 4);
        last_n = n;
        return last_ps = it->second;
    }

    std::string hlsl = psh_to_hlsl(def, cube);
    perf::Scope perf_scope(perf::kCompile);
    perf::add_compile();
    ID3DBlob *code = nullptr, *errors = nullptr;
    HRESULT hr = E_FAIL;
    for (const char *profile : {"ps_2_0", "ps_2_a", "ps_2_b"}) {
        if (errors) { errors->Release(); errors = nullptr; }
        hr = D3DCompile(hlsl.data(), hlsl.size(), "nv2a_psh", nullptr, nullptr, "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL2, 0,
                        &code, &errors);
        if (SUCCEEDED(hr)) break;
    }
    IDirect3DPixelShader9 *ps = nullptr;
    static int logged;
    if (FAILED(hr)) {
        logf("[psh] compile failed: %s", errors ? (const char *)errors->GetBufferPointer() : "?");
        log_def(def);
        static int dumped;
        if (dumped++ < 3) logf("[psh] HLSL:\n%s", hlsl.c_str());
    } else if (FAILED(host3d::dev()->CreatePixelShader((const DWORD *)code->GetBufferPointer(), &ps))) {
        logf("[psh] CreatePixelShader failed");
        ps = nullptr;
    } else if (logged++ < 40) {
        logf("[psh] pixel shader #%zu translated (cube mask 0x%x)", g_cache.size() + 1, cube);
        log_def(def);
    }
    if (code) code->Release();
    if (errors) errors->Release();
    g_cache[k] = ps; // failures are cached too (drawn with the fallback)
    memcpy(last_key, key, n * 4);
    last_n = n;
    return last_ps = ps;
}

void upload_ps_constants(IDirect3DDevice9 *d)
{
    const uint32_t *rs = render_state();
    float c[23][4];
    for (int i = 0; i < 8; i++) {
        unpack_color(rs[XRS_PSCONSTANT0_0 + i], c[i]);
        unpack_color(rs[XRS_PSCONSTANT1_0 + i], c[8 + i]);
    }
    unpack_color(rs[43], c[16]); // PSFinalCombinerConstant0
    unpack_color(rs[44], c[17]); // PSFinalCombinerConstant1
    unpack_color(rs[XRS_FOGCOLOR], c[18]);
    for (int s = 0; s < 4; s++) {
        const uint32_t *ts = texture_state(s);
        memcpy(&c[19 + s][0], &ts[22], 4); // BUMPENVMAT00
        memcpy(&c[19 + s][1], &ts[23], 4); // BUMPENVMAT01
        memcpy(&c[19 + s][2], &ts[25], 4); // BUMPENVMAT10
        memcpy(&c[19 + s][3], &ts[24], 4); // BUMPENVMAT11
    }
    d->SetPixelShaderConstantF(0, &c[0][0], 23);
}

void ps_set_constant(const PixelShaderDef *def, uint32_t reg, const float *v)
{
    uint32_t color = pack_color(v);
    uint32_t *rs = render_state();
    for (int i = 0; i < 8; i++) {
        if (((def->PSC0Mapping >> (4 * i)) & 0xF) == reg) rs[XRS_PSCONSTANT0_0 + i] = color;
        if (((def->PSC1Mapping >> (4 * i)) & 0xF) == reg) rs[XRS_PSCONSTANT1_0 + i] = color;
    }
    if ((def->PSFinalCombinerConstants & 0xF) == reg) rs[43] = color;
    if (((def->PSFinalCombinerConstants >> 4) & 0xF) == reg) rs[44] = color;
}

} // namespace xd3d
