// Cheats (PC SETTINGS > CHEATS; issue #41). All off by default, all live. Each site is checked against the bytes the
// NTSC-U build has there (xbe_va maps it to this build); on a mismatch that cheat logs and does nothing.
//
// The player: world (*0x2D45D4) + 0x30 is the player character (vtable 0x28CFE0; AI characters use 0x28C1E8). The
// update methods are shared: the firing code below runs for enemies too (FUN_001F08D0 is slot +0x44 of both vtables).
//
// Unlock all missions: the mission progress table 0x2D0D80 (an in-memory copy of the profile, records of 0x20 bytes at
// 0x2D0D84 + difficulty * 0x100 + mission * 0x20, unlocked byte at record +0x1A = 0x2D0D9E + ...) is written back to
// the profile at the next save (FUN_001F9370), so the cheat changes the reads, not the table:
//  - FUN_00115030 (fills the FELevelUnlocked1..8 UI variables, je at 0x115047), the check at 0x11B98B and the
//    mission-start check at 0x124D36 take the "unlocked" way (NOPs). The cursor default (0x115066) is left alone.
//  - Picking a mission (the FE callback at 0x11B894, mission index as its argument) scans that mission's unlocked byte
//    from BLACK OPS down (0x11B958 and 0x11B9F8) for the highest difficulty it is unlocked on; FE_MissionDifficulty
//    (0x2EC560) = that + 1 tells the difficulty page how many difficulties it offers (0 for a locked mission: nothing
//    could be picked). Both scans end at a load of a global (0x11B973 mov ecx, [0x2EC38C]; 0x11BA0B mov edx,
//    [0x2D0D60]), which become jumps to thunks that raise the result to at least HARD (EASY, NORMAL, HARD); BLACK OPS
//    stays as the profile has it.
//
// God mode: the player's four damage functions (FUN_001ED1E0, FUN_001ED2B0, FUN_001ED700, FUN_001ED990) each start
// with the game's own invulnerability check, mov al, [obj+0x8F2] (a byte scripts set and clear). Each read becomes a
// call that also ORs in the cheat, so the game's own invulnerable path runs.
//
// Infinite ammo and grenades: firing (FUN_001EEFD0, ebp = weapon controller, owner character [ebp+0xF0]) decrements
// the gun's magazine (word +0x18 of [ebp+0xF4], 0x1EFBBB) and its rounds left in all (dword +0x14, 0x1EFBC5; the HUD's
// reserve is that minus the magazine, a reload refills the magazine from it). Grenades are the character's word +0x286
// (one of the ammo words at +0x278; the throw state is entered only when it is non-zero, 0x1EFF39), decremented by
// the throw code at 0x1E8933 (edi = the character). The instructions around them become jumps to thunks that skip
// the decrements when the character is the player. With none picked up yet there is nothing to keep, so each player
// update also fills the player's grenades to the game's maximum for that ammo type (word 7 of the per-type maximum
// table 0x288D04 = 9). (The game's own silver-weapon refill, world +0x5CB0, tops up a per-type
// pool at [ebp+0xFC] that neither the HUD nor a reload uses: no help here, tested.)
//
// Damage to enemies: a bullet hit on an AI character goes through its slot +0x20 (FUN_001E74B0, esi = the shooter):
// FUN_001CC1A0 returns the damage for the body part hit, stored at 0x1E7801 (movss [esp+0x20], xmm0); the thunk
// multiplies it first when the shooter is the player. The other damage method (slot +0x2C, FUN_001DF830, subss at
// 0x1DF95D: explosions and other non-bullet damage, no shooter in its record) is scaled for every source. A shielded
// enemy's block still blocks.
#include "cheats.h"

#include "common.h"
#include "xbe.h"

#include <cstring>

extern "C" {
volatile uint8_t g_cheat_god;     // read by the invulnerability thunks
volatile uint8_t g_cheat_ammo_on; // read by the ammo thunks
volatile uint8_t g_cheat_unlock_on;
float g_cheat_damage = 1.0f;      // read by the damage thunks
uint32_t g_cheat_world;           // address of the world pointer (*0x2D45D4) in this build
uint32_t g_cheat_resume[6];       // per thunk: the instruction after the replaced code
uint32_t g_cheat_global[2];       // the unlock thunks' original absolute operands
}

namespace {

// the replaced mov r8, [reg+0x8F2] (6 bytes) -> call thunk; nop
__declspec(naked) void god_esi_al()
{
    __asm {
        mov al, byte ptr [esi + 0x8F2]
        or al, byte ptr [g_cheat_god]
        ret
    }
}
__declspec(naked) void god_edi_al()
{
    __asm {
        mov al, byte ptr [edi + 0x8F2]
        or al, byte ptr [g_cheat_god]
        ret
    }
}
__declspec(naked) void god_ebx_al()
{
    __asm {
        mov al, byte ptr [ebx + 0x8F2]
        or al, byte ptr [g_cheat_god]
        ret
    }
}

// 0x1DF95D: subss xmm0, [ebp+0x10] (xmm1 is free: reloaded at 0x1DF96B)
__declspec(naked) void damage_any_thunk()
{
    __asm {
        movss xmm1, dword ptr [ebp + 0x10]
        mulss xmm1, dword ptr [g_cheat_damage]
        subss xmm0, xmm1
        jmp dword ptr [g_cheat_resume + 0]
    }
}

// 0x1E7801: movss [esp+0x20], xmm0 (6 bytes); the je after it uses the flags of the test before it
__declspec(naked) void damage_bullet_thunk()
{
    __asm {
        pushfd
        push eax
        mov eax, dword ptr [g_cheat_world]
        mov eax, dword ptr [eax]
        add eax, 0x30
        cmp esi, eax
        pop eax
        jne store
        mulss xmm0, dword ptr [g_cheat_damage]
    store:
        popfd
        movss dword ptr [esp + 0x20], xmm0
        jmp dword ptr [g_cheat_resume + 4]
    }
}

// 0x1EFBB5 (19 bytes): mov eax, [ebp+0xF4]; dec word [eax+0x18]; mov eax, [ebp+0xF4]; dec dword [eax+0x14]
__declspec(naked) void ammo_gun_thunk()
{
    __asm {
        mov eax, dword ptr [ebp + 0xF4]
        cmp byte ptr [g_cheat_ammo_on], 0
        je take
        push ecx
        mov ecx, dword ptr [g_cheat_world]
        mov ecx, dword ptr [ecx]
        add ecx, 0x30
        cmp ecx, dword ptr [ebp + 0xF0]
        pop ecx
        je done
    take:
        dec word ptr [eax + 0x18]
        dec dword ptr [eax + 0x14]
    done:
        jmp dword ptr [g_cheat_resume + 8]
    }
}

// 0x1E8933 (8 bytes): dec eax; mov [edi+0x286], ax (ax = the grenades left, non-zero here)
__declspec(naked) void ammo_grenade_thunk()
{
    __asm {
        cmp byte ptr [g_cheat_ammo_on], 0
        je take
        push ecx
        mov ecx, dword ptr [g_cheat_world]
        mov ecx, dword ptr [ecx]
        add ecx, 0x30
        cmp ecx, edi
        pop ecx
        je done
    take:
        dec eax
        mov word ptr [edi + 0x286], ax
    done:
        jmp dword ptr [g_cheat_resume + 12]
    }
}

// 0x11B973: mov ecx, [0x2EC38C] (eax = highest difficulty the picked mission is unlocked on, -1 none)
__declspec(naked) void unlock_scan1_thunk()
{
    __asm {
        cmp byte ptr [g_cheat_unlock_on], 0
        je keep
        cmp eax, 2
        jge keep
        mov eax, 2
    keep:
        mov ecx, dword ptr [g_cheat_global + 0]
        mov ecx, dword ptr [ecx]
        jmp dword ptr [g_cheat_resume + 16]
    }
}

// 0x11BA0B: mov edx, [0x2D0D60] (the same scan again, before FE_MissionDifficulty = eax + 1)
__declspec(naked) void unlock_scan2_thunk()
{
    __asm {
        cmp byte ptr [g_cheat_unlock_on], 0
        je keep
        cmp eax, 2
        jge keep
        mov eax, 2
    keep:
        mov edx, dword ptr [g_cheat_global + 4]
        mov edx, dword ptr [edx]
        jmp dword ptr [g_cheat_resume + 20]
    }
}

struct Bytes {
    uint32_t addr; // NTSC-U
    uint8_t len;
    uint8_t orig[19];
};

bool match(const Bytes &b)
{
    return memcmp((const void *)(uintptr_t)xbe_va(b.addr), b.orig, b.len) == 0;
}

void write(uint32_t addr, const uint8_t *data, size_t len)
{
    auto *p = (volatile uint8_t *)(uintptr_t)xbe_va(addr);
    for (size_t i = 0; i < len; i++) p[i] = data[i];
}

void nop_or_restore(const Bytes &b, bool nop)
{
    uint8_t n[19];
    memset(n, 0x90, sizeof n);
    write(b.addr, nop ? n : b.orig, b.len);
}

void call_patch(uint32_t addr, void (*thunk)())
{
    uint32_t at = xbe_va(addr);
    uint8_t c[6] = {0xE8, 0, 0, 0, 0, 0x90};
    int32_t rel = (int32_t)((uintptr_t)thunk - (at + 5));
    memcpy(c + 1, &rel, 4);
    write(addr, c, 6);
}

// a jmp to the thunk over `len` bytes (the rest NOP); the thunk resumes at addr + len
void jmp_patch(uint32_t addr, size_t len, void (*thunk)(), int resume)
{
    uint32_t at = xbe_va(addr);
    g_cheat_resume[resume] = at + (uint32_t)len;
    uint8_t c[19];
    memset(c, 0x90, sizeof c);
    c[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)thunk - (at + 5));
    memcpy(c + 1, &rel, 4);
    write(addr, c, len);
}

const Bytes kUnlock[] = {
    {0x00115047, 2, {0x74, 0x23}},                         // je: not unlocked on EASY -> FELevelUnlocked<i> stays 0
    {0x0011B98B, 2, {0x75, 0x04}},                         // jne after cmp [edx+0x2D0D9E], 1
    {0x00124D36, 6, {0x0F, 0x85, 0x94, 0x01, 0x00, 0x00}}, // jne after cmp [eax+0x2D0D9E], 1 (mission start)
};
const Bytes kScan1 = {0x0011B973, 2, {0x8B, 0x0D}}; // mov ecx, [abs] (operand checked by build: read from the code)
const Bytes kScan2 = {0x0011BA0B, 2, {0x8B, 0x15}}; // mov edx, [abs]

struct God {
    Bytes b;
    void (*thunk)();
};
const God kGod[] = {
    {{0x001ED1FC, 6, {0x8A, 0x86, 0xF2, 0x08, 0x00, 0x00}}, god_esi_al}, // FUN_001ED1E0
    {{0x001ED2BD, 6, {0x8A, 0x86, 0xF2, 0x08, 0x00, 0x00}}, god_esi_al}, // FUN_001ED2B0
    {{0x001ED71B, 6, {0x8A, 0x87, 0xF2, 0x08, 0x00, 0x00}}, god_edi_al}, // FUN_001ED700
    {{0x001ED9AC, 6, {0x8A, 0x83, 0xF2, 0x08, 0x00, 0x00}}, god_ebx_al}, // FUN_001ED990
};

const Bytes kGun = {0x001EFBB5, 19, {0x8B, 0x85, 0xF4, 0x00, 0x00, 0x00, 0x66, 0xFF, 0x48, 0x18, 0x8B, 0x85, 0xF4, 0x00, 0x00,
                                     0x00, 0xFF, 0x48, 0x14}};
const Bytes kGrenade = {0x001E8933, 8, {0x48, 0x66, 0x89, 0x87, 0x86, 0x02, 0x00, 0x00}};

const Bytes kDamageAny = {0x001DF95D, 5, {0xF3, 0x0F, 0x5C, 0x45, 0x10}};          // subss xmm0, [ebp+0x10]
const Bytes kDamageBullet = {0x001E7801, 6, {0xF3, 0x0F, 0x11, 0x44, 0x24, 0x20}}; // movss [esp+0x20], xmm0

bool g_unlock_ok, g_god_ok, g_ammo_ok, g_damage_ok;

template <class T, size_t N> bool all_match(const T (&list)[N], const Bytes T::*m)
{
    for (const T &x : list)
        if (!match(x.*m)) return false;
    return true;
}
template <size_t N> bool all_match(const Bytes (&list)[N])
{
    for (const Bytes &x : list)
        if (!match(x)) return false;
    return true;
}

} // namespace

void install_cheats()
{
    g_cheat_world = xbe_va(0x002D45D4);
    g_unlock_ok = all_match(kUnlock) && match(kScan1) && match(kScan2);
    g_god_ok = all_match(kGod, &God::b);
    g_ammo_ok = match(kGun) && match(kGrenade);
    g_damage_ok = match(kDamageAny) && match(kDamageBullet);
    if (!g_unlock_ok) logf("cheats: mission checks do not match this build; UNLOCK ALL MISSIONS does nothing");
    if (!g_god_ok) logf("cheats: player damage checks do not match this build; GOD MODE does nothing");
    if (!g_ammo_ok) logf("cheats: ammo code does not match this build; INFINITE AMMO does nothing");
    if (!g_damage_ok) logf("cheats: enemy damage code does not match this build; DAMAGE TO ENEMIES does nothing");
    // thunks stay in place; flags decide
    if (g_unlock_ok) {
        memcpy(&g_cheat_global[0], (const void *)(uintptr_t)(xbe_va(kScan1.addr) + 2), 4);
        memcpy(&g_cheat_global[1], (const void *)(uintptr_t)(xbe_va(kScan2.addr) + 2), 4);
        jmp_patch(kScan1.addr, 6, unlock_scan1_thunk, 4);
        jmp_patch(kScan2.addr, 6, unlock_scan2_thunk, 5);
    }
    if (g_god_ok)
        for (const God &g : kGod) call_patch(g.b.addr, g.thunk);
    if (g_ammo_ok) {
        jmp_patch(kGun.addr, kGun.len, ammo_gun_thunk, 2);
        jmp_patch(kGrenade.addr, kGrenade.len, ammo_grenade_thunk, 3);
    }
    if (g_damage_ok) {
        jmp_patch(kDamageAny.addr, kDamageAny.len, damage_any_thunk, 0);
        jmp_patch(kDamageBullet.addr, kDamageBullet.len, damage_bullet_thunk, 1);
    }
    cheats_apply();
    logf("cheats: installed (unlock %d, god %d, ammo %d, damage %d)", g_cfg.cheat_unlock, g_cfg.cheat_god, g_cfg.cheat_ammo,
         g_cfg.cheat_damage);
}

void cheats_frame()
{
    if (!g_cheat_ammo_on) return;
    uint32_t world = *(const uint32_t *)(uintptr_t)g_cheat_world;
    if (!world) return;
    uint16_t max = *(const uint16_t *)(uintptr_t)xbe_va(0x00288D04 + 7 * 2);
    auto *grenades = (uint16_t *)(uintptr_t)(world + 0x30 + 0x286);
    if (max > 0 && max < 100 && *grenades < max) *grenades = max;
}

void cheats_apply()
{
    if (g_unlock_ok)
        for (const Bytes &b : kUnlock) nop_or_restore(b, g_cfg.cheat_unlock);
    g_cheat_unlock_on = g_unlock_ok && g_cfg.cheat_unlock ? 1 : 0;
    g_cheat_god = g_god_ok && g_cfg.cheat_god ? 1 : 0;
    g_cheat_ammo_on = g_ammo_ok && g_cfg.cheat_ammo ? 1 : 0;
    static const float kMult[] = {1.0f, 2.0f, 4.0f, 1.0e6f};
    g_cheat_damage = kMult[g_cfg.cheat_damage >= 0 && g_cfg.cheat_damage < 4 ? g_cfg.cheat_damage : 0];
}
