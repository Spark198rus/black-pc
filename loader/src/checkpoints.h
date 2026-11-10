// Mid-mission saves: the last checkpoint is kept in a file; CONTINUE LAST GAME starts its mission there (checkpoints.cpp).
#pragma once

void install_checkpoints();
void checkpoints_continue();              // PCContinue (CONTINUE LAST GAME on the main menu)
void checkpoints_page(const char *page);  // each front-end page change (input/bindings.cpp): a completed mission drops it
void checkpoints_drop(const char *why);   // the kept checkpoint goes (RESTART MISSION, fe/calls.cpp)
