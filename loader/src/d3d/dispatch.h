// Hooks for the XBE's D3D section. Every function in the section is patched at its entry: functions with
// an HLE replacement jump to a generated thunk, the rest to a logger that stops the run on first call.
#pragma once

#include "common.h"

namespace xd3d {

// One replacement. `params` lists where each parameter of the __stdcall implementation comes from, in
// order: "s" = next stack argument of the original function, or a register name (eax ebx ecx edx esi edi)
// for LTCG register arguments. The thunk preserves ecx/edx (and eax when !returns), because LTCG callers
// may keep values in registers the original function did not touch.
struct Hle {
    uint32_t addr;
    const char *name;
    void *impl;
    const char *params;
    bool returns;
};

// Implemented in the d3d/*.cpp files; each returns its table. An entry with impl == nullptr means
// "leave the original code in place" (pure functions that only read fields the HLE keeps valid).
const Hle *hle_device_table(size_t *count);
const Hle *hle_resource_table(size_t *count);
const Hle *hle_draw_table(size_t *count);
const Hle *hle_dsound_table(size_t *count); // audio/dsound.cpp
const Hle *hle_input_table(size_t *count);  // input/xinput.cpp

bool install_d3d_hooks();

} // namespace xd3d

// Trace helper for D3D calls (first call always, every call with --trace-d3d).
extern bool g_trace_d3d;
#define DTRACE(fmt, ...)                                                                               \
    do {                                                                                               \
        static std::atomic<bool> s_seen_{false};                                                       \
        if (g_trace_d3d || !s_seen_.exchange(true))                                                    \
            logf("[d3d] %s(" fmt ")%s", __func__, ##__VA_ARGS__, g_trace_d3d ? "" : "  <first call>"); \
    } while (0)
