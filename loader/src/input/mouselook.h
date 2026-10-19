// Mouse look: the mouse rotates the player directly (patches in the game's player update).
#pragma once

#include <cstdint>

void install_mouselook_patches();
bool mouselook_installed();
// The frame (g_frame_count) of the last player update, 0 before the first: menus, pauses and cutscenes do not
// run it (input/bindings.cpp tells play from menus by it).
uint32_t mouselook_play_frame();
// The mouse angle for the next player update, in degrees (yaw + = left, pitch + = the game's pitch rate
// direction). Called once per pad poll; replaces what the previous frame did not use.
void mouselook_set_frame(float yaw_deg, float pitch_deg);
