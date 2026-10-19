#include "hooks.h"

#include "image.h"
#include "xbe.h"
#include "xthread.h"

namespace {

bool in_image(uint32_t addr)
{
    const Xbe *x = image_xbe();
    return x && addr >= x->base && addr < x->base + x->size_of_image;
}

// "module.dll+0x1234" for an address in a loaded module (blackrt.map from the build gives port functions).
std::string module_offset(uint32_t addr)
{
    HMODULE m = nullptr;
    wchar_t path[MAX_PATH];
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)(uintptr_t)addr, &m) ||
        !GetModuleFileNameW(m, path, MAX_PATH))
        return "";
    std::wstring w = path;
    char b[160];
    snprintf(b, sizeof b, "%s+0x%x", narrow(w.substr(w.find_last_of(L"\\/") + 1)).c_str(), addr - (uint32_t)(uintptr_t)m);
    return b;
}

std::string where(uint32_t addr)
{
    if (!in_image(addr)) {
        std::string mo = module_offset(addr);
        if (!mo.empty()) return mo;
    }
    std::string s = sym_name(addr);
    if (const Xbe *x = image_xbe())
        if (const XbeSection *sec = x->section_containing(addr)) s += " [" + sec->name + "]";
    return s;
}


bool safe_read(const uint32_t *p, uint32_t *out)
{
    __try {
        *out = *p;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

// Return addresses on the stack: values inside the image preceded by a call instruction.
void log_stack_trace(const uint32_t *esp)
{
    // Stay inside the current thread's stack (reading past its top faults).
    log_stack_trace_ex(esp, (const uint32_t *)((NT_TIB *)NtCurrentTeb())->StackBase);
}

void log_stack_trace_ex(const uint32_t *esp, const uint32_t *top)
{
    const Xbe *x = image_xbe();
    if (!x) return;
    static uint32_t rt_lo, rt_hi; // blackrt.dll: port frames are listed too
    if (!rt_lo) {
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&log_stack_trace_ex, &m);
        auto *nt = (const IMAGE_NT_HEADERS *)((const uint8_t *)m + ((const IMAGE_DOS_HEADER *)m)->e_lfanew);
        rt_lo = (uint32_t)(uintptr_t)m;
        rt_hi = rt_lo + nt->OptionalHeader.SizeOfImage;
    }
    int found = 0;
    for (int i = 0; i < 512 && found < 16 && esp + i < top; i++) {
        uint32_t v;
        if (!safe_read(esp + i, &v)) break;
        if (!(in_image(v) && v >= x->base + 8) && !(v >= rt_lo + 8 && v < rt_hi)) continue;
        const uint8_t *p = (const uint8_t *)(uintptr_t)v;
        bool call = p[-5] == 0xE8 || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) || (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) ||
                    (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10);
        if (!call) continue;
        logf("    ret [esp+0x%03x] %08x %s", i * 4, v, where(v).c_str());
        found++;
    }
}

namespace {

const char *exception_name(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "PRIVILEGED_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case EXCEPTION_BREAKPOINT: return "BREAKPOINT";
    case EXCEPTION_SINGLE_STEP: return "SINGLE_STEP";
    case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
    default: return "exception";
    }
}

void dump_exception(EXCEPTION_POINTERS *ep, const char *kind)
{
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *c = ep->ContextRecord;
    XThread *t = xthread_current_or_null();
    logf("%s %s (0x%08lx) at %08lx %s, thread '%s'", kind, exception_name(er->ExceptionCode), er->ExceptionCode, c->Eip,
         where(c->Eip).c_str(), t ? t->name : "host");
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        uint32_t op = (uint32_t)er->ExceptionInformation[0];
        const char *opname = op == 0 ? "read" : op == 1 ? "write" : op == 8 ? "execute" : "?";
        uint32_t addr = (uint32_t)er->ExceptionInformation[1];
        const char *note = "";
        if (addr >= 0xFD000000 && addr < 0xFE000000) note = "  (NV2A GPU registers)";
        else if (addr >= 0xFE800000 && addr < 0xFE880000) note = "  (MCPX APU registers)";
        else if (addr >= 0xFEC00000 && addr < 0xFEC01000) note = "  (ACI audio registers)";
        else if (addr < 0x10000) note = "  (null pointer)";
        logf("    %s of address %08x%s", opname, addr, note);
    }
    logf("    eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx", c->Eax, c->Ebx, c->Ecx, c->Edx,
         c->Esi, c->Edi, c->Ebp, c->Esp);
    if (in_image(c->Eip)) {
        const uint8_t *p = (const uint8_t *)(uintptr_t)c->Eip;
        logf("    code: %02x %02x %02x %02x %02x %02x %02x %02x", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    }
    log_stack_trace((const uint32_t *)(uintptr_t)c->Esp);
}

uint8_t g_probe_orig[16];
thread_local int t_probe_rearm = -1;
std::atomic<uint32_t> g_probe_hits[16];

int probe_index(uint32_t addr)
{
    for (int i = 0; i < g_cfg.probe_count; i++)
        if (g_cfg.probe[i] == addr) return i;
    return -1;
}

LONG CALLBACK vectored_handler(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_BREAKPOINT && g_cfg.probe_count) {
        CONTEXT *c = ep->ContextRecord;
        int i = probe_index((uint32_t)(uintptr_t)ep->ExceptionRecord->ExceptionAddress);
        if (i >= 0) {
            const uint32_t *s = (const uint32_t *)(uintptr_t)c->Esp;
            uint32_t n = ++g_probe_hits[i];
            if (n <= 50) {
                XThread *t = xthread_current_or_null();
                float xmm0 = 0.0f; // FXSAVE image: XMM0 at byte 160
                if ((c->ContextFlags & CONTEXT_EXTENDED_REGISTERS) == CONTEXT_EXTENDED_REGISTERS) memcpy(&xmm0, c->ExtendedRegisters + 160, 4);
                logf("probe %08x %s #%u, thread '%s': eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx xmm0=%g | ret %08x args "
                     "%08x %08x %08x %08x %08x",
                     g_cfg.probe[i], where(g_cfg.probe[i]).c_str(), n, t ? t->name : "host", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi,
                     c->Edi, xmm0, s[0], s[1], s[2], s[3], s[4], s[5]);
            }
            *(uint8_t *)(uintptr_t)g_cfg.probe[i] = g_probe_orig[i]; // run the original instruction once
            c->Eip = g_cfg.probe[i];
            c->EFlags |= 0x100; // trap after it to re-arm
            t_probe_rearm = i;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (code == EXCEPTION_SINGLE_STEP && t_probe_rearm >= 0) {
        *(uint8_t *)(uintptr_t)g_cfg.probe[t_probe_rearm] = 0xCC;
        t_probe_rearm = -1;
        if (!(ep->ContextRecord->Dr6 & 0xF)) return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && g_pad_watch && (ep->ContextRecord->Dr6 & 8)) {
        CONTEXT *c = ep->ContextRecord;
        c->Dr6 &= ~8u;
        uint32_t eip = c->Eip;
        static uint32_t seen[64], counts[64], total;
        if (eip >= 0x10000 && eip < 0x587a40 && total < 400) { // game code only (the HLE writes the buffer too)
            int k = 0;
            while (k < 64 && seen[k] && seen[k] != eip) k++;
            if (k < 64) {
                seen[k] = eip;
                if (counts[k]++ < 3) {
                    total++;
                    logf("padwatch: %08x = %d / %g accessed by instruction before %08x %s | eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx "
                         "esi=%08lx edi=%08lx ebp=%08lx",
                         g_pad_watch, *(int16_t *)(uintptr_t)g_pad_watch, *(float *)(uintptr_t)g_pad_watch, eip, where(eip).c_str(),
                         c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp);
                    log_stack_trace((const uint32_t *)(uintptr_t)c->Esp);
                }
            }
        }
        if (!(c->Dr6 & 0xF)) return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && g_cfg.watch_count && (ep->ContextRecord->Dr6 & 0xF)) {
        CONTEXT *c = ep->ContextRecord;
        for (int i = 0; i < 4; i++) {
            if (!(c->Dr6 & (1u << i)) || i >= g_cfg.watch_count) continue;
            uint32_t a = g_cfg.watch[i];
            XThread *t = xthread_current_or_null();
            logf("watch: write to %08x = %08x by instruction before %08lx %s, thread '%s'", a, *(uint32_t *)(uintptr_t)a, c->Eip,
                 where(c->Eip).c_str(), t ? t->name : "host");
            log_stack_trace((const uint32_t *)(uintptr_t)c->Esp);
        }
        c->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    switch (code) {
    case 0x40010006: // OutputDebugString
    case 0x4001000A:
    case 0x406D1388: // thread naming
    case 0xE06D7363: // C++ exception
    case EXCEPTION_BREAKPOINT: // int3 hooks are handled by their own handler
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code < 0x80000000) return EXCEPTION_CONTINUE_SEARCH;
    static std::atomic<int> logged{0};
    if (logged++ < 20) dump_exception(ep, "first-chance");
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS *ep)
{
    dump_exception(ep, "UNHANDLED");
    logc("exiting with code 1 (unhandled %s at %08lx %s; registers and stack in the log)",
         exception_name(ep->ExceptionRecord->ExceptionCode), ep->ContextRecord->Eip, where(ep->ContextRecord->Eip).c_str());
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 1); // the crashing thread may be the one that would do the exit
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void dump_threads()
{
    std::vector<XThread *> threads = xthread_all();
    for (XThread *t : threads) {
        if (!t->host_handle || t->host_tid == GetCurrentThreadId()) continue;
        if (SuspendThread(t->host_handle) == (DWORD)-1) continue;
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(t->host_handle, &c)) {
            logf("thread '%s' (tid %lu): eip %08lx %s, esp %08lx", t->name, t->host_tid, c.Eip, where(c.Eip).c_str(), c.Esp);
            log_stack_trace_ex((const uint32_t *)(uintptr_t)c.Esp, (const uint32_t *)(uintptr_t)(c.Esp + 0x4000));
        }
        ResumeThread(t->host_handle);
    }
}

void install_probes()
{
    for (int i = 0; i < g_cfg.probe_count; i++) {
        uint8_t *p = (uint8_t *)(uintptr_t)g_cfg.probe[i];
        g_probe_orig[i] = *p;
        *p = 0xCC;
        logf("probe armed at %08x %s", g_cfg.probe[i], where(g_cfg.probe[i]).c_str());
    }
}

void install_crash_handlers()
{
    AddVectoredExceptionHandler(1, vectored_handler);
    SetUnhandledExceptionFilter(unhandled_filter);
}
