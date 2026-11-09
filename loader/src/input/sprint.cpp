// Sprint (PC SETTINGS > CONTROLS > SPRINT, the Sprint key; issue #41). Not in the original: off by default. On, the
// Sprint key runs 1.25x as fast (user: fits the game's pacing better than 1.5x; the menu shows only ON / OFF).
//
// The player's movement FUN_001CC490 (only caller 0x1ECFBC in the player input FUN_001EC450; esi = the player
// controller) picks speed constants by stance (standing: forward 6.0, back 5.5, side 6.0, cap 4.5; crouched, ctrl
// +0x40: 3.0 / 3.0 / 1.0, cap 2.0; the weapon flag +0x106 of [[ctrl+0x8C]+0x29C]: 4.5 / 4.5 / 3.0, cap 3.25) and
// scales both the wanted velocity and the cap by f = world (*0x2D45D4) +0x514 * 0.3 + 1.0, computed twice
// (0x1CC5C5 and 0x1CC6FD). The constants 0.3 (0x295D00) and 1.0 (0x295C74) are pooled, so the operands of the
// mulss / addss at 0x1CC5CD / 0x1CC5D5 and 0x1CC705 / 0x1CC70D are pointed at port floats 0.3 * s and 1.0 * s:
// s scales the speed and the cap together. ml_sway (input/mouselook.cpp) runs earlier in the same player update
// and sets s: 1.25 while the key is held, moving forward (+0xE4 > 0), not crouched (+0x40), not aiming
// (+0x45), without the +0x106 weapon flag and not reloading; 1 otherwise.
//
// Reloading (user: no reload while sprinting, a reload blocks sprint): the weapon controller's state +0xD8 is 4 while
// a magazine reload plays, 8 from the refill (an animation event, 0x1E89DA -> FUN_001BAF70) to the end of the
// animation, 5 / 6 in the shotgun's shell-by-shell reload; 0 idle, 1 firing. While sprinting the reload request
// (controller +0x44, action 11, set at 0x1ECC2E) is cleared. A reload cannot be interrupted instead: the first-person
// animation drives it (state 4 set back to 0 still played the reload and its event still refilled, tested), and the
// original has no way to stop it (the weapon state machine's state 4 takes no input).
#include "input/sprint.h"

#include "common.h"
#include "xbe.h"

#include <cstring>

extern "C" {
float g_sprint_mul = 0.3f, g_sprint_add = 1.0f; // the operands' new targets
}

namespace {

struct Operand {
    uint32_t insn; // NTSC-U address
    uint8_t op[4];
    uint32_t mem;  // the absolute operand (NTSC-U), mapped with xbe_va
    float *target;
};
const Operand kOps[] = {
    {0x001CC5CD, {0xF3, 0x0F, 0x59, 0x05}, 0x00295D00, &g_sprint_mul}, // mulss xmm0, [0.3]
    {0x001CC5D5, {0xF3, 0x0F, 0x58, 0x05}, 0x00295C74, &g_sprint_add}, // addss xmm0, [1.0]
    {0x001CC705, {0xF3, 0x0F, 0x59, 0x05}, 0x00295D00, &g_sprint_mul},
    {0x001CC70D, {0xF3, 0x0F, 0x58, 0x05}, 0x00295C74, &g_sprint_add},
};

bool g_installed;
volatile bool g_held;

} // namespace

void install_sprint_patch()
{
    for (const Operand &o : kOps) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)xbe_va(o.insn);
        if (memcmp(code, o.op, 4) || *(const uint32_t *)(code + 4) != xbe_va(o.mem)) {
            logf("sprint: code at 0x%08x does not match this build; sprint does nothing", xbe_va(o.insn));
            return;
        }
    }
    for (const Operand &o : kOps) *(uint32_t *)(uintptr_t)(xbe_va(o.insn) + 4) = (uint32_t)(uintptr_t)o.target;
    g_installed = true;
    logf("sprint: movement speed factor at 0x%08x / 0x%08x follows the Sprint key (sprint %s)", xbe_va(0x001CC5C5),
         xbe_va(0x001CC6FD), g_cfg.sprint ? "on" : "off");
}

void sprint_input(bool held) { g_held = held; }

void sprint_frame(uint8_t *ctrl)
{
    if (!g_installed) return;
    float s = 1.0f;
    if (g_held && g_cfg.sprint) {
        float forward = *(const float *)(ctrl + 0xE4);
        bool crouched = ctrl[0x40] != 0, aiming = ctrl[0x45] != 0, slow = false;
        uint32_t body = *(const uint32_t *)(ctrl + 0x8C);
        uint32_t weapon = body ? *(const uint32_t *)(uintptr_t)(body + 0x29C) : 0;
        bool reloading = false;
        if (weapon) {
            slow = *(const uint8_t *)(uintptr_t)(weapon + 0x106) != 0;
            uint32_t state = *(const uint32_t *)(uintptr_t)(weapon + 0xD8);
            reloading = state == 4 || state == 8 || state == 5 || state == 6;
        }
        if (forward > 0.0f && !crouched && !aiming && !slow && !reloading) {
            s = 1.25f;
            ctrl[0x44] = 0; // no reload starts while sprinting (the request, action 11, set at 0x1ECC2E)
        }
    }
    g_sprint_mul = 0.3f * s;
    g_sprint_add = 1.0f * s;
}
