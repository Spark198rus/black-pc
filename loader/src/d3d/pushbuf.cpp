#include "d3d/pushbuf.h"

#include "d3d/device.h"
#include "d3d/draw.h"

#include <cstring>
#include <set>
#include <vector>

namespace xd3d {
namespace {

// NV097 (Kelvin) methods seen in inline game code.
constexpr uint32_t M_NO_OPERATION = 0x0100;
constexpr uint32_t M_BREAK_VERTEX_BUFFER_CACHE = 0x1710;
constexpr uint32_t M_SET_BEGIN_END = 0x17FC;
constexpr uint32_t M_INLINE_ARRAY = 0x1818;
constexpr uint32_t M_VERTEX_DATA2F = 0x1880; // 16 attributes x 2 floats
constexpr uint32_t M_VERTEX_DATA2S = 0x1900; // 16 x 1 dword (2 shorts)
constexpr uint32_t M_VERTEX_DATA4UB = 0x1940; // 16 x 1 dword
constexpr uint32_t M_VERTEX_DATA4S = 0x1980; // 16 x 2 dwords
constexpr uint32_t M_VERTEX_DATA4F = 0x1A00; // 16 x 4 floats

uint32_t *g_read; // next unread dword
uint32_t g_prim; // current SET_BEGIN_END primitive, 0 = outside begin/end
float g_attr[16][4];
uint32_t g_attr_used; // attributes written since begin
std::vector<float> g_imm; // immediate-mode vertices: 16 x float4 each
std::vector<uint32_t> g_inline; // inline array dwords
std::set<uint32_t> g_unknown;

void emit_vertex()
{
    g_imm.insert(g_imm.end(), &g_attr[0][0], &g_attr[0][0] + 64);
}

void set_attr(int a, float x, float y, float z, float w)
{
    g_attr[a][0] = x;
    g_attr[a][1] = y;
    g_attr[a][2] = z;
    g_attr[a][3] = w;
    g_attr_used |= 1u << a;
    if (a == 0 && g_prim) emit_vertex(); // writing the position attribute issues the vertex
}

void method(uint32_t m, uint32_t v)
{
    if (m >= M_VERTEX_DATA4F && m < M_VERTEX_DATA4F + 16 * 16) {
        uint32_t a = (m - M_VERTEX_DATA4F) / 16, c = (m - M_VERTEX_DATA4F) % 16 / 4;
        float f;
        memcpy(&f, &v, 4);
        g_attr[a][c] = f;
        if (c == 3) set_attr(a, g_attr[a][0], g_attr[a][1], g_attr[a][2], g_attr[a][3]);
        return;
    }
    if (m >= M_VERTEX_DATA2F && m < M_VERTEX_DATA2F + 16 * 8) {
        uint32_t a = (m - M_VERTEX_DATA2F) / 8, c = (m - M_VERTEX_DATA2F) % 8 / 4;
        float f;
        memcpy(&f, &v, 4);
        g_attr[a][c] = f;
        if (c == 1) set_attr(a, g_attr[a][0], g_attr[a][1], 0.0f, 1.0f);
        return;
    }
    if (m >= M_VERTEX_DATA4UB && m < M_VERTEX_DATA4UB + 16 * 4) {
        uint32_t a = (m - M_VERTEX_DATA4UB) / 4;
        set_attr(a, (v & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, ((v >> 16) & 0xFF) / 255.0f, (v >> 24) / 255.0f);
        return;
    }
    if (m >= M_VERTEX_DATA2S && m < M_VERTEX_DATA2S + 16 * 4) {
        uint32_t a = (m - M_VERTEX_DATA2S) / 4;
        set_attr(a, (float)(int16_t)(v & 0xFFFF), (float)(int16_t)(v >> 16), 0.0f, 1.0f);
        return;
    }
    switch (m) {
    case M_SET_BEGIN_END:
        if (v) {
            g_prim = v;
            g_attr_used = 0;
            g_imm.clear();
            g_inline.clear();
        } else {
            if (!g_imm.empty()) draw_immediate(g_prim, g_imm.data(), (uint32_t)(g_imm.size() / 64), g_attr_used);
            if (!g_inline.empty()) draw_inline_array(g_prim, g_inline.data(), (uint32_t)g_inline.size());
            g_prim = 0;
            g_imm.clear();
            g_inline.clear();
        }
        return;
    case M_INLINE_ARRAY:
        g_inline.push_back(v);
        return;
    case M_BREAK_VERTEX_BUFFER_CACHE:
    case M_NO_OPERATION:
        return;
    }
    if (g_unknown.insert(m).second) logf("[pb] unhandled NV2A method 0x%04x = 0x%08x (logged once)", m, v);
}

} // namespace

void pb_init(uint32_t *base) { g_read = base; }

void pb_flush()
{
    if (!g_dev) return;
    uint32_t *put = *(uint32_t **)(g_dev + DEV_PUT);
    uint32_t *p = g_read;
    while (p < put) {
        uint32_t cmd = *p++;
        if ((cmd & 3) == 1 || (cmd & 3) == 2 || (cmd & 0xE0000000) == 0x20000000) {
            logf("[pb] jump/call command 0x%08x in the inline push buffer: not expected, skipping rest", cmd);
            p = put;
            break;
        }
        uint32_t count = (cmd >> 18) & 0x7FF;
        uint32_t m = cmd & 0x1FFC;
        bool noninc = (cmd & 0x40000000) != 0;
        if (cmd == 0) continue;
        if (((cmd >> 13) & 7) != 0 || m == 0) { // other subchannels / object binds / skipped payloads
            p += count;
            continue;
        }
        for (uint32_t i = 0; i < count && p < put; i++) method(noninc ? m : m + 4 * i, *p++);
    }
    g_read = p;
}

void pb_end_primitive()
{
    if (g_prim) method(M_SET_BEGIN_END, 0);
}

void pb_reset()
{
    *(uint32_t **)(g_dev + DEV_PUT) = g_push_base;
    g_read = g_push_base;
}

} // namespace xd3d
