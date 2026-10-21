// Field of view in play.
//
// The renderer's camera (renderer *0x2D12B8, camera at +0x740) keeps its FOV in degrees at +0x750 (= camera +0x10).
// It is horizontal: the view setup (FUN_000D2F40, FUN_002033A0) takes tan(fov / 2) x near (+0x70) as the half
// width and divides by the aspect +0x74 (1.778 in widescreen, 1.333 in 4:3) for the half height, so the widescreen
// picture shows less height than 4:3 (43 instead of 55 degrees; 86 degrees in widescreen matches the 4:3 height).
// In play the player's weapon code FUN_001BB110 rewrites it every frame as 70 / zoom (zoom 1 at the hip, more
// while aiming: 44.1 with the pistol); the camera system resets it to 70 (FUN_00202850) and the renderer sets it
// and the blend target +0x7A0 to 70 at start (FUN_00203610). Cutscene cameras blend their own authored values.
// All three take 70 from the constant 0x2888C8, which the compiler shares with the pitch clamp (+-70 degrees,
// FUN_001BD3F0) and a few unrelated uses, so the constant stays and those three loads read the port's value
// instead. Aiming keeps its magnification: the aimed FOV is the setting / zoom.
//
// The first-person gun and hands are a render list of their own (renderer *0x2D67F4 + 0xCAE4), drawn by
// FUN_001EE820 at 0x1EE97C or 0x1EEB2B (FUN_00176F60, with the game's flag 0x2F3C44 set around it). Made for 70 degrees, they
// shrink and their animations show their ends at a wider FOV, so that call goes through a thunk that marks the
// list's draws, and the translated vertex shaders scale their clip-space x/y (fov_viewmodel_scale, constant c200)
// so they project as with the game's own FOV (70 / zoom); depth stays, so clipping and sorting do not change.
// The muzzle flash is not on the gun in the original either: it sits on the aim line near the screen centre.
#include "fov.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
volatile uint8_t g_fov_viewmodel;         // set while the first-person list draws (the thunk)
uint32_t g_fov_viewmodel_list = 0x00176F60; // the render-list function the patched call goes to
}

namespace {

float g_fov_deg = 70.0f;

// call FUN_00176F60 with the first-person list: before the effects pass, or after it when
// [[0x2D1990]+0x830D4] == 1 (FUN_001EE820)
constexpr uint32_t kViewmodelCalls[] = {0x001EE97C, 0x001EEB2B};

#if defined(_MSC_VER)
__declspec(naked) void viewmodel_thunk()
{
    __asm {
        mov byte ptr [g_fov_viewmodel], 1
        call dword ptr [g_fov_viewmodel_list]
        mov byte ptr [g_fov_viewmodel], 0
        ret
    }
}
#else
__attribute__((naked)) void viewmodel_thunk()
{
    __asm__ volatile (
        "movb $1, g_fov_viewmodel\n\t"
        "call *g_fov_viewmodel_list\n\t"
        "movb $0, g_fov_viewmodel\n\t"
        "ret\n\t"
    );
}
#endif

struct Site {
    uint32_t addr;
    uint8_t expect[8]; // movss xmmN, dword ptr ds:[0x2888C8]
};
const Site kSites[] = {
    {0x001BB2CA, {0xF3, 0x0F, 0x10, 0x0D, 0xC8, 0x88, 0x28, 0x00}}, // xmm1: 70 / zoom in play (FUN_001BB110)
    {0x00202F92, {0xF3, 0x0F, 0x10, 0x05, 0xC8, 0x88, 0x28, 0x00}}, // xmm0: camera back to the player's FOV (FUN_00202850)
    {0x002038E5, {0xF3, 0x0F, 0x10, 0x05, 0xC8, 0x88, 0x28, 0x00}}, // xmm0: renderer start, +0x750 and +0x7A0 (FUN_00203610)
};

} // namespace

void fov_set(float degrees) { g_fov_deg = std::clamp(degrees, 50.0f, 120.0f); }

float fov_viewmodel_scale()
{
    if (!g_fov_viewmodel || std::fabs(g_fov_deg - 70.0f) < 0.01f) return 1.0f;
    uint32_t renderer = *(const uint32_t *)(uintptr_t)0x002D12B8;
    if (!renderer) return 1.0f;
    float now = *(const float *)(uintptr_t)(renderer + 0x750); // this frame's FOV: the setting / zoom
    if (!(now > 1.0f && now < 170.0f)) return 1.0f;
    float game = now * 70.0f / g_fov_deg; // what the game would use: 70 / zoom
    const float half = 3.14159265f / 360.0f;
    return std::tan(now * half) / std::tan(game * half);
}

void install_fov_patch()
{
    fov_set(g_cfg.fov);
    for (const Site &s : kSites)
        if (memcmp((const void *)(uintptr_t)s.addr, s.expect, sizeof s.expect) != 0) {
            logf("fov: code at 0x%08x does not match this build; field of view stays the game's", s.addr);
            return;
        }
    uint32_t target = (uint32_t)(uintptr_t)&g_fov_deg;
    for (const Site &s : kSites) memcpy((void *)(uintptr_t)(s.addr + 4), &target, 4); // the load's address operand
    for (uint32_t at : kViewmodelCalls) {
        auto *call = (uint8_t *)(uintptr_t)at;
        int32_t rel;
        memcpy(&rel, call + 1, 4);
        if (call[0] == 0xE8 && at + 5 + rel == g_fov_viewmodel_list) {
            rel = (int32_t)((uintptr_t)&viewmodel_thunk - (at + 5));
            memcpy(call + 1, &rel, 4);
        } else {
            logf("fov: the first-person list call at 0x%08x does not match; the gun follows the FOV there", at);
        }
    }
    logf("fov: field of view in play %.0f degrees (horizontal; the game's is 70)", g_fov_deg);
}
