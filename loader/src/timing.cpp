// The Xbox time-stamp counter. XAPI's QueryPerformanceCounter is a bare `rdtsc` and its
// QueryPerformanceFrequency a constant 733,333,333 Hz (the Xbox CPU clock); the game's own timer helpers
// make the same assumption. On a PC `rdtsc` ticks at the host clock (3-5 GHz), so everything the game
// times in seconds (fades, blur ramps) ran several times too fast. These patches return a 733.33 MHz
// counter derived from the host performance counter instead. The game's calibrated timer (FUN_00180c00,
// which measures raw `rdtsc` against QueryPerformanceCounter) then calibrates itself correctly.
#include "timing.h"

#include "common.h"

#include <cmath>
#include <cstring>

namespace {

constexpr uint64_t XBOX_TSC_HZ = 733333333;

// The Xbox never sleeps with a game running, but the PC does: after a sleep (or a debugger break) the host counter
// has moved on by the whole pause, and the game saw one frame that long. So the counter the game reads moves by the
// host time between two readings, but by a second at most; the game reads it many times a frame.
uint64_t xbox_tsc()
{
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    static SRWLOCK lock = SRWLOCK_INIT;
    static int64_t last_host, game; // host counts
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&lock);
    if (!last_host) game = last_host = now.QuadPart;
    int64_t step = now.QuadPart - last_host;
    if (step > 0) {
        game += step < freq.QuadPart ? step : freq.QuadPart;
        last_host = now.QuadPart;
    }
    uint64_t t = (uint64_t)game;
    ReleaseSRWLockExclusive(&lock);
    if (step > 2 * freq.QuadPart)
        logf("timing: the clock jumped %.1f s (the PC slept?); the game sees 1 s", (double)step / freq.QuadPart);
    uint64_t f = (uint64_t)freq.QuadPart;
    return t / f * XBOX_TSC_HZ + t % f * XBOX_TSC_HZ / f;
}

// XAPI QueryPerformanceCounter (0x232da), stdcall
BOOL __stdcall xapi_QueryPerformanceCounter(LARGE_INTEGER *p)
{
    p->QuadPart = (LONGLONG)xbox_tsc();
    return TRUE;
}

void __cdecl store_tsc() { *(uint64_t *)(uintptr_t)0x002C0468 = xbox_tsc(); }
uint32_t __cdecl tsc_low() { return (uint32_t)xbox_tsc(); }

#if defined(_MSC_VER)
__declspec(naked) void store_tsc_thunk() // FUN_00084190: *(uint64 *)0x2c0468 = rdtsc
{
    __asm {
        push ecx
        call store_tsc
        pop ecx
        ret
    }
}

__declspec(naked) void tsc_low_thunk() // FUN_000841a0: return (uint32)rdtsc
{
    __asm {
        push ecx
        call tsc_low
        pop ecx
        ret
    }
}
#else
__attribute__((naked)) void store_tsc_thunk()
{
    __asm__ volatile (
        "push %ecx\n\t"
        "call store_tsc\n\t"
        "pop %ecx\n\t"
        "ret\n\t"
    );
}

__attribute__((naked)) void tsc_low_thunk()
{
    __asm__ volatile (
        "push %ecx\n\t"
        "call tsc_low\n\t"
        "pop %ecx\n\t"
        "ret\n\t"
    );
}
#endif

struct Patch {
    uint32_t addr;
    uint8_t expect[6];
    size_t expect_len;
    const void *target;
    const char *what;
};

bool patch(const Patch &p)
{
    auto *code = (uint8_t *)(uintptr_t)p.addr;
    if (memcmp(code, p.expect, p.expect_len) != 0) {
        logf("timing: %s at 0x%08x does not match this build; not patched", p.what, p.addr);
        return false;
    }
    code[0] = 0xE9;
    int32_t rel = (int32_t)((uintptr_t)p.target - (p.addr + 5));
    memcpy(code + 1, &rel, 4);
    return true;
}

} // namespace

namespace {
float g_frame_60 = 1.0f / 60.0f;
float g_frame_30 = 1.0f / 30.0f;

// A disp32/imm32 operand at `at` inside the instruction at `insn` (whose first bytes must match).
struct Operand {
    uint32_t insn;
    uint8_t expect[6];
    uint32_t at, old_value;
};
} // namespace

// Weapon refire at 60 fps. FUN_001EEFD0 (weapon state machine, ebp = weapon user) fires when its timer
// [ebp+0xDC] is <= 0 (FUN_001F0210 subtracts the frame time first) and then sets the timer to the weapon's
// refire interval (e.g. 0.04 s). Checked once per frame, the interval is effectively rounded up to whole
// frames: at 30 fps 0.04 s fires every 2nd frame (66 ms, ~15 shots/s), at 60 fps every 3rd (50 ms, ~20/s).
// The original rate is the 30 fps rounding, so the interval is rounded up to whole 1/30 s steps (minus a
// little, so the 4th 1/60 s frame fires and not the 5th).
extern "C" uint32_t __cdecl refire_interval(uint32_t bits)
{
    float t;
    memcpy(&t, &bits, 4);
    if (t > 0.0f) t = ceilf(t * 30.0f - 1e-3f) / 30.0f - 0.0005f;
    memcpy(&bits, &t, 4);
    return bits;
}

#if defined(_MSC_VER)
#define REFIRE_THUNK(name, reg)                                                                                    \
    __declspec(naked) void name()                                                                                  \
    {                                                                                                              \
        __asm pushad                                                                                               \
        __asm push reg                                                                                             \
        __asm call refire_interval                                                                                 \
        __asm add esp, 4                                                                                           \
        __asm mov dword ptr [ebp + 0xDC], eax                                                                      \
        __asm popad                                                                                                \
        __asm ret                                                                                                  \
    }
#else
#define REFIRE_THUNK(name, reg)                                                                                    \
    __attribute__((naked)) void name()                                                                             \
    {                                                                                                              \
        __asm__ volatile (                                                                                         \
            "pushal\n\t"                                                                                           \
            "push %" #reg "\n\t"                                                                                   \
            "call refire_interval\n\t"                                                                             \
            "add $4, %esp\n\t"                                                                                     \
            "movl %eax, 0xDC(%ebp)\n\t"                                                                            \
            "popal\n\t"                                                                                            \
            "ret\n\t"                                                                                              \
        );                                                                                                         \
    }
#endif
REFIRE_THUNK(refire_edx, edx)
REFIRE_THUNK(refire_ecx, ecx)
REFIRE_THUNK(refire_eax, eax)

bool install_refire_patches()
{
    struct Site {
        uint32_t addr;
        uint8_t expect[6]; // mov [ebp+0DCh], reg
        void (*thunk)();
    } sites[] = {
        {0x001EFC72, {0x89, 0x95, 0xDC, 0x00, 0x00, 0x00}, refire_edx}, // interval from the fire mode (+0xCC)
        {0x001EFC94, {0x89, 0x8D, 0xDC, 0x00, 0x00, 0x00}, refire_ecx}, // interval (+0x20), main fire path
        {0x001EFCA6, {0x89, 0x85, 0xDC, 0x00, 0x00, 0x00}, refire_eax}, // interval (+0x20), first shot
        {0x001EFEF3, {0x89, 0x8D, 0xDC, 0x00, 0x00, 0x00}, refire_ecx}, // interval (+0x20), other fire path
    };
    for (const Site &s : sites)
        if (memcmp((const void *)(uintptr_t)s.addr, s.expect, 6)) {
            logf("timing: weapon refire site 0x%08x does not match this build", s.addr);
            return false;
        }
    for (const Site &s : sites) { // call thunk; nop (the 6-byte store it replaces)
        auto *code = (uint8_t *)(uintptr_t)s.addr;
        code[0] = 0xE8;
        int32_t rel = (int32_t)((uintptr_t)s.thunk - (s.addr + 5));
        memcpy(code + 1, &rel, 4);
        code[5] = 0x90;
    }
    return true;
}

bool install_60fps_patches()
{
    uint32_t frame = (uint32_t)(uintptr_t)&g_frame_60;
    const Operand ops[] = {
        {0x0020C2A4, {0xF3, 0x0F, 0x10, 0x05, 0xC0, 0x5F}, 0x0020C2A8, 0x00295FC0}, // movss xmm0, [1/30] (NTSC frame time)
        {0x0020C2AC, {0xC7, 0x05, 0x00, 0x38, 0x2F, 0x00}, 0x0020C2B2, 0x21},       // mov [0x2F3800], 33 (ms per frame)
        {0x00193EAE, {0xF3, 0x0F, 0x59, 0x05, 0xC0, 0x5F}, 0x00193EB2, 0x00295FC0}, // mulss xmm0, [1/30]
        {0x00212210, {0xF3, 0x0F, 0x10, 0x05, 0xC0, 0x5F}, 0x00212214, 0x00295FC0}, // movss xmm0, [1/30] -> 0x2F57F4
    };
    for (const Operand &o : ops)
        if (memcmp((const void *)(uintptr_t)o.insn, o.expect, 6) || *(uint32_t *)(uintptr_t)o.at != o.old_value) {
            logf("timing: 60 fps patch site 0x%08x does not match this build; staying at 30 fps", o.insn);
            return false;
        }
    for (const Operand &o : ops) *(uint32_t *)(uintptr_t)o.at = o.old_value == 0x21 ? 16u : frame;
    bool refire = install_refire_patches();
    // Reload blur (the post-process controller at *0x2D1990 + 0x830A0, mode 1): its hold time is set at
    // 0x17B314 to reload-animation length [anim+0x1C] (in 30 fps animation frames) * frame time (game
    // +0x2013C, = 1/60 now) * 0.5 - c, so it halved at 60 fps and the blur cleared early. Use 1/30 there.
    static const uint8_t blur_expect[8] = {0xF3, 0x0F, 0x59, 0x82, 0x3C, 0x01, 0x02, 0x00}; // mulss xmm0, [edx+0x2013C]
    auto *blur = (uint8_t *)(uintptr_t)0x0017B314;
    if (!memcmp(blur, blur_expect, 8)) {
        blur[3] = 0x05; // mulss xmm0, [disp32]
        uint32_t a = (uint32_t)(uintptr_t)&g_frame_30;
        memcpy(blur + 4, &a, 4);
    } else {
        logf("timing: reload blur site 0x0017B314 does not match this build");
    }
    logf("timing: world steps 1/60 s per frame (60 fps)%s", refire ? "; weapon refire keeps the 30 fps rhythm" : "");
    return true;
}

void install_timing_patches()
{
    const Patch patches[] = {
        {0x000232DA, {0x8B, 0x4C, 0x24, 0x04, 0x0F, 0x31}, 6, (const void *)&xapi_QueryPerformanceCounter, "QueryPerformanceCounter"},
        {0x00084190, {0x53, 0x0F, 0x31}, 3, (const void *)&store_tsc_thunk, "timer helper (store rdtsc)"},
        {0x000841A0, {0x83, 0xEC, 0x08, 0x53, 0x0F, 0x31}, 6, (const void *)&tsc_low_thunk, "timer helper (rdtsc low)"},
    };
    int n = 0;
    for (const Patch &p : patches) n += patch(p);
    logf("timing: %d of %zu rdtsc users now tick at 733.33 MHz (host QPC based)", n, sizeof patches / sizeof patches[0]);
}
