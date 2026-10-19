#include "d3d/nv2a_vsh.h"

#include <cstdio>
#include <cstring>

namespace xd3d {
namespace {

uint32_t field(const uint32_t *w, int dword, int pos, int size) { return (w[dword] >> pos) & ((1u << size) - 1); }

const char *kMacNames[] = {"nop", "mov", "mul", "add", "mad", "dp3", "dph", "dp4", "dst", "min", "max", "slt", "sge", "arl"};
const char *kIluNames[] = {"nop", "mov", "rcp", "rcc", "rsq", "expp", "logp", "lit"};
const char kComp[] = "xyzw";

std::string mask_str(uint8_t m)
{
    if (m == 0xF) return "";
    std::string s = ".";
    for (int i = 0; i < 4; i++)
        if (m & (8 >> i)) s += kComp[i];
    return s;
}

std::string swz_str(const VshSrc &s)
{
    if (s.swz[0] == 0 && s.swz[1] == 1 && s.swz[2] == 2 && s.swz[3] == 3) return "";
    std::string r = ".";
    for (int i = 0; i < 4; i++) r += kComp[s.swz[i]];
    if (s.swz[1] == s.swz[0] && s.swz[2] == s.swz[0] && s.swz[3] == s.swz[0]) r.resize(2);
    return r;
}

const char *out_name(uint8_t a)
{
    switch (a) {
    case VSH_OPOS: return "oPos";
    case VSH_OD0: return "oD0";
    case VSH_OD1: return "oD1";
    case VSH_OFOG: return "oFog";
    case VSH_OPTS: return "oPts";
    case VSH_OB0: return "oB0";
    case VSH_OB1: return "oB1";
    case 9: return "oT0";
    case 10: return "oT1";
    case 11: return "oT2";
    case 12: return "oT3";
    default: return "o?";
    }
}

std::string src_str(const VshInst &in, const VshSrc &s)
{
    char b[48];
    switch (s.mux) {
    case MUX_R: snprintf(b, sizeof b, "r%u", s.reg); break;
    case MUX_V: snprintf(b, sizeof b, "v%u", in.v); break;
    case MUX_C:
        if (in.a0x) snprintf(b, sizeof b, "c[a0.x%+d]", (int)in.cnst - 96);
        else snprintf(b, sizeof b, "c%d", (int)in.cnst - 96);
        break;
    default: snprintf(b, sizeof b, "?"); break;
    }
    return std::string(s.neg ? "-" : "") + b + swz_str(s);
}

} // namespace

void vsh_decode(const uint32_t *w, VshInst &o)
{
    o.ilu = (uint8_t)field(w, 1, 25, 3);
    o.mac = (uint8_t)field(w, 1, 21, 4);
    o.cnst = (uint8_t)field(w, 1, 13, 8);
    o.v = (uint8_t)field(w, 1, 9, 4);
    o.a.neg = (uint8_t)field(w, 1, 8, 1);
    o.a.swz[0] = (uint8_t)field(w, 1, 6, 2);
    o.a.swz[1] = (uint8_t)field(w, 1, 4, 2);
    o.a.swz[2] = (uint8_t)field(w, 1, 2, 2);
    o.a.swz[3] = (uint8_t)field(w, 1, 0, 2);
    o.a.reg = (uint8_t)field(w, 2, 28, 4);
    o.a.mux = (uint8_t)field(w, 2, 26, 2);
    o.b.neg = (uint8_t)field(w, 2, 25, 1);
    o.b.swz[0] = (uint8_t)field(w, 2, 23, 2);
    o.b.swz[1] = (uint8_t)field(w, 2, 21, 2);
    o.b.swz[2] = (uint8_t)field(w, 2, 19, 2);
    o.b.swz[3] = (uint8_t)field(w, 2, 17, 2);
    o.b.reg = (uint8_t)field(w, 2, 13, 4);
    o.b.mux = (uint8_t)field(w, 2, 11, 2);
    o.c.neg = (uint8_t)field(w, 2, 10, 1);
    o.c.swz[0] = (uint8_t)field(w, 2, 8, 2);
    o.c.swz[1] = (uint8_t)field(w, 2, 6, 2);
    o.c.swz[2] = (uint8_t)field(w, 2, 4, 2);
    o.c.swz[3] = (uint8_t)field(w, 2, 2, 2);
    o.c.reg = (uint8_t)((field(w, 2, 0, 2) << 2) | field(w, 3, 30, 2));
    o.c.mux = (uint8_t)field(w, 3, 28, 2);
    o.out_mac_mask = (uint8_t)field(w, 3, 24, 4);
    o.out_r = (uint8_t)field(w, 3, 20, 4);
    o.out_ilu_mask = (uint8_t)field(w, 3, 16, 4);
    o.out_o_mask = (uint8_t)field(w, 3, 12, 4);
    o.out_orb = (uint8_t)field(w, 3, 11, 1);
    o.out_address = (uint8_t)field(w, 3, 3, 8);
    o.out_mux = (uint8_t)field(w, 3, 2, 1);
    o.a0x = (uint8_t)field(w, 3, 1, 1);
    o.final = (uint8_t)field(w, 3, 0, 1);
}

std::string vsh_disasm(const uint32_t *program, uint32_t count)
{
    std::string out;
    for (uint32_t i = 0; i < count; i++) {
        VshInst in;
        vsh_decode(program + i * 4, in);
        char line[256];
        std::string parts;
        if (in.mac != MAC_NOP) {
            std::string dsts;
            if (in.out_mac_mask) dsts += "r" + std::to_string(in.out_r) + mask_str(in.out_mac_mask);
            if (in.out_o_mask && !in.out_mux) {
                if (!dsts.empty()) dsts += "+";
                dsts += in.out_orb ? std::string(out_name(in.out_address)) + mask_str(in.out_o_mask)
                                   : "c" + std::to_string((int)in.out_address - 96) + mask_str(in.out_o_mask);
            }
            if (in.mac == MAC_ARL) dsts = "a0.x";
            std::string srcs;
            switch (in.mac) {
            case MAC_MOV: case MAC_ARL: srcs = src_str(in, in.a); break;
            case MAC_ADD: srcs = src_str(in, in.a) + ", " + src_str(in, in.c); break;
            case MAC_MAD: srcs = src_str(in, in.a) + ", " + src_str(in, in.b) + ", " + src_str(in, in.c); break;
            default: srcs = src_str(in, in.a) + ", " + src_str(in, in.b); break;
            }
            parts += std::string(in.mac < 14 ? kMacNames[in.mac] : "mac?") + " " + dsts + ", " + srcs;
        }
        if (in.ilu != ILU_NOP) {
            std::string dsts;
            if (in.out_ilu_mask) dsts += "r" + std::to_string(in.mac != MAC_NOP ? 1 : in.out_r) + mask_str(in.out_ilu_mask);
            if (in.out_o_mask && in.out_mux) {
                if (!dsts.empty()) dsts += "+";
                dsts += in.out_orb ? std::string(out_name(in.out_address)) + mask_str(in.out_o_mask)
                                   : "c" + std::to_string((int)in.out_address - 96) + mask_str(in.out_o_mask);
            }
            if (!parts.empty()) parts += "  +  ";
            parts += std::string(kIluNames[in.ilu]) + " " + dsts + ", " + src_str(in, in.c);
        }
        if (parts.empty()) parts = "nop";
        snprintf(line, sizeof line, "  %3u: %s%s\n", i, parts.c_str(), in.final ? "   (final)" : "");
        out += line;
    }
    return out;
}

// ---- HLSL generation ----

namespace {

std::string hlsl_src(const VshInst &in, const VshSrc &s)
{
    char b[48];
    switch (s.mux) {
    case MUX_R:
        if (s.reg == 12) snprintf(b, sizeof b, "oPos"); // r12 is the position output
        else snprintf(b, sizeof b, "r%u", s.reg);
        break;
    case MUX_V: snprintf(b, sizeof b, "v%u", in.v); break;
    case MUX_C:
        if (in.a0x) snprintf(b, sizeof b, "c[a0+%u]", in.cnst);
        else snprintf(b, sizeof b, "c[%u]", in.cnst);
        break;
    default: snprintf(b, sizeof b, "float4(0,0,0,0)"); break;
    }
    std::string r = b;
    if (!(s.swz[0] == 0 && s.swz[1] == 1 && s.swz[2] == 2 && s.swz[3] == 3)) {
        r += '.';
        for (int i = 0; i < 4; i++) r += kComp[s.swz[i]];
    }
    return s.neg ? "(-" + r + ")" : r;
}

std::string hlsl_mask(uint8_t m)
{
    std::string s;
    for (int i = 0; i < 4; i++)
        if (m & (8 >> i)) s += kComp[i];
    return s;
}

const char *hlsl_out(uint8_t a)
{
    switch (a) {
    case VSH_OPOS: return "oPos";
    case VSH_OD0: return "oD0";
    case VSH_OD1: return "oD1";
    case VSH_OFOG: return "oFog";
    case VSH_OPTS: return "oPts";
    case VSH_OB0: return "oB0";
    case VSH_OB1: return "oB1";
    case 9: return "oT0";
    case 10: return "oT1";
    case 11: return "oT2";
    case 12: return "oT3";
    default: return nullptr;
    }
}

} // namespace

std::string vsh_to_hlsl(const uint32_t *program, uint32_t count, uint32_t input_mask)
{
    std::string s;
    s += "float4 c[192] : register(c0);\n"
         "float4 vpScale : register(c192);\n"
         "float4 vpOffset : register(c193);\n"
         "float4 texScale[4] : register(c194);\n" // 1/size for linear textures (NV2A uses texel coordinates)
         "float4 fogParams : register(c198);\n"   // start, end, density
         "float4 fogMode : register(c199);\n"     // weights: linear, exp, exp2, none (oFog is the factor)
         "float4 vmScale : register(c200);\n"     // clip x/y factor: the first-person gun at the game's FOV (fov.cpp)
         "struct VSIn {\n";
    bool any_input = false;
    for (int i = 0; i < 16; i++)
        if (input_mask & (1u << i)) {
            s += "  float4 v" + std::to_string(i) + " : TEXCOORD" + std::to_string(i) + ";\n";
            any_input = true;
        }
    if (!any_input) s += "  float4 unused : TEXCOORD0;\n";
    s += "};\n"
         "struct VSOut {\n"
         "  float4 pos : POSITION; float4 d0 : COLOR0; float4 d1 : COLOR1;\n"
         "  float4 t0 : TEXCOORD0; float4 t1 : TEXCOORD1; float4 t2 : TEXCOORD2; float4 t3 : TEXCOORD3;\n"
         "  float fog : FOG; float pts : PSIZE;\n"
         "};\n"
         "VSOut main(VSIn I) {\n";
    for (int i = 0; i < 16; i++) {
        if (input_mask & (1u << i)) s += "  float4 v" + std::to_string(i) + " = I.v" + std::to_string(i) + ";\n";
        else s += "  float4 v" + std::to_string(i) + " = float4(0,0,0,1);\n";
    }
    s += "  float4 r0=0,r1=0,r2=0,r3=0,r4=0,r5=0,r6=0,r7=0,r8=0,r9=0,r10=0,r11=0;\n"
         "  float4 oPos=float4(0,0,0,1), oD0=float4(1,1,1,1), oD1=0, oFog=0, oPts=1, oB0=0, oB1=0;\n"
         "  float4 oT0=0, oT1=0, oT2=0, oT3=0;\n"
         "  int a0 = 0;\n";
    bool const_write_noted = false, writes_fog = false;
    for (uint32_t i = 0; i < count; i++) {
        VshInst in;
        vsh_decode(program + i * 4, in);
        if (in.mac == MAC_NOP && in.ilu == ILU_NOP) {
            if (in.final) break;
            continue;
        }
        s += "  { // " + std::to_string(i) + "\n";
        s += "    float4 A = " + hlsl_src(in, in.a) + ";\n";
        s += "    float4 B = " + hlsl_src(in, in.b) + ";\n";
        s += "    float4 C = " + hlsl_src(in, in.c) + ";\n";
        if (in.mac != MAC_NOP) {
            std::string m;
            switch (in.mac) {
            case MAC_MOV: m = "A"; break;
            case MAC_MUL: m = "A * B"; break;
            case MAC_ADD: m = "A + C"; break;
            case MAC_MAD: m = "A * B + C"; break;
            case MAC_DP3: m = "dot(A.xyz, B.xyz).xxxx"; break;
            case MAC_DPH: m = "(dot(A.xyz, B.xyz) + B.w).xxxx"; break;
            case MAC_DP4: m = "dot(A, B).xxxx"; break;
            case MAC_DST: m = "float4(1.0, A.y * B.y, A.z, B.w)"; break;
            case MAC_MIN: m = "min(A, B)"; break;
            case MAC_MAX: m = "max(A, B)"; break;
            case MAC_SLT: m = "(float4)(A < B)"; break;
            case MAC_SGE: m = "(float4)(A >= B)"; break;
            case MAC_ARL: m = "A"; break;
            default: m = "float4(0,0,0,0)"; break;
            }
            s += "    float4 M = " + m + ";\n";
        }
        if (in.ilu != ILU_NOP) {
            std::string l;
            switch (in.ilu) {
            case ILU_MOV: l = "C"; break;
            case ILU_RCP: l = "(1.0 / C.x).xxxx"; break;
            case ILU_RCC:
                l = "((C.x < 0.0 ? -1.0 : 1.0) * clamp(1.0 / abs(C.x), 5.42101e-20, 1.884467e19)).xxxx";
                break;
            case ILU_RSQ: l = "rsqrt(abs(C.x)).xxxx"; break;
            case ILU_EXP: l = "float4(exp2(floor(C.x)), C.x - floor(C.x), exp2(C.x), 1.0)"; break;
            case ILU_LOG:
                l = "float4(floor(log2(abs(C.x))), abs(C.x) / exp2(floor(log2(abs(C.x)))), log2(abs(C.x)), 1.0)";
                break;
            case ILU_LIT:
                l = "float4(1.0, max(C.x, 0.0), (C.x > 0.0 && C.y > 0.0) ? pow(C.y, clamp(C.w, -127.9961, 127.9961)) : 0.0, 1.0)";
                break;
            default: l = "float4(0,0,0,0)"; break;
            }
            s += "    float4 L = " + l + ";\n";
        }
        // writes (after all reads: the two units run in parallel)
        if (in.mac == MAC_ARL) {
            s += "    a0 = (int)floor(M.x + 0.001);\n";
        } else if (in.mac != MAC_NOP && in.out_mac_mask) {
            std::string dst = in.out_r == 12 ? "oPos" : "r" + std::to_string(in.out_r);
            std::string mk = hlsl_mask(in.out_mac_mask);
            s += "    " + dst + "." + mk + " = M." + mk + ";\n";
        }
        if (in.ilu != ILU_NOP && in.out_ilu_mask) {
            uint8_t r = in.mac != MAC_NOP ? 1 : in.out_r; // a paired ILU writes r1
            std::string dst = r == 12 ? "oPos" : "r" + std::to_string(r);
            std::string mk = hlsl_mask(in.out_ilu_mask);
            s += "    " + dst + "." + mk + " = L." + mk + ";\n";
        }
        if (in.out_o_mask) {
            bool from_ilu = in.out_mux != 0;
            if ((from_ilu && in.ilu != ILU_NOP) || (!from_ilu && in.mac != MAC_NOP && in.mac != MAC_ARL)) {
                std::string mk = hlsl_mask(in.out_o_mask);
                if (in.out_orb) {
                    if (const char *o = hlsl_out(in.out_address)) {
                        s += std::string("    ") + o + "." + mk + " = " + (from_ilu ? "L." : "M.") + mk + ";\n";
                        if (!strcmp(o, "oFog")) writes_fog = true;
                    }
                } else if (!const_write_noted) {
                    s += "    // constant write not supported\n";
                    const_write_noted = true;
                }
            }
        }
        s += "  }\n";
        if (in.final) break;
    }
    // The NV2A rasterizes from these screen positions with 4 fractional bits, so vertices of separately
    // transformed pieces that meet (destructible walls are built from chunks, each with its own matrix) land
    // on the same point. The host's finer precision at a higher render size left hairline cracks that
    // flickered along such seams: snap to a sub-pixel grid of the Xbox screen space. 1/16 (the NV2A's own)
    // still left some at 3-4x the render size; 1/8 closes gaps twice as wide (0.4 host pixels a step at 3.45x).
    s += "  oPos.xy = floor(oPos.xy * 8.0 + 0.5) * 0.125;\n";
    // The NV2A expects screen-space positions; undo the viewport transform to get D3D clip space.
    // The NV2A does not divide by w (x/y/z are already screen coordinates; w only feeds perspective-
    // correct interpolation), so 2D shaders often leave w = 0: treat 0 / non-finite w as 1.
    s += "  VSOut o;\n"
         "  float w = (oPos.w == 0.0 || !(abs(oPos.w) < 3.0e38)) ? 1.0 : oPos.w;\n"
         "  o.pos.w = w;\n"
         "  o.pos.xyz = (oPos.xyz - vpOffset.xyz) / vpScale.xyz * w;\n"
         "  o.pos.xy *= vmScale.xy;\n"
         "  o.d0 = oD0; o.d1 = oD1;\n"
         "  o.t0 = oT0 * float4(texScale[0].xy, 1, 1); o.t1 = oT1 * float4(texScale[1].xy, 1, 1);\n"
         "  o.t2 = oT2 * float4(texScale[2].xy, 1, 1); o.t3 = oT3 * float4(texScale[3].xy, 1, 1);\n"
         "  o.pts = oPts.x;\n";
    // The NV2A fog unit turns the oFog coordinate into a factor with the table mode; D3D9 ignores oFog
    // under table fog, so compute the factor here (the device runs vertex fog on this path).
    if (writes_fog)
        s += "  float fc = abs(oFog.x), fden = fogParams.y - fogParams.x;\n"
             "  float flin = (fogParams.y - fc) / (abs(fden) > 1e-6 ? fden : 1e-6);\n"
             "  float fd = fogParams.z * fc;\n"
             "  o.fog = saturate(dot(fogMode, float4(flin, exp(-fd), exp(-fd * fd), oFog.x)));\n";
    else
        s += "  o.fog = 1.0;\n";
    s += "  return o;\n"
         "}\n";
    return s;
}

} // namespace xd3d
