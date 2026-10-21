// Tactical lean (experimental): while a lean key is held the first-person view rolls and the eye moves sideways.
//
// The player's view matrix is built once per frame by FUN_001dfd90 (ecx = esi = the player's camera object, called
// only from the first-person update FUN_001e5fa0 at 0x1E5FE1). Rows, D3D style (row vectors, y up):
//   +0xD0 left (right-handed), +0xE0 up, +0xF0 forward, +0x100 eye position
// It starts from the yaw rotation (0x14FE0, yaw = [[esi+0x324]+0x18] in degrees) at the smoothed position +0xA0,
// applies pitch (0x16260, [[esi+0x324]+0x1C]), then raises the eye: +0x104 = y + [esi+0x2E0] - 0.2 (0x1DFFEC). The
// same matrix is the render camera's source (FUN_002007d0 copies *0x2D45D4 + 0x100, = esi + 0xD0, into the camera
// manager's quaternion +0x760 / position +0x770) and the player's aim matrix ([ctrl+0x8C]+0xD0: auto-aim casts along
// it), so the lean is applied there, right after the eye height is set: the store at 0x1DFFEC is replaced by a call to
// a thunk that does the store, then rolls left/up about the forward row and moves the eye sideways, as far as the
// game's line test (FUN_001daff0) finds no wall. The first-person gun is placed from this matrix (0x14DE0 at
// 0x1E6150), so it follows the lean (it looks unrolled on screen because it rolls with the view).
// Nothing reads the matrix back as a position the next frame (the next frame starts again from +0xA0).
// Addresses are the NTSC-U build's; xbe_va maps them (PAL: the same code, .text 0x30 and the data 0x20 further on).
#include "input/lean.h"

#include "common.h"
#include "xbe.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" void __cdecl lean_apply(uint8_t *cam);
extern "C" uint32_t g_lean_line_test = 0x001DAFF0; // FUN_001DAFF0 in this build, set at install

namespace {

constexpr uint32_t kSite = 0x001DFFEC;
const uint8_t kExpect[8] = {0xF3, 0x0F, 0x11, 0x86, 0x04, 0x01, 0x00, 0x00}; // movss [esi+0x104], xmm0
constexpr float kTime = 0.2f;    // seconds from upright to full lean

volatile int g_dir;  // -1 left, 0 none, +1 right: from the key poll (bindings.cpp)
float g_amount;      // eased -1..1, advanced once per view update
bool g_installed;
bool g_blocked; // the last view update shortened the eye shift (logged when it starts)

// 0x1DFFEC: the eye-height store it replaces, then the lean. eax / ecx / edx are not live here (the next instructions
// call FUN_0017f0a0 with eax already pushed, then reload them); no xmm register is live either.
__declspec(naked) void lean_thunk()
{
    __asm {
        movss dword ptr [esi + 0x104], xmm0
        push eax
        push ecx
        push edx
        push esi
        call lean_apply
        add esp, 4
        pop edx
        pop ecx
        pop eax
        ret
    }
}

constexpr float kMargin = 0.15f; // the leaned eye stays this far from walls

// FUN_001daff0: the game's line test (the first-person gun uses it at 0x1E6256 to pull the muzzle out of walls).
// eax = start, ecx = end (16-byte aligned vec4s); stdcall (world *0x2D45D4, mask, entity to ignore, flag, out);
// returns al = hit; out +0x00 = hit point, +0x10 = normal, +0x20 = fraction. Mask 0x13, flag 1 as the gun's test.
// Checked: a 50 m cast ahead hits terrain at fraction 0.884. The forest trees of the first mission are not in it (no hit
// with masks 0x13, 0x7F or 0xFFFF where a trunk stood 1-2 m away), so the leaned eye can still go into a tree.
// The vectors are static (aligned for the game's movaps; only the game thread runs the lean).
struct alignas(16) Ray {
    float from[4], to[4];
    uint8_t result[0x40];
};
Ray g_ray;

bool line_hit(uint32_t world, uint32_t ignore, float *hit)
{
    memset(g_ray.result, 0, sizeof g_ray.result);
    const float *from = g_ray.from, *to = g_ray.to;
    uint8_t *result = g_ray.result;
    uint8_t r;
    __asm {
        push result
        push 1
        push ignore
        push 0x13
        push world
        mov eax, from
        mov ecx, to
        mov edx, g_lean_line_test
        call edx
        mov r, al
    }
    if (r) memcpy(hit, g_ray.result, 12);
    return r != 0;
}

} // namespace

extern "C" void __cdecl lean_apply(uint8_t *cam)
{
    // only the player's camera: its matrix is the one at *0x2D45D4 + 0x100 (the render camera's source)
    uint32_t view = *(const uint32_t *)(uintptr_t)xbe_va(0x002D45D4);
    if (!view || view + 0x100 != (uint32_t)(uintptr_t)(cam + 0xD0)) return;
    float dt = 1.0f / (float)g_cfg.fps; // the world's fixed step
    float target = (float)g_dir, step = dt / kTime;
    g_amount += std::clamp(target - g_amount, -step, step);
    if (g_amount == 0.0f) return;
    float s = g_amount * g_amount * (3.0f - 2.0f * std::fabs(g_amount)); // smoothstep of |amount|
    s = g_amount < 0 ? -s : s; // + = right
    // The game is right-handed: row +0xD0 points to the camera's left (checked: shifting along it moved the eye left).
    float *left = (float *)(cam + 0xD0), *up = (float *)(cam + 0xE0), *pos = (float *)(cam + 0x100);
    float a = s * g_cfg.lean_angle * 0.017453292f, c = std::cos(a), sn = std::sin(a);
    // leaning right tips the top of the view to the right: up turns away from left, left turns toward up
    for (int i = 0; i < 3; i++) {
        float l = left[i], u = up[i];
        left[i] = l * c + u * sn;
        up[i] = u * c - l * sn;
    }
    // the eye moves sideways (along the rolled left axis), no further than the nearest wall allows
    float want = s * g_cfg.lean_offset; // + = right
    float dist = std::fabs(want);
    if (dist < 1e-4f) return;
    float *from = g_ray.from, *to = g_ray.to, hit[3], dir[3];
    for (int i = 0; i < 3; i++) dir[i] = -left[i] * (want > 0 ? 1.0f : -1.0f);
    for (int i = 0; i < 3; i++) {
        from[i] = pos[i];
        to[i] = pos[i] + dir[i] * (dist + kMargin);
    }
    from[3] = to[3] = 1.0f;
    if (line_hit(view, (uint32_t)(uintptr_t)cam, hit)) {
        float dx = hit[0] - from[0], dy = hit[1] - from[1], dz = hit[2] - from[2];
        float allowed = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - kMargin);
        if (allowed < dist && !g_blocked) logf("lean: wall %.2f m to the side, eye shift %.2f of %.2f m", allowed + kMargin, allowed, dist);
        g_blocked = allowed < dist;
        dist = std::min(dist, allowed);
    } else {
        g_blocked = false;
    }
    for (int i = 0; i < 3; i++) pos[i] += dir[i] * dist;
}

void install_lean_patch()
{
    uint32_t site = xbe_va(kSite);
    auto *code = (uint8_t *)(uintptr_t)site;
    if (memcmp(code, kExpect, sizeof kExpect) != 0) {
        logf("lean: code at 0x%08x does not match this build; no lean", site);
        return;
    }
    g_lean_line_test = xbe_va(0x001DAFF0);
    code[0] = 0xE8; // call thunk, then nops over the rest of the 8-byte store
    int32_t rel = (int32_t)((uintptr_t)lean_thunk - (site + 5));
    memcpy(code + 1, &rel, 4);
    code[5] = code[6] = code[7] = 0x90;
    g_installed = true;
    logf("lean: installed at 0x%08x (view build FUN_001dfd90)", site);
}

void lean_input(int dir) { g_dir = std::clamp(dir, -1, 1); }
