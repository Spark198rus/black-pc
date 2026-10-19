// NV2A vertex program microcode: decoding, disassembly, translation to HLSL.
// Each instruction is 4 dwords (the first unused). The MAC and ILU units execute in parallel; both read
// their inputs before either writes. Field positions follow the public NV2A documentation.
#pragma once

#include <cstdint>
#include <string>

namespace xd3d {

enum VshMac : uint8_t { MAC_NOP, MAC_MOV, MAC_MUL, MAC_ADD, MAC_MAD, MAC_DP3, MAC_DPH, MAC_DP4, MAC_DST, MAC_MIN, MAC_MAX,
                        MAC_SLT, MAC_SGE, MAC_ARL };
enum VshIlu : uint8_t { ILU_NOP, ILU_MOV, ILU_RCP, ILU_RCC, ILU_RSQ, ILU_EXP, ILU_LOG, ILU_LIT };
enum VshMux : uint8_t { MUX_R = 1, MUX_V = 2, MUX_C = 3 };

struct VshSrc {
    uint8_t neg, swz[4], reg, mux;
};

struct VshInst {
    uint8_t ilu, mac, cnst, v;
    VshSrc a, b, c;
    uint8_t out_mac_mask, out_r, out_ilu_mask, out_o_mask, out_orb, out_address, out_mux, a0x, final;
};

void vsh_decode(const uint32_t *words, VshInst &out);
std::string vsh_disasm(const uint32_t *program, uint32_t count);

// Output register addresses (OUT_ADDRESS when OUT_ORB = 1)
enum : uint8_t { VSH_OPOS = 0, VSH_OD0 = 3, VSH_OD1 = 4, VSH_OFOG = 5, VSH_OPTS = 6, VSH_OB0 = 7, VSH_OB1 = 8, VSH_OT0 = 9 };

// HLSL source for vs_2_0 (constants c0-c191 = Xbox c-96..c95, c192/c193 = viewport scale/offset used
// to undo the screen-space position transform, inputs v0-v15 as TEXCOORD0-15).
std::string vsh_to_hlsl(const uint32_t *program, uint32_t count, uint32_t input_mask);

} // namespace xd3d
