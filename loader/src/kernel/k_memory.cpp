// Memory: contiguous ("physical") memory, pool, virtual memory.
//
// The Xbox maps all 64 MB of RAM at 0x80000000 and code converts between virtual and physical with
// "| 0x80000000" / "& 0x7FFFFFFF". The loader commits a 64 MB block at exactly 0x80000000 (the exe is
// large-address-aware, so 32-bit code on 64-bit Windows has 4 GB) and hands out contiguous memory from it;
// physical address = virtual - 0x80000000.
#include "kernel/kexports.h"
#include "kernel/nt.h"

#include <map>
#include <mutex>

namespace {

constexpr uint32_t PAGE = 0x1000;
constexpr uint32_t CONTIG_LOW_RESERVED = 0x00100000;  // keep physical 0 - 1 MB unused (kernel area on a real Xbox)
constexpr uint32_t GPU_INSTANCE_SIZE = 0x00010000;    // top 64 KB: NV2A instance memory

std::mutex g_contig_mutex;
uint8_t *g_contig_base;                     // == 0x80000000 when the fixed reservation worked
std::map<uint32_t, uint32_t> g_contig_free;  // offset -> size
std::map<uint32_t, uint32_t> g_contig_used;  // offset -> size

HANDLE g_pool_heap;
constexpr uint32_t POOL_MAGIC = 0x4C4F4F50; // 'POOL'
struct PoolHeader {
    uint32_t magic, size, tag, pad;
};

uint32_t strip_cache_bits(uint32_t protect) { return protect & ~(0x200u | 0x400u); } // PAGE_NOCACHE, PAGE_WRITECOMBINE

} // namespace

bool kmem_init()
{
    g_pool_heap = HeapCreate(0, 0, 0);
    g_contig_base = (uint8_t *)VirtualAlloc((void *)(uintptr_t)CONTIGUOUS_BASE, CONTIGUOUS_SIZE, MEM_RESERVE | MEM_COMMIT,
                                            PAGE_EXECUTE_READWRITE);
    if (!g_contig_base) {
        logf("memory: could not reserve contiguous memory at 0x%08x (error %lu); using any address", CONTIGUOUS_BASE,
             GetLastError());
        g_contig_base = (uint8_t *)VirtualAlloc(nullptr, CONTIGUOUS_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!g_contig_base) return false;
    }
    g_contig_free[CONTIG_LOW_RESERVED] = CONTIGUOUS_SIZE - CONTIG_LOW_RESERVED - GPU_INSTANCE_SIZE;
    g_contig_used[CONTIGUOUS_SIZE - GPU_INSTANCE_SIZE] = GPU_INSTANCE_SIZE;
    logf("memory: contiguous RAM %p-%p (%u MB)", g_contig_base, g_contig_base + CONTIGUOUS_SIZE, CONTIGUOUS_SIZE >> 20);
    return true;
}

namespace xk {

// --- pool ---

void *__stdcall ExAllocatePoolWithTag(uint32_t NumberOfBytes, uint32_t Tag)
{
    KTRACE("%u, '%.4s'", NumberOfBytes, (const char *)&Tag);
    // 16-byte aligned user block with the header right before it; pad holds the raw HeapAlloc pointer.
    uint8_t *raw = (uint8_t *)HeapAlloc(g_pool_heap, HEAP_ZERO_MEMORY, NumberOfBytes + sizeof(PoolHeader) + 16);
    if (!raw) return nullptr;
    uint8_t *user = (uint8_t *)(((uintptr_t)raw + sizeof(PoolHeader) + 15) & ~(uintptr_t)15);
    PoolHeader *h = (PoolHeader *)user - 1;
    h->magic = POOL_MAGIC;
    h->size = NumberOfBytes;
    h->tag = Tag;
    h->pad = (uint32_t)(uintptr_t)raw;
    return user;
}

void __stdcall ExFreePool(void *P)
{
    KTRACE("%p", P);
    if (!P) return;
    PoolHeader *h = (PoolHeader *)P - 1;
    if (h->magic != POOL_MAGIC) {
        logf("[k] ExFreePool(%p): not a pool block", P);
        return;
    }
    h->magic = 0;
    HeapFree(g_pool_heap, 0, (void *)(uintptr_t)h->pad);
}

uint32_t __stdcall ExQueryPoolBlockSize(void *PoolBlock)
{
    KTRACE("%p", PoolBlock);
    PoolHeader *h = (PoolHeader *)PoolBlock - 1;
    return h->magic == POOL_MAGIC ? h->size : 0;
}

// --- contiguous memory ---

void *__stdcall MmAllocateContiguousMemoryEx(uint32_t NumberOfBytes, uint32_t LowestAcceptableAddress,
                                             uint32_t HighestAcceptableAddress, uint32_t Alignment, uint32_t ProtectionType)
{
    uint32_t size = (NumberOfBytes + PAGE - 1) & ~(PAGE - 1);
    if (!size) size = PAGE;
    uint32_t align = Alignment < PAGE ? PAGE : Alignment;
    if (align & (align - 1)) { // round up to a power of two
        uint32_t a = PAGE;
        while (a < align) a <<= 1;
        align = a;
    }
    void *result = nullptr;
    {
        std::lock_guard<std::mutex> g(g_contig_mutex);
        for (auto it = g_contig_free.begin(); it != g_contig_free.end(); ++it) {
            uint32_t b = it->first, s = it->second;
            uint32_t start = (b + align - 1) & ~(align - 1);
            if (start < LowestAcceptableAddress) start = (LowestAcceptableAddress + align - 1) & ~(align - 1);
            if (start + size > b + s) continue;
            if ((uint64_t)start + size - 1 > HighestAcceptableAddress) continue;
            g_contig_free.erase(it);
            if (start > b) g_contig_free[b] = start - b;
            if (start + size < b + s) g_contig_free[start + size] = b + s - (start + size);
            g_contig_used[start] = size;
            result = g_contig_base + start;
            break;
        }
    }
    if (result) memset(result, 0, size);
    KTRACE("0x%x, 0x%x, 0x%x, 0x%x, 0x%x) -> %p (", NumberOfBytes, LowestAcceptableAddress, HighestAcceptableAddress, Alignment,
           ProtectionType, result);
    if (!result) logf("[k] MmAllocateContiguousMemoryEx: out of contiguous memory for 0x%x bytes", NumberOfBytes);
    return result;
}

void *__stdcall MmAllocateContiguousMemory(uint32_t NumberOfBytes)
{
    KTRACE("0x%x", NumberOfBytes);
    return MmAllocateContiguousMemoryEx(NumberOfBytes, 0, 0xFFFFFFFF, 0, PAGE_READWRITE);
}

void __stdcall MmFreeContiguousMemory(void *BaseAddress)
{
    KTRACE("%p", BaseAddress);
    uint32_t off = (uint32_t)((uint8_t *)BaseAddress - g_contig_base);
    std::lock_guard<std::mutex> g(g_contig_mutex);
    auto it = g_contig_used.find(off);
    if (it == g_contig_used.end()) {
        logf("[k] MmFreeContiguousMemory(%p): not an allocation", BaseAddress);
        return;
    }
    uint32_t size = it->second;
    g_contig_used.erase(it);
    auto next = g_contig_free.lower_bound(off);
    if (next != g_contig_free.end() && next->first == off + size) {
        size += next->second;
        next = g_contig_free.erase(next);
    }
    if (next != g_contig_free.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == off) {
            prev->second += size;
            return;
        }
    }
    g_contig_free[off] = size;
}

void *__stdcall MmClaimGpuInstanceMemory(uint32_t NumberOfBytes, uint32_t *NumberOfPaddingBytes)
{
    KTRACE("0x%x, %p", NumberOfBytes, NumberOfPaddingBytes);
    if (NumberOfPaddingBytes) *NumberOfPaddingBytes = 0;
    // Returns the end of the instance area; the GPU driver uses the memory below it.
    return g_contig_base + CONTIGUOUS_SIZE;
}

uint32_t __stdcall MmGetPhysicalAddress(void *BaseAddress)
{
    KTRACE("%p", BaseAddress);
    uint8_t *p = (uint8_t *)BaseAddress;
    if (p >= g_contig_base && p < g_contig_base + CONTIGUOUS_SIZE + 1) return (uint32_t)(p - g_contig_base);
    KNOTE_ONCE("MmGetPhysicalAddress(%p) on non-contiguous memory; returning address & 0x7FFFFFFF", BaseAddress);
    return (uint32_t)(uintptr_t)p & 0x7FFFFFFF;
}

void __stdcall MmLockUnlockBufferPages(void *BaseAddress, uint32_t NumberOfBytes, BOOLEAN UnlockPages)
{
    KTRACE("%p, 0x%x, %u", BaseAddress, NumberOfBytes, UnlockPages);
}

void __stdcall MmLockUnlockPhysicalPage(uint32_t PhysicalAddress, BOOLEAN UnlockPage)
{
    KTRACE("0x%x, %u", PhysicalAddress, UnlockPage);
}

void __stdcall MmPersistContiguousMemory(void *BaseAddress, uint32_t NumberOfBytes, BOOLEAN Persist)
{
    KTRACE("%p, 0x%x, %u", BaseAddress, NumberOfBytes, Persist);
}

uint32_t __stdcall MmQueryAddressProtect(void *VirtualAddress)
{
    KTRACE("%p", VirtualAddress);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(VirtualAddress, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    return mbi.Protect;
}

uint32_t __stdcall MmQueryAllocationSize(void *BaseAddress)
{
    KTRACE("%p", BaseAddress);
    uint8_t *p = (uint8_t *)BaseAddress;
    if (p >= g_contig_base && p < g_contig_base + CONTIGUOUS_SIZE) {
        std::lock_guard<std::mutex> g(g_contig_mutex);
        auto it = g_contig_used.find((uint32_t)(p - g_contig_base));
        return it == g_contig_used.end() ? 0 : it->second;
    }
    // Sum committed regions of the allocation that starts at BaseAddress.
    uint32_t total = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    for (uint8_t *q = p; VirtualQuery(q, &mbi, sizeof mbi) && mbi.AllocationBase == BaseAddress; q += mbi.RegionSize)
        if (mbi.State == MEM_COMMIT) total += (uint32_t)mbi.RegionSize;
    return total;
}

void __stdcall MmSetAddressProtect(void *BaseAddress, uint32_t NumberOfBytes, uint32_t NewProtect)
{
    KTRACE("%p, 0x%x, 0x%x", BaseAddress, NumberOfBytes, NewProtect);
    DWORD old;
    if (!VirtualProtect(BaseAddress, NumberOfBytes, strip_cache_bits(NewProtect), &old))
        logf("[k] MmSetAddressProtect: VirtualProtect failed (%lu)", GetLastError());
}

// --- virtual memory ---

NTSTATUS __stdcall NtAllocateVirtualMemory(void **BaseAddress, uint32_t ZeroBits, uint32_t *RegionSize, uint32_t AllocationType,
                                           uint32_t Protect)
{
    void *req = BaseAddress ? *BaseAddress : nullptr;
    uint32_t req_size = RegionSize ? *RegionSize : 0;
    // Keep MEM_COMMIT/RESERVE/RESET. Drop MEM_TOP_DOWN (would give addresses above 2 GB, which Xbox
    // code reads as physical/kernel addresses) and the Xbox-only MEM_NOZERO (0x800000).
    uint32_t type = AllocationType & (MEM_COMMIT | MEM_RESERVE | MEM_RESET);
    SIZE_T size = req_size;
    PVOID base = req;
    NTSTATUS s = host::NtAllocateVirtualMemory(GetCurrentProcess(), &base, ZeroBits, &size, type, strip_cache_bits(Protect));
    if (NT_OK(s)) {
        *BaseAddress = base;
        *RegionSize = (uint32_t)size;
        if ((uintptr_t)base >= 0x80000000u) logf("[k] warning: NtAllocateVirtualMemory returned %p above 2 GB", base);
    }
    KTRACE("*%p, %u, 0x%x, 0x%x, 0x%x) -> 0x%08x, %p+0x%x (", req, ZeroBits, req_size, AllocationType, Protect, s,
           NT_OK(s) ? base : nullptr, (uint32_t)size);
    return s;
}

NTSTATUS __stdcall NtFreeVirtualMemory(void **BaseAddress, uint32_t *RegionSize, uint32_t FreeType)
{
    KTRACE("*%p, 0x%x, 0x%x", BaseAddress ? *BaseAddress : nullptr, RegionSize ? *RegionSize : 0, FreeType);
    SIZE_T size = *RegionSize;
    PVOID base = *BaseAddress;
    NTSTATUS s = host::NtFreeVirtualMemory(GetCurrentProcess(), &base, &size, FreeType);
    if (NT_OK(s)) {
        *BaseAddress = base;
        *RegionSize = (uint32_t)size;
    }
    return s;
}

NTSTATUS __stdcall NtQueryVirtualMemory(void *BaseAddress, MEMORY_BASIC_INFORMATION *Buffer)
{
    KTRACE("%p, %p", BaseAddress, Buffer);
    if (!VirtualQuery(BaseAddress, Buffer, sizeof *Buffer)) return XSTATUS_INVALID_PARAMETER;
    return XSTATUS_SUCCESS;
}

} // namespace xk
