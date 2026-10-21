// POSIX High-Resolution Timing and 60 FPS Patches Implementation
#include "k_timing_posix.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <time.h>

namespace xposix {

namespace {

constexpr uint64_t XBOX_TSC_HZ = 733333333ULL;
float g_frame_60 = 1.0f / 60.0f;
float g_frame_30 = 1.0f / 30.0f;

std::mutex g_timing_mutex;
int64_t g_last_host_ns = 0;
int64_t g_game_ns = 0;

} // namespace

void timing_init()
{
    std::lock_guard<std::mutex> lock(g_timing_mutex);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    g_last_host_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    g_game_ns = g_last_host_ns;
}

uint64_t xbox_tsc()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    int64_t now_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;

    std::lock_guard<std::mutex> lock(g_timing_mutex);
    if (!g_last_host_ns) {
        g_game_ns = g_last_host_ns = now_ns;
    }

    int64_t step = now_ns - g_last_host_ns;
    if (step > 0) {
        // Clamp jumps to 1 second maximum (e.g. system suspension)
        g_game_ns += (step < 1000000000LL) ? step : 1000000000LL;
        g_last_host_ns = now_ns;
    }

    uint64_t t = (uint64_t)g_game_ns;
    return (t / 1000000000ULL) * XBOX_TSC_HZ + ((t % 1000000000ULL) * XBOX_TSC_HZ) / 1000000000ULL;
}

int64_t ktime_now_100ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    // 100ns intervals since January 1, 1601 (UTC)
    return ((int64_t)ts.tv_sec + 11644473600LL) * 10000000LL + (ts.tv_nsec / 100);
}

BOOL __stdcall xapi_QueryPerformanceCounter(LARGE_INTEGER *p)
{
    if (!p) return FALSE;
    p->QuadPart = (LONGLONG)xbox_tsc();
    return TRUE;
}

extern "C" {

void __cdecl store_tsc()
{
    *(uint64_t *)(uintptr_t)0x002C0468 = xbox_tsc();
}

uint32_t __cdecl tsc_low()
{
    return (uint32_t)xbox_tsc();
}

void __attribute__((naked)) store_tsc_thunk()
{
    __asm__ volatile (
        "pushl %%ecx\n\t"
        "call store_tsc\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        : : : "memory"
    );
}

void __attribute__((naked)) tsc_low_thunk()
{
    __asm__ volatile (
        "pushl %%ecx\n\t"
        "call tsc_low\n\t"
        "popl %%ecx\n\t"
        "ret\n\t"
        : : : "memory"
    );
}

uint32_t __cdecl refire_interval(uint32_t bits)
{
    float t;
    memcpy(&t, &bits, 4);
    if (t > 0.0f) t = ceilf(t * 30.0f - 1e-3f) / 30.0f - 0.0005f;
    memcpy(&bits, &t, 4);
    return bits;
}

void __attribute__((naked)) refire_edx()
{
    __asm__ volatile (
        "pushal\n\t"
        "pushl %%edx\n\t"
        "call refire_interval\n\t"
        "addl $4, %%esp\n\t"
        "movl %%eax, 0xDC(%%ebp)\n\t"
        "popal\n\t"
        "ret\n\t"
        : : : "memory"
    );
}

void __attribute__((naked)) refire_ecx()
{
    __asm__ volatile (
        "pushal\n\t"
        "pushl %%ecx\n\t"
        "call refire_interval\n\t"
        "addl $4, %%esp\n\t"
        "movl %%eax, 0xDC(%%ebp)\n\t"
        "popal\n\t"
        "ret\n\t"
        : : : "memory"
    );
}

void __attribute__((naked)) refire_eax()
{
    __asm__ volatile (
        "pushal\n\t"
        "pushl %%eax\n\t"
        "call refire_interval\n\t"
        "addl $4, %%esp\n\t"
        "movl %%eax, 0xDC(%%ebp)\n\t"
        "popal\n\t"
        "ret\n\t"
        : : : "memory"
    );
}

} // extern "C"

namespace {

struct Patch {
    uint32_t addr;
    uint8_t  expect[6];
    size_t   expect_len;
    const void *target;
    const char *what;
};

bool patch(const Patch &p)
{
    auto *code = (uint8_t *)(uintptr_t)p.addr;
    if (memcmp(code, p.expect, p.expect_len) != 0) {
        printf("k_timing_posix: %s at 0x%08X does not match binary; skipping\n", p.what, p.addr);
        return false;
    }
    code[0] = 0xE9; // jmp rel32
    int32_t rel = (int32_t)((uintptr_t)p.target - (p.addr + 5));
    memcpy(code + 1, &rel, 4);
    return true;
}

} // namespace

void install_timing_patches()
{
    const Patch patches[] = {
        {0x000232DA, {0x8B, 0x4C, 0x24, 0x04, 0x0F, 0x31}, 6, (const void *)&xapi_QueryPerformanceCounter, "QueryPerformanceCounter"},
        {0x00084190, {0x53, 0x0F, 0x31, 0x00, 0x00, 0x00}, 3, (const void *)&store_tsc_thunk, "timer helper (store rdtsc)"},
        {0x000841A0, {0x83, 0xEC, 0x08, 0x53, 0x0F, 0x31}, 6, (const void *)&tsc_low_thunk, "timer helper (rdtsc low)"},
    };

    int n = 0;
    for (const auto &p : patches) {
        if (patch(p)) n++;
    }
    printf("  [TIMING] %d de %zu patches de timing (RDTSC calibrado a 733.33 MHz) aplicados com sucesso!\n",
           n, sizeof(patches) / sizeof(patches[0]));
}

bool install_60fps_patches()
{
    struct Operand {
        uint32_t insn;
        uint8_t  expect[6];
        uint32_t at;
        uint32_t old_value;
    };

    uint32_t frame = (uint32_t)(uintptr_t)&g_frame_60;
    const Operand ops[] = {
        {0x0020C2A4, {0xF3, 0x0F, 0x10, 0x05, 0xC0, 0x5F}, 0x0020C2A8, 0x00295FC0}, // movss xmm0, [1/30]
        {0x0020C2AC, {0xC7, 0x05, 0x00, 0x38, 0x2F, 0x00}, 0x0020C2B2, 0x21},       // mov [0x2F3800], 33 (ms)
        {0x00193EAE, {0xF3, 0x0F, 0x59, 0x05, 0xC0, 0x5F}, 0x00193EB2, 0x00295FC0}, // mulss xmm0, [1/30]
        {0x00212210, {0xF3, 0x0F, 0x10, 0x05, 0xC0, 0x5F}, 0x00212214, 0x00295FC0}, // movss xmm0, [1/30]
    };

    for (const auto &o : ops) {
        if (memcmp((const void *)(uintptr_t)o.insn, o.expect, 6) != 0 ||
            *(uint32_t *)(uintptr_t)o.at != o.old_value) {
            printf("k_timing_posix: 60 fps patch at 0x%08X does not match; aborting 60fps\n", o.insn);
            return false;
        }
    }

    for (const auto &o : ops) {
        *(uint32_t *)(uintptr_t)o.at = (o.old_value == 0x21) ? 16u : frame;
    }

    // Refire patches
    struct Site {
        uint32_t addr;
        uint8_t  expect[6];
        void (*thunk)();
    } sites[] = {
        {0x001EFC72, {0x89, 0x95, 0xDC, 0x00, 0x00, 0x00}, refire_edx},
        {0x001EFC94, {0x89, 0x8D, 0xDC, 0x00, 0x00, 0x00}, refire_ecx},
        {0x001EFCA6, {0x89, 0x85, 0xDC, 0x00, 0x00, 0x00}, refire_eax},
        {0x001EFEF3, {0x89, 0x8D, 0xDC, 0x00, 0x00, 0x00}, refire_ecx},
    };

    for (const auto &s : sites) {
        if (memcmp((const void *)(uintptr_t)s.addr, s.expect, 6) != 0) {
            printf("k_timing_posix: weapon refire site 0x%08X mismatch\n", s.addr);
            return false;
        }
    }

    for (const auto &s : sites) {
        auto *code = (uint8_t *)(uintptr_t)s.addr;
        code[0] = 0xE8; // call rel32
        int32_t rel = (int32_t)((uintptr_t)s.thunk - (s.addr + 5));
        memcpy(code + 1, &rel, 4);
        code[5] = 0x90; // nop
    }

    // Reload blur patch
    static const uint8_t blur_expect[8] = {0xF3, 0x0F, 0x59, 0x82, 0x3C, 0x01, 0x02, 0x00};
    auto *blur = (uint8_t *)(uintptr_t)0x0017B314;
    if (memcmp(blur, blur_expect, 8) == 0) {
        blur[3] = 0x05;
        uint32_t a = (uint32_t)(uintptr_t)&g_frame_30;
        memcpy(blur + 4, &a, 4);
    }

    printf("  [TIMING] Patches de 60 FPS e ritmo de disparo aplicados com sucesso!\n");
    return true;
}

} // namespace xposix

void install_timing_patches()
{
    xposix::install_timing_patches();
}

bool install_60fps_patches()
{
    return xposix::install_60fps_patches();
}
