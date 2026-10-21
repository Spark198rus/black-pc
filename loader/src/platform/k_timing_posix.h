// POSIX High-Resolution Timing and 60 FPS Patches for Xbox kernel
#pragma once

#include "loader/src/platform/windefs.h"
#include <cstdint>

namespace xposix {

void timing_init();
uint64_t xbox_tsc();
int64_t ktime_now_100ns();

// Game binary patches
void install_timing_patches();
bool install_60fps_patches();

// Native replacement for XAPI QueryPerformanceCounter
BOOL __stdcall xapi_QueryPerformanceCounter(LARGE_INTEGER *p);

} // namespace xposix
