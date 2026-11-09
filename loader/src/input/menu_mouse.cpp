// Mouse in the menus (issue #41). In the menus the mouse is not captured (host3d::set_menu_mode): the cursor shows, a
// click goes to the menu, and play starting or resuming captures it (input/bindings.cpp).
//
// - The wheel is up / down in every menu (the menus' own inputs, as the arrow keys); left / right on MISSION SELECT,
//   where a click anywhere confirms.
// - The port's pages (the main menu's PC SETTINGS page, its group pages and KEY BINDINGS, the pause menu's PC SETTINGS page)
//   report each update where their rows are (fe/pages.cpp emit_rows_report). The cursor is mapped to the menus' stage:
//   the picture's 0..1 to the 640 x 480 the front end draws (the stage width is 853.33 in widescreen, 640 in 4:3), then
//   through the inverse of the stage matrix (FE object *0x2D199C + 0x3740: a, b, c, d, tx, ty; FUN_00113E70 shifts tx
//   by 106.667 in widescreen), then into the rows' container. Hovering a row moves the focus there (up / down inputs,
//   one per few polls, so the menu's own cursor sound and animation play); a click on the focused row selects it (a
//   choice steps right, a link opens, a key row waits for a key; on a slider's bar the value moves to the point
//   pressed and follows while held), a right click is Back.
// - The game's own pages (every page with a BkMenuList but the port's): the port gives them the cursor in stage units
//   and each tests its buttons itself (fe/pages.cpp emit_ring_report), reporting the ring offset under the cursor from
//   the focused button; hover steps there, a click selects (the game's rows are links or the game's own choices).
// A step is sent only after the page has reported again since the last one (its report trails the focus by an update).
#include "input/menu_mouse.h"

#include "common.h"
#include "d3d/host.h"
#include "fe/bind_list.h"
#include "fe/pause_ui.h"
#include "fe/pc_settings.h"
#include "fe/vars.h"
#include "xbe.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace menu_mouse {
namespace {

const char *const kInts[] = {"FEPCMAx", "FEPCMAy", "FEPCMSx", "FEPCMSy", "FEPCMR0x", "FEPCMR0y", "FEPCMDy",
                             "FEPCMW",  "FEPCMH",  "FEPCMFocus", "FEPCMPage", "FEPCMSeq",
                             "FEPCMouseX", "FEPCMouseY", "FEPCGHit", "FEPCGSeq", "FEPCGRx", "FEPCGKind", "FEPCMBx", "FEPCMBw", "FEPCGBw", "FEPCGVal"};

float g_script_x = -1, g_script_y = -1; // tests: press-file mpos:X,Y (0..1 of the picture)
struct Report {
    const char *name;
    int last = 0, age = 1000; // polls since it last counted up
    bool fresh() const { return age < 6; }
    void poll()
    {
        int seq = fe_int_get(name);
        age = seq != last ? 0 : age + 1;
        last = seq;
    }
};
Report g_own{"FEPCMSeq"}, g_game{"FEPCGSeq"};
float g_last_x = -1, g_last_y = -1;
bool g_follow, g_click, g_left_was, g_right_was;
bool g_moved; // the cursor moved and no row was under it yet (a game page's hit comes an update later)
int g_step_seq = -1; // the report's count when the last step went out
int g_drag = -1;     // the slider setting being dragged (pressed on its bar, until the button is let go)
int g_game_hit = -100, g_game_hit_age = 1000; // a game page's last hit and polls since
int g_game_rx, g_game_kind;                    // with it: the cursor's x along the row, a choice row (2: on a slider's bar)
int g_game_bw, g_game_val;                     // a slider's bar width and value 0..1000

// a click on a game page's focused row: select, or on a choice row (the game's one-line ON / OFF toggles) the value
// clicked: from about 200 to 276 stage units along the row (the first value, ON) left, past it right; on the caption right
int game_click()
{
    if (!g_game_kind) return 5;
    if (g_game_rx < 200) return 4;
    return g_game_rx < 276 ? 3 : 4;
}

float f64(const char *name) { return fe_int_get(name) / 64.0f; }

// what a click on row r of the reporting page does
int click_input(int page, int r)
{
    int kind = 0; // 1 link, 2 choice, 3 slider, 4 key
    if (page == pauseui::kPageId) kind = pauseui::row_kind(r);
    else if (page == pcset::kRootPage) kind = 1; // a group opens
    else if (page == 100) kind = 4;
    else {
        const auto &gs = pcset::groups();
        if (page >= 0 && page < (int)gs.size() && r < (int)gs[page].rows.size()) {
            int n = gs[page].rows[r];
            kind = pcset::link(n) ? 1 : pcset::is_slider(n) ? 3 : 2;
        }
    }
    return kind == 2 || kind == 3 ? 4 : kind ? 5 : 0;
}

int row_count(int page)
{
    if (page == pauseui::kPageId) {
        int n = 0;
        while (n < pauseui::kRows && pauseui::row_kind(n)) n++;
        return n;
    }
    if (page == 100) return bindlist::kRows;
    if (page == pcset::kRootPage) return (int)pcset::groups().size();
    const auto &gs = pcset::groups();
    return page >= 0 && page < (int)gs.size() ? (int)gs[page].rows.size() : 0;
}

// a page that picks with left / right instead of a list (MISSION SELECT: the mission name, A confirms): the wheel is
// left / right there and a click anywhere confirms (user: the wheel and the click did nothing there)
bool sideways_page()
{
    uint32_t fe = *(const uint32_t *)(uintptr_t)xbe_va(0x002D199C);
    if (!fe) return false;
    return _strnicmp((const char *)(uintptr_t)(fe + 0x3840), "MissionModeSelect", 17) == 0;
}

// the setting of row r of a port page, -1 if none
int row_setting(int page, int r)
{
    if (page == pauseui::kPageId) return pauseui::row_setting(r);
    const auto &gs = pcset::groups();
    if (page >= 0 && page < (int)gs.size() && r >= 0 && r < (int)gs[page].rows.size()) return gs[page].rows[r];
    return -1;
}

// the picture point (0..1) on the menus' stage
bool to_stage(float px, float py, float *sx, float *sy)
{
    uint32_t fe = *(const uint32_t *)(uintptr_t)xbe_va(0x002D199C);
    uint32_t renderer = *(const uint32_t *)(uintptr_t)xbe_va(0x002D67F4);
    if (!fe) return false;
    bool wide = renderer && *(const uint8_t *)(uintptr_t)(renderer + 0xC);
    float X = px * (wide ? 853.333f : 640.0f), Y = py * 480.0f;
    const float *m = (const float *)(uintptr_t)(fe + 0x3740); // a b c d tx ty: X = a x + c y + tx, Y = b x + d y + ty
    float det = m[0] * m[3] - m[1] * m[2];
    if (fabsf(det) < 1e-6f) return false;
    float dx = X - m[4], dy = Y - m[5];
    *sx = (m[3] * dx - m[2] * dy) / det;
    *sy = (-m[1] * dx + m[0] * dy) / det;
    return true;
}

float g_lx; // the last own_row point, in the rows' container

// the port's page: the row under the stage point, or -1
int own_row(float sx, float sy, int page)
{
    float Sx = f64("FEPCMSx"), Sy = f64("FEPCMSy");
    if (fabsf(Sx) < 1e-4f || fabsf(Sy) < 1e-4f) return -1;
    float lx = (sx - f64("FEPCMAx")) / Sx, ly = (sy - f64("FEPCMAy")) / Sy;
    g_lx = lx;
    float x0 = f64("FEPCMR0x"), y0 = f64("FEPCMR0y"), pitch = f64("FEPCMDy"), w = f64("FEPCMW");
    // row 0's clip can be just its caption (the PC SETTINGS list: "VIDEO"); the highlight bar spans ~345 stage units
    // from a little left of the text (measured), so every row is at least that wide
    float width = std::max(w, 345.0f / fabsf(Sx));
    if (pitch <= 1 || lx < x0 - 10.0f / fabsf(Sx) || lx > x0 + width) return -1;
    int r = (int)floorf((ly - y0) / pitch + 0.5f); // rows are centred on their _y (measured)
    return r >= 0 && r < row_count(page) ? r : -1;
}

} // namespace

void install()
{
    for (const char *n : kInts) fe_int_set(n, 0); // registered from the start: the pages read and write them
    fe_int_set("FEPCMouseX", -100000);
    fe_int_set("FEPCGHit", -100);
}

void script_pos(float x, float y)
{
    g_script_x = x;
    g_script_y = y;
}

int poll(bool in_menus, bool left, bool right, int key_input)
{
    host3d::set_menu_mode(in_menus);
    int wheel = host3d::menu_wheel_take();
    { // one wheel step per 0.25 s at most (user): notches in between are dropped, so a flick does not skip pages
        static ULONGLONG last;
        ULONGLONG now = GetTickCount64();
        if (wheel && now - last < 250) wheel = 0;
        else if (wheel) last = now;
    }
    bool left_edge = left && !g_left_was, right_edge = right && !g_right_was;
    g_left_was = left;
    g_right_was = right;
    if (!in_menus) {
        g_follow = g_click = g_moved = false;
        fe_int_set("FEPCMouseX", -100000);
        return 0;
    }
    if (key_input) { // the keyboard leads
        g_follow = g_click = g_moved = false;
        return 0;
    }
    g_own.poll();
    g_game.poll();
    int h = fe_int_get("FEPCGHit"); // the pages write it only on a hit; cleared here, kept a few polls
    if (h != -100) {
        g_game_hit = h;
        g_game_hit_age = 0;
        g_game_rx = fe_int_get("FEPCGRx");
        g_game_kind = fe_int_get("FEPCGKind");
        g_game_bw = fe_int_get("FEPCGBw");
        g_game_val = fe_int_get("FEPCGVal");
        fe_int_set("FEPCGHit", -100);
    } else if (g_game_hit_age < 1000) {
        g_game_hit_age++;
    }
    float px, py, sx = 0, sy = 0;
    bool have = g_script_x >= 0 ? (px = g_script_x, py = g_script_y, true) : host3d::cursor_in_picture(&px, &py);
    if (have) have = to_stage(px, py, &sx, &sy);
    fe_int_set("FEPCMouseX", have ? (int)lroundf(sx * 4) : -100000);
    fe_int_set("FEPCMouseY", have ? (int)lroundf(sy * 4) : -100000);
    bool moved = have && (fabsf(px - g_last_x) > 0.002f || fabsf(py - g_last_y) > 0.002f);
    if (have) {
        g_last_x = px;
        g_last_y = py;
    }
    // the ring offset from the focus to the row under the cursor, on whichever page reports
    const Report *rep = g_own.fresh() ? &g_own : g_game.fresh() ? &g_game : nullptr;
    int offset = -100, page = -1, row = -1;
    if (rep == &g_own && have) {
        page = fe_int_get("FEPCMPage") - 1;
        row = own_row(sx, sy, page);
        int focus = fe_int_get("FEPCMFocus");
        if (row >= 0 && focus >= 0) offset = row - focus;
    } else if (rep == &g_game && have) {
        offset = g_game_hit_age < 4 ? g_game_hit : -100;
    }
    bool over = offset != -100;
    if (moved) g_moved = true;
    if (g_moved && over) {
        g_follow = true;
        g_moved = false;
    }
    // a port page's slider: pressed on its bar, it steps toward the cursor while the button is held, one step per report
    // (the setting's own steps and sound), so a click anywhere on the bar sets it there and dragging follows
    if (!left) g_drag = -1;
    if (rep == &g_game && left && g_game_kind == 2 && g_game_hit == 0 && g_game_hit_age < 4 && g_game_bw > 1) {
        if (left_edge) g_drag = 1000; // a game slider (no setting of the port's)
        if (g_drag == 1000) {
            float target = std::clamp(g_game_rx * 1000.0f / g_game_bw, 0.0f, 1000.0f);
            if (fabsf(g_game_val - target) <= 25.0f || rep->last == g_step_seq) return 0;
            g_step_seq = rep->last;
            return g_game_val < target ? 4 : 3;
        }
    }
    if (rep == &g_own && have && left) {
        int n = row_setting(page, fe_int_get("FEPCMFocus"));
        float bx = f64("FEPCMBx"), bw = f64("FEPCMBw");
        if (left_edge && offset == 0 && n >= 0 && pcset::is_slider(n) && bw > 1 && g_lx >= bx - 15 && g_lx <= bx + bw + 15) g_drag = n;
        if (g_drag >= 0 && g_drag == n && bw > 1) {
            float target = std::clamp((g_lx - bx) / bw * 100.0f, 0.0f, 100.0f);
            float cur = (float)pcset::slider_pos(n);
            if (fabsf(cur - target) <= 2.5f || rep->last == g_step_seq) return 0;
            g_step_seq = rep->last;
            return cur < target ? 4 : 3;
        }
    }
    if (right_edge) return 6;
    if (sideways_page()) {
        g_follow = g_click = false;
        if (left_edge) return 5;
        return wheel ? (wheel > 0 ? 3 : 4) : 0;
    }
    if (left_edge && over) {
        if (offset == 0) return rep == &g_own ? click_input(page, row) : game_click();
        g_follow = g_click = true;
    }
    if (wheel) return wheel > 0 ? 1 : 2;
    if (!rep || !g_follow) return 0;
    if (!over) return 0; // off the rows: the focus stays
    if (offset == 0) {
        g_follow = false;
        if (g_click) {
            g_click = false;
            return rep == &g_own ? click_input(page, row) : game_click();
        }
        return 0;
    }
    if (rep->last == g_step_seq) return 0; // the page has not reported since the last step
    g_step_seq = rep->last;
    return offset > 0 ? 2 : 1;
}

} // namespace menu_mouse
