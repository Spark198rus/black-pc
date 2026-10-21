// Reload blur switch: the full-screen blur the game fades in while the first-person reload animation plays.
#pragma once

void install_reload_blur_patch();
void reload_blur_set(bool on); // live; also sets g_cfg.reload_blur
