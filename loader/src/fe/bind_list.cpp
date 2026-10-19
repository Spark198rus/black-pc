// The KEY BINDINGS page's list (built by fe/pages.cpp on the main menu's and the pause menu's CONTROLS pages): the
// game's actions with their two keys (input/bindings.cpp) and RESET TO DEFAULTS, six rows at a time.
//
// Each row's list object reports every input as PCBind(row, FEInputId, MyMenu.MovedCursor): 1 up, 2 down, 3 left,
// 4 right, 5 select. BkMenuList moves the focus between the rows itself (the first row has nothing above, the last
// nothing below) and forwards the input to the row that has the focus afterwards, so up on the top row or down on the
// bottom one without a move scrolls the list. Left / right choose the key (shown in brackets), select waits for a
// key for it (bindings::capture_start; Esc cancels, Delete clears).
#include "fe/bind_list.h"

#include "common.h"
#include "fe/vars.h"
#include "input/bindings.h"
#include "settings.h"

#include <algorithm>
#include <cwctype>

namespace bindlist {
namespace {

int g_top, g_focus, g_slot, g_capture = -1; // g_capture: the item waiting for a key

int items()
{
    int n;
    bindings::actions(&n);
    return n + 1; // + RESET TO DEFAULTS
}

std::wstring upper(std::wstring s)
{
    for (wchar_t &c : s) c = (wchar_t)towupper(c);
    return s;
}

std::wstring short_name(uint16_t k)
{
    switch (k) {
    case 0: return L"-";
    case VK_LBUTTON: return L"MOUSE 1";
    case VK_RBUTTON: return L"MOUSE 2";
    case VK_MBUTTON: return L"MOUSE 3";
    case VK_XBUTTON1: return L"MOUSE 4";
    case VK_XBUTTON2: return L"MOUSE 5";
    case bindings::KEY_WHEEL_UP: return L"WHEEL UP";
    case bindings::KEY_WHEEL_DOWN: return L"WHEEL DOWN";
    case VK_LSHIFT: return L"L SHIFT";
    case VK_RSHIFT: return L"R SHIFT";
    case VK_LCONTROL: return L"L CTRL";
    case VK_RCONTROL: return L"R CTRL";
    case VK_LMENU: return L"L ALT";
    case VK_RMENU: return L"R ALT";
    }
    return upper(bindings::key_name(k));
}

std::wstring caption(int item)
{
    int n;
    const bindings::Action *a = bindings::actions(&n);
    if (item >= n) return L"RESET TO DEFAULTS";
    std::wstring name = a[item].name; // "Action (use, pick up)": the part in brackets, which fits beside the keys
    size_t open = name.find(L'('), close = name.find(L')');
    if (open != std::wstring::npos && close > open) name = name.substr(open + 1, close - open - 1);
    return upper(name);
}

std::wstring keys(int item, bool focused)
{
    int n;
    bindings::actions(&n);
    if (item >= n) return L"";
    std::wstring s;
    for (int slot = 0; slot < bindings::kSlots; slot++) {
        std::wstring k = item == g_capture && slot == g_slot && bindings::capturing() ? L"PRESS A KEY" : short_name(bindings::key(item, slot));
        if (focused && slot == g_slot) k = L"[" + k + L"]";
        s += (slot ? L" / " : L"") + k;
    }
    return s;
}

} // namespace

void open()
{
    g_top = g_focus = g_slot = 0;
    g_capture = -1;
    refresh();
}

void input(int row, int id, bool moved)
{
    if (row < 0 || row >= kRows || bindings::capturing()) return;
    g_focus = row;
    int last = std::max(0, items() - kRows);
    if (id == 1 && !moved && row == 0 && g_top > 0) g_top--;
    else if (id == 2 && !moved && row == kRows - 1 && g_top < last) g_top++;
    else if (id == 3) g_slot = 0;
    else if (id == 4) g_slot = 1;
    else if (id == 5) {
        int item = g_top + row, n;
        bindings::actions(&n);
        if (item >= n) {
            bindings::reset_defaults();
            settings_save();
            logf("[input] key bindings: defaults");
        } else if (!bindings::fixed(item, g_slot)) {
            g_capture = item;
            bindings::capture_start(item, g_slot);
        }
    }
    refresh();
}

std::wstring row_label(int row) { return g_top + row < items() ? caption(g_top + row) : L""; }

std::wstring row_keys(int row) { return g_top + row < items() ? keys(g_top + row, row == g_focus) : L""; }

void refresh()
{
    if (bindings::capture_take_change()) g_capture = -1;
    for (int i = 0; i < kRows; i++) {
        fe_var_set(fevar::kLabel + i, row_label(i));
        fe_var_set(fevar::kValue + i, row_keys(i));
    }
}

} // namespace bindlist
