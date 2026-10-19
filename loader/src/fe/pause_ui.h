// PC SETTINGS in the pause menu: one page whose rows the port fills (fe/pause_ui.cpp).
#pragma once

namespace pauseui {

constexpr int kRows = 6;
// the page's texts: row i's caption fevar::kLabel + i, value fevar::kValue + i (beside the caption), number
// fevar::kNumber + i (after the bullet bar), the title fevar::kTitle (fe/vars.h)

void open(int bar_frames);              // PCOpen(frames of the bullet bar): the page was prepared, at its top
void row(int row, int id, bool moved);  // PCRow(row, FEInputId 1-5, MovedCursor) from a row's list object
void back();                            // PCBack: Back below the top screen
void refresh();                         // texts and the rows' state into the page's values

} // namespace pauseui
