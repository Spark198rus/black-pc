#pragma once

#include "common.h"

class Xbe;

// Map the XBE over black.exe's own image (which owns the XBE's address range). Copies the headers and
// preloaded sections; inserted-file sections are loaded later by XeLoadSection.
bool image_map(const Xbe &xbe);
const Xbe *image_xbe();
