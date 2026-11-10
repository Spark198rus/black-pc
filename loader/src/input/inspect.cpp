// Show weapon (the Show weapon key, default T; issue #41 follow-up, user: like the inspect key in CS2). Not a new
// animation: BLACK's first-person idle clip FP_<gun>_S_001 ends in the hand turning the gun over (the game plays it
// every ~20-25 s while the player stands still). The key moves the idle clip's time to the start of that part, so
// it plays at once; the game then loops the idle clip as usual. Nothing happens unless the idle clip is the one
// playing (moving, firing, aiming, reloading and switching play other clips).
// The clip [player+0x0C] is a node of the gun's animation database (levels/*/fpguns/*.db, loaded as is): +0 type
// (low byte the kind, next byte the child count), +4 the length in frames (filled at load for leaves), children
// from +0x10. Kind 2 is a sequence, 8 a repeat, 0x16 a leaf (one animation). Every idle clip (13 guns) is a
// sequence of one sequence whose children are the plain idle loop (leaves or repeats of it) and, last, one other
// leaf: the inspection (pistol 202 of 1402 frames, AK 232 of 1307, shotgun 262, SMG 210, sniper 262, ...).
// The first-person animation object: the argument of FUN_0017B130 (runs every frame for every animated character)
// whose current name +0x5C starts with FP_; requested name +0x76; its animation player [obj+0x54] has the clip time
// in 60 Hz frames at +0x14 (not wrapped: the player takes it modulo the length), the previous time at +0x18 and the
// clip length at +0x1C (FUN_000970C0 sets a clip and zeroes the time).
#include "input/inspect.h"
#include "xbe.h"

#include "common.h"

#include <cmath>
#include <cstring>

extern "C" {
uint32_t g_insp_resume;
uint8_t *g_insp_obj;      // the first-person animation object
uint32_t g_insp_seen;     // frame it was last seen
void __cdecl insp_seen(uint8_t *obj);
}

namespace {

// The length of the idle clip's last leaf (the inspection), or 0 if the clip is not shaped like that.
float inspection_length(const uint8_t *node)
{
    for (int depth = 0; node && depth < 4; depth++) {
        uint32_t type = *(const uint32_t *)node, kind = type & 0xFF, count = (type >> 8) & 0xFF;
        if (kind != 2 || count == 0 || count > 16) return 0.0f;
        const uint8_t *last = *(const uint8_t *const *)(node + 0x10 + 4 * (count - 1));
        if (count == 1) {
            node = last;
            continue;
        }
        if (!last || (*(const uint32_t *)last & 0xFF) != 0x16) return 0.0f;
        return *(const float *)(last + 4);
    }
    return 0.0f;
}

// 0x17B130: sub esp, 8; push ebp; mov ebp, [esp+0x10] (8 bytes); the object is [esp+4] at entry
__declspec(naked) void insp_thunk()
{
    __asm {
        pushad
        push dword ptr [esp + 36]
        call insp_seen
        add esp, 4
        popad
        sub esp, 8
        push ebp
        mov ebp, dword ptr [esp + 0x10]
        jmp dword ptr [g_insp_resume]
    }
}

bool g_installed, g_held, g_was_held;

bool idle_name(const uint8_t *n) // FP_<gun>_S_001
{
    char s[14];
    memcpy(s, n, 13);
    s[13] = 0;
    size_t l = strlen(s);
    return l > 7 && !memcmp(s, "FP_", 3) && !strcmp(s + l - 6, "_S_001");
}

} // namespace

extern "C" void __cdecl insp_seen(uint8_t *obj)
{
    if (obj && !memcmp(obj + 0x5C, "FP_", 3)) {
        g_insp_obj = obj;
        g_insp_seen = g_frame_count.load();
    }
}

void install_inspect_patch()
{
    static const uint8_t kExpect[] = {0x83, 0xEC, 0x08, 0x55, 0x8B, 0x6C, 0x24, 0x10};
    uint8_t *at = (uint8_t *)(uintptr_t)xbe_va(0x0017B130);
    if (memcmp(at, kExpect, sizeof kExpect)) {
        logf("inspect: code at 0x%08x does not match this build; the Show weapon key does nothing", xbe_va(0x0017B130));
        return;
    }
    g_insp_resume = (uint32_t)(uintptr_t)at + 8;
    int32_t r = (int32_t)((uintptr_t)&insp_thunk - ((uintptr_t)at + 5));
    at[0] = 0xE9;
    memcpy(at + 1, &r, 4);
    at[5] = at[6] = at[7] = 0x90;
    g_installed = true;
    logf("inspect: the Show weapon key plays the end of the first-person idle clip (FUN_0017B130 at 0x%08x)", xbe_va(0x0017B130));
}

void inspect_input(bool held) { g_held = held; }

void inspect_frame()
{
    bool press = g_held && !g_was_held;
    g_was_held = g_held;
    if (!press || !g_installed || !g_insp_obj || g_frame_count.load() - g_insp_seen > 10) return;
    uint8_t *obj = g_insp_obj;
    if (!idle_name(obj + 0x5C) || memcmp(obj + 0x5C, obj + 0x76, 13)) return; // idle, no other clip requested
    uint8_t *inst = *(uint8_t **)(obj + 0x54);
    if (!inst) return;
    float &t = *(float *)(inst + 0x14), &prev = *(float *)(inst + 0x18), len = *(const float *)(inst + 0x1C);
    float tail = inspection_length(*(const uint8_t *const *)(inst + 0x0C));
    if (!(tail > 0.0f && tail < len)) {
        static bool told_shape;
        if (!told_shape) logf("inspect: idle clip %.13s is not a sequence ending in one animation; nothing to show", (const char *)(obj + 0x5C));
        told_shape = true;
        return;
    }
    float start = len - tail, phase = fmodf(t, len);
    if (phase >= start - 1.0f) return; // already showing it
    t = prev = t - phase + start;
    static char told[14]; // once per gun
    if (memcmp(told, obj + 0x5C, 13)) {
        memcpy(told, obj + 0x5C, 13);
        logf("inspect: idle clip %.13s, %.0f frames, the inspection its last %.0f: jumped from %.0f to %.0f", told, len, tail, phase, start);
    }
}
