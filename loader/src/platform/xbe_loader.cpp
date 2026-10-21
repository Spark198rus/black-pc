// Cross-platform XBE Loader Implementation for Linux x86
#include "xbe_loader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/mman.h>

namespace {

constexpr uint32_t ENTRY_XOR[3] = {0xA8FC57AB, 0x94859D4B, 0x40B5C16E}; // retail, debug, sega
constexpr uint32_t THUNK_XOR[3] = {0x5B6D40B6, 0xEFB1F152, 0x2290059D};
const char *const KIND_NAMES[3] = {"retail", "debug", "sega"};

inline uint32_t rd32(const std::vector<uint8_t> &d, size_t off)
{
    uint32_t v = 0;
    if (off + 4 <= d.size()) memcpy(&v, &d[off], 4);
    return v;
}

inline uint16_t rd16(const std::vector<uint8_t> &d, size_t off)
{
    uint16_t v = 0;
    if (off + 2 <= d.size()) memcpy(&v, &d[off], 2);
    return v;
}

} // namespace

bool XbeLoader::load(const std::string &path, std::string &err)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) {
        err = "cannot open file: " + path;
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        err = "empty or invalid file: " + path;
        return false;
    }

    data.resize((size_t)sz);
    size_t rd = fread(data.data(), 1, data.size(), f);
    fclose(f);
    if (rd != data.size()) {
        err = "failed to read complete file: " + path;
        return false;
    }

    if (data.size() < 0x180 || memcmp(data.data(), "XBEH", 4) != 0) {
        err = "not an XBE (missing XBEH magic)";
        return false;
    }

    base = rd32(data, 0x104);
    size_of_headers = rd32(data, 0x108);
    size_of_image = rd32(data, 0x10C);
    timedate = rd32(data, 0x114);
    cert_addr = rd32(data, 0x118);
    num_sections = rd32(data, 0x11C);
    sect_addr = rd32(data, 0x120);
    init_flags = rd32(data, 0x124);
    uint32_t raw_entry = rd32(data, 0x128);
    tls_addr = rd32(data, 0x12C);
    stack_commit = rd32(data, 0x130);
    heap_reserve = rd32(data, 0x134);
    heap_commit = rd32(data, 0x138);
    pe_base = rd32(data, 0x13C);
    pe_size = rd32(data, 0x140);
    debug_path_addr = rd32(data, 0x14C);
    uint32_t raw_thunk = rd32(data, 0x158);
    libver_count = rd32(data, 0x160);
    libver_addr = rd32(data, 0x164);

    // Decode entry point and thunk table using retail/debug/sega XOR keys
    kind.clear();
    for (int k = 0; k < 3; k++) {
        uint32_t e = raw_entry ^ ENTRY_XOR[k];
        if (e >= base && e < base + size_of_image) {
            kind = KIND_NAMES[k];
            entry = e;
            thunk_addr = raw_thunk ^ THUNK_XOR[k];
            break;
        }
    }
    if (kind.empty()) {
        err = "entry point does not decode into the image with any known key";
        return false;
    }

    // Parse sections
    sections.clear();
    for (uint32_t i = 0; i < num_sections; i++) {
        size_t o = sect_addr - base + i * sizeof(XbeSectionHeader);
        if (o + sizeof(XbeSectionHeader) > data.size()) {
            err = "section table out of range";
            return false;
        }
        XbeSectionHeader sh;
        memcpy(&sh, &data[o], sizeof sh);
        XbeSection s;
        s.index = i;
        s.flags = sh.flags;
        s.vaddr = sh.virtual_addr;
        s.vsize = sh.virtual_size;
        s.raw_addr = sh.raw_addr;
        s.raw_size = sh.raw_size;
        s.header_va = sect_addr + i * (uint32_t)sizeof(XbeSectionHeader);
        size_t no = sh.name_addr - base;
        while (no < data.size() && data[no] && s.name.size() < 64) {
            s.name += (char)data[no++];
        }
        sections.push_back(s);
    }

    // Parse certificate
    size_t co = cert_addr - base;
    if (co + 0xB0 <= data.size()) {
        cert_timedate = rd32(data, co + 4);
        title_id = rd32(data, co + 8);
        title.clear();
        for (int i = 0; i < 40; i++) {
            uint16_t c = rd16(data, co + 0x0C + i * 2);
            if (!c) break;
            title += (char)(c < 128 ? c : '?');
        }
        allowed_media = rd32(data, co + 0x9C);
        region = rd32(data, co + 0xA0);
        ratings = rd32(data, co + 0xA4);
        disk_number = rd32(data, co + 0xA8);
        version = rd32(data, co + 0xAC);
    }

    // Parse TLS
    has_tls = false;
    if (tls_addr) {
        if (const uint8_t *p = at_va(tls_addr, sizeof(XbeTls))) {
            memcpy(&tls, p, sizeof tls);
            has_tls = true;
        }
    }

    // Parse SDK library versions
    libraries.clear();
    if (const uint8_t *p = at_va(libver_addr, libver_count * 16)) {
        for (uint32_t i = 0; i < libver_count; i++) {
            XbeLibrary l;
            const uint8_t *q = p + i * 16;
            l.name.assign((const char *)q, strnlen((const char *)q, 8));
            memcpy(&l.major, q + 8, 2);
            memcpy(&l.minor, q + 10, 2);
            memcpy(&l.build, q + 12, 2);
            memcpy(&l.flags, q + 14, 2);
            libraries.push_back(l);
        }
    }

    return true;
}

const uint8_t *XbeLoader::at_va(uint32_t va, uint32_t len) const
{
    if (va >= base && va + len <= base + size_of_headers) return &data[va - base];
    for (const auto &s : sections) {
        if (va >= s.vaddr && va + len <= s.vaddr + s.raw_size) {
            size_t o = s.raw_addr + (va - s.vaddr);
            if (o + len <= data.size()) return &data[o];
        }
    }
    return nullptr;
}

uint32_t XbeLoader::u32_at_va(uint32_t va) const
{
    uint32_t v = 0;
    if (const uint8_t *p = at_va(va, 4)) memcpy(&v, p, 4);
    return v;
}

std::vector<uint32_t> XbeLoader::kernel_imports() const
{
    std::vector<uint32_t> ords;
    for (uint32_t va = thunk_addr;; va += 4) {
        const uint8_t *p = at_va(va, 4);
        if (!p) break;
        uint32_t v;
        memcpy(&v, p, 4);
        if (!v) break;
        ords.push_back(v & 0x7FFFFFFF);
    }
    return ords;
}

const XbeSection *XbeLoader::section_named(const char *name) const
{
    for (const auto &s : sections) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

bool XbeLoader::map_image(std::string &err)
{
    if (data.empty() || base == 0 || size_of_image == 0) {
        err = "XBE not loaded or invalid header";
        return false;
    }

    // Allocate memory at base address (0x00010000) using MAP_FIXED
    void *mapped = mmap((void *)(uintptr_t)base, size_of_image,
                        PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mapped == MAP_FAILED) {
        err = "mmap failed for base address 0x" + std::to_string(base);
        return false;
    }

    // Copy XBE headers
    memcpy((void *)(uintptr_t)base, data.data(), size_of_headers);

    // Map each section
    for (const auto &s : sections) {
        XbeSectionHeader *hdr = (XbeSectionHeader *)(uintptr_t)s.header_va;
        if (s.flags & XBE_SEC_PRELOAD) {
            if (s.raw_addr + s.raw_size > data.size()) {
                err = "section " + s.name + " raw data outside file bounds";
                return false;
            }
            // Copy section raw data
            memcpy((void *)(uintptr_t)s.vaddr, data.data() + s.raw_addr, s.raw_size);

            // Zero out uninitialized BSS tail
            if (s.vsize > s.raw_size) {
                memset((void *)(uintptr_t)(s.vaddr + s.raw_size), 0, s.vsize - s.raw_size);
            }
            if (hdr) hdr->reference_count = 1;
        } else {
            if (hdr) hdr->reference_count = 0;
        }
    }

    is_mapped = true;
    return true;
}

bool XbeLoader::resolve_thunks(void* (*resolver)(uint32_t ordinal), int &resolved_count)
{
    if (!is_mapped || !thunk_addr) return false;
    uint32_t *thunk = (uint32_t *)(uintptr_t)thunk_addr;
    resolved_count = 0;

    for (int i = 0; thunk[i]; i++) {
        uint32_t ord = thunk[i] & 0x7FFFFFFF;
        void *target = resolver(ord);
        if (target) {
            thunk[i] = (uint32_t)(uintptr_t)target;
            resolved_count++;
        }
    }
    return true;
}
