// See bigbuf.h. The init function at 0x1F49F0 allocates the arena (MmAllocateContiguousMemoryEx(0x2A4D800)) and
// stores region pointers into a struct with `lea ebp,[eax+OFFSET]` / `add ebp,SIZE`. Two regions are 0xE1800 bytes:
//   A = arena+0x2623800 (0x1F4BF0 and 0x1F4C0E lea, size imm at 0x1F4C04)
//   B = arena+0x2705000 (0x1F4C2C and 0x1F4C4A lea, size imm at 0x1F4C40)
// Later regions sit at fixed arena offsets, so they cannot grow in place. Instead the four `lea` become
// `mov ebp,imm32 ; nop` pointing at a separate contiguous block, and the two sizes are raised.
#include "bigbuf.h"

#include "common.h"
#include "kernel/kexports.h"
#include "kernel/xboxkrnl.h"

#include <cstring>

namespace {

struct Lea {
    uint32_t addr;
    uint32_t old_off;
    bool b; // false: buffer A, true: buffer B
};
struct Size {
    uint32_t addr; // address of the imm32 of `add ebp, imm32` (81 C5 imm32)
};

constexpr Lea kLeas[] = {
    {0x001F4BF0, 0x02623800, false},
    {0x001F4C0E, 0x02623800, false},
    {0x001F4C2C, 0x02705000, true},
    {0x001F4C4A, 0x02705000, true},
};
constexpr uint32_t kSizeAt[] = {0x001F4C04, 0x001F4C40};
constexpr uint32_t kOldSize = 0x000E1800;

} // namespace

bool install_bigbuf_patch()
{
    if (g_cfg.buf_mb == 0) return true; // disabled
    const uint32_t buf = g_cfg.buf_mb << 20;

    // Check everything before changing anything.
    for (const Lea &l : kLeas) {
        const uint8_t *c = (const uint8_t *)(uintptr_t)l.addr;
        uint32_t imm;
        memcpy(&imm, c + 2, 4);
        if (c[0] != 0x8D || c[1] != 0xA8 || imm != l.old_off) {
            logf("bigbuf: lea at 0x%08x does not match this build; not patched", l.addr);
            return false;
        }
    }
    for (uint32_t at : kSizeAt) {
        const uint8_t *c = (const uint8_t *)(uintptr_t)at;
        uint32_t imm;
        memcpy(&imm, c, 4);
        if (c[-2] != 0x81 || c[-1] != 0xC5 || imm != kOldSize) {
            logf("bigbuf: add at 0x%08x does not match this build; not patched", at);
            return false;
        }
    }

    // Top of RAM, just under the 64 KB of GPU instance memory, so D3D's allocations keep their addresses.
    const uint32_t total = 2 * buf;
    const uint32_t lowest = CONTIGUOUS_SIZE - 0x10000 - total;
    uint8_t *block = (uint8_t *)xk::MmAllocateContiguousMemoryEx(total, lowest, 0xFFFFFFFF, 0x1000, 4);
    if (!block) {
        logf("bigbuf: no contiguous memory for 2 x %u MB; buffers left at the original size", g_cfg.buf_mb);
        return false;
    }
    const uint32_t base[2] = {(uint32_t)(uintptr_t)block, (uint32_t)(uintptr_t)block + buf};

    for (const Lea &l : kLeas) { // mov ebp, imm32 ; nop
        uint8_t *c = (uint8_t *)(uintptr_t)l.addr;
        DWORD old;
        VirtualProtect(c, 6, PAGE_EXECUTE_READWRITE, &old);
        c[0] = 0xBD;
        memcpy(c + 1, &base[l.b ? 1 : 0], 4);
        c[5] = 0x90;
    }
    for (uint32_t at : kSizeAt) {
        DWORD old;
        VirtualProtect((void *)(uintptr_t)at, 4, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)at, &buf, 4);
    }
    logf("bigbuf: weapon buffers A=0x%08x B=0x%08x, %u MB each (was 0x%x)", base[0], base[1], g_cfg.buf_mb, kOldSize);
    return true;
}
