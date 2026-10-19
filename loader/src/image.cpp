#include "image.h"

#include "kernel/xboxkrnl.h"
#include "xbe.h"

#include <cstddef>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

const Xbe *g_xbe;
std::mutex g_section_mutex;

constexpr uint32_t PAGE = 0x1000;

} // namespace

const Xbe *image_xbe() { return g_xbe; }

bool image_map(const Xbe &xbe)
{
    g_xbe = &xbe;
    // The exe must be loaded at the XBE base and be at least as large as the XBE image.
    auto *exe = (uint8_t *)GetModuleHandleW(nullptr);
    if ((uint32_t)(uintptr_t)exe != xbe.base) {
        logf("image: black.exe is at %p, the XBE needs base 0x%08x (link the exe with /BASE:0x%x /FIXED)", exe, xbe.base, xbe.base);
        return false;
    }
    auto *dos = (IMAGE_DOS_HEADER *)exe;
    auto *nt = (IMAGE_NT_HEADERS32 *)(exe + dos->e_lfanew);
    uint32_t exe_end = xbe.base + nt->OptionalHeader.SizeOfImage;
    uint32_t xbe_end = (xbe.base + xbe.size_of_image + PAGE - 1) & ~(PAGE - 1);
    if (xbe_end > exe_end) {
        logf("image: black.exe covers 0x%08x-0x%08x but the XBE needs up to 0x%08x (enlarge the stub's reserve)", xbe.base, exe_end,
             xbe_end);
        return false;
    }

    // Windows still reads the main module's PE header after startup (CreateThread takes default stack sizes
    // from it, via PEB->ImageBaseAddress = 0x10000). The XBE header must sit at 0x10000 too, so splice a
    // minimal PE header into it: "MZ" over the first two magic bytes, e_lfanew (offset 0x3C, inside the
    // XBE's RSA signature, which only the boot ROM checks) pointing at a copy of the exe's NT headers placed
    // in the unused gap between the end of the XBE headers and the first section.
    uint32_t nt_size = (uint32_t)(offsetof(IMAGE_NT_HEADERS32, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader +
                                  nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER));
    std::vector<uint8_t> nt_copy((uint8_t *)nt, (uint8_t *)nt + nt_size);
    uint32_t first_section = 0xFFFFFFFF;
    for (const auto &s : xbe.sections) first_section = s.vaddr < first_section ? s.vaddr : first_section;
    uint32_t nt_off = (xbe.size_of_headers + 0xFF) & ~0xFFu;
    if (xbe.base + nt_off + nt_size > first_section) {
        logf("image: no room for the PE header between the XBE headers and the first section");
        return false;
    }

    // From here on the exe's own headers and code are gone: only blackrt.dll code may run.
    DWORD old;
    if (!VirtualProtect(exe, xbe_end - xbe.base, PAGE_EXECUTE_READWRITE, &old)) {
        logf("image: VirtualProtect(0x%08x, 0x%x) failed (%lu)", xbe.base, xbe_end - xbe.base, GetLastError());
        return false;
    }
    memset(exe, 0, xbe_end - xbe.base);
    memcpy(exe, xbe.data.data(), xbe.size_of_headers);

    auto *new_nt = (IMAGE_NT_HEADERS32 *)(exe + nt_off);
    memcpy(new_nt, nt_copy.data(), nt_size);
    memset(new_nt->OptionalHeader.DataDirectory, 0, sizeof new_nt->OptionalHeader.DataDirectory); // nothing to walk
    exe[0] = 'M';
    exe[1] = 'Z';
    *(uint32_t *)(exe + offsetof(IMAGE_DOS_HEADER, e_lfanew)) = nt_off;

    int loaded = 0;
    for (const auto &s : xbe.sections) {
        auto *hdr = (XbeSectionHeader *)(uintptr_t)s.header_va;
        if (s.flags & XBE_SEC_PRELOAD) {
            if (s.raw_addr + s.raw_size > xbe.data.size()) {
                logf("image: section %s raw data outside the file", s.name.c_str());
                return false;
            }
            memcpy((void *)(uintptr_t)s.vaddr, xbe.data.data() + s.raw_addr, s.raw_size);
            hdr->reference_count = 1;
            loaded++;
        } else {
            hdr->reference_count = 0;
        }
    }
    logf("image: XBE mapped at 0x%08x-0x%08x, %d of %zu sections preloaded", xbe.base, xbe.base + xbe.size_of_image, loaded,
         xbe.sections.size());
    return true;
}

NTSTATUS image_load_section(XbeSectionHeader *section)
{
    std::lock_guard<std::mutex> g(g_section_mutex);
    const XbeSection *s = nullptr;
    for (const auto &x : g_xbe->sections)
        if (x.header_va == (uint32_t)(uintptr_t)section) s = &x;
    if (!s) {
        logf("[k] XeLoadSection(%p): not a section header of this XBE", section);
        return XSTATUS_INVALID_PARAMETER;
    }
    if (section->reference_count++ == 0) {
        memcpy((void *)(uintptr_t)s->vaddr, g_xbe->data.data() + s->raw_addr, s->raw_size);
        if (s->vsize > s->raw_size) memset((void *)(uintptr_t)(s->vaddr + s->raw_size), 0, s->vsize - s->raw_size);
    }
    logf("[k] XeLoadSection('%s' at 0x%08x, 0x%x bytes) -> refcount %u", s->name.c_str(), s->vaddr, s->vsize, section->reference_count);
    return XSTATUS_SUCCESS;
}

NTSTATUS image_unload_section(XbeSectionHeader *section)
{
    std::lock_guard<std::mutex> g(g_section_mutex);
    if (section->reference_count) section->reference_count--;
    // Memory is left in place: on the Xbox the pages would be freed, here nothing needs them back.
    logf("[k] XeUnloadSection(%p) -> refcount %u", section, section->reference_count);
    return XSTATUS_SUCCESS;
}
