// On-screen button prompts that name the bound keyboard key or mouse button instead of a pad button.
//
// The game's hint texts (HINT_INGAME_%d, the press-to-continue line FE_HINTPROMPT, ...) hold one placeholder
// character per action: U+F000 + the action's index in the binding table (U+F00B "reload", U+F01B "grenade";
// U+F025 is the A button itself). FUN_00113BF0 (cdecl, one argument: the NUL-terminated wide text, edited in
// place) replaces each of them: action -> channel through the binding table *0x2F3BD4 (0x25 -> channel 0x10 = A),
// channel -> a button name in the table at 0x2AC290 ("BUTTON_A", "BUTTON_DPAD_UP", ...), name -> the localized
// text (FUN_001F3A80 on the string table *0x2D45D8), which is a glyph the fonts draw as that pad button. Callers:
// 0x11862B, 0x129EB2 (in-game hints, 0x78-character field), 0x12B4DB (FE_HINTPROMPT: before the call, every
// placeholder is changed into the action bound to channel 0x10, so it shows A) and 0x12B4EA (the hint's text).
//
// The patch sends FUN_00113BF0 to expand(): while the keyboard and mouse were used last, each placeholder becomes
// the first key bound to that action in brackets ("[R] to reload your weapon"), or the icon of a mouse button (added
// to the fonts by fe/fonts.cpp; Mouse 4 / 5 have no icon yet and keep their name); an action with no key keeps its
// pad glyph (the original function, run on just that character). The FE_HINTPROMPT call goes to expand_continue(),
// which names Enter (the key that continues where the game waits for A, see bindings.cpp). While a controller was
// used last, the original function runs unchanged. The device is the last one pressed (prompts_poll from read_pad, xinput.cpp).
#include "input/prompts.h"

#include "common.h"
#include "fe/fonts.h"
#include "fe/vars.h"
#include "input/bindings.h"
#include "xbe.h"

#include <cstring>
#include <cwctype>
#include <string>

extern "C" {
volatile uint8_t g_prompt_keys = 1; // 1: name keyboard keys (keyboard and mouse used last); 0: pad glyphs
uint32_t g_prompt_resume;           // FUN_00113BF0 after the replaced prologue
}

static void legend(bool keyboard); // the menus' control legend

namespace {

constexpr uint32_t kExpand = 0x00113BF0;   // as analyzed in the NTSC-U build; xbe_va maps it to this build
constexpr uint32_t kContinueCall = 0x0012B4DB; // call FUN_00113BF0 for FE_HINTPROMPT
// sub esp, 0x40; push ebx; push ebp (the 5 bytes replaced by the jump)
const uint8_t kPrologue[5] = {0x83, 0xEC, 0x40, 0x53, 0x55};
constexpr size_t kMaxChars = 0x78; // the smallest field the callers expand into (in-game hints), with its NUL
constexpr wchar_t kFirst = 0xF001, kLast = 0xF0FF;

bool g_installed;

// The original function, callable from C++ (cdecl, one argument).
__declspec(naked) void __cdecl original_expand(wchar_t *)
{
    __asm {
        sub esp, 40h
        push ebx
        push ebp
        jmp dword ptr [g_prompt_resume]
    }
}

std::wstring key_label(uint16_t k)
{
    switch (k) {
    case VK_LBUTTON: return L"LEFT MOUSE";
    case VK_RBUTTON: return L"RIGHT MOUSE";
    case VK_MBUTTON: return L"MIDDLE MOUSE";
    case VK_XBUTTON1: return L"MOUSE 4";
    case VK_XBUTTON2: return L"MOUSE 5";
    case bindings::KEY_WHEEL_UP: return L"WHEEL UP";
    case bindings::KEY_WHEEL_DOWN: return L"WHEEL DOWN";
    }
    std::wstring s = bindings::key_name(k);
    for (wchar_t &c : s) c = (wchar_t)towupper(c);
    return s;
}

// The first key bound to the game action, or 0.
uint16_t bound_key(int game_action)
{
    int n;
    const bindings::Action *a = bindings::actions(&n);
    for (int i = 0; i < n; i++) {
        if (a[i].game != game_action) continue;
        for (int s = 0; s < bindings::kSlots; s++)
            if (uint16_t k = bindings::key(i, s)) return k;
    }
    return 0;
}

// The pad glyph the game would show for one placeholder.
std::wstring pad_glyph(wchar_t c)
{
    wchar_t buf[40] = {c, 0};
    original_expand(buf);
    buf[39] = 0;
    return buf;
}

void expand_text(wchar_t *text, bool to_continue)
{
    std::wstring in = text, out;
    for (size_t i = 0; i < in.size(); i++) {
        wchar_t c = in[i];
        if (c < kFirst || c > kLast) {
            out += c;
            continue;
        }
        uint16_t k = to_continue ? (uint16_t)VK_RETURN : c == 0xF025 ? (uint16_t)VK_RETURN : bound_key(c - 0xF000);
        wchar_t icon = fe_mouse_icon(k); // a mouse button: its icon, drawn like the pad buttons (fe/fonts.cpp)
        std::wstring label = icon ? std::wstring(1, icon) : k ? L"[" + key_label(k) + L"]" : pad_glyph(c);
        // never longer than the caller's field: a label that does not fit falls back to the glyph
        size_t rest = in.size() - i - 1;
        if (out.size() + label.size() + rest >= kMaxChars) label = pad_glyph(c);
        out += label;
    }
    memcpy(text, out.c_str(), (out.size() + 1) * sizeof(wchar_t));
}

void __cdecl expand(wchar_t *text)
{
    if (!g_prompt_keys || !text) {
        original_expand(text);
        return;
    }
    expand_text(text, false);
}

void __cdecl expand_continue(wchar_t *text)
{
    if (!g_prompt_keys || !text) {
        original_expand(text);
        return;
    }
    expand_text(text, true);
}

// ---- the weapon pick-up / swap prompt ("hold %s to pick up", GM_PickWeapon / GM_SwapWeapon1) ----
// The HUD (0x132897..) finds the use action's channel (*0x2F3BD4 +0x34 = action 13) among the 10 assignable buttons
// at 0x2A7DD4 (index in edi) and formats the text with the glyph string at 0x2AB72C[edi] (one Latin-1 byte the font
// draws as that button). Both reads of that table (mov edx / mov ecx, [edi*4 + 0x2AB72C]: 7 bytes each) become a
// call to a thunk plus two nops; the thunk loads the same register with pick_label(the glyph).
constexpr uint32_t kPickRead[2] = {0x0013296F, 0x00132A16};
constexpr uint32_t kGlyphTable = 0x002AB72C;
constexpr int kUseAction = 13;

} // namespace

extern "C" {
uint32_t g_glyph_table; // 0x2AB72C in this build

const char *__cdecl pick_label(const char *glyph)
{
    static char buf[32];
    uint16_t k = g_prompt_keys ? bound_key(kUseAction) : 0;
    if (!k) return glyph;
    std::wstring w = L"[" + key_label(k) + L"]";
    size_t n = 0;
    for (wchar_t c : w)
        if (n + 1 < sizeof buf) buf[n++] = c < 0x80 ? (char)c : '?';
    buf[n] = 0;
    static bool logged;
    if (!logged) {
        logged = true;
        logf("prompts: weapon pick-up prompt names %s", buf);
    }
    return buf;
}
}

namespace {

__declspec(naked) void pick_thunk_edx()
{
    __asm {
        push eax
        push ecx
        mov ecx, dword ptr [g_glyph_table]
        push dword ptr [ecx + edi*4]
        call pick_label
        add esp, 4
        mov edx, eax
        pop ecx
        pop eax
        ret
    }
}

__declspec(naked) void pick_thunk_ecx()
{
    __asm {
        push eax
        push edx
        mov ecx, dword ptr [g_glyph_table]
        push dword ptr [ecx + edi*4]
        call pick_label
        add esp, 4
        mov ecx, eax
        pop edx
        pop eax
        ret
    }
}

bool install_pick_patch()
{
    static const uint8_t kModrm[2] = {0x14, 0x0C}; // edx, ecx
    void (*thunks[2])() = {pick_thunk_edx, pick_thunk_ecx};
    uint32_t table = xbe_va(kGlyphTable);
    uint8_t *sites[2];
    for (int i = 0; i < 2; i++) {
        sites[i] = (uint8_t *)(uintptr_t)xbe_va(kPickRead[i]);
        uint32_t disp;
        memcpy(&disp, sites[i] + 3, 4);
        if (sites[i][0] != 0x8B || sites[i][1] != kModrm[i] || sites[i][2] != 0xBD || disp != table) return false;
    }
    g_glyph_table = table;
    for (int i = 0; i < 2; i++) {
        uint8_t *c = sites[i];
        int32_t rel = (int32_t)((uintptr_t)thunks[i] - ((uintptr_t)c + 5));
        c[0] = 0xE8; // call thunk; nop; nop
        memcpy(c + 1, &rel, 4);
        c[5] = c[6] = 0x90;
    }
    return true;
}

} // namespace

void install_prompts_patch()
{
    if (!install_pick_patch()) logf("prompts: the weapon pick-up prompt code does not match this build; it keeps the pad button");

    uint32_t site = xbe_va(kExpand), call = xbe_va(kContinueCall);
    auto *code = (uint8_t *)(uintptr_t)site;
    auto *cc = (uint8_t *)(uintptr_t)call;
    int32_t old_rel;
    memcpy(&old_rel, cc + 1, 4);
    if (memcmp(code, kPrologue, sizeof kPrologue) != 0 || cc[0] != 0xE8 || call + 5 + old_rel != site) {
        logf("prompts: code at 0x%08x / 0x%08x does not match this build; prompts keep the pad buttons", site, call);
        return;
    }
    g_prompt_resume = site + sizeof kPrologue;
    code[0] = 0xE9; // jmp expand
    int32_t rel = (int32_t)((uintptr_t)expand - (site + 5));
    memcpy(code + 1, &rel, 4);
    rel = (int32_t)((uintptr_t)expand_continue - (call + 5)); // call expand_continue
    memcpy(cc + 1, &rel, 4);
    fe_int_set("FEPCKeys", g_prompt_keys);
    g_installed = true;
    logf("prompts: button prompts name the bound keys while the keyboard and mouse are in use (FUN_00113bf0)");
}

// The menus' control legend (fe/pages.cpp, core_prompts): the captions come from the game's text table (FE_CONFIRM,
// FE_BACK: FUN_001F3A80, thiscall on *0x2D45D8 with the name's hash, empty until the table is loaded, flag byte
// 0x2F3B0E), with the menu keys in front while the keyboard is in use (fixed keys, bindings.cpp: Enter selects,
// Backspace goes back). Filled at the first poll that finds the table and again at each change of device.
namespace {
bool g_legend_stale = true;

std::wstring game_text(const char *name)
{
    if (!*(const uint8_t *)(uintptr_t)xbe_va(0x002F3B0E)) return {};
    void *table = *(void **)(uintptr_t)xbe_va(0x002D45D8);
    if (!table) return {};
    using Lookup = const char *(__fastcall *)(void *self, void *unused, uint32_t hash); // thiscall, one argument
    const char *s = ((Lookup)(uintptr_t)xbe_va(0x001F3A80))(table, nullptr, fe_hash(name));
    if (!s || !*s) return {};
    wchar_t buf[64];
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, 64);
    return n > 0 ? std::wstring(buf) : std::wstring();
}
} // namespace

static void legend(bool keyboard)
{
    fe_int_set("FEPCKeys", keyboard ? 1 : 0);
    std::wstring confirm = game_text("FE_CONFIRM"), back = game_text("FE_BACK");
    if (confirm.empty() || back.empty()) return; // the text is not loaded yet: the next poll tries again
    fe_var_set(fevar::kKeyConfirm, keyboard ? L"ENTER  " + confirm : confirm);
    fe_var_set(fevar::kKeyBack, keyboard ? L"BACKSPACE  " + back : back);
    g_legend_stale = false;
}

void prompts_poll(int device)
{
    bool keyboard = device == 1;
    uint8_t v = keyboard ? 1 : 0;
    if (device >= 0 && g_prompt_keys != v) {
        g_prompt_keys = v;
        g_legend_stale = true;
        if (g_installed) logf("prompts: %s", keyboard ? "keyboard and mouse in use: prompts name keys" : "controller in use: prompts show pad buttons");
    }
    if (g_legend_stale && g_installed) legend(g_prompt_keys != 0);
}
