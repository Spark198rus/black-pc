// The KEY BINDINGS page's list: the actions and their two keys, six rows at a time (fe/bind_list.cpp).
#pragma once

#include <string>

namespace bindlist {

constexpr int kRows = 6; // rows on the page (captions and keys in the page's values, fe/vars.h)

void open();                            // the page was prepared: back to the top
void input(int row, int id, bool moved); // PCBind(row, FEInputId, MovedCursor) from a row's list object
void refresh();                         // the rows' texts into their variables
std::wstring row_label(int row);        // what row (0-5) shows: caption and keys (the pause page shows them itself)
std::wstring row_keys(int row);

} // namespace bindlist
