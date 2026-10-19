// Field of view in play: the game's horizontal FOV (70 degrees) replaced by the port's setting.
#pragma once

void install_fov_patch(); // after the XBE is mapped; applies g_cfg.fov
void fov_set(float degrees); // live (PC SETTINGS > VIDEO > FIELD OF VIEW)
// Clip-space x/y factor for the draw being made: > 1 for the first-person gun and hands at a wider FOV (they keep
// the game's own projection), 1 otherwise.
float fov_viewmodel_scale();
