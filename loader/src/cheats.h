// Cheats (PC SETTINGS > CHEATS): unlock all missions, god mode, infinite ammo, damage to enemies.
#pragma once

void install_cheats();
void cheats_apply(); // live: applies g_cfg.cheat_* (called after each change)
void cheats_frame(); // once per player update (input/mouselook.cpp): keeps the grenades full
