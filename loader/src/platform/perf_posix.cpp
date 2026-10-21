// POSIX Performance Profiling Stubs for Native Linux Build
#include "perf.h"

namespace perf {

volatile bool g_on = false;

void toggle() {}
bool recording() { return false; }
void swap_begin() {}
void pace_end(uint32_t, uint32_t) {}
void frame_end(uint32_t, uint32_t) {}

void add_draw(uint32_t, uint32_t) {}
void add_texture_upload(uint32_t) {}
void add_compile() {}
void add_rt_copy() {}

const char *last_report_text() { return ""; }
const wchar_t *last_report_dir() { return L""; }

Scope::Scope(Cat c) : cat(c), on(false), start(0), parent(nullptr) {}
Scope::~Scope() {}
void Scope::pause(int64_t) {}

} // namespace perf
