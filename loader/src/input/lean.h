// Tactical lean (experimental): roll the first-person view and move the eye sideways while a lean key is held.
#pragma once

void install_lean_patch();
void lean_input(int dir); // each pad poll: -1 lean left, +1 lean right, 0 upright
