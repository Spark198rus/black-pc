// PC SETTINGS in the pause menu: one page whose rows the port fills (fe/pause_ui.cpp).
#pragma once

namespace pauseui {

constexpr int kRows = 6;
constexpr int kPageId = 200; // the pause page in the rows report (fe/pages.cpp emit_rows_report)
// row i on the screen now: 0 none, 1 link (opens a screen), 2 choice, 3 slider, 4 key (input/menu_mouse.cpp)
int row_kind(int i);
// the page's texts: row i's caption fevar::kLabel + i, value fevar::kValue + i (beside the caption), number
// fevar::kNumber + i (after the bullet bar), the title fevar::kTitle (fe/vars.h)

void open(int bar_frames);              // PCOpen(frames of the bullet bar): the page was prepared, at its top
void row(int row, int id, bool moved);  // PCRow(row, FEInputId 1-5, MovedCursor) from a row's list object
void back();                            // PCBack: Back below the top screen
void refresh();                         // texts and the rows' state into the page's values

} // namespace pauseui
