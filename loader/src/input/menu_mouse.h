// Mouse in the menus: the wheel everywhere, hover and click on the port's own pages (input/menu_mouse.cpp).
#pragma once

namespace menu_mouse {

void install(); // registers the values the pages report
// each pad poll (input/bindings.cpp): in_menus = not in play; left / right = the buttons held; key_input = a menu key
// pressed this poll. Returns a menu input (1 up, 2 down, 3 left, 4 right, 5 select, 6 back) or 0.
int poll(bool in_menus, bool left, bool right, int key_input);
void script_pos(float x, float y); // tests: the cursor at x, y (0..1 of the picture); < 0 = the real cursor

} // namespace menu_mouse
