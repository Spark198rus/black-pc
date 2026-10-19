// Performance recorder: see perf.h. Everything that touches the recording's data runs on the render thread
// (start, per-frame bookkeeping, stop); the sampler thread only fills preallocated samples; the report is
// built and written by a worker thread after the data has been handed over, so stopping does not stall
// the game.
#include "perf.h"

#include "common.h"
#include "d3d/host.h"
#include "xthread.h"
#include "xbe.h"

#include <d3d9.h>
#include <dbghelp.h>
#include <intrin.h>
#include <shellapi.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace perf {

volatile bool g_on;

namespace {

int64_t qpc()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double tick_ms()
{
    static double ms = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / (double)f.QuadPart;
    }();
    return ms;
}

float ms(int64_t ticks) { return (float)(ticks * tick_ms()); }

// ---- live counters (any thread) ----
std::atomic<int64_t> g_cat_ticks[kCatCount];
std::atomic<uint32_t> g_draws, g_verts, g_vbytes, g_tex_uploads, g_tex_bytes, g_compiles, g_rt_copies;
thread_local Scope *t_top;

// ---- recording (render thread) ----
struct Frame {
    float t;              // ms since the start, at the end of the frame
    float work;           // ms from the previous Present to this Swap (game code + port + driver)
    float pace;           // ms waiting for the vblank
    float present;        // ms from the vblank to the end of Present (gamma/AA passes, Present)
    float cat[kCatCount]; // ms per category (exclusive)
    float others_kcyc;    // CPU of the other game threads during the frame, in 1000s of cycles
    uint32_t draws, verts, vbytes, tex_uploads, tex_bytes, compiles, rt_copies;
    uint16_t vblanks, interval, rw, rh;
};

enum Phase : uint8_t { kWork, kPace, kPresent };

struct Sample {
    uint32_t eip;
    uint32_t chain[6]; // return addresses found on the stack, innermost first (heuristic)
    uint32_t frame;    // index of the frame being built
    uint8_t n, phase;
    uint8_t running;   // the thread used at least half of the time since the previous sample
};

constexpr size_t kMaxFrames = 60 * 60 * 15; // 15 minutes at 60 fps
constexpr size_t kMaxSamples = 1000 * 60 * 15;

struct ThreadCpu {
    DWORD tid;
    HANDLE h;
    uint64_t c0, c1;
    std::string name;
    bool game, render;
    uint32_t affinity; // CPU mask at the end of the recording (0 = unknown)
};

struct Recording {
    std::vector<Frame> frames;
    std::unique_ptr<Sample[]> samples; // kMaxSamples, not zero-filled (starting must not stall)
    size_t nsamples = 0;
    std::vector<ThreadCpu> threads;
    double seconds;
    double tsc_hz;
    std::string started, cpu, gpu;
    int fps;
    bool aa, widescreen;
    uint32_t game_lo, game_hi, rt_lo, rt_hi;
    std::wstring rt_path;
};

std::atomic<bool> g_toggle_req{false};
std::atomic<int> g_state{0}; // 0 idle, 1 recording, 2 writing the report
volatile uint8_t g_phase;
volatile uint32_t g_frame_index;
int64_t g_t0, g_t_work, g_t_swap, g_t_pace;
uint64_t g_tsc0;
uint16_t g_vblanks, g_interval;
Recording *g_rec;
size_t g_nsamples;
std::atomic<bool> g_sampler_run{false};
std::thread g_sampler;
HANDLE g_render_thread;
DWORD g_render_tid;
uint32_t g_rt_lo, g_rt_hi;
constexpr uint32_t kGameLo = 0x00011000;
std::mutex g_report_lock;
std::string g_report_text;
std::wstring g_report_dir;
std::string g_gpu_name;

// ---- stack heuristics for the sampler ----
bool in_code(uint32_t v)
{
    return (v >= kGameLo + 8 && v < xbe_va(0x00587A40)) || (v >= g_rt_lo + 8 && v < g_rt_hi);
}

bool after_call(uint32_t v)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)v;
    if (p[-5] == 0xE8) return true;                                          // call rel32
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10 && (p[-1] & 0xC0) != 0x40) return true; // call reg / [reg]
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) return true;                // call [reg+d8] / [sib]
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) return true;                // call [abs] / [reg+d32]
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10) return true;                // call [sib+d32]
    return false;
}

void sampler_main()
{
    DWORD_PTR proc = 0, sys = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);
    if (g_game_cpu && (proc & ~g_game_cpu)) SetThreadAffinityMask(GetCurrentThread(), proc & ~g_game_cpu);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    timeBeginPeriod(1);
    uint32_t stack_lo = 0, stack_hi = 0;
    uint64_t last_cyc = 0, last_tsc = 0;
    CONTEXT c;
    while (g_sampler_run.load(std::memory_order_relaxed)) {
        Sleep(1);
        size_t i = g_nsamples;
        if (i >= kMaxSamples) continue;
        if (SuspendThread(g_render_thread) == (DWORD)-1) continue;
        c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(g_render_thread, &c)) {
            Sample &s = g_rec->samples[i];
            s.eip = c.Eip;
            s.phase = g_phase;
            s.frame = g_frame_index;
            s.n = 0;
            uint64_t cyc = 0, tsc = __rdtsc();
            QueryThreadCycleTime(g_render_thread, &cyc);
            s.running = !last_tsc || (cyc - last_cyc) * 2 >= tsc - last_tsc;
            last_cyc = cyc;
            last_tsc = tsc;
            uint32_t sp = c.Esp;
            if (sp < stack_lo || sp >= stack_hi) {
                MEMORY_BASIC_INFORMATION mbi;
                if (VirtualQuery((const void *)(uintptr_t)sp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
                    stack_lo = (uint32_t)(uintptr_t)mbi.BaseAddress;
                    stack_hi = stack_lo + (uint32_t)mbi.RegionSize;
                } else stack_lo = stack_hi = 0;
            }
            uint32_t end = std::min<uint32_t>(stack_hi, sp + 4096);
            for (uint32_t a = sp; a + 4 <= end && s.n < 6; a += 4) {
                uint32_t v = *(const uint32_t *)(uintptr_t)a;
                if (in_code(v) && after_call(v)) s.chain[s.n++] = v;
            }
            g_nsamples = i + 1;
        }
        ResumeThread(g_render_thread);
    }
    timeEndPeriod(1);
}

// ---- threads ----
uint32_t thread_affinity(HANDLE h)
{
    struct Tbi {
        LONG exit_status;
        void *teb;
        void *pid, *tid;
        ULONG_PTR affinity;
        LONG priority, base_priority;
    } tbi = {};
    using Query = LONG(NTAPI *)(HANDLE, int, void *, ULONG, ULONG *);
    static auto query = (Query)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    return query && query(h, 0 /* ThreadBasicInformation */, &tbi, sizeof tbi, nullptr) >= 0 ? (uint32_t)tbi.affinity : 0;
}

std::string thread_name(DWORD tid, bool *game)
{
    *game = false;
    for (XThread *t : xthread_all())
        if (t->host_tid == tid) {
            *game = true;
            return t->name[0] ? t->name : "game thread";
        }
    using GetDesc = HRESULT(WINAPI *)(HANDLE, PWSTR *);
    static auto get_desc = (GetDesc)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
    std::string name;
    if (get_desc)
        if (HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid)) {
            PWSTR d = nullptr;
            if (SUCCEEDED(get_desc(h, &d)) && d) {
                name = narrow(d);
                LocalFree(d);
            }
            CloseHandle(h);
        }
    if (name.empty()) { // name it after the module its start address is in (driver, audio, runtime threads)
        name = "host thread";
        using Query = LONG(NTAPI *)(HANDLE, int, void *, ULONG, ULONG *);
        static auto query = (Query)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
        if (HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid)) {
            void *start = nullptr;
            HMODULE m = nullptr;
            wchar_t path[MAX_PATH];
            if (query && query(h, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof start, nullptr) >= 0 && start &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)start, &m) &&
                GetModuleFileNameW(m, path, MAX_PATH)) {
                std::wstring w = path;
                name += " (" + narrow(w.substr(w.find_last_of(L"\\/") + 1)) + ")";
            }
            CloseHandle(h);
        }
    }
    return name;
}

void snapshot_threads(std::vector<ThreadCpu> &list, bool end)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te = {sizeof te};
    DWORD me = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != me) continue;
        auto it = std::find_if(list.begin(), list.end(), [&](const ThreadCpu &t) { return t.tid == te.th32ThreadID; });
        if (it == list.end()) {
            ThreadCpu t{};
            t.tid = te.th32ThreadID;
            t.h = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, t.tid);
            if (!t.h) t.h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, t.tid);
            if (!t.h) continue;
            t.name = thread_name(t.tid, &t.game);
            t.render = t.tid == g_render_tid;
            if (!end) QueryThreadCycleTime(t.h, &t.c0); // threads created while recording start from 0
            list.push_back(t);
            it = list.end() - 1;
        }
        if (end) {
            QueryThreadCycleTime(it->h, &it->c1);
            it->affinity = thread_affinity(it->h);
        }
    }
    CloseHandle(snap);
    if (end)
        for (ThreadCpu &t : list) {
            if (!t.c1) QueryThreadCycleTime(t.h, &t.c1); // ended threads keep their last value
            CloseHandle(t.h);
            t.h = nullptr;
        }
}

// Per-frame CPU of the other game threads (they share the game CPU with the render thread).
std::vector<std::pair<HANDLE, uint64_t>> g_others;

uint64_t others_delta()
{
    uint64_t sum = 0;
    for (auto &[h, last] : g_others) {
        uint64_t c = 0;
        QueryThreadCycleTime(h, &c);
        sum += c - last;
        last = c;
    }
    return sum;
}

std::string now_text(const char *fmt)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[64];
    snprintf(b, sizeof b, fmt, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

std::string cpu_name()
{
    char buf[128] = {};
    DWORD size = sizeof buf;
    RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", "ProcessorNameString", RRF_RT_REG_SZ,
                 nullptr, buf, &size);
    std::string s = buf;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// ---- report ----
struct Sym {
    std::string key;  // aggregation key (function or module)
    bool game = false, port = false;
};

class Symbolizer {
public:
    explicit Symbolizer(const Recording &r) : r_(r)
    {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
        std::wstring dir = r.rt_path.substr(0, r.rt_path.find_last_of(L"\\/"));
        ok_ = SymInitializeW(GetCurrentProcess(), dir.c_str(), FALSE) != FALSE;
        if (ok_) SymLoadModuleExW(GetCurrentProcess(), nullptr, r.rt_path.c_str(), nullptr, r.rt_lo, r.rt_hi - r.rt_lo, nullptr, 0);
    }
    ~Symbolizer()
    {
        if (ok_) SymCleanup(GetCurrentProcess());
    }

    const Sym &at(uint32_t a)
    {
        auto it = cache_.find(a);
        if (it != cache_.end()) return it->second;
        Sym s;
        if (a >= r_.game_lo && a < r_.game_hi) {
            s.game = true;
            std::string name;
            uint32_t start;
            if (sym_function(a, &name, &start)) s.key = name;
            else {
                char b[32];
                snprintf(b, sizeof b, "game 0x%06x", a & ~0xFFu);
                s.key = b;
            }
        } else if (a >= r_.rt_lo && a < r_.rt_hi) {
            s.port = true;
            s.key = port_name(a);
        } else s.key = module_of(a);
        return cache_[a] = s;
    }

private:
    std::string port_name(uint32_t a)
    {
        if (ok_) {
            alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
            auto *si = (SYMBOL_INFO *)buf;
            si->SizeOfStruct = sizeof(SYMBOL_INFO);
            si->MaxNameLen = 255;
            DWORD64 disp = 0;
            if (SymFromAddr(GetCurrentProcess(), a, &disp, si)) {
                std::string n = si->Name;
                for (const char *junk : {"`anonymous namespace'::", "xd3d::", "host3d::"}) {
                    size_t p;
                    while ((p = n.find(junk)) != std::string::npos) n.erase(p, strlen(junk));
                }
                return "port: " + n;
            }
        }
        char b[48];
        snprintf(b, sizeof b, "port: blackrt+0x%x", a - r_.rt_lo);
        return b;
    }

    std::string module_of(uint32_t a)
    {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((const void *)(uintptr_t)a, &mbi, sizeof mbi) || !mbi.AllocationBase) return "unknown code";
        auto it = modules_.find(mbi.AllocationBase);
        if (it != modules_.end()) return it->second;
        wchar_t path[MAX_PATH] = L"";
        std::string name = "unknown code";
        if (GetModuleFileNameW((HMODULE)mbi.AllocationBase, path, MAX_PATH)) {
            std::wstring p = path;
            name = narrow(p.substr(p.find_last_of(L"\\/") + 1));
        } else if (mbi.Type == MEM_PRIVATE) name = "generated code";
        return modules_[mbi.AllocationBase] = name;
    }

    const Recording &r_;
    bool ok_ = false;
    std::unordered_map<uint32_t, Sym> cache_;
    std::map<void *, std::string> modules_;
};

struct Agg {
    double sum[22] = {};
    int n = 0;
};

// Columns averaged per frame group.
enum Col { cWork, cPresent, cPace, cDraw, cConvert, cTexture, cCompile, cRtCopy, cRest, cOthers, cDraws, cVerts, cVKB, cTexUp, cTexKB,
           cCompiles, cRtCopies, cCpu, cColCount };

void add_frame(Agg &a, const Frame &f, double kcyc_ms)
{
    double cats = 0;
    for (int c = 0; c < kCatCount; c++) cats += f.cat[c];
    double v[cColCount] = {f.work, f.present, f.pace, f.cat[kDraw], f.cat[kConvert], f.cat[kTexture], f.cat[kCompile], f.cat[kRtCopy],
                           std::max(0.0, f.work + f.present - cats), f.others_kcyc * kcyc_ms, (double)f.draws, (double)f.verts,
                           f.vbytes / 1024.0, (double)f.tex_uploads, f.tex_bytes / 1024.0, (double)f.compiles, (double)f.rt_copies,
                           f.work + f.present};
    for (int c = 0; c < cColCount; c++) a.sum[c] += v[c];
    a.n++;
}

double avg(const Agg &a, int c) { return a.n ? a.sum[c] / a.n : 0.0; }

double percentile(std::vector<float> v, double p)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)std::min<double>(v.size() - 1, p * (v.size() - 1) + 0.5);
    return v[i];
}

void write_report(Recording *r)
{
    // affinity: keep off the game CPU
    DWORD_PTR proc = 0, sys = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);
    if (g_game_cpu && (proc & ~g_game_cpu)) SetThreadAffinityMask(GetCurrentThread(), proc & ~g_game_cpu);

    std::wstring dir = g_cfg.port_dir + L"\\perf";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\" + widen(r->started);
    for (int i = 2; !CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() == ERROR_ALREADY_EXISTS && i < 100; i++) {
        size_t cut = dir.rfind(L"__");
        if (i > 2 && cut != std::wstring::npos) dir.resize(cut); // two recordings in one second (parallel test runs)
        dir += L"__" + std::to_wstring(i);
    }
    const std::vector<Frame> &F = r->frames;
    double kcyc_ms = r->tsc_hz > 0 ? 1e6 / r->tsc_hz : 0; // 1000 cycles -> ms

    // frames.csv
    if (FILE *f = _wfopen((dir + L"\\frames.csv").c_str(), L"w")) {
        fprintf(f, "frame,t_ms,vblanks,late,cpu_ms,work_ms,present_ms,pace_ms,draw_ms,convert_ms,texture_ms,compile_ms,rtcopy_ms,"
                   "other_game_threads_ms,draws,vertices,vertex_kb,texture_uploads,texture_kb,compiles,rt_copies,render_w,render_h\n");
        for (size_t i = 0; i < F.size(); i++) {
            const Frame &x = F[i];
            fprintf(f, "%zu,%.2f,%u,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%.1f,%u,%.1f,%u,%u,%u,%u\n", i, x.t, x.vblanks,
                    x.vblanks > x.interval ? 1 : 0, x.work + x.present, x.work, x.present, x.pace, x.cat[kDraw], x.cat[kConvert],
                    x.cat[kTexture], x.cat[kCompile], x.cat[kRtCopy], x.others_kcyc * kcyc_ms, x.draws, x.verts, x.vbytes / 1024.0,
                    x.tex_uploads, x.tex_bytes / 1024.0, x.compiles, x.rt_copies, x.rw, x.rh);
        }
        fclose(f);
    }

    // frame statistics: groups 0 = all frames, 1 = the slowest 5% (by CPU time), 2 = late frames
    Agg agg[3];
    uint64_t vblanks = 0, intervals = 0;
    std::vector<float> cpu;
    for (const Frame &x : F) cpu.push_back(x.work + x.present);
    double slow_from = percentile(cpu, 0.95);
    std::vector<uint8_t> group(F.size()); // bit 1 = slowest 5%, bit 2 = late
    for (size_t i = 0; i < F.size(); i++) {
        const Frame &x = F[i];
        group[i] = (cpu[i] >= slow_from ? 1 : 0) | (x.vblanks > x.interval ? 2 : 0);
        add_frame(agg[0], x, kcyc_ms);
        if (group[i] & 1) add_frame(agg[1], x, kcyc_ms);
        if (group[i] & 2) add_frame(agg[2], x, kcyc_ms);
        vblanks += x.vblanks;
        intervals += x.interval;
    }
    const Agg &all = agg[0], &late = agg[2];
    double budget = F.empty() ? 16.68 : F[0].interval * 1001.0 / 60.0;

    // samples
    Symbolizer sym(*r);
    size_t ns = r->nsamples;
    enum Cls { kGame, kPort, kDriver, kOther, kBlocked, kClsCount };
    static const char *cls_name[kClsCount] = {"game code (the XBE)", "port code (blackrt: D3D/kernel/audio HLE)",
                                              "Direct3D 9 runtime / GPU driver / Windows (called by the port)",
                                              "other code (no caller found)", "blocked: sleeping or waiting inside the frame"};
    double cls[3][kClsCount] = {};
    std::map<std::string, double> self[3], incl[3];
    double frame_samples[3] = {}, idle = 0;
    auto generic = [](const std::string &k) {
        for (const char *g : {"memcpy", "memset", "memmove", "malloc", "free", "ILT+", "operator new", "std::"})
            if (k.find(g) != std::string::npos) return true;
        return false;
    };
    for (size_t i = 0; i < ns; i++) {
        const Sample &smp = r->samples[i];
        int grp = smp.frame < group.size() ? group[smp.frame] : 0;
        if (smp.phase == kPace) {
            idle++;
            continue;
        }
        const Sym &e = sym.at(smp.eip);
        // the innermost game/port function on the stack, skipping the recorder's own helpers
        const Sym *caller = nullptr;
        for (int k = 0; k < smp.n && !caller; k++) {
            const Sym &cs = sym.at(smp.chain[k]);
            if ((cs.game || cs.port) && cs.key.rfind("port: perf::", 0) != 0 && cs.key != e.key) caller = &cs;
        }
        int c;
        std::string key = e.key;
        if (!smp.running) c = kBlocked;
        else if (e.game) c = kGame;
        else if (e.port) c = kPort;
        else c = caller && caller->port ? kDriver : kOther;
        if (caller && (!(e.game || e.port) || generic(e.key))) key += "  <- " + caller->key;
        if (!smp.running) key = "[blocked] " + key;
        std::set<std::string> seen;
        seen.insert(e.key);
        for (int k = 0; k < smp.n; k++) {
            const std::string &ck = sym.at(smp.chain[k]).key;
            if (ck.rfind("port: perf::", 0) != 0) seen.insert(ck);
        }
        for (int g = 0; g < 3; g++) {
            if (g && !(grp & g)) continue;
            cls[g][c]++;
            frame_samples[g]++;
            self[g][key]++;
            if (smp.running)
                for (const std::string &k : seen) incl[g][k]++;
        }
    }

    std::string out;
    char line[512];
    auto put = [&](const char *fmt, auto... args) {
        snprintf(line, sizeof line, fmt, args...);
        out += line;
    };
    put("BLACK performance recording %s\n", r->started.c_str());
    put("%.1f s, %zu frames, frame rate setting %d fps, anti-aliasing %s, %s\n", r->seconds, F.size(), r->fps, r->aa ? "on" : "off",
        r->widescreen ? "16:9" : "4:3");
    if (!F.empty()) put("render size %ux%u at the start", F.front().rw, F.front().rh);
    if (!F.empty() && (F.back().rw != F.front().rw || F.back().rh != F.front().rh)) put(", %ux%u at the end", F.back().rw, F.back().rh);
    put("\nCPU: %s\nGPU: %s\n\n", r->cpu.c_str(), r->gpu.c_str());
    double speed = vblanks ? 100.0 * intervals / vblanks : 100.0;
    put("Game speed: %.1f%% of real time (%llu frames took %llu vblanks; the game steps a fixed time per frame, so late frames "
        "slow it down)\n",
        speed, (unsigned long long)F.size(), (unsigned long long)vblanks);
    put("Late frames: %d of %zu (%.1f%%) needed more than their %.1f ms\n\n", late.n, F.size(), F.empty() ? 0.0 : 100.0 * late.n / F.size(),
        budget);
    put("Frame time on the render thread (work + present, without the vblank wait), ms:\n");
    put("  average %.2f   median %.2f   90%% %.2f   99%% %.2f   max %.2f   (budget %.2f)\n\n", avg(all, cCpu), percentile(cpu, 0.5),
        percentile(cpu, 0.9), percentile(cpu, 0.99), cpu.empty() ? 0.0 : *std::max_element(cpu.begin(), cpu.end()), budget);
    put("Average frame, ms                        all  slowest5%%     late\n");
    static const struct {
        int col;
        const char *name;
    } rows[] = {{cCpu, "frame time (work + present)"},
                {cWork, "  work (game + port + driver)"},
                {cDraw, "    draw calls (state, D3D9 calls)"},
                {cConvert, "    vertex conversion"},
                {cTexture, "    texture checks/uploads"},
                {cCompile, "    shader compiles"},
                {cRtCopy, "    render target copies"},
                {cRest, "    the rest (game code, other HLE)"},
                {cPresent, "  present (gamma/AA passes, Present)"},
                {cPace, "vblank wait (idle)"},
                {cOthers, "other game threads (same CPU)"},
                {cDraws, "draws"},
                {cVerts, "vertices submitted"},
                {cVKB, "vertex data KB"},
                {cTexUp, "texture uploads"},
                {cTexKB, "texture upload KB"},
                {cCompiles, "shader compiles"},
                {cRtCopies, "render target copies"}};
    for (const auto &row : rows)
        put("  %-36s %8.2f %8.2f %8.2f\n", row.name, avg(agg[0], row.col), avg(agg[1], row.col), avg(agg[2], row.col));

    put("\nWhere the frame time goes (%zu samples of the render thread; %.0f%% of them were in the vblank wait):\n", ns,
        ns ? 100.0 * idle / ns : 0.0);
    put("  %-62s %15s %15s %15s\n", "", "all", "slowest 5%", "late");
    for (int c = 0; c < kClsCount; c++) {
        put("  %-62s", cls_name[c]);
        for (int g = 0; g < 3; g++) {
            double pc = frame_samples[g] ? 100.0 * cls[g][c] / frame_samples[g] : 0;
            put("  %5.1f%% %5.2fms", pc, pc * avg(agg[g], cCpu) / 100);
        }
        put("\n");
    }
    auto top = [&](const char *title, std::map<std::string, double> *m, int count) {
        std::vector<std::pair<double, std::string>> v;
        for (auto &[k, n] : m[0]) v.push_back({n, k});
        std::sort(v.rbegin(), v.rend());
        put("\n%s\n   all  slow5%%   late  function\n", title);
        for (int i = 0; i < (int)v.size() && i < count; i++) {
            put(" ");
            for (int g = 0; g < 3; g++) {
                double n = m[g].count(v[i].second) ? m[g][v[i].second] : 0;
                put(" %5.1f%%", 100.0 * n / std::max(1.0, frame_samples[g]));
            }
            put("  %s\n", v[i].second.c_str());
        }
    };
    top("Hot spots, self time (percent of the frame-time samples; '<-' = the port/game function that called it):", self, 45);
    top("Hot spots, including callees (from return addresses found on the stack, a heuristic; running samples only):", incl, 30);

    // threads
    std::sort(r->threads.begin(), r->threads.end(), [](const ThreadCpu &a, const ThreadCpu &b) { return a.c1 - a.c0 > b.c1 - b.c0; });
    put("\nThreads (CPU time while recording, %% of one CPU; game CPU mask 0x%x; * = may run on the game CPU):\n", (unsigned)g_game_cpu);
    if (FILE *f = _wfopen((dir + L"\\threads.csv").c_str(), L"w")) {
        fprintf(f, "tid,name,game_thread,render,cpu_mask,cpu_ms,percent_of_one_cpu\n");
        for (const ThreadCpu &t : r->threads) {
            double tms = r->tsc_hz > 0 ? (t.c1 - t.c0) * 1000.0 / r->tsc_hz : 0;
            double pc = r->seconds > 0 ? tms / (r->seconds * 10.0) : 0;
            fprintf(f, "%lu,%s,%d,%d,0x%x,%.1f,%.1f\n", t.tid, t.name.c_str(), t.game ? 1 : 0, t.render ? 1 : 0, t.affinity, tms, pc);
            bool shares = g_game_cpu && (t.affinity & g_game_cpu);
            if (pc >= 0.5)
                put("  %5.1f%% %c %-28s tid %-6lu cpus 0x%-5x %s\n", pc, shares ? '*' : ' ', t.name.c_str(), t.tid, t.affinity,
                    t.render ? "(render thread)" : t.game ? "(game thread)" : "");
        }
        fclose(f);
    }
    put("\nFiles: frames.csv (one row per frame), threads.csv, this summary.\n");

    if (FILE *f = _wfopen((dir + L"\\summary.txt").c_str(), L"w")) {
        fputs(out.c_str(), f);
        fclose(f);
    }
    char head[512];
    snprintf(head, sizeof head,
             "%.1f s recorded, %zu frames\nGame speed %.1f%%, late frames %.1f%%\nFrame time: average %.1f ms, 99%% %.1f ms (budget %.1f)\n"
             "game code %.0f%%, port %.0f%%, driver/D3D9 %.0f%%",
             r->seconds, F.size(), speed, F.empty() ? 0.0 : 100.0 * late.n / F.size(), avg(all, cCpu), percentile(cpu, 0.99), budget,
             frame_samples[0] ? 100.0 * cls[0][kGame] / frame_samples[0] : 0, frame_samples[0] ? 100.0 * cls[0][kPort] / frame_samples[0] : 0,
             frame_samples[0] ? 100.0 * cls[0][kDriver] / frame_samples[0] : 0);
    wchar_t full[MAX_PATH];
    GetFullPathNameW(dir.c_str(), MAX_PATH, full, nullptr);
    {
        std::lock_guard<std::mutex> g(g_report_lock);
        g_report_text = head;
        g_report_dir = full;
    }
    logf("[perf] recording saved to %s: %s", narrow(full).c_str(), head);
    delete r;
    g_state = 0;
    if (HWND w = host3d::window()) PostMessageW(w, WM_APP_PERF, 0, 0);
}

void start()
{
    if (!g_render_thread) {
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_render_thread,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        g_render_tid = GetCurrentThreadId();
        HMODULE m = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)&toggle, &m);
        auto *nt = (const IMAGE_NT_HEADERS *)((const uint8_t *)m + ((const IMAGE_DOS_HEADER *)m)->e_lfanew);
        g_rt_lo = (uint32_t)(uintptr_t)m;
        g_rt_hi = g_rt_lo + nt->OptionalHeader.SizeOfImage;
        if (IDirect3DDevice9 *d = host3d::dev()) {
            IDirect3D9 *d3d = nullptr;
            D3DADAPTER_IDENTIFIER9 id = {};
            if (SUCCEEDED(d->GetDirect3D(&d3d)) && d3d) {
                if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) g_gpu_name = id.Description;
                d3d->Release();
            }
        }
    }
    auto *r = new Recording;
    r->frames.reserve(kMaxFrames);
    r->samples.reset(new Sample[kMaxSamples]);
    r->started = now_text("%04u-%02u-%02u_%02u-%02u-%02u");
    r->cpu = cpu_name();
    r->gpu = g_gpu_name;
    r->fps = g_cfg.fps;
    r->aa = g_cfg.aa;
    r->widescreen = g_cfg.widescreen;
    r->game_lo = kGameLo;
    r->game_hi = xbe_va(0x00587A40);
    r->rt_lo = g_rt_lo;
    r->rt_hi = g_rt_hi;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW((HMODULE)(uintptr_t)g_rt_lo, path, MAX_PATH);
    r->rt_path = path;
    snapshot_threads(r->threads, false);
    g_others.clear();
    for (XThread *t : xthread_all())
        if (t->host_handle && t->host_tid != g_render_tid) {
            uint64_t c = 0;
            QueryThreadCycleTime(t->host_handle, &c);
            g_others.push_back({t->host_handle, c});
        }
    for (auto &c : g_cat_ticks) c = 0;
    for (auto *c : {&g_draws, &g_verts, &g_vbytes, &g_tex_uploads, &g_tex_bytes, &g_compiles, &g_rt_copies}) *c = 0;
    g_rec = r;
    g_nsamples = 0;
    g_frame_index = 0;
    g_phase = kWork;
    g_t0 = g_t_work = qpc();
    g_tsc0 = __rdtsc();
    g_sampler_run = true;
    g_sampler = std::thread(sampler_main);
    g_on = true;
    g_state = 1;
    logf("[perf] recording started");
}

void stop()
{
    g_on = false;
    g_sampler_run = false;
    if (g_sampler.joinable()) g_sampler.join();
    Recording *r = g_rec;
    g_rec = nullptr;
    int64_t t1 = qpc();
    uint64_t tsc1 = __rdtsc();
    r->seconds = (t1 - g_t0) * tick_ms() / 1000.0;
    r->tsc_hz = r->seconds > 0 ? (tsc1 - g_tsc0) / r->seconds : 0;
    r->nsamples = g_nsamples;
    snapshot_threads(r->threads, true);
    g_state = 2;
    logf("[perf] recording stopped after %.1f s (%zu frames, %zu samples); writing the report", r->seconds, r->frames.size(),
         r->nsamples);
    std::thread(write_report, r).detach();
}

// --perf-at "S:D": record D seconds starting S seconds after the first frame (scripted runs).
void scripted()
{
    static ULONGLONG first = GetTickCount64();
    static double at = -1, dur = 0;
    static bool parsed, started, done;
    if (!parsed) {
        parsed = true;
        if (!g_cfg.perf_at.empty()) swscanf_s(g_cfg.perf_at.c_str(), L"%lf:%lf", &at, &dur);
    }
    if (at < 0 || done) return;
    double t = (GetTickCount64() - first) / 1000.0;
    if (!started && t >= at && g_state == 0) {
        started = true;
        g_toggle_req = true;
    } else if (started && g_state == 1 && t >= at + dur) {
        done = true;
        g_toggle_req = true;
    }
}

} // namespace

Scope::Scope(Cat c) : cat(c), on(g_on)
{
    if (!on) return;
    int64_t now = qpc();
    parent = t_top;
    if (parent) parent->pause(now);
    start = now;
    t_top = this;
}

Scope::~Scope()
{
    if (!on) return;
    int64_t now = qpc();
    g_cat_ticks[cat] += now - start;
    t_top = parent;
    if (parent) parent->start = now;
}

void Scope::pause(int64_t now) { g_cat_ticks[cat] += now - start; }

void toggle() { g_toggle_req = true; }
bool recording() { return g_state == 1; }

void swap_begin()
{
    if (!g_on) return;
    g_t_swap = qpc();
    g_phase = kPace;
}

void pace_end(uint32_t vblanks, uint32_t interval)
{
    g_vblanks = (uint16_t)std::min<uint32_t>(vblanks, 65535);
    g_interval = (uint16_t)interval;
    if (!g_on) return;
    g_t_pace = qpc();
    g_phase = kPresent;
}

void frame_end(uint32_t render_w, uint32_t render_h)
{
    scripted();
    if (g_on && g_rec) {
        int64_t now = qpc();
        Frame f{};
        f.t = ms(now - g_t0);
        f.work = ms(g_t_swap - g_t_work);
        f.pace = ms(g_t_pace - g_t_swap);
        f.present = ms(now - g_t_pace);
        for (int c = 0; c < kCatCount; c++) f.cat[c] = ms(g_cat_ticks[c].exchange(0));
        f.others_kcyc = others_delta() / 1000.0f;
        f.draws = g_draws.exchange(0);
        f.verts = g_verts.exchange(0);
        f.vbytes = g_vbytes.exchange(0);
        f.tex_uploads = g_tex_uploads.exchange(0);
        f.tex_bytes = g_tex_bytes.exchange(0);
        f.compiles = g_compiles.exchange(0);
        f.rt_copies = g_rt_copies.exchange(0);
        f.vblanks = g_vblanks;
        f.interval = g_interval;
        f.rw = (uint16_t)render_w;
        f.rh = (uint16_t)render_h;
        if (g_rec->frames.size() < kMaxFrames) g_rec->frames.push_back(f);
        else g_toggle_req = true; // full: stop
        g_t_work = now;
        g_frame_index = (uint32_t)g_rec->frames.size();
        g_phase = kWork;
    }
    if (g_toggle_req.exchange(false)) {
        if (g_state == 0) start();
        else if (g_state == 1) stop();
    }
}

void add_draw(uint32_t vertices, uint32_t bytes)
{
    if (!g_on) return;
    g_draws.fetch_add(1, std::memory_order_relaxed);
    g_verts.fetch_add(vertices, std::memory_order_relaxed);
    g_vbytes.fetch_add(bytes, std::memory_order_relaxed);
}

void add_texture_upload(uint32_t bytes)
{
    if (!g_on) return;
    g_tex_uploads++;
    g_tex_bytes += bytes;
}

void add_compile()
{
    if (g_on) g_compiles++;
}

void add_rt_copy()
{
    if (g_on) g_rt_copies++;
}

const char *last_report_text()
{
    static std::string copy;
    std::lock_guard<std::mutex> g(g_report_lock);
    copy = g_report_text;
    return copy.c_str();
}

const wchar_t *last_report_dir()
{
    static std::wstring copy;
    std::lock_guard<std::mutex> g(g_report_lock);
    copy = g_report_dir;
    return copy.c_str();
}

} // namespace perf
