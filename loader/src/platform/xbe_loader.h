// Cross-platform XBE (Xbox Executable) Loader for Linux x86 and Win32
#pragma once

#include "loader/src/platform/windefs.h"

#include <cstdint>
#include <string>
#include <vector>

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
    uint8_t  digest[20];
};
#pragma pack(pop)
static_assert(sizeof(XbeSectionHeader) == 56, "XBE section header must be 56 bytes");

enum : uint32_t {
    XBE_SEC_WRITABLE       = 0x01,
    XBE_SEC_PRELOAD        = 0x02,
    XBE_SEC_EXECUTABLE     = 0x04,
    XBE_SEC_INSERTED_FILE  = 0x08,
    XBE_SEC_HEAD_PAGE_RO   = 0x10,
    XBE_SEC_TAIL_PAGE_RO   = 0x20,
};

struct XbeSection {
    uint32_t index;
    std::string name;
    uint32_t flags;
    uint32_t vaddr;
    uint32_t vsize;
    uint32_t raw_addr;
    uint32_t raw_size;
    uint32_t header_va;
};

struct XbeTls {
    uint32_t data_start;
    uint32_t data_end;
    uint32_t index_addr;
    uint32_t callbacks_addr;
    uint32_t zero_fill;
    uint32_t characteristics;
};

struct XbeLibrary {
    std::string name;
    uint16_t major, minor, build, flags;
};

class XbeLoader {
public:
    XbeLoader() = default;

    // Load and parse XBE from disk using POSIX I/O
    bool load(const std::string &path, std::string &err);

    // Map XBE image and preloaded sections into memory at 0x00010000 via mmap MAP_FIXED
    bool map_image(std::string &err);

    // Resolve kernel thunks using a callback function
    bool resolve_thunks(void* (*resolver)(uint32_t ordinal), int &resolved_count);

    const uint8_t *at_va(uint32_t va, uint32_t len) const;
    uint32_t u32_at_va(uint32_t va) const;
    std::vector<uint32_t> kernel_imports() const;
    const XbeSection *section_named(const char *name) const;

    std::vector<uint8_t> data;
    std::string kind; // "retail", "debug", "sega"
    uint32_t base = 0;
    uint32_t size_of_headers = 0;
    uint32_t size_of_image = 0;
    uint32_t timedate = 0;
    uint32_t cert_addr = 0;
    uint32_t num_sections = 0;
    uint32_t sect_addr = 0;
    uint32_t init_flags = 0;
    uint32_t entry = 0;
    uint32_t tls_addr = 0;
    uint32_t stack_commit = 0;
    uint32_t heap_reserve = 0;
    uint32_t heap_commit = 0;
    uint32_t pe_base = 0;
    uint32_t pe_size = 0;
    uint32_t debug_path_addr = 0;
    uint32_t thunk_addr = 0;
    uint32_t libver_count = 0;
    uint32_t libver_addr = 0;

    // Certificate fields
    uint32_t title_id = 0;
    uint32_t cert_timedate = 0;
    uint32_t allowed_media = 0;
    uint32_t region = 0;
    uint32_t ratings = 0;
    uint32_t disk_number = 0;
    uint32_t version = 0;
    std::string title;

    std::vector<XbeSection> sections;
    bool has_tls = false;
    XbeTls tls{};
    std::vector<XbeLibrary> libraries;
    bool is_mapped = false;
};
