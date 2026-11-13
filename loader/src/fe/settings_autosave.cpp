// Saved SFX and music volumes (issue #32: the volumes came back at full on the next start; the other Options
// settings were kept).
//
// In BLACK the two volumes are not profile settings: the profile keeps them (its settings block, saved with it), but
// the game neither saves them on their own nor applies them when a profile loads, so every start is at full volume
// (the same on the Xbox). Two places, both changed here:
//
// Saving. StartAutosaveSettings (the front end's call when Options is left; registered at 0x121384) is FUN_00114E00:
// it copies the settings as they were last saved ([0x2D1984] + 0x20, 0x2F dwords) to the stack, overwrites four of
// those fields with the current values, and compares the result with the current settings (game object [0x2D45F0] +
// 0x2014C). Only if they differ, a profile is loaded ([[0x2D19B4]]) and autosave is on (its byte +0x6A5 clear) does
// it start the autosave (game +0x20264 = 1, [0x2D19BC] +0xC = 5). The four fields it hides are +4 (game +0x20150),
// +8 (+0x20154, the custom soundtrack: [0x2AC344]) and +0x10 / +0x14 (+0x2015C / +0x20160: the SFX and the music
// volume, 0..1). So a volume change alone never started a save (found with a probe: SFX 100 -> 80 left only +0x10
// different, and no save followed). The two volume stores (movss [esp+0x1C], xmm0 at 0x114E43 and movss [esp+0x18],
// xmm0 at 0x114E62) become NOPs, so the old volumes stay in the comparison.
//
// Loading. FUN_001FB300 (ret 4, the argument = the loaded profile record [0x2D1984]; its settings block at +0x20,
// the volumes at +0x30 / +0x34) applies a loaded profile: settings +0 to game +0x2014C, +0xC to +0x20158, the bytes +0x18 / +0x19 / +0x1B /
// +0x1C / +0x24 and the 0x25 dwords from +0x28, the progress after them; it skips +4, +8 and both volumes (+0x10,
// +0x14). Its callers: the load at the start (0x1FEC2F), the profile menu's LOAD (0x1FF1A1) and 0x2046F3. A jump at
// its entry (mov eax, [0x2D45F0], 5 bytes) runs it and then copies the two volumes from the record (when 0..1) to
// game +0x2015C / +0x20160 and to the sliders' values (0x2F416C / 0x2F4170, 0..100, as FUN_0011B1D0 does), and
// calls FUN_00119B50, which hands the volumes to the sound code.
#include "fe/settings_autosave.h"

#include "common.h"
#include "xbe.h"

#include <cmath>
#include <cstring>

extern "C" {
uint32_t g_vol_resume, g_vol_game_ptr, g_vol_apply;
void __cdecl vol_loaded(const uint8_t *record);
}

namespace {

// FUN_001FB300's first instruction, then the rest of it
__declspec(naked) void vol_original()
{
    __asm {
        mov eax, dword ptr [g_vol_game_ptr]
        mov eax, dword ptr [eax]
        jmp dword ptr [g_vol_resume]
    }
}

// at FUN_001FB300: [esp+4] = the profile record
__declspec(naked) void vol_thunk()
{
    __asm {
        push dword ptr [esp + 4]
        call vol_original // ret 4
        push dword ptr [esp + 4]
        call vol_loaded
        add esp, 4
        ret 4
    }
}

} // namespace

extern "C" void __cdecl vol_loaded(const uint8_t *record)
{
    if (!record) return;
    uint32_t game = *(const uint32_t *)(uintptr_t)g_vol_game_ptr;
    if (!game) return;
    float sfx = *(const float *)(record + 0x30), music = *(const float *)(record + 0x34);
    if (!(sfx >= 0.0f && sfx <= 1.0f && music >= 0.0f && music <= 1.0f)) return;
    *(float *)(uintptr_t)(game + 0x2015C) = sfx;
    *(float *)(uintptr_t)(game + 0x20160) = music;
    *(int32_t *)(uintptr_t)xbe_va(0x002F416C) = (int32_t)(sfx * 100.0f + 0.5f);
    *(int32_t *)(uintptr_t)xbe_va(0x002F4170) = (int32_t)(music * 100.0f + 0.5f);
    ((void(__cdecl *)())(uintptr_t)g_vol_apply)();
    static bool told;
    if (!told) logf("fe: profile loaded: SFX volume %.0f, music volume %.0f", sfx * 100.0f, music * 100.0f);
    told = true;
}

void install_settings_autosave_patch()
{
    struct Site {
        uint32_t addr;
        uint8_t expect[6];
    };
    static const Site kSites[] = {
        {0x00114E43, {0xF3, 0x0F, 0x11, 0x44, 0x24, 0x1C}}, // movss [esp+0x1C], xmm0 (music volume)
        {0x00114E62, {0xF3, 0x0F, 0x11, 0x44, 0x24, 0x18}}, // movss [esp+0x18], xmm0 (SFX volume)
    };
    for (const Site &s : kSites)
        if (memcmp((const void *)(uintptr_t)xbe_va(s.addr), s.expect, 6)) {
            logf("fe: code at 0x%08x does not match this build; the volumes are not saved", xbe_va(s.addr));
            return;
        }
    static const uint8_t kApply[] = {0xA1, 0xF0, 0x45, 0x2D, 0x00, 0x53, 0x55}; // mov eax, [0x2D45F0]; push ebx; push ebp
    uint8_t *apply = (uint8_t *)(uintptr_t)xbe_va(0x001FB300);
    uint8_t expect_apply[7];
    memcpy(expect_apply, kApply, 7);
    uint32_t game_ptr = xbe_va(0x002D45F0);
    memcpy(expect_apply + 1, &game_ptr, 4);
    if (memcmp(apply, expect_apply, 7)) {
        logf("fe: code at 0x%08x does not match this build; saved volumes are not applied", xbe_va(0x001FB300));
        return;
    }
    for (const Site &s : kSites) memset((void *)(uintptr_t)xbe_va(s.addr), 0x90, 6);
    g_vol_game_ptr = game_ptr;
    g_vol_apply = xbe_va(0x00119B50);
    g_vol_resume = (uint32_t)(uintptr_t)apply + 5;
    int32_t rel = (int32_t)((uintptr_t)&vol_thunk - ((uintptr_t)apply + 5));
    apply[0] = 0xE9;
    memcpy(apply + 1, &rel, 4);
    logf("fe: the SFX and music volumes are saved with the profile and applied when it loads (FUN_00114E00, FUN_001FB300)");
}
