// Skip any movie (default; PC SETTINGS > AUDIO AND GAME, --no-movie-skip for the original behaviour).
//
// The game skips a movie only once it has been earned: the boot logos never, the intro and a mission's movies once
// that mission has been completed (the front end's FMVPlayer script requests the next movie on A or START; with a
// fresh profile it ignores the press). Movies play through one player, game object (*0x2D45F0) + 0x2026C: state
// +0x1A8 (0x1C playing, 0x37 idle), flags +0x198 (bit 0 = loop, the menu backgrounds), stop request +0x1B9, file
// name +0x84 (FUN_001F3630 queues one; FUN_00209DC0 runs it once per frame). The sequencer FUN_0020ECF0 tells the
// front end "FmvHasFinished" whenever the player is idle, so a stopped movie continues exactly like one that ended.
// Here a press of A, START or Enter during a movie that does not loop stops it, unless the game reacted to the press
// itself within 10 frames (a movie it already lets you skip moves on to the next one, which is not stopped again).
#include "input/movies.h"

#include "common.h"
#include "xbe.h"

#include <cstring>

namespace {

constexpr uint32_t kGameObject = 0x002D45F0, kPlayer = 0x2026C;
constexpr uint32_t kState = 0x1A8, kFlags = 0x198, kStop = 0x1B9, kName = 0x84;
constexpr int kPlaying = 0x1C, kWait = 10;

bool g_was_down;
int g_wait;          // polls left before the press counts as ignored by the game
char g_name[64];     // the movie that was playing at the press

uint8_t *player()
{
    uint32_t game = *(const uint32_t *)(uintptr_t)xbe_va(kGameObject);
    return game ? (uint8_t *)(uintptr_t)(game + kPlayer) : nullptr;
}

bool playing_once(const uint8_t *p) { return *(const int32_t *)(p + kState) == kPlaying && !(*(const uint32_t *)(p + kFlags) & 1); }

} // namespace

void movies_poll(uint16_t buttons, const uint8_t analog[8], bool enter)
{
    bool down = analog[0] > 30 || (buttons & 0x10) || enter; // A, START or Enter
    bool press = down && !g_was_down;
    g_was_down = down;
    if (!g_cfg.skip_movies || !g_cfg.supported_build) return;
    uint8_t *p = player();
    if (!p) return;
    static bool told;
    if (!told) logf("movies: any movie can be skipped (player at %08x)", (uint32_t)(uintptr_t)p);
    told = true;
    if (press && !g_wait && playing_once(p)) {
        g_wait = kWait;
        memcpy(g_name, p + kName, sizeof g_name - 1);
        g_name[sizeof g_name - 1] = 0;
        return;
    }
    if (g_wait && --g_wait == 0 && playing_once(p) && strncmp((const char *)(p + kName), g_name, sizeof g_name - 1) == 0) {
        p[kStop] = 1; // what the game's own stop does (FUN_001F3610); the player winds down and reports it finished
        logf("movies: skipped %s", g_name);
    }
}
