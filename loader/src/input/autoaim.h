// Auto-aim switch: the game's pull of the view toward the nearest enemy ahead (patch in the player update).
#pragma once

void install_autoaim_patch();
void autoaim_set(bool on); // live; also sets g_cfg.autoaim
