#pragma once

#include "common.h"

class Xbe;

// Fill the XBE's kernel thunk table (already mapped) with HLE implementations or logging stubs.
bool kernel_resolve_thunks(const Xbe &xbe);
const char *kernel_export_name(uint32_t ordinal);
