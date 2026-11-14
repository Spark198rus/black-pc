// On-screen button prompts: the bound keyboard key or mouse button while those are in use, the pad button otherwise.
#pragma once

void install_prompts_patch();
// Once per pad poll (read_pad, input/xinput.cpp) with the device pressed at that poll: 1 the keyboard or mouse,
// 0 a controller, -1 none (the last one stays). Also fills the menus' control legend once the game's text is loaded.
void prompts_poll(int device);
