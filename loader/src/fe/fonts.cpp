// The mouse button icons (by fez, loader/res/mouse, tools/gen_mouse_icons.py) added to the game's two fonts, so the
// prompts can show them the way they show the pad buttons (input/prompts.cpp).
//
// The font format and how the icons are added: util/font.h.
#include "fe/fonts.h"

#include "common.h"
#include "input/bindings.h"

#include "util/files.h"
#include "util/font.h"

#include <vector>

namespace {

#include "fe/mouse_icons.inc"

constexpr wchar_t kFirstIcon = 0xE000; // left, right, middle, wheel up, wheel down (the .inc's order)
bool g_icons;

// The font with the icons, or an empty result (logged) when the file is not laid out as expected.
std::vector<uint8_t> add_icons(const std::vector<uint8_t> &in, const char *name)
{
    std::vector<util::GreyAlpha> icons;
    for (int n = 0; n < kMouseIconCount; n++) icons.push_back({kMouseIconSize, kMouseIconPixels[n]});
    util::font::Patched r = util::font::add_icons(in, icons, kFirstIcon);
    if (r.data.empty()) {
        logf("fonts: %s: %s; no mouse icons", name, r.error.c_str());
        return {};
    }
    for (int n = 0; n < kMouseIconCount; n++)
        if (!util::font::find_glyph(r.data, (wchar_t)(kFirstIcon + n))) {
            logf("fonts: %s: icon U+%04X not found after patching; no mouse icons", name, kFirstIcon + n);
            return {};
        }
    logf("fonts: %s: %d mouse icons (%dx%d) added as U+%04X.., texture %dx%d", name, kMouseIconCount, r.icon_size,
         r.icon_size, kFirstIcon, r.width, r.height);
    return std::move(r.data);
}

} // namespace

bool fe_build_fonts(const std::wstring &game_dir, const std::wstring &out_root)
{
    bool ok = true;
    for (const wchar_t *file : {L"Small.bin", L"Big.bin"}) {
        std::wstring rel = std::wstring(L"\\language\\fonts\\") + file, dst = out_root + rel;
        std::vector<uint8_t> in, out;
        if (!util::read_file(game_dir + rel, in)) {
            logf("fonts: %s not found; no mouse icons", narrow(game_dir + rel).c_str());
            ok = false;
        } else if ((out = add_icons(in, narrow(file).c_str())).empty() || !util::write_file(dst, out)) {
            if (!out.empty()) logf("fonts: cannot write %s", narrow(dst).c_str());
            ok = false;
        }
        if (!ok) break;
    }
    if (!ok) // the game's own fonts for both, so the prompts never ask for a glyph one of them lacks
        for (const wchar_t *file : {L"Small.bin", L"Big.bin"}) DeleteFileW((out_root + L"\\language\\fonts\\" + file).c_str());
    g_icons = ok;
    return ok;
}

wchar_t fe_mouse_icon(uint16_t key)
{
    if (!g_icons) return 0;
    switch (key) {
    case VK_LBUTTON: return kFirstIcon;
    case VK_RBUTTON: return kFirstIcon + 1;
    case VK_MBUTTON: return kFirstIcon + 2;
    case bindings::KEY_WHEEL_UP: return kFirstIcon + 3;
    case bindings::KEY_WHEEL_DOWN: return kFirstIcon + 4;
    }
    return 0;
}
