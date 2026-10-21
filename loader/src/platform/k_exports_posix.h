// POSIX Xbox Kernel Exports Resolver and Dispatcher Header
#pragma once

#include "loader/src/platform/windefs.h"
#include <cstdint>

namespace xposix {

void exports_init();
bool kposix_resolve_thunks(uint32_t *thunk_table);
const char *kposix_export_name(uint32_t ordinal);

// Global statistics / state
uint32_t kposix_get_call_count(uint32_t ordinal);
void     kposix_set_trace(bool enable);

} // namespace xposix
