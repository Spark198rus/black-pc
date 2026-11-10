// Mid-mission saves (issue #41): the last checkpoint is kept in a file, and CONTINUE LAST GAME on the main menu starts
// its mission there after a restart.
//
// The game's own CONTINUE resets the loaded level in memory and starts it at the checkpoint's section; a fresh load
// can start there as well (tested, NOTES.md "Mid-mission saves"). So:
// - after the game takes a checkpoint (FUN_001C8D20, called at 0x1EB834 with eax = the index, ebx = the player's
//   record), everything it keeps goes to <save folder>\checkpoint.bin: the index 0x2A7EDC, the level time 0x2F3BF4,
//   the record (0x54 bytes) and its +0x50 block ([*[*0x2D12D0]]+1 bytes), the world table buffer [0x2F3BFC]
//   (world +0x934 x 12 bytes), the stats 0x2F3C00..0x2F3C0F, with the mission (index into the mission -> level table
//   0x288648), the level (game +0x2020C) and the difficulty (game +0x2014C);
// - CONTINUE LAST GAME (fe/pages.cpp, shown while the file exists: externObject.FEPCHasCheckpoint) calls PCContinue,
//   which starts the mission as MISSION SELECT does (the front end's LevelSelect FUN_0011B890 with the mission, then
//   SelectedDifficulty FUN_00124F00 with the difficulty, which starts it);
// - at that start the world init (0x1EDD7D, reading game +0x2020D) gets the checkpoint's section, and right before
//   the player init reads 0x2F3BF8 (0x1DECCD, after the level allocated its table buffer) the copy goes back and the
//   flags are set (0x2F3BF8 = 1, 0x2EC464 = 1): the player init takes the record, 0x20CF0C restores the table and
//   the stats, as after the game's own CONTINUE;
// - the file goes when the mission is completed (the results page comes up) or restarted (the front end's RestartLevel
//   from the pause menu, RestartMission from MISSION FAILED; fe/calls.cpp).
// A start from MISSION SELECT begins at the mission's start as before; a new checkpoint replaces the file.
#include "checkpoints.h"

#include "common.h"
#include "fe/vars.h"
#include "xbe.h"

#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
uint32_t g_cpt_take;          // FUN_001C8D20
uint32_t g_cpt_flag;          // 0x2F3BF8 in this build
uint32_t g_cpt_world_resume;  // after the replaced movzx at 0x1EDD7D
uint32_t g_cpt_player_resume; // after the replaced mov at 0x1DECCD
void __cdecl cpt_taken(uint8_t *record);
void __cdecl cpt_world_init(uint8_t *game);
void __cdecl cpt_player_init(uint8_t *player);
}

namespace {

constexpr uint32_t kMagic = 0x31544B43; // "CKT1"

struct Snapshot {
    bool have = false;
    int mission = -1, level = -1, difficulty = -1, index = -1;
    float time = 0;
    uint8_t record[0x54] = {};
    std::vector<uint8_t> block, table;
    uint8_t stats[0x10] = {};
};
Snapshot g_snap;
bool g_installed, g_requested, g_pending; // CONTINUE LAST GAME asked; the next player init gets the copy

template <class T> T &at(uint32_t ntsc) { return *(T *)(uintptr_t)xbe_va(ntsc); }
uint8_t *game() { return (uint8_t *)(uintptr_t)at<uint32_t>(0x002D45F0); }
uint8_t *world() { return (uint8_t *)(uintptr_t)at<uint32_t>(0x002D45D4); }
uint32_t block_size()
{
    uint32_t t = at<uint32_t>(0x002D12D0);
    uint32_t p = t ? *(const uint32_t *)(uintptr_t)t : 0;
    return p ? *(const uint8_t *)(uintptr_t)(p + 1) : 0;
}

std::wstring file() { return g_cfg.hdd_dir + L"\\checkpoint.bin"; }

void publish() { fe_int_set("FEPCHasCheckpoint", g_snap.have ? 1 : 0); }

void save()
{
    const Snapshot &s = g_snap;
    FILE *f = _wfopen(file().c_str(), L"wb");
    if (!f) {
        logf("checkpoints: cannot write %ls", file().c_str());
        return;
    }
    auto put = [f](const void *p, size_t n) { fwrite(p, 1, n, f); };
    auto put32 = [&](uint32_t v) { put(&v, 4); };
    put32(kMagic);
    put32((uint32_t)s.mission);
    put32((uint32_t)s.level);
    put32((uint32_t)s.difficulty);
    put32((uint32_t)s.index);
    put(&s.time, 4);
    put(s.record, sizeof s.record);
    put32((uint32_t)s.block.size());
    put(s.block.data(), s.block.size());
    put32((uint32_t)s.table.size());
    put(s.table.data(), s.table.size());
    put(s.stats, sizeof s.stats);
    fclose(f);
}

bool load()
{
    FILE *f = _wfopen(file().c_str(), L"rb");
    if (!f) return false;
    Snapshot s;
    bool ok = true;
    auto get = [&](void *p, size_t n) { ok = ok && fread(p, 1, n, f) == n; };
    auto get32 = [&]() { uint32_t v = 0; get(&v, 4); return v; };
    ok = get32() == kMagic;
    s.mission = (int)get32();
    s.level = (int)get32();
    s.difficulty = (int)get32();
    s.index = (int)get32();
    get(&s.time, 4);
    get(s.record, sizeof s.record);
    uint32_t n = get32();
    if (ok && n < 0x10000) {
        s.block.resize(n);
        get(s.block.data(), n);
    } else ok = false;
    n = get32();
    if (ok && n < 0x100000) {
        s.table.resize(n);
        get(s.table.data(), n);
    } else ok = false;
    get(s.stats, sizeof s.stats);
    fclose(f);
    if (!ok || s.mission < 0 || s.mission > 7 || s.difficulty < 0 || s.difficulty > 3) {
        logf("checkpoints: %ls is not a checkpoint of this port; ignored", file().c_str());
        return false;
    }
    s.have = true;
    g_snap = s;
    return true;
}

// 0x1EB834: call FUN_001C8D20 -> this; eax and ebx as the game passes them
__declspec(naked) void take_thunk()
{
    __asm {
        call dword ptr [g_cpt_take]
        pushad
        push ebx
        call cpt_taken
        add esp, 4
        popad
        ret
    }
}

// 0x1EDD7D: movzx edx, byte [ecx+0x2020D] (ecx = the game object)
__declspec(naked) void world_thunk()
{
    __asm {
        pushad
        push ecx
        call cpt_world_init
        add esp, 4
        popad
        movzx edx, byte ptr [ecx + 0x2020D]
        jmp dword ptr [g_cpt_world_resume]
    }
}

// 0x1DECCD: mov al, [0x2F3BF8] (ebx = the player character)
__declspec(naked) void player_thunk()
{
    __asm {
        pushad
        push ebx
        call cpt_player_init
        add esp, 4
        popad
        push ecx
        mov ecx, dword ptr [g_cpt_flag]
        mov al, byte ptr [ecx]
        pop ecx
        jmp dword ptr [g_cpt_player_resume]
    }
}

} // namespace

extern "C" void __cdecl cpt_taken(uint8_t *record)
{
    uint8_t *g = game(), *w = world();
    if (!g || !w || !record) return;
    Snapshot s;
    s.level = g[0x2020C];
    for (int i = 0; i < 8; i++)
        if (at<int8_t>(0x00288648 + i) == s.level) s.mission = i;
    s.difficulty = *(int32_t *)(g + 0x2014C);
    s.index = at<int32_t>(0x002A7EDC);
    s.time = at<float>(0x002F3BF4);
    memcpy(s.record, record, sizeof s.record);
    uint32_t bs = block_size();
    const uint8_t *blk = (const uint8_t *)(uintptr_t)*(uint32_t *)(record + 0x50);
    if (blk && bs) s.block.assign(blk, blk + bs);
    uint32_t n = *(uint32_t *)(w + 0x934) * 12;
    const uint8_t *tbl = (const uint8_t *)(uintptr_t)at<uint32_t>(0x002F3BFC);
    if (tbl && n) s.table.assign(tbl, tbl + n);
    memcpy(s.stats, &at<uint8_t>(0x002F3C00), sizeof s.stats);
    if (s.mission < 0) {
        logf("checkpoints: checkpoint %d of level %d is in no mission; not kept", s.index, s.level);
        return;
    }
    s.have = true;
    g_snap = s;
    save();
    publish();
    logf("checkpoints: checkpoint %d kept (mission %d, level %d, difficulty %d, time %.1f, block %u bytes, table %u bytes)",
         s.index, s.mission, s.level, s.difficulty, s.time, (unsigned)s.block.size(), (unsigned)s.table.size());
}

extern "C" void __cdecl cpt_world_init(uint8_t *g)
{
    const Snapshot &s = g_snap;
    bool asked = g_requested;
    g_requested = g_pending = false;
    if (!asked) return;
    bool own = at<uint8_t>(0x002F3BF8) != 0;
    if (!s.have || own || g[0x2020C] != s.level || *(int32_t *)(g + 0x2014C) != s.difficulty) {
        logf("checkpoints: CONTINUE LAST GAME, but this start is not its mission (level %d / %d, difficulty %d / %d, own %d)",
             g[0x2020C], s.level, *(int32_t *)(g + 0x2014C), s.difficulty, (int)own);
        return;
    }
    g[0x2020D] = (uint8_t)s.index;
    g_pending = true;
    logf("checkpoints: this start begins at checkpoint %d", s.index);
}

extern "C" void __cdecl cpt_player_init(uint8_t *player)
{
    if (!g_pending) return;
    g_pending = false;
    const Snapshot &s = g_snap;
    uint8_t *w = world();
    uint8_t *record = player + 0x3C0;
    uint32_t blk = *(uint32_t *)(record + 0x50); // the record's block stays where this level put it
    memcpy(record, s.record, sizeof s.record);
    *(uint32_t *)(record + 0x50) = blk;
    bool block_ok = blk && block_size() == s.block.size();
    if (block_ok) memcpy((void *)(uintptr_t)blk, s.block.data(), s.block.size());
    uint32_t tbl = at<uint32_t>(0x002F3BFC);
    uint32_t n = w ? *(uint32_t *)(w + 0x934) * 12 : 0;
    bool table_ok = tbl && n == s.table.size();
    if (table_ok) memcpy((void *)(uintptr_t)tbl, s.table.data(), n);
    memcpy(&at<uint8_t>(0x002F3C00), s.stats, sizeof s.stats);
    at<int32_t>(0x002A7EDC) = s.index;
    at<float>(0x002F3BF4) = s.time;
    at<uint8_t>(0x002F3BF8) = 1;
    at<int32_t>(0x002EC464) = 1;
    logf("checkpoints: restored checkpoint %d (block %s, table %s)", s.index, block_ok ? "copied" : "size differs, left",
         table_ok ? "copied" : "size differs, left");
}

void install_checkpoints()
{
    fe_int_set("FEPCHasCheckpoint", 0);
    uint8_t *call = (uint8_t *)(uintptr_t)xbe_va(0x001EB834);
    int32_t rel;
    memcpy(&rel, call + 1, 4);
    static const uint8_t kWorld[] = {0x0F, 0xB6, 0x91, 0x0D, 0x02, 0x02, 0x00};
    uint8_t *wsite = (uint8_t *)(uintptr_t)xbe_va(0x001EDD7D), *psite = (uint8_t *)(uintptr_t)xbe_va(0x001DECCD);
    if (call[0] != 0xE8 || xbe_va(0x001EB834) + 5 + rel != xbe_va(0x001C8D20) || memcmp(wsite, kWorld, 7) || psite[0] != 0xA0 ||
        *(uint32_t *)(psite + 1) != xbe_va(0x002F3BF8)) {
        logf("checkpoints: code does not match this build; checkpoints are not kept");
        return;
    }
    g_cpt_take = xbe_va(0x001C8D20);
    g_cpt_flag = xbe_va(0x002F3BF8);
    int32_t r = (int32_t)((uintptr_t)&take_thunk - ((uintptr_t)call + 5));
    memcpy(call + 1, &r, 4);
    g_cpt_world_resume = (uint32_t)(uintptr_t)wsite + 7;
    uint8_t j[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    r = (int32_t)((uintptr_t)&world_thunk - ((uintptr_t)wsite + 5));
    memcpy(j + 1, &r, 4);
    memcpy(wsite, j, 7);
    g_cpt_player_resume = (uint32_t)(uintptr_t)psite + 5;
    r = (int32_t)((uintptr_t)&player_thunk - ((uintptr_t)psite + 5));
    psite[0] = 0xE9;
    memcpy(psite + 1, &r, 4);
    g_installed = true;
    if (load())
        logf("checkpoints: %ls: checkpoint %d of mission %d, difficulty %d (CONTINUE LAST GAME)", file().c_str(), g_snap.index,
             g_snap.mission, g_snap.difficulty);
    publish();
}

void checkpoints_continue()
{
    if (!g_installed || !g_snap.have) return;
    g_requested = true;
    char mission[8], difficulty[8];
    snprintf(mission, sizeof mission, "%d", g_snap.mission);
    snprintf(difficulty, sizeof difficulty, "%d", g_snap.difficulty);
    logf("checkpoints: CONTINUE LAST GAME: mission %s, difficulty %s, checkpoint %d", mission, difficulty, g_snap.index);
    using Call = void(__cdecl *)(const char *);
    ((Call)(uintptr_t)xbe_va(0x0011B890))(mission);    // LevelSelect
    ((Call)(uintptr_t)xbe_va(0x00124F00))(difficulty); // SelectedDifficulty: sets it and starts the mission
}

void checkpoints_drop(const char *why)
{
    if (!g_snap.have) return;
    g_snap = Snapshot{};
    _wremove(file().c_str());
    publish();
    logf("checkpoints: %s; the kept checkpoint is gone", why);
}

void checkpoints_page(const char *page)
{
    if (_strnicmp(page, "ResultsScreen", 13) == 0 || _strnicmp(page, "MissionComplete", 15) == 0) checkpoints_drop("mission complete");
}
