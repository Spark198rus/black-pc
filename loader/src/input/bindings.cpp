// Keyboard and mouse bound to the game's own actions.
//
// The game reads every player action through its binding table (*0x2F3BD4: 37 dwords, action -> controller
// channel; FUN_001F1520 fills it from one of the presets at 0x2A7AF0, 0x94 bytes each: default, assault, scout,
// southpaw, custom; the custom-controls screen edits single entries). The channels are made from the pad by the
// controller update FUN_000C5A80 (channel object: floats at +0x44, current/previous digital state at +0x25/+0x09):
//   0-3 D-pad up/down/left/right, 4/5 left stick up/down, 6/7 left/right, 8/9 right stick up/down, 10/11 left/right,
//   12 START, 13 BACK, 14/15 left/right stick click, 16-23 A B X Y BLACK WHITE, left trigger, right trigger,
//   24-27 the four stick axes signed; 28 = unbound.
// A bound key looks up its action's channel in the live table and presses the pad input that feeds that channel,
// so the keys mean the same action whatever controller layout is chosen in the game, and controllers keep working.
//
// Menus, pauses and cutscenes (no player update, see in_play) take no bound keys and nothing goes through the pad:
// arrows, Enter and Backspace are the menus' own inputs (up, down, left, right, select, back; held arrows repeat),
// which fe/calls.cpp gives the menus directly; Esc there only lets go of the mouse. Outside the menus the game itself
// waits for the pad (PRESS A TO CONTINUE after a hint, START to skip the mission's intro text): there Enter is A or
// START (see apply). A key held across a switch between play and menus is ignored until it is released (a press that
// starts play must not also act in it).
// Port actions (game index -1, at the end of the list) are not the game's: the lean keys go to input/lean.cpp.
#include "input/bindings.h"
#include "xbe.h"

#include "common.h"
#include "d3d/host.h"
#include "input/lean.h"
#include "input/mouselook.h"
#include "settings.h"

#include <algorithm>
#include <atomic>
#include <bitset>
#include <cctype>
#include <cstring>

namespace bindings {
namespace {

// Names as on the game's controls screen (FE_ACTION_* strings; the tables built at 0x214820 pair them with the
// binding-table index). The movement axes are split into their stick halves.
const Action kActions[] = {
    {"run_forward", L"Run forward", 1, {'W', 0}},
    {"run_back", L"Run back", 2, {'S', 0}},
    {"step_left", L"Sidestep left", 3, {'A', 0}},
    {"step_right", L"Sidestep right", 4, {'D', 0}},
    {"fire", L"Fire", 9, {VK_LBUTTON, 0}},
    {"aim", L"Aim", 16, {VK_RBUTTON, 0}},
    {"reload", L"Reload", 11, {'R', 0}},
    {"use", L"Action (use, pick up)", 13, {'E', 0}},
    {"crouch", L"Crouch", 12, {'C', 0}},
    {"grenade", L"Grenade", 27, {'G', 0}},
    {"melee", L"Melee", 32, {'F', 0}},
    {"next_weapon", L"Next weapon", 14, {KEY_WHEEL_DOWN, 0}},
    {"prev_weapon", L"Previous weapon", 15, {KEY_WHEEL_UP, 0}},
    {"firing_mode", L"Change firing mode", 30, {'Z', 0}},
    {"suppressor", L"Toggle suppressor", 31, {'Q', 0}},
    {"health_pack", L"Health pack", 33, {'X', 0}},
    {"music", L"Next music track", 34, {0, 0}},
    {"turn_left", L"Turn left", 5, {0, 0}},
    {"turn_right", L"Turn right", 6, {0, 0}},
    {"look_up", L"Look up", 7, {0, 0}},
    {"look_down", L"Look down", 8, {0, 0}},
    {"pause", L"Pause", 25, {VK_ESCAPE, 0}},
    {"objectives", L"Show objectives", 36, {VK_SPACE, 0}},
    // port actions (appended: the indices above stay)
    {"lean_left", L"Lean left", -1, {'V', 0}},
    {"lean_right", L"Lean right", -1, {'B', 0}},
};
constexpr int kCount = sizeof kActions / sizeof kActions[0];
constexpr int kPause = 21;

uint16_t g_keys[kCount][kSlots];
bool g_keys_set; // defaults applied

void ensure_defaults()
{
    if (g_keys_set) return;
    g_keys_set = true;
    for (int i = 0; i < kCount; i++)
        for (int s = 0; s < kSlots; s++) g_keys[i][s] = kActions[i].defaults[s];
}

struct Named {
    uint16_t key;
    const char *name;
};
const Named kNames[] = {
    {VK_LBUTTON, "Mouse1"}, {VK_RBUTTON, "Mouse2"}, {VK_MBUTTON, "Mouse3"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
    {KEY_WHEEL_UP, "WheelUp"}, {KEY_WHEEL_DOWN, "WheelDown"}, {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_ESCAPE, "Esc"},
    {VK_TAB, "Tab"}, {VK_BACK, "Backspace"}, {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_MENU, "Alt"},
    {VK_LSHIFT, "LShift"}, {VK_RSHIFT, "RShift"}, {VK_LCONTROL, "LCtrl"}, {VK_RCONTROL, "RCtrl"}, {VK_LMENU, "LAlt"},
    {VK_RMENU, "RAlt"}, {VK_CAPITAL, "CapsLock"}, {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"}, {VK_PRIOR, "PageUp"},
    {VK_NEXT, "PageDown"}, {VK_MULTIPLY, "Num*"}, {VK_ADD, "Num+"}, {VK_SUBTRACT, "Num-"}, {VK_DECIMAL, "Num."},
    {VK_DIVIDE, "Num/"}, {VK_PAUSE, "Pause"}, {VK_SCROLL, "ScrollLock"}, {VK_NUMLOCK, "NumLock"}, {VK_APPS, "Menu"},
};

// black.ini spelling: the names above, letters and digits as themselves, F1-F24, Num0-Num9, others as 0xNN.
std::string ini_name(uint16_t k)
{
    if (!k) return "none";
    for (const Named &n : kNames)
        if (n.key == k) return n.name;
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) return std::string(1, (char)k);
    if (k >= VK_F1 && k <= VK_F24) return "F" + std::to_string(k - VK_F1 + 1);
    if (k >= VK_NUMPAD0 && k <= VK_NUMPAD9) return "Num" + std::to_string(k - VK_NUMPAD0);
    char buf[8];
    snprintf(buf, sizeof buf, "0x%02X", k);
    return buf;
}

uint16_t parse_key(const std::string &s)
{
    if (s.empty() || _stricmp(s.c_str(), "none") == 0) return 0;
    for (const Named &n : kNames)
        if (_stricmp(n.name, s.c_str()) == 0) return n.key;
    if (s.size() == 1 && isalnum((unsigned char)s[0])) return (uint16_t)toupper((unsigned char)s[0]);
    if ((s[0] == 'F' || s[0] == 'f') && s.size() <= 3 && isdigit((unsigned char)s[1])) {
        int n = atoi(s.c_str() + 1);
        if (n >= 1 && n <= 24) return (uint16_t)(VK_F1 + n - 1);
    }
    if (_strnicmp(s.c_str(), "Num", 3) == 0 && s.size() == 4 && isdigit((unsigned char)s[3])) return (uint16_t)(VK_NUMPAD0 + s[3] - '0');
    if (_strnicmp(s.c_str(), "0x", 2) == 0) return (uint16_t)strtoul(s.c_str(), nullptr, 16);
    return 0;
}

// ---- per-poll state (game thread) ----

std::bitset<0x102> g_stale;      // held across a play/menu switch: ignored until released
std::bitset<0x102> g_script;     // --press k:Name, this poll only
bool g_was_play;
bool g_freed_by_esc; // the pause key (or Esc in a menu) let go of the mouse: take it again when play starts
bool g_release_was_down;

// Menu keys -> the menus' input ids (FEInputId); held arrows repeat like the game's own pad input.
struct MenuKey {
    uint16_t vk;
    int input;
    bool repeats;
};
const MenuKey kMenuKeys[] = {{VK_UP, 1, true}, {VK_DOWN, 2, true}, {VK_LEFT, 3, true}, {VK_RIGHT, 4, true},
                             {VK_RETURN, 5, false}, {VK_BACK, 6, false}};
constexpr int kMenuKeyCount = sizeof kMenuKeys / sizeof kMenuKeys[0];
constexpr ULONGLONG kRepeatDelay = 400, kRepeatEvery = 120; // ms
bool g_menu_down[kMenuKeyCount];
int g_repeat = -1; // the held arrow that repeats
ULONGLONG g_repeat_at;
std::atomic<int> g_menu_input;            // pressed at the last poll, until the menus take it
std::atomic<uint32_t> g_menu_input_frame; // when
bool g_menu_select;                       // Enter held (movies: skip)

// A hint that stops the game (PRESS A TO CONTINUE: the HUD's hint code at 0x131C47 sets game object (*0x2D45F0) +
// 0x21098 = 3 for the hint ids at 0x28AA80) is closed by the game on A or START of the player's controller (its channel
// block: A pressed +0x35, START +0x31; checks at 0x129B10 and around), not through the menus' input.
bool hint_stopped()
{
    uint32_t game = *(const uint32_t *)(uintptr_t)xbe_va(0x002D45F0);
    return game && *(const int32_t *)(uintptr_t)(game + 0x21098) == 3;
}

// No menu page is up: the front end's current page name (FE object *0x2D199C + 0x3840, as FUN_00118A80 compares it with
// "PauseMenu") is empty. So it is while a mission's intro text types out (the location and time before play), which the
// game skips on START of the player's controller.
bool no_menu_page()
{
    uint32_t fe = *(const uint32_t *)(uintptr_t)xbe_va(0x002D199C);
    return fe && *(const char *)(uintptr_t)(fe + 0x3840) == 0;
}

bool raw_held(uint16_t k, bool focus, int mouse)
{
    if (k < g_script.size() && g_script[k]) return true;
    switch (k) {
    case VK_LBUTTON: return mouse & 1;
    case VK_RBUTTON: return mouse & 2;
    case VK_MBUTTON: return mouse & 4;
    case VK_XBUTTON1: return mouse & 8;
    case VK_XBUTTON2: return mouse & 16;
    case KEY_WHEEL_UP:
    case KEY_WHEEL_DOWN: return false; // handled per notch
    }
    return focus && k < 0x100 && (GetAsyncKeyState(k) & 0x8000);
}

// Each wheel notch is one press of the key bound to that direction: held 2 polls, then 2 released so quick notches
// stay separate presses.
uint16_t wheel_key(int wheel)
{
    static int pending, hold, gap; // pending: notches, + = up, - = down
    static uint16_t held;
    pending = std::clamp(pending + wheel, -4, 4);
    if (hold) {
        hold--;
        if (!hold) gap = 2;
        return held;
    }
    if (gap) {
        gap--;
        return 0;
    }
    if (!pending) return 0;
    held = pending > 0 ? KEY_WHEEL_UP : KEY_WHEEL_DOWN;
    pending += pending > 0 ? -1 : 1;
    hold = 1;
    return held;
}

const int32_t *game_table()
{
    if (!mouselook_installed()) return nullptr; // not the build these addresses are for
    const int32_t *t = *(const int32_t *const *)(uintptr_t)xbe_va(0x002F3BD4);
    return t ? t : (const int32_t *)(uintptr_t)xbe_va(0x002A7AF0); // before setup: game's default preset
}

struct Pad {
    uint16_t buttons = 0;
    uint8_t analog[8] = {};
    int axis[4] = {}; // left x, left y, right x, right y: sum of +1 / -1 pushes
};

void press_channel(int ch, Pad &p)
{
    static const int8_t kAxis[8][2] = {{1, 1}, {1, -1}, {0, -1}, {0, 1}, {3, 1}, {3, -1}, {2, -1}, {2, 1}}; // channels 4-11
    if (ch < 0) return;
    if (ch < 4) p.buttons |= (uint16_t)(1 << ch);
    else if (ch < 12) p.axis[kAxis[ch - 4][0]] += kAxis[ch - 4][1];
    else if (ch < 16) p.buttons |= (uint16_t)(0x10 << (ch - 12));
    else if (ch < 24) p.analog[ch - 16] = 255;
}

} // namespace

const Action *actions(int *count)
{
    *count = kCount;
    return kActions;
}

uint16_t key(int action, int slot)
{
    ensure_defaults();
    return g_keys[action][slot];
}

void set_key(int action, int slot, uint16_t k)
{
    ensure_defaults();
    if (!fixed(action, slot)) g_keys[action][slot] = k;
}

void reset_defaults()
{
    g_keys_set = false;
    ensure_defaults();
}

bool fixed(int action, int slot) { return action == kPause && slot == 0; }

std::wstring key_name(uint16_t k)
{
    if (!k) return L"";
    switch (k) {
    case VK_LBUTTON: return L"Left mouse button";
    case VK_RBUTTON: return L"Right mouse button";
    case VK_MBUTTON: return L"Middle mouse button";
    case VK_XBUTTON1: return L"Mouse button 4";
    case VK_XBUTTON2: return L"Mouse button 5";
    case KEY_WHEEL_UP: return L"Mouse wheel up";
    case KEY_WHEEL_DOWN: return L"Mouse wheel down";
    case VK_LSHIFT: return L"Left Shift";
    case VK_RSHIFT: return L"Right Shift";
    case VK_LCONTROL: return L"Left Ctrl";
    case VK_RCONTROL: return L"Right Ctrl";
    case VK_LMENU: return L"Left Alt";
    case VK_RMENU: return L"Right Alt";
    }
    bool oem = (k >= 0xBA && k <= 0xC0) || (k >= 0xDB && k <= 0xDF) || k == 0xE2;
    if (oem) { // the character on the key in the current layout
        wchar_t buf[32];
        UINT sc = MapVirtualKeyW(k, MAPVK_VK_TO_VSC);
        if (sc && GetKeyNameTextW((LONG)(sc << 16), buf, 32) > 0) return buf;
    }
    return widen(ini_name(k));
}

bool load_ini(const char *k, const char *value)
{
    if (strncmp(k, "key_", 4) != 0) return false;
    ensure_defaults();
    for (int i = 0; i < kCount; i++) {
        if (strcmp(k + 4, kActions[i].ini) != 0) continue;
        std::string v = value;
        size_t comma = v.find(',');
        for (int s = 0; s < kSlots; s++) {
            std::string part = s == 0 ? v.substr(0, comma) : comma == std::string::npos ? "" : v.substr(comma + 1);
            set_key(i, s, parse_key(part));
        }
        return true;
    }
    return true; // a key_ line for an action this build does not have
}

void save_ini(FILE *f)
{
    ensure_defaults();
    for (int i = 0; i < kCount; i++) {
        std::string v = ini_name(g_keys[i][0]);
        if (g_keys[i][1]) v += "," + ini_name(g_keys[i][1]);
        fprintf(f, "key_%s = %s\n", kActions[i].ini, v.c_str());
    }
}

void script_hold(const std::string &name)
{
    uint16_t k = parse_key(name);
    if (k && k < g_script.size()) g_script[k] = true;
}

int take_menu_input()
{
    int v = g_menu_input.exchange(0);
    return v && g_frame_count - g_menu_input_frame <= 2 ? v : 0;
}

bool menu_select_held() { return g_menu_select; }

bool in_play()
{
    uint32_t f = mouselook_play_frame();
    return f && g_frame_count - f <= 3;
}

void apply(uint16_t &buttons, uint8_t analog[8], int16_t &lx, int16_t &ly, int16_t &rx, int16_t &ry, bool focus,
           bool mouse_live, int wheel)
{
    ensure_defaults();
    bool play = in_play();
    int mouse = mouse_live ? host3d::mouse_buttons() : 0;
    uint16_t wkey = wheel_key(mouse_live ? wheel : 0);

    if (play != g_was_play) { // keys held now belong to the other mode: ignore them until released
        logf("[input] %s (frame %u)", play ? "in play: keys drive the game's actions" : "menu, pause or cutscene: "
             "arrows / Enter / Backspace drive the menus", g_frame_count.load());
        for (int i = 0; i < kCount; i++)
            for (int s = 0; s < kSlots; s++)
                if (raw_held(g_keys[i][s], focus, mouse)) g_stale[g_keys[i][s]] = true;
        for (uint16_t k : {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK, VK_ESCAPE})
            if (raw_held(k, focus, 0)) g_stale[k] = true;
        if (play && g_freed_by_esc && focus) host3d::request_capture(true);
        if (play) g_freed_by_esc = false;
        g_was_play = play;
    }
    auto held = [&](uint16_t k) {
        if (!k) return false;
        if (k == KEY_WHEEL_UP || k == KEY_WHEEL_DOWN) return k == wkey || g_script[k];
        if (!raw_held(k, focus, mouse)) {
            g_stale[k] = false;
            return false;
        }
        return !g_stale[k];
    };

    Pad p;
    const int32_t *table = game_table();
    bool pause_down = false;
    int lean = 0;
    for (int i = 0; play && i < kCount && table; i++) {
        bool down = false;
        for (int s = 0; s < kSlots; s++) down |= held(g_keys[i][s]);
        if (!down) continue;
        if (i == kPause) pause_down = true;
        if (kActions[i].game < 0) { // port actions
            if (!strcmp(kActions[i].ini, "lean_left")) lean--;
            else if (!strcmp(kActions[i].ini, "lean_right")) lean++;
            continue;
        }
        int ch = table[kActions[i].game];
        if (ch < 0 || ch >= 24) {
            static std::bitset<kCount> told;
            if (!told[i]) logf("[input] %ls: the game's controller layout has no button for this action", kActions[i].name);
            told[i] = true;
            continue;
        }
        press_channel(ch, p);
    }
    lean_input(lean); // 0 outside play: the view eases back upright
    bool menu_esc = false;
    int input = 0;
    ULONGLONG now = GetTickCount64();
    for (int i = 0; i < kMenuKeyCount; i++) {
        bool h = !play && held(kMenuKeys[i].vk);
        if (h && !g_menu_down[i]) {
            input = kMenuKeys[i].input;
            g_repeat = kMenuKeys[i].repeats ? i : -1;
            g_repeat_at = now + kRepeatDelay;
        }
        g_menu_down[i] = h;
    }
    if (g_repeat >= 0 && !g_menu_down[g_repeat]) g_repeat = -1;
    if (!input && g_repeat >= 0 && now >= g_repeat_at) {
        input = kMenuKeys[g_repeat].input;
        g_repeat_at = now + kRepeatEvery;
    }
    if (input) {
        g_menu_input_frame = g_frame_count.load();
        g_menu_input = input;
    }
    g_menu_select = g_menu_down[4];
    // Enter is the pad's A for the whole press when, as it went down, a hint stopped the game, or START when no menu
    // page was up (the intro text); a press a menu took stays the menu's.
    enum { kMenu, kA, kStart };
    static bool enter_was;
    static int enter_as, told;
    if (g_menu_select && !enter_was) {
        enter_as = hint_stopped() ? kA : no_menu_page() ? kStart : kMenu;
        if (enter_as != kMenu && told++ < 20) logf("[input] Enter = %s", enter_as == kA ? "A (a hint stops the game)" : "START (no menu page)");
    }
    enter_was = g_menu_select;
    if (g_menu_select && enter_as == kA) p.analog[0] = 255;
    if (g_menu_select && enter_as == kStart) p.buttons |= 0x10;
    if (!play) menu_esc = held(VK_ESCAPE);
    // The pause key in play and Esc in menus also let go of the mouse; play starting or
    // resuming takes it again.
    bool release = play ? pause_down : menu_esc;
    if (release && !g_release_was_down && host3d::mouse_captured()) {
        host3d::request_capture(false);
        g_freed_by_esc = true;
    }
    g_release_was_down = release;
    g_script.reset();

    buttons |= p.buttons;
    for (int i = 0; i < 8; i++) analog[i] = std::max(analog[i], p.analog[i]);
    int16_t *axes[4] = {&lx, &ly, &rx, &ry};
    for (int i = 0; i < 4; i++) { // keys push the stick fully, unless a pad already pushes that axis further
        int v = std::clamp(p.axis[i], -1, 1) * 32767;
        if (v && abs(v) > abs(*axes[i])) *axes[i] = (int16_t)v;
    }
}


// ---- in-game capture (game thread) ----
namespace {
int g_cap_action = -1, g_cap_slot;
bool g_cap_wait_up, g_cap_changed;

bool candidate(int vk) // keys that can be bound: mouse buttons and keyboard keys, Shift/Ctrl/Alt as left/right
{
    if (vk == 3 || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return false;
    return vk >= 1 && vk <= 0xFE;
}
} // namespace

void capture_start(int action, int slot)
{
    if (action < 0 || action >= kCount || fixed(action, slot)) return;
    g_cap_action = action;
    g_cap_slot = slot;
    g_cap_wait_up = true;
    logf("[input] key bindings: press a key for %ls (key %d)", kActions[action].name, slot + 1);
}

bool capturing() { return g_cap_action >= 0; }

bool capture_take_change()
{
    bool c = g_cap_changed;
    g_cap_changed = false;
    return c;
}

bool capture_poll(bool focus, int wheel)
{
    if (g_cap_action < 0) return false;
    ensure_defaults();
    uint16_t pressed = 0;
    bool any = false;
    for (int vk = 1; vk <= 0xFE; vk++) {
        bool down = g_script[vk] || (focus && (GetAsyncKeyState(vk) & 0x8000)); // tests: --press-file k:Name
        if (!candidate(vk) || !down) continue;
        any = true;
        if (!pressed) pressed = (uint16_t)vk;
    }
    g_script.reset(); // apply() does not run while capturing
    if (g_cap_wait_up) { // the key that started the capture is still down
        if (!any) g_cap_wait_up = false;
        return true;
    }
    if (!pressed && wheel && focus) pressed = wheel > 0 ? KEY_WHEEL_UP : KEY_WHEEL_DOWN;
    if (!pressed) return true;
    int a = g_cap_action, s = g_cap_slot;
    g_cap_action = -1;
    g_cap_changed = true;
    for (uint16_t k : {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK, VK_ESCAPE})
        if (focus && (GetAsyncKeyState(k) & 0x8000)) g_stale[k] = true; // not also a menu press when capture ends
    if (pressed == VK_ESCAPE) {
        logf("[input] key bindings: cancelled");
        return true;
    }
    uint16_t k = pressed == VK_DELETE ? 0 : pressed;
    if (k)
        for (int i = 0; i < kCount; i++)
            for (int j = 0; j < kSlots; j++)
                if ((i != a || j != s) && g_keys[i][j] == k && !fixed(i, j)) g_keys[i][j] = 0; // taken from that action
    g_keys[a][s] = k;
    settings_save();
    logf("[input] key bindings: %ls key %d = %s", kActions[a].name, s + 1, k ? ini_name(k).c_str() : "none");
    return true;
}
} // namespace bindings
