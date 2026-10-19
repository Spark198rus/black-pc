// Mouse look straight into the player's view angles, bypassing the stick path.
//
// FUN_001ec450 (player input, ebx = player controller) reads the remappable stick channels into look values
// (+0xD8 horizontal = right-stick left minus right, +0xDC vertical, an invert flag at +0x101; channels below
// 0.1 are zeroed after the controller's own dead zone) and calls FUN_001bd3f0(vertical, horizontal) with
// esi = the controller. That function shapes the input (power curve with exponent +0xC8, turn acceleration
// near full deflection via +0xC0/+0xC4/+0xD0/+0xD4, smoothing +0xCC into +0xA0/+0xA4, x0.7 while aiming) and
// integrates the view angles in degrees:
//   yaw   [esi+0x18] += speed +0xB8 * input +0xA4 * zoom factor * frame time   (wrapped to -180..180)
//   pitch [esi+0x1C] += speed +0xBC * input +0xA0 * zoom factor * frame time   (clamped to -70..70)
// The zoom factor (1 / (1 + (zoom - 1) * 0.8)) is in xmm2 at both additions.
//
// The two additions are redirected here and also add the mouse angle of the current frame, times the zoom
// factor (scopes slow the mouse down like the stick). The game then wraps and clamps as usual. Menus,
// pauses and cutscenes do not run this code; each pad poll replaces the previous frame's unused angle.
//
// Weapon sway: FUN_001e5fa0 eases the first-person weapon toward offsets scaled by the look values (it reads
// them through the player object, where the controller sits at +0x4F0: weapon +0x5C8/+0x5CC = controller
// +0xD8/+0xDC; +0x5D0/+0x5D4 = the move values). Mouse look leaves the look values at 0, so the weapon did not
// swivel. Right after the view update (0x1ECF76, after the call to FUN_001bd3f0, where no xmm register is
// live) the stick deflection that would have turned the view as fast as this frame's mouse angle is added to
// them: rate = angle / (speed * frame time), deflection = rate ^ (1 / curve exponent +0xC8), clamped to 1 and
// smoothed a little. +0xD8 has the yaw rate's sign, +0xDC the opposite of the pitch rate's (as the game
// stores it). Later readers in the frame are the weapon sway; the values are rebuilt from the pad next frame.
#include "input/mouselook.h"

#include "common.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
float g_ml_pending_yaw, g_ml_pending_pitch; // degrees for the next view update (set at each pad poll)
float g_ml_used_yaw, g_ml_used_pitch;       // what the last view update used (for the weapon sway)
float g_ml_xmm7;                            // scratch save
void __cdecl ml_sway(uint8_t *ctrl);
}

namespace {

bool g_installed;
std::atomic<uint32_t> g_play_frame; // the frame of the last player update (ml_sway runs once in each)

// 0x1BD6A8: addss xmm3, [esi+0x18]  (xmm3 = this frame's stick yaw step; xmm2 = zoom factor)
__declspec(naked) void yaw_thunk()
{
    __asm {
        addss xmm3, dword ptr [esi + 0x18]
        movss dword ptr [g_ml_xmm7], xmm7
        movss xmm7, dword ptr [g_ml_pending_yaw]
        movss dword ptr [g_ml_used_yaw], xmm7
        mulss xmm7, xmm2
        addss xmm3, xmm7
        movss xmm7, dword ptr [g_ml_xmm7]
        mov dword ptr [g_ml_pending_yaw], 0
        ret
    }
}

// 0x1BD6C7: addss xmm0, [esi+0x1C]  (xmm0 = this frame's stick pitch step; clamped right after)
__declspec(naked) void pitch_thunk()
{
    __asm {
        addss xmm0, dword ptr [esi + 0x1C]
        movss dword ptr [g_ml_xmm7], xmm7
        movss xmm7, dword ptr [g_ml_pending_pitch]
        movss dword ptr [g_ml_used_pitch], xmm7
        mulss xmm7, xmm2
        addss xmm0, xmm7
        movss xmm7, dword ptr [g_ml_xmm7]
        mov dword ptr [g_ml_pending_pitch], 0
        ret
    }
}

// 0x1ECF76: movss xmm0, [ebx+0x1C] right after the view update; ebx = the player controller.
__declspec(naked) void sway_thunk()
{
    __asm {
        push ebx
        call ml_sway
        add esp, 4
        movss xmm0, dword ptr [ebx + 0x1C]
        ret
    }
}

struct Site {
    uint32_t addr;
    uint8_t expect[5];
    void (*thunk)();
};

const Site kSites[] = {
    {0x001BD6A8, {0xF3, 0x0F, 0x58, 0x5E, 0x18}, yaw_thunk},
    {0x001BD6C7, {0xF3, 0x0F, 0x58, 0x46, 0x1C}, pitch_thunk},
    {0x001ECF76, {0xF3, 0x0F, 0x10, 0x43, 0x1C}, sway_thunk},
};

} // namespace

void install_mouselook_patches()
{
    for (const Site &s : kSites)
        if (memcmp((const void *)(uintptr_t)s.addr, s.expect, 5) != 0) {
            logf("mouselook: code at 0x%08x does not match this build; mouse look disabled", s.addr);
            return;
        }
    for (const Site &s : kSites) { // call thunk (replaces the 5-byte addss)
        auto *code = (uint8_t *)(uintptr_t)s.addr;
        code[0] = 0xE8;
        int32_t rel = (int32_t)((uintptr_t)s.thunk - (s.addr + 5));
        memcpy(code + 1, &rel, 4);
    }
    g_installed = true;
    logf("mouselook: view angles take mouse input directly (FUN_001bd3f0); weapon sway follows it (0x1ECF76)");
}

bool mouselook_installed() { return g_installed; }

uint32_t mouselook_play_frame() { return g_play_frame; }

extern "C" void __cdecl ml_sway(uint8_t *ctrl)
{
    g_play_frame = std::max(1u, g_frame_count.load());
    static float sh, sv; // smoothed deflections
    auto f = [&](uint32_t off) { return *(const float *)(ctrl + off); };
    float dt = *(const float *)(uintptr_t)(*(const uint32_t *)(uintptr_t)0x002D45D4 + 0x1C);
    float curve = f(0xC8) > 0.1f ? f(0xC8) : 1.0f;
    auto deflection = [&](float deg, float speed) {
        if (speed <= 0.0f || dt <= 0.0f || deg == 0.0f) return 0.0f;
        float r = fabsf(deg) / (speed * dt);
        float s = powf(r < 1.0f ? r : 1.0f, 1.0f / curve);
        return deg < 0.0f ? -s : s;
    };
    float h = deflection(g_ml_used_yaw, f(0xB8)), v = -deflection(g_ml_used_pitch, f(0xBC));
    float a = dt > 0.0f ? 1.0f - expf(-dt / 0.05f) : 1.0f; // ~50 ms: mouse counts per frame are uneven
    sh += (h - sh) * a;
    sv += (v - sv) * a;
    g_ml_used_yaw = g_ml_used_pitch = 0.0f;
    float *look_h = (float *)(ctrl + 0xD8), *look_v = (float *)(ctrl + 0xDC);
    *look_h = std::clamp(*look_h + sh, -1.0f, 1.0f);
    *look_v = std::clamp(*look_v + sv, -1.0f, 1.0f);
}

void mouselook_set_frame(float yaw_deg, float pitch_deg)
{
    g_ml_pending_yaw = yaw_deg;
    g_ml_pending_pitch = pitch_deg;
}
