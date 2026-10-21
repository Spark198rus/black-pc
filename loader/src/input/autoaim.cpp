// Auto-aim switch, off by default (PC SETTINGS > AUDIO AND GAME, --autoaim / --no-autoaim).
//
// The game's auto-aim is a pull of the view toward a target, not bullet correction. Each player update
// (FUN_001ec450, ebx = player controller), while a stick is moved (any look/move axis) or the button at
// action table +0x24 is held, it clears the picked target [ebx+0xAC] and casts a 30 m query along the
// camera (FUN_001cda30 with the camera matrix [ebx+0x8C]+0xD0, the radius at 0x2F57E8, callback
// FUN_001bd3a0, which keeps the nearest entity with an AI part [+0x39C] in +0xAC / +0xB0 / +0xB4). Then
// FUN_001d1e40(ebx) turns the offset to that target (positions from FUN_001ccdd0, moved into camera space
// with the inverted camera matrix) into turn-rate kicks, each axis clamped to 3.5: pitch rate +0xA0 += y,
// yaw rate +0xA4 -= x. FUN_001bd3f0 then smooths those rates with the stick and integrates them into the
// view angles (see mouselook.cpp), so the view drifts onto the target. Nothing else reads +0xAC; the other
// caller of the query (0x1DCDF4) is enemy AI.
//
// The patch puts a check at the entry of FUN_001d1e40: when auto-aim is off it returns at once, so the
// rates keep only the stick's share. The flag is read on every call, so the menu switches it live.
#include "input/autoaim.h"

#include "common.h"

#include <cstring>

extern "C" {
volatile uint8_t g_autoaim_off;          // read by the thunk on every call
uint32_t g_autoaim_resume = 0x001D1E46; // the instruction after the replaced prologue
}

namespace {

constexpr uint32_t kSite = 0x001D1E40;
const uint8_t kExpect[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0}; // push ebp; mov ebp,esp; and esp,-16 (replaced)
bool g_installed;

#if defined(_MSC_VER)
__declspec(naked) void assist_thunk()
{
    __asm {
        cmp byte ptr [g_autoaim_off], 0
        jne skip
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF0h
        jmp dword ptr [g_autoaim_resume]
    skip:
        ret 4
    }
}
#else
__attribute__((naked)) void assist_thunk()
{
    __asm__ volatile (
        "cmpb $0, g_autoaim_off\n\t"
        "jne 1f\n\t"
        "push %ebp\n\t"
        "mov %esp, %ebp\n\t"
        "and $0xfffffff0, %esp\n\t"
        "jmp *g_autoaim_resume\n\t"
        "1:\n\t"
        "ret $4\n\t"
    );
}
#endif

} // namespace

void install_autoaim_patch()
{
    auto *code = (uint8_t *)(uintptr_t)kSite;
    if (memcmp(code, kExpect, sizeof kExpect) != 0) {
        logf("autoaim: code at 0x%08x does not match this build; the auto-aim switch does nothing", kSite);
        return;
    }
    autoaim_set(g_cfg.autoaim);
    code[0] = 0xE9; // jmp thunk, then a nop over the rest of the 6-byte prologue
    int32_t rel = (int32_t)((uintptr_t)assist_thunk - (kSite + 5));
    memcpy(code + 1, &rel, 4);
    code[5] = 0x90;
    g_installed = true;
    logf("autoaim: switch installed at FUN_001d1e40 (auto-aim %s)", g_cfg.autoaim ? "on" : "off");
}

void autoaim_set(bool on)
{
    g_cfg.autoaim = on;
    g_autoaim_off = on ? 0 : 1;
}
