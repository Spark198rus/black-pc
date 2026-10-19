#include "common.h"
#include "xbe.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

Config g_cfg;
DWORD_PTR g_game_cpu;

static CRITICAL_SECTION g_log_cs;
static FILE *g_log_file;
static LARGE_INTEGER g_t0, g_freq;
static bool g_log_ready;

void log_init(const std::wstring &path)
{
    InitializeCriticalSection(&g_log_cs);
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_t0);
    g_log_file = _wfopen(path.c_str(), L"w");
    g_log_ready = true;
}

namespace {

void log_line(const char *fmt, va_list ap, bool console)
{
    char msg[2048];
    vsnprintf(msg, sizeof msg, fmt, ap);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double t = double(now.QuadPart - g_t0.QuadPart) / double(g_freq.QuadPart);
    char line[2200];
    snprintf(line, sizeof line, "%9.3f [%5lu] %s\n", t, GetCurrentThreadId(), msg);
    if (!g_log_ready) {
        fputs(line, stderr);
        return;
    }
    EnterCriticalSection(&g_log_cs);
    if (console || g_cfg.verbose) {
        fputs(line, stdout);
        fflush(stdout);
    }
    if (g_log_file) {
        fputs(line, g_log_file);
        fflush(g_log_file);
    }
    LeaveCriticalSection(&g_log_cs);
}

} // namespace

void logv(const char *fmt, va_list ap) { log_line(fmt, ap, false); }

void logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_line(fmt, ap, false);
    va_end(ap);
}

void logc(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_line(fmt, ap, true);
    va_end(ap);
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char msg[2048];
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    logc("FATAL: %s", msg);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), 3);
    for (;;) {
    }
}

std::string narrow(const std::wstring &w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string &s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// --- symbols ---------------------------------------------------------------------------------

static std::map<uint32_t, std::pair<std::string, uint32_t>> g_syms; // addr -> (name, size)

void symbols_load(const std::wstring &csv_path)
{
    std::ifstream f(csv_path);
    if (!f) return;
    std::string line;
    std::getline(f, line); // header: address,name,size,section,refs
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string addr, name, size;
        if (!std::getline(ss, addr, ',') || !std::getline(ss, name, ',') || !std::getline(ss, size, ','))
            continue;
        g_syms[xbe_va((uint32_t)strtoul(addr.c_str(), nullptr, 16))] = {name, (uint32_t)strtoul(size.c_str(), nullptr, 10)};
    }
    logf("symbols: %zu functions from %s", g_syms.size(), narrow(csv_path).c_str());
}

bool sym_function(uint32_t addr, std::string *name, uint32_t *start)
{
    auto it = g_syms.upper_bound(addr);
    if (it == g_syms.begin()) return false;
    --it;
    if (addr - it->first >= std::max<uint32_t>(it->second.second, 1) + 16) return false;
    *name = it->second.first;
    *start = it->first;
    return true;
}

std::string sym_name(uint32_t addr)
{
    char buf[160];
    auto it = g_syms.upper_bound(addr);
    if (it != g_syms.begin()) {
        --it;
        uint32_t off = addr - it->first;
        if (off < std::max<uint32_t>(it->second.second, 1) + 16) {
            if (off)
                snprintf(buf, sizeof buf, "%s+0x%x", it->second.first.c_str(), off);
            else
                snprintf(buf, sizeof buf, "%s", it->second.first.c_str());
            return buf;
        }
    }
    snprintf(buf, sizeof buf, "0x%08x", addr);
    return buf;
}
