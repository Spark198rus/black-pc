// Fast menus (PC SETTINGS > TWEAKS > FAST MENUS; issue #41): the menus' animations play twice as fast. Off by default.
//
// The front end update FUN_00124330 (FE object *0x2D199C) passes the frame time in ms (FE +0x377C x 1000: 16 at 60 fps)
// to FUN_00112520, which hands it to the Apt player's advance loop FUN_00110650 (eax = ms, 0x11258C): the loop adds it to
// an accumulator and runs one Apt frame per 33 ms (every page of FEMain.bin has 33 ms frames), so the time it is given is
// the menus' speed. The `mov eax, [ebp+8]; call 0x110650` there (8 bytes, 0x112589) becomes a jump to a thunk that
// doubles eax while a menu page is up (not in play: the HUD uses the same player) and the page is not a movie, credits,
// legal, boot or intro screen (timed against their sound). Page names seen (FE +0x3840): BootSequence, PressStart,
// memcardmessage, IntroCredits, BlackIntroSequence, blacktitlemenu2, Difficulty, FMVPlayer, PauseMenu, '' in play. Only at frame times up to 20 ms (60 fps): doubled, 32 ms still runs
// at most one Apt frame per update, so no FE call (sounds, page changes, PC SETTINGS calls; one buffer, dispatched once
// per update by FUN_00112F30) and no menu input (FEInputId, seen by every frame of an update) is doubled or lost. At
// 30 fps (33 ms) it would run two, so it does nothing there.
#include "fe/menu_speed.h"

#include "common.h"
#include "input/bindings.h"
#include "xbe.h"

#include <cctype>
#include <cstring>

extern "C" {
uint32_t g_ms_advance;  // FUN_00110650 in this build
uint32_t g_ms_resume;   // the instruction after the replaced call
uint32_t __cdecl menu_speed_ms(uint32_t ms);
}

namespace {

bool g_installed;

// 0x112589: mov eax, [ebp+8]; call FUN_00110650 (eax = ms); the result in eax is tested after
__declspec(naked) void advance_thunk()
{
    __asm {
        mov eax, dword ptr [ebp + 8]
        push ecx
        push edx
        push eax
        call menu_speed_ms
        add esp, 4
        pop edx
        pop ecx
        call dword ptr [g_ms_advance]
        jmp dword ptr [g_ms_resume]
    }
}

bool slow_page(const char *name)
{
    static const char *const kKeep[] = {"fmv", "credit", "legal", "boot", "intro"};
    char low[64];
    size_t n = 0;
    for (; name[n] && n < sizeof low - 1; n++) low[n] = (char)tolower((unsigned char)name[n]);
    low[n] = 0;
    for (const char *k : kKeep)
        if (strstr(low, k)) return true;
    return false;
}

} // namespace

extern "C" uint32_t __cdecl menu_speed_ms(uint32_t ms)
{
    if (!g_cfg.fast_menus || ms == 0 || ms > 20 || bindings::in_play()) return ms;
    uint32_t fe = *(const uint32_t *)(uintptr_t)xbe_va(0x002D199C);
    if (!fe) return ms;
    const char *page = (const char *)(uintptr_t)(fe + 0x3840);
    if (!page[0] || slow_page(page)) return ms;
    return ms * 2;
}

void install_menu_speed_patch()
{
    static const uint8_t kExpect[] = {0x8B, 0x45, 0x08, 0xE8};
    uint32_t at = xbe_va(0x00112589);
    const uint8_t *code = (const uint8_t *)(uintptr_t)at;
    if (memcmp(code, kExpect, sizeof kExpect) != 0) {
        logf("menus: code at 0x%08x does not match this build; FAST MENUS does nothing", at);
        return;
    }
    int32_t rel;
    memcpy(&rel, code + 4, 4);
    g_ms_advance = at + 8 + (uint32_t)rel; // the call's target
    if (g_ms_advance != xbe_va(0x00110650)) {
        logf("menus: the call at 0x%08x goes to 0x%08x, not the Apt advance; FAST MENUS does nothing", at + 3, g_ms_advance);
        return;
    }
    g_ms_resume = at + 8;
    uint8_t j[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    int32_t to = (int32_t)((uintptr_t)&advance_thunk - (at + 5));
    memcpy(j + 1, &to, 4);
    auto *p = (volatile uint8_t *)(uintptr_t)at;
    for (int i = 0; i < 8; i++) p[i] = j[i];
    g_installed = true;
    logf("menus: Apt advance at 0x%08x takes the menu speed (fast menus %s)", at, g_cfg.fast_menus ? "on" : "off");
}
