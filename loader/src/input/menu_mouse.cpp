// Mouse in the menus (issue #41). In the menus the mouse is not captured (host3d::set_menu_mode): the cursor shows, a
// click goes to the menu, and play starting or resuming captures it (input/bindings.cpp).
//
// - The wheel is up / down in every menu (the menus' own inputs, as the arrow keys).
// - The port's pages (the main menu's PC SETTINGS page, its group pages and KEY BINDINGS, the pause menu's PC SETTINGS page)
//   report each update where their rows are (fe/pages.cpp emit_rows_report). The cursor is mapped to the menus' stage:
//   the picture's 0..1 to the 640 x 480 the front end draws (the stage width is 853.33 in widescreen, 640 in 4:3), then
//   through the inverse of the stage matrix (FE object *0x2D199C + 0x3740: a, b, c, d, tx, ty; FUN_00113E70 shifts tx
//   by 106.667 in widescreen), then into the rows' container. Hovering a row moves the focus there (up / down inputs,
//   one per few polls, so the menu's own cursor sound and animation play); a click on the focused row selects it (a
//   choice or a slider steps right, a link opens, a key row waits for a key), a right click is Back.
// The game's own pages are not clickable yet (their rows are not the port's).
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

namespace menu_mouse {
namespace {

const char *const kInts[] = {"FEPCMAx", "FEPCMAy", "FEPCMSx", "FEPCMSy", "FEPCMR0x", "FEPCMR0y", "FEPCMDy",
                             "FEPCMW",  "FEPCMH",  "FEPCMFocus", "FEPCMPage", "FEPCMSeq"};

float g_script_x = -1, g_script_y = -1; // tests: press-file mpos:X,Y (0..1 of the picture)
int g_last_seq, g_seq_age = 1000;       // polls since the page last reported
float g_last_x = -1, g_last_y = -1;
int g_target = -1, g_click = -1, g_step_wait;
bool g_left_was, g_right_was;

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

// the row under the picture point (px, py: 0..1), or -1
int hit_row(float px, float py, int page)
{
    uint32_t fe = *(const uint32_t *)(uintptr_t)xbe_va(0x002D199C);
    uint32_t renderer = *(const uint32_t *)(uintptr_t)xbe_va(0x002D67F4);
    if (!fe) return -1;
    bool wide = renderer && *(const uint8_t *)(uintptr_t)(renderer + 0xC);
    float X = px * (wide ? 853.333f : 640.0f), Y = py * 480.0f;
    const float *m = (const float *)(uintptr_t)(fe + 0x3740); // a b c d tx ty: X = a x + c y + tx, Y = b x + d y + ty
    float det = m[0] * m[3] - m[1] * m[2];
    if (fabsf(det) < 1e-6f) return -1;
    float dx = X - m[4], dy = Y - m[5];
    float sx = (m[3] * dx - m[2] * dy) / det, sy = (-m[1] * dx + m[0] * dy) / det;
    float Sx = f64("FEPCMSx"), Sy = f64("FEPCMSy");
    if (fabsf(Sx) < 1e-4f || fabsf(Sy) < 1e-4f) return -1;
    float lx = (sx - f64("FEPCMAx")) / Sx, ly = (sy - f64("FEPCMAy")) / Sy;
    float x0 = f64("FEPCMR0x"), y0 = f64("FEPCMR0y"), pitch = f64("FEPCMDy"), w = f64("FEPCMW");
    int n = row_count(page);
    int hit = -1;
    // row 0's clip can be just its caption (the PC SETTINGS list: "VIDEO"); the highlight bar spans ~345 stage units from
    // a little left of the text (measured), so every row is at least that wide
    float width = std::max(w, 345.0f / fabsf(Sx));
    if (pitch > 1 && lx >= x0 - 10.0f / fabsf(Sx) && lx <= x0 + width) {
        int r = (int)floorf((ly - y0) / pitch + 0.5f); // rows are centred on their _y (measured)
        if (r >= 0 && r < n) hit = r;
    }
    return hit;
}

} // namespace

void install()
{
    for (const char *n : kInts) fe_int_set(n, 0); // registered from the start: the pages write them
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
    if (!in_menus) {
        g_target = g_click = -1;
        g_left_was = left;
        g_right_was = right;
        return 0;
    }
    bool left_edge = left && !g_left_was, right_edge = right && !g_right_was;
    g_left_was = left;
    g_right_was = right;
    if (key_input) {
        g_target = g_click = -1; // the keyboard leads
        return 0;
    }
    int seq = fe_int_get("FEPCMSeq");
    g_seq_age = seq != g_last_seq ? 0 : g_seq_age + 1;
    g_last_seq = seq;
    bool page_up = g_seq_age < 6;
    int page = fe_int_get("FEPCMPage") - 1;
    float px, py;
    bool have = g_script_x >= 0 ? (px = g_script_x, py = g_script_y, true) : host3d::cursor_in_picture(&px, &py);
    int hit = have && page_up ? hit_row(px, py, page) : -1;
    bool moved = have && (fabsf(px - g_last_x) > 0.002f || fabsf(py - g_last_y) > 0.002f);
    if (have) {
        g_last_x = px;
        g_last_y = py;
    }
    if (moved && hit >= 0) g_target = hit;
    int focus = fe_int_get("FEPCMFocus");
    if (right_edge) return 6;
    if (left_edge && hit >= 0) {
        if (hit == focus) return click_input(page, hit);
        g_target = g_click = hit;
    }
    if (wheel) return wheel > 0 ? 1 : 2;
    if (!page_up || focus < 0) return 0;
    if (g_target >= 0 && g_target != focus) {
        if (g_step_wait > 0) {
            g_step_wait--;
            return 0;
        }
        g_step_wait = 2;
        return g_target > focus ? 2 : 1;
    }
    g_target = -1;
    if (g_click >= 0 && g_click == focus) {
        g_click = -1;
        return click_input(page, focus);
    }
    return 0;
}

} // namespace menu_mouse
