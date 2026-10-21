// Values the port shows in, and gives to, the front end's pages.
//
// The front-end object (*0x2D199C) keeps named values in tables of 0x70 entries from table +4, 0x14 bytes each:
// {type, value, hash (-1 = free), references, first watcher}. Names are hashed with FUN_000BF2A0 (CRC-32 style:
// start -1, arithmetic shift, table 0x2AC908).
// - +0x2530: externObject's members whose names start with "FE", read and written by the scripts (FEFunctionTrigger,
//   FEGammaLevel...; FUN_00114050 looks "HUD..." up in +0x1C78 and other names in +0x13A8). Type 1 is an integer:
//   the value points at an int32 (FUN_001146A0).
// - +0x13A8, then +0x2DF4 (FUN_00112CC0): what a text field whose variable starts with '&' shows (FUN_001184D0, at
//   the field's creation). Type 4 is a string: the value points at UTF-16 text (FE_SOUNDTRACKNAME, FUN_001145B0).
//   The field takes a reference, becomes the entry's first watcher (the old one is chained at field +0x48) and
//   shows the text at field +0x28. On a change the game stores the new pointer in the entry and in every watcher's
//   +0x28 (FUN_00114960). When fields are destroyed (FUN_00113D30) the entry loses its watchers and a reference;
//   at none it is freed.
//
// The port's values: strings PC_V0..18 and named integers (+0x2530). The tables are shared with the game's own values,
// so the port keeps to what it uses: the strings go in +0x13A8, which the game hardly uses (9 of 112 slots at most in a
// run to the first mission), not in +0x2DF4, which the game fills to the last slot at the main menu. Each holds a reference of its own, so
// it outlasts the pages, and is put back whenever the game has cleared it.
#include "fe/vars.h"

#include "common.h"

#include <cstring>

namespace {

constexpr uint32_t kFeObject = 0x002D199C, kStrings = 0x13A8, kMembers = 0x2530, kCrcTable = 0x002AC908;
constexpr int kEntries = 0x70, kTypeInt = 1, kTypeString = 4, kMaxInts = 24;
// references the port's entries hold: text fields going away never bring them to none (which frees the slot while the
// fields still point at it, and their later releases would count down whatever the game put there)
constexpr int32_t kOwnRefs = 0x10000;

struct Entry {
    uint32_t type, value, hash;
    int32_t refs;
    uint32_t watchers;
};

struct Text {
    bool used;
    uint32_t hash;
    char16_t buf[128]; // UTF-16 LE text for the Xbox Flash/Apt UI
};
struct Int {
    uint32_t hash;
    int32_t value; // the game's entry points here: the array never moves
    char name[24];
};
Text g_texts[kFeVars];
Int g_ints[kMaxInts];
int g_nints;

Entry *table(uint32_t off)
{
    uint32_t fe = *(const uint32_t *)(uintptr_t)kFeObject;
    return fe ? (Entry *)(uintptr_t)(fe + off + 4) : nullptr;
}

Entry *find(Entry *t, uint32_t hash)
{
    for (int i = 0; i < kEntries; i++)
        if (t[i].hash == hash) return &t[i];
    return nullptr;
}

// the entry for hash, put back first if the game has cleared it
Entry *ensure(Entry *t, uint32_t hash, uint32_t type, uint32_t value)
{
    if (Entry *e = find(t, hash)) return e;
    Entry *e = find(t, 0xFFFFFFFFu);
    if (!e) {
        static bool told;
        if (!told) logf("fe: no free front-end value slot for the port's values");
        told = true;
        return nullptr;
    }
    *e = {type, value, hash, kOwnRefs, 0};
    return e;
}

} // namespace

uint32_t fe_hash(const char *name)
{
    const uint32_t *crc = (const uint32_t *)(uintptr_t)kCrcTable;
    int32_t h = -1;
    for (const char *p = name; *p; p++) h = (h >> 8) ^ (int32_t)crc[(uint8_t)((h & 0xFF) ^ (int32_t)(signed char)*p)];
    return (uint32_t)h;
}

void fe_vars_poll()
{
    static int s_least[4] = {1000, 1000, 1000, 1000}; // each table's fewest free slots so far, logged when it drops
    const uint32_t offs[4] = {0x13A8, 0x1C78, kMembers, kStrings};
    for (int k = 0; k < 4; k++)
        if (Entry *t = table(offs[k])) {
            int free = 0;
            for (int i = 0; i < kEntries; i++) free += t[i].hash == 0xFFFFFFFFu;
            if (free < s_least[k]) {
                logf("fe: value table +0x%X: %d of %d slots free", offs[k], free, kEntries);
                s_least[k] = free;
            }
        }
    for (int n = 0; n < kFeVars; n++) // all of them from the start: a page's text fields look theirs up when created
        if (!g_texts[n].used) fe_var_set(n, L"");
    if (Entry *t = table(kStrings))
        for (Text &v : g_texts)
            if (!ensure(t, v.hash, kTypeString, (uint32_t)(uintptr_t)v.buf)) break;
    if (Entry *t = table(kMembers))
        for (int i = 0; i < g_nints; i++)
            if (!ensure(t, g_ints[i].hash, kTypeInt, (uint32_t)(uintptr_t)&g_ints[i].value)) break;
}

void fe_var_set(int n, const std::wstring &text)
{
    if (n < 0 || n >= kFeVars) return;
    Text &v = g_texts[n];
    if (!v.used) {
        char name[16];
        snprintf(name, sizeof name, "PC_V%d", n);
        v.hash = fe_hash(name);
        v.used = true;
        v.buf[0] = 0;
    }
    // In place: the text fields hold the buffer's address and draw from it as UTF-16 LE.
    size_t i = 0;
    for (; i < text.size() && i < 127; i++) {
        v.buf[i] = (char16_t)text[i];
    }
    v.buf[i] = 0;
}

void fe_int_set(const char *name, int value)
{
    for (int i = 0; i < g_nints; i++)
        if (strcmp(g_ints[i].name, name) == 0) {
            g_ints[i].value = value;
            return;
        }
    if (g_nints == kMaxInts) {
        logf("fe: too many port values (%s)", name);
        return;
    }
    Int &v = g_ints[g_nints++];
    strncpy(v.name, name, sizeof v.name - 1);
    v.hash = fe_hash(name);
    v.value = value;
}
