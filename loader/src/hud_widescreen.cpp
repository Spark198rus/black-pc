// The HUD in widescreen (issue #1): BLACK draws its HUD in 640 x 480 units also when the picture is 16:9 (the
// console's widescreen setting makes the picture anamorphic: 640 units across 16:9), so every element came out a
// third wider than its art and the side panels sat in from the edges.
//
// The 2D overlay (the HUD, its messages, the crosshair) is a list of nodes that FUN_00021260 walks (eax = the list
// entry, [eax+8] the first node, [node] the next): for each node it computes its rectangle from the node's position
// +8 / +0xC and size +0x10 / +0x14 and its layer [node+0x40] (origin +0x20 / +0x24, scale +0x28 / +0x2C, colour
// +0x30): x = node.x * sx + ox, y = node.y * sy + oy, w = node.w * sx, h = node.h * sy (at [esp+0x70], x y w h), then
// calls the node's draw [node+0x44] with that rectangle. The layers already anchor their elements: the left panel's
// origin is x 30, the right panel's 610 (its nodes at negative x), the centre's 320. In widescreen the game itself
// sets y scale 4/3 on the three centre layers at (320, 240) (the crosshair and the markers around it) and 1 on all
// others (4:3: 1 everywhere; seen with a probe).
//
// Here, right after the rectangle is computed (0x21355, before the draw), in widescreen: the layer's anchor is the
// left edge (origin x < 213), the right edge (> 427) or the centre, and the element is narrowed by 3/4 towards it
// (its origin moves 3/4 of the way, then the offset and the width are 3/4). A layer the game already corrected
// (y scale = 4/3 x scale) gets 3/4 in both directions instead, so it is the size it has in 4:3. An element that
// spans the whole width (from x <= 20 to >= 620: the black screen behind the mission intro text, fades) keeps its
// rectangle (user: narrowed, it showed the game at the sides).
//
// Text: the text layout FUN_0012C6F0 (rectangle, text) advances the glyphs by the rectangle's width but aligns a line
// (centre / right: line width x the alignment factor [0x2AB7C4 + 4 * align]) with its height (movss xmm1, [ebp-0x2C]
// at 0x12C887), so a narrowed centred line started too far left (by 1/6 of its width; seen on the objectives
// message). That load is a call that also multiplies by g_hudws_align: 3/4 while a narrowed node draws, else 1
// (set by the rectangle hook, back to 1 right after the node's draw at 0x213A0).
#include "hud_widescreen.h"

#include "common.h"
#include "xbe.h"

#include <cmath>
#include <cstring>

extern "C" {
void __cdecl hudws_rect(float *rect, const uint8_t *node);
float g_hudws_align = 1.0f;
}

namespace {

bool g_installed;

// 0x21355: mov esi, [esp+0x1C]; mov edi, [esp+0x18] (8 bytes, replaced by a call); ebx = the node, the rectangle
// is at [esp+0x70] of the caller
__declspec(naked) void hudws_thunk()
{
    __asm {
        pushad
        push ebx
        lea eax, [esp + 36 + 4 + 0x70]
        push eax
        call hudws_rect
        add esp, 8
        popad
        mov esi, dword ptr [esp + 4 + 0x1C]
        mov edi, dword ptr [esp + 4 + 0x18]
        ret
    }
}

// 0x213A0: mov ebx, [ebx]; add esp, 8 (right after the node's draw; 5 bytes, replaced by a call)
__declspec(naked) void hudws_after_thunk()
{
    __asm {
        pop eax // our return address (eax is free here: the loop reloads it)
        mov ebx, dword ptr [ebx]
        add esp, 8
        mov dword ptr [g_hudws_align], 0x3F800000 // 1.0
        jmp eax
    }
}

// 0x12C887: movss xmm1, [ebp-0x2C] (the text rectangle's height, for the line alignment; 5 bytes)
__declspec(naked) void hudws_align_thunk()
{
    __asm {
        movss xmm1, dword ptr [ebp - 0x2C]
        mulss xmm1, dword ptr [g_hudws_align]
        ret
    }
}

} // namespace

extern "C" void __cdecl hudws_rect(float *rect, const uint8_t *node)
{
    g_hudws_align = 1.0f;
    if (!g_cfg.widescreen) return;
    const uint8_t *layer = *(const uint8_t *const *)(node + 0x40);
    if (!layer) return;
    if (rect[0] <= 20.0f && rect[0] + rect[2] >= 620.0f) return; // a full-screen backdrop
    float ox = *(const float *)(layer + 0x20), oy = *(const float *)(layer + 0x24);
    float sx = *(const float *)(layer + 0x28), sy = *(const float *)(layer + 0x2C);
    constexpr float k = 0.75f;
    float anchor = ox < 213.0f ? 0.0f : ox > 427.0f ? 640.0f : 320.0f;
    rect[0] = anchor + (ox - anchor) * k + (rect[0] - ox) * k;
    rect[2] *= k;
    g_hudws_align = k;
    if (sx > 0.0f && std::fabs(sy - sx * (4.0f / 3.0f)) < sx * 0.01f) { // the game's own widescreen correction
        rect[1] = oy + (rect[1] - oy) * k;
        rect[3] *= k;
    }
}

void install_hud_widescreen_patch()
{
    struct Site {
        uint32_t addr;
        uint8_t expect[8];
        uint32_t len;
        void (*thunk)();
    };
    static const Site kSites[] = {
        {0x00021355, {0x8B, 0x74, 0x24, 0x1C, 0x8B, 0x7C, 0x24, 0x18}, 8, hudws_thunk},  // the rectangle is ready
        {0x000213A0, {0x8B, 0x1B, 0x83, 0xC4, 0x08}, 5, hudws_after_thunk},              // after the node's draw
        {0x0012C887, {0xF3, 0x0F, 0x10, 0x4D, 0xD4}, 5, hudws_align_thunk},              // text line alignment
    };
    for (const Site &s : kSites)
        if (memcmp((const void *)(uintptr_t)xbe_va(s.addr), s.expect, s.len)) {
            logf("hud: code at 0x%08x does not match this build; the HUD stays stretched in widescreen", xbe_va(s.addr));
            return;
        }
    for (const Site &s : kSites) {
        uint8_t *at = (uint8_t *)(uintptr_t)xbe_va(s.addr);
        int32_t r = (int32_t)((uintptr_t)s.thunk - ((uintptr_t)at + 5));
        at[0] = 0xE8;
        memcpy(at + 1, &r, 4);
        for (uint32_t i = 5; i < s.len; i++) at[i] = 0x90;
    }
    g_installed = true;
    logf("hud: in widescreen the HUD keeps its shape, anchored to the sides (FUN_00021260 at 0x%08x)", xbe_va(0x00021260));
}
