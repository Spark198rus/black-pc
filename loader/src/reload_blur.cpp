// Reload blur switch, on by default (PC SETTINGS > TWEAKS, --no-reload-blur / --reload-blur).
//
// The blur is mode 1 of the post-process controller at *0x2D1990 + 0x830A0 (mode +0x34 = game +0x830D4, timer +0x14,
// strength +0x10 = blend alpha of the blur pass FUN_001772E0). The controller update FUN_0015E260 runs mode 1 through
// FUN_001595F0: fade in over +0x1C, hold +0x20, fade out +0x24, then back to mode 0.
//
// Mode 1 is started by the first-person animation event handler FUN_0017B130, which looks at the last three
// characters of the animation name, in two ways:
//  - "101" / "501" (one-piece reloads; not names starting with "FP_RPG"): it computes the hold time from the
//    animation length (0x17B314, see timing.cpp), then only starts the blur when no other effect runs:
//      0x17B356  mov ecx, [eax+0x830D4]
//      0x17B35C  test ecx, ecx
//      0x17B35E  jne 0x17B3AA        ; another mode is active: leave
//      0x17B380  mov dword [eax+0x830D4], 1
//  - "180": the start of a looped reload (the shotguns only, FP_SG_S_* and FP_rm1_S_*: 180 start, 181 one shell,
//    looped, 182 end) calls FUN_001571C0 (mode 1 if idle; fade-in 0.5, hold 6.0, fade-out 1.0), and "182" calls
//    FUN_00159480, which cuts that hold short so the blur fades out with the reload:
//      0x17B293  repe cmpsb           ; name ends in "180"?
//      0x17B295  jne 0x17B2BF         ; no: try "182"
//      0x17B2B0  call 0x1571C0
// With the blur off both jumps become jmps: the "101" / "501" path leaves before setting the mode, and a "180" name
// goes on to the "182" test, fails it and leaves. "182" alone does nothing when mode 1 is not running. The other
// starter of mode 1, FUN_001FD1F0 at 0x1FD545 (scripted, not a reload: no fade-in, hold 0.75, fade-out 1.25), is
// untouched. Mode 1 also moves the gun's draw after the effects pass (FUN_001EE820, see fov.cpp); without the blur
// it simply stays in its usual place. One byte per site, written on every change, so the menu switches it live.
// Addresses are the NTSC-U build's; xbe_va maps them (the PAL build has the same code 0x30 further on).
#include "reload_blur.h"

#include "common.h"
#include "xbe.h"

#include <cstring>

namespace {

struct Site {
    uint32_t addr;       // the jne is at addr + 2
    uint8_t expect[4];
};
const Site kSites[] = {
    {0x0017B35C, {0x85, 0xC9, 0x75, 0x4A}}, // test ecx, ecx; jne +0x4A (0x17B3AA): "101" / "501"
    {0x0017B293, {0xF3, 0xA6, 0x75, 0x28}}, // repe cmpsb; jne +0x28 (0x17B2BF): "180"
};
constexpr uint8_t kJne = 0x75, kJmp = 0xEB;
bool g_installed;

} // namespace

void install_reload_blur_patch()
{
    for (const Site &site : kSites) {
        if (memcmp((const uint8_t *)(uintptr_t)xbe_va(site.addr), site.expect, sizeof site.expect) != 0) {
            logf("reload blur: code at 0x%08x does not match this build; the reload blur switch does nothing", xbe_va(site.addr));
            return;
        }
    }
    g_installed = true;
    reload_blur_set(g_cfg.reload_blur);
    logf("reload blur: switch installed at 0x%08x and 0x%08x (reload blur %s)", xbe_va(kSites[0].addr) + 2,
         xbe_va(kSites[1].addr) + 2, g_cfg.reload_blur ? "on" : "off");
}

void reload_blur_set(bool on)
{
    g_cfg.reload_blur = on;
    if (!g_installed) return;
    for (const Site &site : kSites) *(volatile uint8_t *)(uintptr_t)(xbe_va(site.addr) + 2) = on ? kJne : kJmp;
}
