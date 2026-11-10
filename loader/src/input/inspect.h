// Show weapon: the Show weapon key plays the inspection at the end of the first-person idle clip (input/inspect.cpp).
#pragma once

void install_inspect_patch();
void inspect_input(bool held); // each pad poll (input/bindings.cpp): the Show weapon key, in play only
void inspect_frame();          // each player update (input/mouselook.cpp)
