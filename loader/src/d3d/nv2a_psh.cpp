// NV2A register combiners -> HLSL. Each general combiner stage computes, separately for RGB and alpha,
// AB (product or dot), CD (product or dot) and AB+CD (or a mux on r0.a), with per-input mappings and an
// output scale/bias, writing to r0/r1/v0/v1/t0-t3. The final combiner computes A*B + (1-A)*C + D (RGB)
// and G (alpha). Encodings follow the public NV_register_combiners / Xbox D3DPIXELSHADERDEF definitions.
#include "d3d/nv2a_psh.h"

#include <cstdio>

namespace xd3d {
namespace {

const char *reg_name(uint32_t reg)
{
    switch (reg) {
    case 0: return "float4(0,0,0,0)";
    case 1: return "C0";
    case 2: return "C1";
    case 3: return "FOGR";
    case 4: return "v0";
    case 5: return "v1";
    case 8: return "t0";
    case 9: return "t1";
    case 10: return "t2";
    case 11: return "t3";
    case 12: return "r0";
    case 13: return "r1";
    case 14: return "V1R0";
    case 15: return "EF";
    default: return "float4(0,0,0,0)";
    }
}

// One 8-bit combiner input as an HLSL expression. rgb: RGB portion (else alpha portion).
// V1R0 sum and EF product exist only in the final combiner; general combiners read them as zero.
std::string input(uint32_t b, bool rgb)
{
    uint32_t reg = b & 0xF;
    if (reg >= 14) reg = 0;
    bool alpha_chan = (b & 0x10) != 0;
    uint32_t map = (b >> 5) & 7;
    std::string r = reg_name(reg);
    std::string x;
    if (rgb) x = alpha_chan ? "(" + r + ").aaa" : "(" + r + ").rgb";
    else x = alpha_chan ? "(" + r + ").a" : "(" + r + ").b";
    switch (map) {
    case 0: return "max(" + x + ", 0)";
    case 1: return "(1 - saturate(" + x + "))";
    case 2: return "(2 * max(" + x + ", 0) - 1)";
    case 3: return "(1 - 2 * max(" + x + ", 0))";
    case 4: return "(max(" + x + ", 0) - 0.5)";
    case 5: return "(0.5 - max(" + x + ", 0))";
    case 6: return x;
    default: return "(-" + x + ")";
    }
}

std::string out_map(const std::string &x, uint32_t flags)
{
    switch (flags & 0x38) {
    case 0x08: return "(" + x + " - 0.5)";
    case 0x10: return "(" + x + " * 2)";
    case 0x18: return "((" + x + " - 0.5) * 2)";
    case 0x20: return "(" + x + " * 4)";
    case 0x30: return "(" + x + " * 0.5)";
    default: return x;
    }
}

bool writable(uint32_t reg) { return reg == 4 || reg == 5 || (reg >= 8 && reg <= 13); }

std::string final_input(uint32_t b, bool rgb)
{
    uint32_t reg = b & 0xF;
    bool alpha_chan = (b & 0x10) != 0;
    bool invert = ((b >> 5) & 7) == 1;
    std::string r = reg_name(reg);
    std::string x;
    if (rgb) x = alpha_chan ? "(" + r + ").aaa" : "(" + r + ").rgb";
    else x = alpha_chan ? "(" + r + ").a" : "(" + r + ").b";
    return invert ? "(1 - saturate(" + x + "))" : "saturate(" + x + ")";
}

} // namespace

std::string psh_to_hlsl(const PixelShaderDef &d, uint32_t tex_cube)
{
    std::string s;
    char b[512];
    s += "float4 pc[18] : register(c0);\n"  // c0-7 stage C0, c8-15 stage C1, c16/17 final C0/C1
         "float4 fogColor : register(c18);\n"
         "float4 bumpMat[4] : register(c19);\n"
         "sampler2D s2d0 : register(s0); sampler2D s2d1 : register(s1); sampler2D s2d2 : register(s2); sampler2D s2d3 : register(s3);\n"
         "samplerCUBE scu0 : register(s0); samplerCUBE scu1 : register(s1); samplerCUBE scu2 : register(s2); samplerCUBE scu3 : register(s3);\n"
         "struct PSIn { float4 v0 : COLOR0; float4 v1 : COLOR1; float4 tc0 : TEXCOORD0; float4 tc1 : TEXCOORD1;\n"
         "              float4 tc2 : TEXCOORD2; float4 tc3 : TEXCOORD3; };\n"
         "float4 main(PSIn I) : COLOR0 {\n"
         "  float4 v0 = I.v0, v1 = I.v1;\n"
         "  float4 tc[4] = { I.tc0, I.tc1, I.tc2, I.tc3 };\n"
         "  float4 t0 = 0, t1 = 0, t2 = 0, t3 = 0;\n"
         "  float d0 = 0, d1 = 0, d2 = 0, d3 = 0;\n" // dot-product results of DOTPRODUCT stages
         "  float4 FOGR = float4(fogColor.rgb, 1);\n";
    // ---- texture stages ----
    for (int i = 0; i < 4; i++) {
        uint32_t mode = (d.PSTextureModes >> (5 * i)) & 0x1F;
        uint32_t in_stage = 0; // input texture for dependent modes
        if (i == 2) in_stage = (d.PSInputTexture >> 16) & 1;
        if (i == 3) in_stage = (d.PSInputTexture >> 20) & 3;
        if (i == 1) in_stage = 0;
        if (i > 0 && in_stage >= (uint32_t)i) in_stage = i - 1;
        uint32_t dotmap = i ? (d.PSDotMapping >> (4 * (i - 1))) & 7 : 0;
        std::string tin = "t" + std::to_string(in_stage);
        std::string nin = dotmap == 0 ? tin + ".rgb" : "(" + tin + ".rgb * 2 - 1)";
        bool cube = (tex_cube >> i) & 1;
        switch (mode) {
        case 0: break; // NONE
        case 1: case 2: // PROJECT2D / PROJECT3D
            snprintf(b, sizeof b, "  t%d = %s;\n", i,
                     cube ? ("texCUBE(scu" + std::to_string(i) + ", tc[" + std::to_string(i) + "].xyz)").c_str()
                          : ("tex2Dproj(s2d" + std::to_string(i) + ", float4(tc[" + std::to_string(i) + "].xy, 0, tc[" +
                             std::to_string(i) + "].w == 0 ? 1 : tc[" + std::to_string(i) + "].w))").c_str());
            s += b;
            break;
        case 3: // CUBEMAP
            snprintf(b, sizeof b, "  t%d = texCUBE(scu%d, tc[%d].xyz);\n", i, i, i);
            s += b;
            break;
        case 4: // PASSTHRU: the texture coordinate is the color
            snprintf(b, sizeof b, "  t%d = saturate(tc[%d]);\n", i, i);
            s += b;
            break;
        case 5: { // CLIPPLANE: kill the pixel when a coordinate fails the compare
            uint32_t cmp = (d.PSCompareMode >> (4 * i)) & 0xF;
            for (int c = 0; c < 4; c++) {
                const char comp = "xyzw"[c];
                snprintf(b, sizeof b, "  clip(%stc[%d].%c);\n", (cmp >> c) & 1 ? "" : "-", i, comp);
                s += b;
            }
            break;
        }
        case 6: case 7: // BUMPENVMAP (_LUM): offset the coordinates by the bump map
            snprintf(b, sizeof b,
                     "  { float2 du = %s.rg * 2 - 1; float2 o = float2(dot(du, bumpMat[%d].xz), dot(du, bumpMat[%d].yw));\n"
                     "    t%d = tex2D(s2d%d, tc[%d].xy + o); }\n",
                     tin.c_str(), i, i, i, i, i);
            s += b;
            break;
        case 15: // DPNDNT_AR
            snprintf(b, sizeof b, "  t%d = tex2D(s2d%d, %s.ar);\n", i, i, tin.c_str());
            s += b;
            break;
        case 16: // DPNDNT_GB
            snprintf(b, sizeof b, "  t%d = tex2D(s2d%d, %s.gb);\n", i, i, tin.c_str());
            s += b;
            break;
        case 17: // DOTPRODUCT: keep the dot for the next stage
            snprintf(b, sizeof b, "  d%d = dot(tc[%d].xyz, %s); t%d = d%d.xxxx;\n", i, i, nin.c_str(), i, i);
            s += b;
            break;
        case 9: // DOT_ST: (previous dot, this dot)
            snprintf(b, sizeof b, "  d%d = dot(tc[%d].xyz, %s); t%d = tex2D(s2d%d, float2(d%d, d%d));\n", i, i, nin.c_str(), i, i,
                     i > 0 ? i - 1 : 0, i);
            s += b;
            break;
        case 10: // DOT_ZW: depth replace (not emulated): pass the dot
            snprintf(b, sizeof b, "  d%d = dot(tc[%d].xyz, %s); t%d = d%d.xxxx;\n", i, i, nin.c_str(), i, i);
            s += b;
            break;
        case 13: case 14: case 11: case 12: case 18: { // DOT_STR_3D / CUBE / reflections: 3 dots form a vector
            snprintf(b, sizeof b, "  d%d = dot(tc[%d].xyz, %s);\n", i, i, nin.c_str());
            s += b;
            std::string n = "float3(d" + std::to_string(i > 1 ? i - 2 : 0) + ", d" + std::to_string(i > 0 ? i - 1 : 0) + ", d" +
                            std::to_string(i) + ")";
            if (mode == 12 || mode == 18) { // specular reflection: eye vector from the w components
                std::string e = "float3(tc[" + std::to_string(i > 1 ? i - 2 : 0) + "].w, tc[" + std::to_string(i > 0 ? i - 1 : 0) +
                                "].w, tc[" + std::to_string(i) + "].w)";
                n = "reflect(-" + e + ", normalize(" + n + "))";
            }
            snprintf(b, sizeof b, "  t%d = texCUBE(scu%d, %s);\n", i, i, n.c_str());
            s += b;
            break;
        }
        default: // BRDF and others: not emulated
            snprintf(b, sizeof b, "  t%d = 0; // texture mode %u not emulated\n", i, mode);
            s += b;
            break;
        }
    }
    // r0.a starts as t0.a on the NV2A
    s += "  float4 r0 = float4(0, 0, 0, t0.a), r1 = 0;\n";
    // ---- general combiners ----
    uint32_t stages = d.PSCombinerCount & 0xFF;
    if (stages > 8) stages = 8;
    bool unique_c0 = (d.PSCombinerCount & 0x1000) != 0, unique_c1 = (d.PSCombinerCount & 0x10000) != 0;
    // AB/CD mux: MSB tests r0.a >= 0.5, LSB tests the low bit of the 8-bit r0.a
    const char *mux = (d.PSCombinerCount & 0x100) ? "(r0.a >= 0.5)" : "(fmod(floor(saturate(r0.a) * 255 + 0.5), 2) >= 1)";
    for (uint32_t i = 0; i < stages; i++) {
        snprintf(b, sizeof b, "  { // stage %u\n    float4 C0 = pc[%u], C1 = pc[%u];\n", i, unique_c0 ? i : 0, 8 + (unique_c1 ? i : 0));
        s += b;
        uint32_t ri = d.PSRGBInputs[i], ro = d.PSRGBOutputs[i], ai = d.PSAlphaInputs[i], ao = d.PSAlphaOutputs[i];
        uint32_t rflags = ro >> 12, aflags = ao >> 12;
        // RGB portion
        s += "    float3 A = " + input(ri >> 24, true) + ", B = " + input((ri >> 16) & 0xFF, true) + ", C = " +
             input((ri >> 8) & 0xFF, true) + ", D = " + input(ri & 0xFF, true) + ";\n";
        s += std::string("    float3 ab = ") + ((rflags & 2) ? "dot(A, B).xxx" : "A * B") + ";\n";
        s += std::string("    float3 cd = ") + ((rflags & 1) ? "dot(C, D).xxx" : "C * D") + ";\n";
        s += std::string("    float3 sm = ") + ((rflags & 4) ? std::string("(") + mux + " ? cd : ab)" : std::string("ab + cd")) + ";\n";
        s += "    ab = clamp(" + out_map("ab", rflags) + ", -1, 1); cd = clamp(" + out_map("cd", rflags) +
             ", -1, 1); sm = clamp(" + out_map("sm", rflags) + ", -1, 1);\n";
        // alpha portion
        s += "    float Aa = " + input(ai >> 24, false) + ", Ba = " + input((ai >> 16) & 0xFF, false) + ", Ca = " +
             input((ai >> 8) & 0xFF, false) + ", Da = " + input(ai & 0xFF, false) + ";\n";
        s += "    float aba = Aa * Ba, cda = Ca * Da;\n";
        s += std::string("    float sma = ") + ((aflags & 4) ? std::string("(") + mux + " ? cda : aba)" : std::string("aba + cda")) + ";\n";
        s += "    aba = clamp(" + out_map("aba", aflags) + ", -1, 1); cda = clamp(" + out_map("cda", aflags) +
             ", -1, 1); sma = clamp(" + out_map("sma", aflags) + ", -1, 1);\n";
        // writes (after all reads)
        uint32_t r_cd = ro & 0xF, r_ab = (ro >> 4) & 0xF, r_sum = (ro >> 8) & 0xF;
        uint32_t a_cd = ao & 0xF, a_ab = (ao >> 4) & 0xF, a_sum = (ao >> 8) & 0xF;
        if (writable(r_ab)) s += std::string("    ") + reg_name(r_ab) + ".rgb = ab;\n";
        if (writable(r_cd)) s += std::string("    ") + reg_name(r_cd) + ".rgb = cd;\n";
        if (writable(r_sum)) s += std::string("    ") + reg_name(r_sum) + ".rgb = sm;\n";
        if (writable(a_ab)) s += std::string("    ") + reg_name(a_ab) + ".a = aba;\n";
        if (writable(a_cd)) s += std::string("    ") + reg_name(a_cd) + ".a = cda;\n";
        if (writable(a_sum)) s += std::string("    ") + reg_name(a_sum) + ".a = sma;\n";
        if ((rflags & 0x80) && writable(r_ab)) s += std::string("    ") + reg_name(r_ab) + ".a = ab.b;\n"; // AB blue to alpha
        if ((rflags & 0x40) && writable(r_cd)) s += std::string("    ") + reg_name(r_cd) + ".a = cd.b;\n"; // CD blue to alpha
        s += "  }\n";
    }
    // ---- final combiner ----
    if (d.PSFinalCombinerInputsABCD || d.PSFinalCombinerInputsEFG) {
        uint32_t abcd = d.PSFinalCombinerInputsABCD, efg = d.PSFinalCombinerInputsEFG;
        uint32_t flags = efg & 0xFF;
        s += "  { // final combiner\n    float4 C0 = pc[16], C1 = pc[17];\n";
        snprintf(b, sizeof b, "    float4 V1R0 = float4(%s + %s, 0);\n", (flags & 0x40) ? "(1 - v1.rgb)" : "v1.rgb",
                 (flags & 0x20) ? "(1 - r0.rgb)" : "r0.rgb");
        s += b;
        if (flags & 0x80) s += "    V1R0 = saturate(V1R0);\n";
        s += "    float4 EF = float4(" + final_input(efg >> 24, true) + " * " + final_input((efg >> 16) & 0xFF, true) + ", 0);\n";
        s += "    float3 A = " + final_input(abcd >> 24, true) + ", B = " + final_input((abcd >> 16) & 0xFF, true) + ", C = " +
             final_input((abcd >> 8) & 0xFF, true) + ", D = " + final_input(abcd & 0xFF, true) + ";\n";
        s += "    float G = " + final_input((efg >> 8) & 0xFF, false) + ";\n";
        s += "    return saturate(float4(A * B + (1 - A) * C + D, G));\n  }\n";
    } else {
        s += "  return saturate(r0);\n";
    }
    s += "}\n";
    return s;
}

} // namespace xd3d
