// XBE (Xbox executable) parser. Layout follows tools/xbeinfo.py; offsets are into the image header,
// which sits at the start of the file and is mapped at the image base.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Section header as it lies in the mapped image (56 bytes). XeLoadSection receives a pointer to one.
#pragma pack(push, 1)
struct XbeSectionHeader {
    uint32_t flags;
    uint32_t virtual_addr;
    uint32_t virtual_size;
    uint32_t raw_addr;
    uint32_t raw_size;
    uint32_t name_addr;
    uint32_t reference_count;
    uint32_t head_shared_ref_addr;
    uint32_t tail_shared_ref_addr;
    uint8_t digest[20];
};
#pragma pack(pop)
static_assert(sizeof(XbeSectionHeader) == 56, "XBE section header is 56 bytes");

enum : uint32_t {
    XBE_SEC_WRITABLE = 0x01,
    XBE_SEC_PRELOAD = 0x02,
    XBE_SEC_EXECUTABLE = 0x04,
    XBE_SEC_INSERTED_FILE = 0x08,
    XBE_SEC_HEAD_PAGE_RO = 0x10,
    XBE_SEC_TAIL_PAGE_RO = 0x20,
};

struct XbeSection {
    uint32_t index;
    std::string name;
    uint32_t flags, vaddr, vsize, raw_addr, raw_size;
    uint32_t header_va; // address of this section's XbeSectionHeader in the mapped image
};

struct XbeTls {
    uint32_t data_start, data_end, index_addr, callbacks_addr, zero_fill, characteristics;
};

struct XbeLibrary {
    std::string name;
    uint16_t major, minor, build, flags;
};

class Xbe {
public:
    bool load(const std::wstring &path, std::string &err);

    std::vector<uint8_t> data;
    std::string kind; // retail / debug / sega (XOR key set that decoded the entry point)
    uint32_t base = 0, size_of_headers = 0, size_of_image = 0, timedate = 0, cert_addr = 0;
    uint32_t num_sections = 0, sect_addr = 0, init_flags = 0, entry = 0, tls_addr = 0;
    uint32_t stack_commit = 0, heap_reserve = 0, heap_commit = 0, pe_base = 0, pe_size = 0;
    uint32_t debug_path_addr = 0, thunk_addr = 0, libver_count = 0, libver_addr = 0;

    // certificate
    uint32_t title_id = 0, cert_timedate = 0, allowed_media = 0, region = 0, ratings = 0, disk_number = 0, version = 0;
    std::wstring title;

    std::vector<XbeSection> sections;
    bool has_tls = false;
    XbeTls tls{};
    std::vector<XbeLibrary> libraries;

    // Pointer into the file data for an image virtual address range, or nullptr.
    const uint8_t *at_va(uint32_t va, uint32_t len) const;
    uint32_t u32_at_va(uint32_t va) const;
    std::string debug_path() const;
    std::vector<uint32_t> kernel_imports() const; // ordinals, in thunk-table order
    const XbeSection *section_containing(uint32_t va) const;
    const XbeSection *section_named(const char *name) const;

    void print_info(const char *(*kernel_name)(uint32_t ordinal)) const;
};

// Translate an address from the supported NTSC-U image's layout to the matching section
// in the currently loaded XBE, including the PAL build's 0x30-byte shift inside .text.
// Addresses outside that image's sections pass through.
uint32_t xbe_va(uint32_t ntsc_va);
