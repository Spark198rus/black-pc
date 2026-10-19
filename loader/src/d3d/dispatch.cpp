#include "d3d/dispatch.h"

#include "d3d/draw.h"
#include "hooks.h"
#include "image.h"
#include "xbe.h"
#include "xthread.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

bool g_trace_d3d = false;

namespace {

struct LibFunc {
    uint32_t addr, size;
    const char *name;
};
// Every function in the XBE sections whose library is replaced (D3D, DSOUND): each one is either
// replaced by HLE or stops the run on first call, so library code never touches GPU/APU hardware.
const LibFunc kD3D[] = {
#include "d3d_functions.inc"
};
const LibFunc kDSound[] = {
#include "dsound_functions.inc"
};
const LibFunc kXpp[] = {
#include "xpp_functions.inc"
};

std::vector<LibFunc> g_funcs; // kD3D + kDSound, sorted by address
std::vector<bool> g_int3_hook;
std::atomic<bool> g_unimplemented_hit{false};

uint8_t *g_code;
size_t g_code_used;

uint8_t *code_alloc(size_t n)
{
    if (!g_code) g_code = (uint8_t *)VirtualAlloc(nullptr, 0x80000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    uint8_t *p = g_code + g_code_used;
    g_code_used += (n + 15) & ~15u;
    return p;
}

void write_jmp(uint8_t *from, const void *to)
{
    from[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)to - (uintptr_t)(from + 5));
    memcpy(from + 1, &rel, 4);
}

} // namespace

// Unimplemented library function reached: log it with the arguments and end the run (functions are
// implemented one at a time as the game reaches them).
extern "C" void __stdcall blackrt_lib_unimplemented(uint32_t index, const uint32_t *frame, uint32_t eax, uint32_t ecx,
                                                    uint32_t edx)
{
    if (g_unimplemented_hit.exchange(true)) Sleep(INFINITE);
    const LibFunc &f = g_funcs[index];
    XThread *t = xthread_current_or_null();
    const char *lib = f.addr >= 0x27cf20 ? "XPP" : f.addr >= 0x268880 ? "D3D" : "DSOUND";
    logf("==== UNIMPLEMENTED %s: %s (0x%08x), thread '%s'", lib, f.name, f.addr, t ? t->name : "host");
    logf("    called from %08x %s", frame[0], sym_name(frame[0]).c_str());
    logf("    eax=%08x ecx=%08x edx=%08x  stack args: %08x %08x %08x %08x %08x %08x", eax, ecx, edx, frame[1], frame[2], frame[3],
         frame[4], frame[5], frame[6]);
    log_stack_trace(frame);
    xd3d::screenshot_now("stop");
    request_exit(0, "reached an unimplemented library function");
    Sleep(INFINITE);
}

namespace {

__declspec(naked) void unimplemented_common()
{
    __asm {
        push ebp
        mov ebp, esp
        push edx
        push ecx
        push eax
        lea eax, [ebp+8]
        push eax
        push dword ptr [ebp+4]
        call blackrt_lib_unimplemented
        int 3
    }
}

int reg_code(const std::string &r)
{
    static const char *names[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    for (int i = 0; i < 8; i++)
        if (r == names[i]) return i;
    return -1;
}

// Generate the thunk described in dispatch.h. Returns nullptr on a bad spec.
void *make_thunk(const xd3d::Hle &h)
{
    std::vector<std::string> params;
    {
        std::string p = h.params, tok;
        for (char c : p + " ") {
            if (c == ' ' || c == ',') {
                if (!tok.empty()) params.push_back(tok);
                tok.clear();
            } else {
                tok += c;
            }
        }
    }
    std::vector<int> stack_index(params.size(), -1);
    int stack_args = 0;
    for (size_t i = 0; i < params.size(); i++) {
        if (params[i] == "s") stack_index[i] = stack_args++;
        else if (reg_code(params[i]) < 0) return nullptr;
    }
    uint8_t buf[256];
    int n = 0;
    int saved = 0;
    buf[n++] = 0x51; // push ecx
    buf[n++] = 0x52; // push edx
    saved = 2;
    if (!h.returns) {
        buf[n++] = 0x50; // push eax
        saved = 3;
    }
    int pushed = 0;
    for (int i = (int)params.size() - 1; i >= 0; i--) {
        if (stack_index[i] >= 0) {
            int off = 4 + 4 * stack_index[i] + 4 * (saved + pushed);
            if (off < 128) {
                buf[n++] = 0xFF; buf[n++] = 0x74; buf[n++] = 0x24; buf[n++] = (uint8_t)off; // push [esp+off8]
            } else {
                buf[n++] = 0xFF; buf[n++] = 0xB4; buf[n++] = 0x24;
                memcpy(buf + n, &off, 4);
                n += 4;
            }
        } else {
            buf[n++] = (uint8_t)(0x50 + reg_code(params[i])); // push reg
        }
        pushed++;
    }
    buf[n++] = 0xB8; // mov eax, impl
    uint32_t impl = (uint32_t)(uintptr_t)h.impl;
    memcpy(buf + n, &impl, 4);
    n += 4;
    buf[n++] = 0xFF; buf[n++] = 0xD0; // call eax (impl is __stdcall: pops its parameters)
    if (!h.returns) buf[n++] = 0x58; // pop eax
    buf[n++] = 0x5A; // pop edx
    buf[n++] = 0x59; // pop ecx
    if (stack_args) {
        buf[n++] = 0xC2; // ret imm16
        uint16_t r = (uint16_t)(stack_args * 4);
        memcpy(buf + n, &r, 2);
        n += 2;
    } else {
        buf[n++] = 0xC3;
    }
    uint8_t *p = code_alloc(n);
    memcpy(p, buf, n);
    return p;
}

LONG CALLBACK int3_handler(EXCEPTION_POINTERS *ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t a = (uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress;
    for (size_t i = 0; i < g_funcs.size(); i++) {
        if (g_int3_hook[i] && g_funcs[i].addr == a) {
            CONTEXT *c = ep->ContextRecord;
            blackrt_lib_unimplemented((uint32_t)i, (const uint32_t *)(uintptr_t)c->Esp, c->Eax, c->Ecx, c->Edx);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

namespace xd3d {

bool install_d3d_hooks()
{
    g_funcs.assign(std::begin(kD3D), std::end(kD3D));
    g_funcs.insert(g_funcs.end(), std::begin(kDSound), std::end(kDSound));
    g_funcs.insert(g_funcs.end(), std::begin(kXpp), std::end(kXpp));
    std::sort(g_funcs.begin(), g_funcs.end(), [](const LibFunc &a, const LibFunc &b) { return a.addr < b.addr; });
    g_int3_hook.assign(g_funcs.size(), false);

    std::vector<Hle> all;
    for (auto table : {hle_device_table, hle_resource_table, hle_draw_table, hle_dsound_table, hle_input_table}) {
        size_t n = 0;
        const Hle *t = table(&n);
        all.insert(all.end(), t, t + n);
    }
    // C++ static initializers of the libraries (run by _cinit before main) only set up globals: keep them.
    // Initializer tables of this build: 0x2964F0-0x296B6C (C++) and 0x296B70-0x296B84 (C).
    std::vector<uint32_t> initializers;
    for (uint32_t p = 0x2964F0; p < 0x296B84; p += 4) {
        uint32_t fn = *(uint32_t *)(uintptr_t)p;
        if (fn && fn != 0xFFFFFFFF) initializers.push_back(fn);
    }
    int replaced = 0, stopped = 0, traps = 0, native = 0;
    for (size_t i = 0; i < g_funcs.size(); i++) {
        const LibFunc &f = g_funcs[i];
        uint8_t *fn = (uint8_t *)(uintptr_t)f.addr;
        const Hle *h = nullptr;
        for (const Hle &r : all)
            if (r.addr == f.addr) h = &r;
        if (!h && std::find(initializers.begin(), initializers.end(), f.addr) != initializers.end()) {
            native++;
            continue;
        }
        uint32_t room = f.size;
        if (i + 1 < g_funcs.size() && g_funcs[i + 1].addr - f.addr < room) room = g_funcs[i + 1].addr - f.addr;
        if (h && !h->impl) {
            native++;
            continue;
        }
        void *target;
        if (h) {
            target = make_thunk(*h);
            if (!target) {
                logf("hooks: bad parameter spec '%s' for %s", h->params, h->name);
                return false;
            }
            replaced++;
        } else {
            uint8_t *t = code_alloc(10);
            uint32_t idx = (uint32_t)i;
            t[0] = 0x68; // push index
            memcpy(t + 1, &idx, 4);
            write_jmp(t + 5, (const void *)&unimplemented_common);
            target = t;
            stopped++;
        }
        if (room >= 5) {
            write_jmp(fn, target);
        } else if (!h) {
            fn[0] = 0xCC;
            g_int3_hook[i] = true;
            traps++;
        } else {
            logf("hooks: %s is too small (%u bytes) for a jump", f.name, room);
            return false;
        }
    }
    for (const Hle &r : all) {
        bool found = false;
        for (const LibFunc &f : g_funcs) found |= f.addr == r.addr;
        if (!found && r.impl) { // replacement outside the library sections
            write_jmp((uint8_t *)(uintptr_t)r.addr, make_thunk(r));
            replaced++;
        }
    }
    AddVectoredExceptionHandler(1, int3_handler);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    logf("hooks: D3D+DSOUND: %d functions replaced by HLE, %d run natively, %d stop on first call (%d via int3)", replaced, native,
         stopped, traps);
    return true;
}

} // namespace xd3d
