// Mouse button icons in the game's fonts (fe/fonts.cpp), for the on-screen prompts (input/prompts.cpp).
#pragma once

#include <cstdint>
#include <string>

// Writes copies of Language\Fonts\Small.bin and Big.bin with the mouse icons added under <out_root> (served in place
// of the game's files, like the rebuilt menus). True if both fonts have the icons.
bool fe_build_fonts(const std::wstring &game_dir, const std::wstring &out_root);
// The character that draws the icon for a key (VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, bindings::KEY_WHEEL_UP / DOWN),
// or 0: no icon for that key, or the fonts were not built.
wchar_t fe_mouse_icon(uint16_t key);
