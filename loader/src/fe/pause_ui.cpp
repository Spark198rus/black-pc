// PC SETTINGS in the pause menu. The pause pages are loaded into a heap that has little room in play (one copy of a
// pause page per screen did not fit: the game's allocation for PseMenu.bin came back null), so the pause menu gets a
// single page (fe/pages.cpp, "PCPageIG") with six rows that show whatever screen the port is on: the menu of groups,
// a group's settings, or the key bindings. Each row has a caption, a value beside it, and a bullet bar with a number
// after it; the page's Update applies, from externObject:
//   FEPCR<i>   row i: bit 0 shown, bit 1 bar shown, bits 8-15 the bar's frame, bits 16-19 / 20-23 the row below /
//              above + 1 (0: none)
//   FEPCLayout a counter: when it changes the page relinks the rows and focuses row FEPCFocus
//   FEPCAtRoot Back leaves the page (to the pause menu) instead of reporting PCBack
// A row's list object reports inputs 1-5 as PCRow(row, input, MyMenu.MovedCursor): left / right change a setting,
// select opens a screen or waits for a key, up / down at the ends scroll the key list (fe/bind_list.cpp).
// The settings that apply at the next start (aspect ratio, frame rate) are on the main menu's pages only.
#include "fe/pause_ui.h"

#include "common.h"
#include "fe/bind_list.h"
#include "fe/pc_settings.h"
#include "fe/vars.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pauseui {
namespace {

constexpr int kMenu = -1, kKeys = 1000; // screens besides the groups (index into the pause groups)

struct Row {
    enum Kind { Link, Choice, Slider, Key } kind;
    int id; // Link: screen; Choice / Slider: setting; Key: list row
};

int g_screen = kMenu, g_focus, g_layout, g_frames = 26;
std::vector<std::pair<int, int>> g_stack; // (screen, focused row) to go back to

std::vector<pcset::Group> groups()
{
    std::vector<pcset::Group> out;
    for (const pcset::Group &g : pcset::groups()) {
        pcset::Group h = g;
        h.rows.clear();
        for (int n : g.rows)
            if (!pcset::restart_only(n)) h.rows.push_back(n);
        if (!h.rows.empty()) out.push_back(h);
    }
    return out;
}

std::vector<Row> rows(std::wstring *title)
{
    std::vector<Row> out;
    auto gs = groups();
    if (g_screen == kMenu) {
        *title = L"PC SETTINGS";
        for (int i = 0; i < (int)gs.size() && i < kRows; i++) out.push_back({Row::Link, i});
    } else if (g_screen == kKeys) {
        *title = L"KEY BINDINGS";
        for (int i = 0; i < bindlist::kRows; i++) out.push_back({Row::Key, i});
    } else if (g_screen >= 0 && g_screen < (int)gs.size()) {
        *title = widen(gs[g_screen].title);
        for (int n : gs[g_screen].rows) {
            if (pcset::link(n)) out.push_back({Row::Link, kKeys});
            else out.push_back({pcset::is_slider(n) ? Row::Slider : Row::Choice, n});
            if ((int)out.size() == kRows) break;
        }
    }
    return out;
}

std::wstring caption(const Row &r)
{
    if (r.kind == Row::Key) return bindlist::row_label(r.id);
    if (r.kind == Row::Link) {
        if (r.id == kKeys) return L"KEY BINDINGS";
        auto gs = groups();
        return r.id >= 0 && r.id < (int)gs.size() ? widen(gs[r.id].title) : L"";
    }
    return widen(pcset::label(r.id));
}

void go(int screen, int focus)
{
    g_screen = screen;
    g_focus = focus;
    g_layout++;
    if (screen == kKeys) bindlist::open();
}

} // namespace

void open(int bar_frames)
{
    if (bar_frames >= 2 && bar_frames <= 255) g_frames = bar_frames;
    g_stack.clear();
    go(kMenu, 0);
    refresh();
}

void row(int i, int id, bool moved)
{
    std::wstring title;
    std::vector<Row> rs = rows(&title);
    if (i < 0 || i >= (int)rs.size()) return;
    const Row &r = rs[i];
    if (r.kind == Row::Key) bindlist::input(r.id, id, moved);
    else if (r.kind == Row::Link && id == 5) {
        g_stack.push_back({g_screen, i});
        go(r.id, 0);
    } else if (r.kind == Row::Choice && (id == 3 || id == 4)) {
        pcset::step(r.id, id == 3 ? -1 : 1);
    } else if (r.kind == Row::Slider && (id == 3 || id == 4)) { // one of 25 steps, stopping at the ends
        pcset::slide(r.id, (float)pcset::slider_pos(r.id) + (id == 3 ? -4.0f : 4.0f));
    }
    refresh();
}

void back()
{
    if (g_stack.empty()) go(kMenu, 0);
    else {
        auto [screen, focus] = g_stack.back();
        g_stack.pop_back();
        go(screen, focus);
    }
    refresh();
}

void refresh()
{
    std::wstring title;
    std::vector<Row> rs = rows(&title);
    int n = (int)rs.size();
    bool wrap = g_screen != kKeys; // the key list scrolls at its ends instead
    fe_var_set(fevar::kTitle, title);
    for (int i = 0; i < kRows; i++) {
        uint32_t state = 0;
        std::wstring label, value, number;
        if (i < n) {
            const Row &r = rs[i];
            label = caption(r);
            int down = i + 1 < n ? i + 1 : wrap ? 0 : -1, up = i > 0 ? i - 1 : wrap ? n - 1 : -1;
            int frame = 1;
            if (r.kind == Row::Slider) {
                number = pcset::value(r.id);
                frame = 1 + (int)std::lround(pcset::slider_pos(r.id) * (g_frames - 1) / 100.0);
            } else if (r.kind == Row::Choice) {
                value = pcset::value(r.id);
            } else if (r.kind == Row::Key) {
                value = bindlist::row_keys(r.id);
            }
            state = 1 | (r.kind == Row::Slider ? 2u : 0u) | (uint32_t)frame << 8 | (uint32_t)(down + 1) << 16 | (uint32_t)(up + 1) << 20;
        }
        fe_var_set(fevar::kLabel + i, label);
        fe_var_set(fevar::kValue + i, value);
        fe_var_set(fevar::kNumber + i, number);
        fe_int_set(("FEPCR" + std::to_string(i)).c_str(), (int)state);
    }
    fe_int_set("FEPCFocus", std::min(g_focus, std::max(0, n - 1)));
    fe_int_set("FEPCLayout", g_layout);
    fe_int_set("FEPCAtRoot", g_screen == kMenu);
}

} // namespace pauseui
