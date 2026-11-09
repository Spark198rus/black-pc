// Sprint: the player runs faster while the Sprint key is held (PC SETTINGS > CONTROLS > SPRINT).
#pragma once

#include <cstdint>

void install_sprint_patch();
void sprint_input(bool held);           // each pad poll (input/bindings.cpp): the Sprint key, in play only
void sprint_frame(uint8_t *ctrl);       // each player update (input/mouselook.cpp), before the movement and the weapon
