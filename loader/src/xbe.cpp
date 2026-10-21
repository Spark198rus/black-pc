#include "xbe.h"

#include "common.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

constexpr uint32_t ENTRY_XOR[3] = {0xA8FC57AB, 0x94859D4B, 0x40B5C16E}; // retail, debug, sega
constexpr uint32_t THUNK_XOR[3] = {0x5B6D40B6, 0xEFB1F152, 0x2290059D};
const char *const KIND_NAMES[3] = {"retail", "debug", "sega"};

uint32_t rd32(const std::vector<uint8_t> &d, size_t off)
{
    uint32_t v = 0;
    if (off + 4 <= d.size()) memcpy(&v, &d[off], 4);
    return v;
}

uint16_t rd16(const std::vector<uint8_t> &d, size_t off)
{
    uint16_t v = 0;
    if (off + 2 <= d.size()) memcpy(&v, &d[off], 2);
    return v;
}

std::string fmt_time(uint32_t t)
{
    time_t tt = (time_t)t;
    tm g{};
    gmtime_s(&g, &tt);
    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC", &g);
    return buf;
}

} // namespace

bool Xbe::load(const std::wstring &path, std::string &err)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = "cannot open file (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    LARGE_INTEGER sz;
    GetFileSizeEx(h, &sz);
    data.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = ReadFile(h, data.data(), (DWORD)data.size(), &got, nullptr);
    CloseHandle(h);
    if (!ok || got != data.size()) {
        err = "read failed";
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

    // Entry point and thunk address are XORed with a build-kind key: pick the key that lands in the image.
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
        while (no < data.size() && data[no] && s.name.size() < 64) s.name += (char)data[no++];
        sections.push_back(s);
    }

    size_t co = cert_addr - base;
    cert_timedate = rd32(data, co + 4);
    title_id = rd32(data, co + 8);
    title.clear();
    for (int i = 0; i < 40; i++) {
        wchar_t c = (wchar_t)rd16(data, co + 0x0C + i * 2);
        if (!c) break;
        title += c;
    }
    allowed_media = rd32(data, co + 0x9C);
    region = rd32(data, co + 0xA0);
    ratings = rd32(data, co + 0xA4);
    disk_number = rd32(data, co + 0xA8);
    version = rd32(data, co + 0xAC);

    has_tls = false;
    if (tls_addr) {
        if (const uint8_t *p = at_va(tls_addr, sizeof(XbeTls))) {
            memcpy(&tls, p, sizeof tls);
            has_tls = true;
        }
    }

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

const uint8_t *Xbe::at_va(uint32_t va, uint32_t len) const
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

uint32_t Xbe::u32_at_va(uint32_t va) const
{
    uint32_t v = 0;
    if (const uint8_t *p = at_va(va, 4)) memcpy(&v, p, 4);
    return v;
}

std::string Xbe::debug_path() const
{
    std::string s;
    if (!debug_path_addr) return s;
    size_t o = debug_path_addr - base;
    while (o < data.size() && o < size_of_headers && data[o] && s.size() < 260) s += (char)data[o++];
    return s;
}

std::vector<uint32_t> Xbe::kernel_imports() const
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

const XbeSection *Xbe::section_containing(uint32_t va) const
{
    for (const auto &s : sections)
        if (va >= s.vaddr && va < s.vaddr + std::max(s.vsize, s.raw_size)) return &s;
    return nullptr;
}

const XbeSection *Xbe::section_named(const char *name) const
{
    for (const auto &s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

void Xbe::print_info(const char *(*kernel_name)(uint32_t)) const
{
    static const struct { uint32_t bit; const char *name; } regions[] = {
        {0x1, "NA"}, {0x2, "Japan"}, {0x4, "Rest of World"}, {0x80000000, "Manufacturing"}};
    std::string reg;
    for (auto &r : regions)
        if (region & r.bit) reg += (reg.empty() ? "" : "/") + std::string(r.name);
    std::string t;
    for (wchar_t c : title) t += (c < 128) ? (char)c : '?';

    printf("Build kind      %s  (entry/thunk XOR keys)\n", kind.c_str());
    printf("Title           '%s'  id %08X  version 0x%x  disc %u\n", t.c_str(), title_id, version, disk_number);
    printf("Region          %s (0x%x)   media 0x%x   ratings 0x%x\n", reg.empty() ? "?" : reg.c_str(), region, allowed_media, ratings);
    printf("XBE timestamp   %s   cert %s\n", fmt_time(timedate).c_str(), fmt_time(cert_timedate).c_str());
    printf("Debug path      %s\n", debug_path().c_str());
    printf("Image base      0x%08x   size 0x%x   headers 0x%x\n", base, size_of_image, size_of_headers);
    printf("Entry point     0x%08x\n", entry);
    auto imports = kernel_imports();
    printf("Kernel thunks   0x%08x   (%zu imports)\n", thunk_addr, imports.size());
    printf("PE (orig)       base 0x%08x  size 0x%x   stack commit 0x%x   heap 0x%x/0x%x\n", pe_base, pe_size,
           stack_commit, heap_reserve, heap_commit);
    if (has_tls)
        printf("TLS             data 0x%08x-0x%08x  index@0x%08x  callbacks@0x%08x  zero-fill 0x%x\n", tls.data_start,
               tls.data_end, tls.index_addr, tls.callbacks_addr, tls.zero_fill);

    printf("\nSections (%zu)\n", sections.size());
    printf("  %2s %-10s %10s %9s %9s %9s  flags\n", "#", "name", "vaddr", "vsize", "raw", "rawsize");
    for (const auto &s : sections) {
        std::string f;
        static const char *fn[] = {"writable", "preload", "executable", "inserted file", "head page RO", "tail page RO"};
        for (int b = 0; b < 6; b++)
            if (s.flags & (1u << b)) f += (f.empty() ? "" : ",") + std::string(fn[b]);
        printf("  %2u %-10s 0x%08x %#9x %#9x %#9x  %s\n", s.index, s.name.c_str(), s.vaddr, s.vsize, s.raw_addr, s.raw_size,
               f.empty() ? "-" : f.c_str());
    }

    printf("\nStatically linked SDK libraries (%zu)\n", libraries.size());
    for (const auto &l : libraries)
        printf("  %-10s %u.%u.%-6u qfe %u  approved %u\n", l.name.c_str(), l.major, l.minor, l.build, l.flags & 0x1FFF,
               (l.flags >> 13) & 3);

    auto sorted = imports;
    std::sort(sorted.begin(), sorted.end());
    printf("\nKernel imports (%zu)\n", sorted.size());
    for (uint32_t o : sorted) printf("  %3u  %s\n", o, kernel_name ? kernel_name(o) : "?");
}
