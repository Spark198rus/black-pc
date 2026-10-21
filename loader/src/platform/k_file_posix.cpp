#include "k_file_posix.h"
#include "k_sync_posix.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace xposix {

#pragma pack(push, 1)
struct XSTRING {
    uint16_t Length;
    uint16_t MaximumLength;
    char    *Buffer;
};

struct XOBJECT_ATTRIBUTES {
    void    *RootDirectory;
    XSTRING *ObjectName;
    uint32_t Attributes;
};

struct XIO_STATUS_BLOCK {
    int32_t  Status;
    uint32_t Information;
};
#pragma pack(pop)

namespace {

struct Mount {
    std::string device; // e.g. "\Device\CdRom0"
    std::string key;    // lowercase
    std::string host;   // e.g. "./game"
    bool readonly;
};

struct FileHandle {
    int fd;
    std::string host_path;
    bool is_dir;
    DIR *dir;
    std::string search_mask;
};

struct LinkHandle {
    std::string name;
    std::string target;
};

std::recursive_mutex g_file_mutex;
std::vector<Mount> g_mounts;
std::map<std::string, std::string> g_symlinks; // lowercase link -> device
std::unordered_map<HANDLE, FileHandle*> g_open_files;
std::unordered_map<HANDLE, LinkHandle> g_link_handles;
uint32_t g_next_file_handle = 0x3000;
uint32_t g_next_link_handle = 0x5000;
bool g_initialized = false;
std::string g_cache_dir;

static bool match_wildcard(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return true;
            while (*str) {
                if (match_wildcard(pat, str)) return true;
                str++;
            }
            return false;
        }
        if (!*str) return false;
        if (*pat != '?' && tolower((unsigned char)*pat) != tolower((unsigned char)*str))
            return false;
        pat++;
        str++;
    }
    return !*str;
}

static inline int64_t posix_to_filetime(time_t t)
{
    return ((int64_t)t * 10000000LL) + 116444736000000000LL;
}

std::string to_lower(std::string s)
{
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// Case-insensitive file/folder resolver for Linux filesystem
std::string find_case_insensitive(const std::string &base, const std::string &rel)
{
    std::string current = base;
    size_t start = 0;

    while (start < rel.size()) {
        size_t next = rel.find_first_of("/\\", start);
        std::string part = (next == std::string::npos) ? rel.substr(start) : rel.substr(start, next - start);
        if (!part.empty() && part != ".") {
            DIR *d = opendir(current.c_str());
            if (!d) {
                // If directory can't be opened, append remainder with standard slashes
                current += "/" + rel.substr(start);
                break;
            }
            struct dirent *de;
            bool found = false;
            while ((de = readdir(d)) != nullptr) {
                if (strcasecmp(de->d_name, part.c_str()) == 0) {
                    current += "/" + std::string(de->d_name);
                    found = true;
                    break;
                }
            }
            closedir(d);
            if (!found) {
                current += "/" + part;
            }
        }
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return current;
}

} // namespace

void file_init(const std::string &game_dir, const std::string &savedata_dir)
{
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    if (g_initialized) return;

    std::string p0_path = savedata_dir + "/Partition0.bin";
    struct stat st_p0;
    if (stat(p0_path.c_str(), &st_p0) != 0) {
        mkdir(savedata_dir.c_str(), 0777);
        int fd = open(p0_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) {
            uint8_t zero[4096] = {0};
            for (int i = 0; i < 16; i++) {
                ssize_t written = write(fd, zero, sizeof(zero));
                (void)written;
            }
            close(fd);
        }
    }

    g_mounts.push_back({ "\\Device\\CdRom0", "\\device\\cdrom0", game_dir, true });
    g_mounts.push_back({ "\\Device\\Harddisk0\\Partition0", "\\device\\harddisk0\\partition0", p0_path, false });
    g_mounts.push_back({ "\\Device\\Harddisk0\\Partition1", "\\device\\harddisk0\\partition1", savedata_dir + "/Partition1", false });
    g_mounts.push_back({ "\\Device\\Harddisk0\\Partition5", "\\device\\harddisk0\\partition5", savedata_dir + "/Partition5", false });

    g_symlinks.clear();
    g_symlinks["\\??\\d:"] = "\\Device\\CdRom0";
    g_symlinks["\\??\\t:"] = "\\Device\\Harddisk0\\Partition1";
    g_symlinks["\\??\\u:"] = "\\Device\\Harddisk0\\Partition1";
    g_symlinks["\\??\\z:"] = "\\Device\\Harddisk0\\Partition5";
    g_symlinks["d:"] = "\\Device\\CdRom0";
    g_symlinks["t:"] = "\\Device\\Harddisk0\\Partition1";
    g_symlinks["u:"] = "\\Device\\Harddisk0\\Partition1";
    g_symlinks["z:"] = "\\Device\\Harddisk0\\Partition5";

    g_initialized = true;
}

NTSTATUS IoCreateSymbolicLink(const char *symlink_name, const char *device_name)
{
    if (!symlink_name || !device_name) return 0xC000000DL; // STATUS_INVALID_PARAMETER
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    file_init();

    g_symlinks[to_lower(symlink_name)] = device_name;
    return 0; // STATUS_SUCCESS
}

NTSTATUS IoDeleteSymbolicLink(const char *symlink_name)
{
    if (!symlink_name) return 0xC000000DL;
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    file_init();

    g_symlinks.erase(to_lower(symlink_name));
    return 0;
}

void set_cache_override(const std::string &cache_dir)
{
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    g_cache_dir = cache_dir;
}

std::string resolve_xbox_path(const std::string &xbox_path)
{
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    file_init();

    std::string p = xbox_path;
    // Replace symbolic links
    for (const auto &pair : g_symlinks) {
        if (strncasecmp(p.c_str(), pair.first.c_str(), pair.first.size()) == 0) {
            p = pair.second + p.substr(pair.first.size());
            break;
        }
    }

    // Match mount point
    std::string low = to_lower(p);
    for (const auto &m : g_mounts) {
        if (low.rfind(m.key, 0) == 0) {
            std::string rel = p.substr(m.device.size());
            if (m.readonly && !g_cache_dir.empty()) {
                std::string cached = find_case_insensitive(g_cache_dir, rel);
                struct stat st;
                if (stat(cached.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
                    return cached;
                }
            }
            return find_case_insensitive(m.host, rel);
        }
    }

    // Fallback: match within game directory
    return find_case_insensitive("./game", p);
}

NTSTATUS NtCreateFile(HANDLE *out_handle, ACCESS_MASK desired_access, void *obj_attr, void *io_status,
                      const LARGE_INTEGER *alloc_size, uint32_t file_attr, uint32_t share_access,
                      uint32_t create_disp, uint32_t create_options)
{
    (void)alloc_size; (void)file_attr; (void)share_access;
    if (!out_handle || !obj_attr) return 0xC000000DL;

    XOBJECT_ATTRIBUTES *oa = (XOBJECT_ATTRIBUTES *)obj_attr;
    if (!oa->ObjectName || !oa->ObjectName->Buffer) return 0xC000000DL;

    std::string raw_name(oa->ObjectName->Buffer, oa->ObjectName->Length);
    std::string host_path = resolve_xbox_path(raw_name);
    printf("  [VFS] NtCreateFile: '%s' -> '%s' (disp=%u, access=0x%X)\n",
           raw_name.c_str(), host_path.c_str(), create_disp, (unsigned int)desired_access);

    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);

    bool is_directory_req = (create_options & 0x00000001) != 0;
    if (is_directory_req) {
        if (create_disp == 2 || create_disp == 3) {
            mkdir(host_path.c_str(), 0777);
        }
        DIR *d = opendir(host_path.c_str());
        if (!d) return 0xC000003AL; // STATUS_OBJECT_PATH_NOT_FOUND
        FileHandle *fh = new FileHandle{-1, host_path, true, d, {}};
        HANDLE h = (HANDLE)(uintptr_t)(g_next_file_handle += 4);
        g_open_files[h] = fh;
        *out_handle = h;
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = 0;
            ((XIO_STATUS_BLOCK *)io_status)->Information = (create_disp == 2) ? 2 : 1;
        }
        printf("  [VFS] NtCreateFile dir created/opened -> Handle %p\n", h);
        return 0;
    }

    int flags = O_RDONLY;
    if (desired_access & (0x40000000 | 0x10000000 | 0x0002 | 0x0004)) flags = O_RDWR;
    if (create_disp == 2) flags |= O_CREAT | O_TRUNC; // FILE_CREATE
    if (create_disp == 3) flags |= O_CREAT; // FILE_OPEN_IF

    struct stat st;
    if (stat(host_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        DIR *d = opendir(host_path.c_str());
        if (!d) return 0xC000003AL; // STATUS_OBJECT_PATH_NOT_FOUND
        FileHandle *fh = new FileHandle{-1, host_path, true, d, {}};
        HANDLE h = (HANDLE)(uintptr_t)(g_next_file_handle += 4);
        g_open_files[h] = fh;
        *out_handle = h;
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = 0;
            ((XIO_STATUS_BLOCK *)io_status)->Information = 1; // FILE_OPENED
        }
        printf("  [VFS] NtCreateFile dir ok -> Handle %p\n", h);
        return 0;
    }

    if (create_disp == 2 || create_disp == 3) {
        size_t last_slash = host_path.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string parent = host_path.substr(0, last_slash);
            mkdir(parent.c_str(), 0777);
        }
    }

    int fd = open(host_path.c_str(), flags, 0666);
    if (fd < 0) {
        printf("  [VFS] NtCreateFile failed (errno=%d: %s)\n", errno, strerror(errno));
        return 0xC0000034L; // STATUS_OBJECT_NAME_NOT_FOUND
    }

    FileHandle *fh = new FileHandle{fd, host_path, false, nullptr, {}};
    HANDLE h = (HANDLE)(uintptr_t)(g_next_file_handle += 4);
    g_open_files[h] = fh;
    *out_handle = h;

    if (io_status) {
        ((XIO_STATUS_BLOCK *)io_status)->Status = 0;
        ((XIO_STATUS_BLOCK *)io_status)->Information = 1; // FILE_OPENED
    }
    printf("  [VFS] NtCreateFile file ok -> Handle %p\n", h);
    return 0;
}

NTSTATUS NtOpenFile(HANDLE *out_handle, ACCESS_MASK desired_access, void *obj_attr, void *io_status,
                    uint32_t share_access, uint32_t open_options)
{
    return NtCreateFile(out_handle, desired_access, obj_attr, io_status, nullptr, 0, share_access, 1, open_options);
}

NTSTATUS NtReadFile(HANDLE handle, HANDLE event, void *apc_routine, void *apc_context, void *io_status,
                    void *buffer, uint32_t length, const LARGE_INTEGER *byte_offset)
{
    (void)apc_routine; (void)apc_context;
    if (!buffer) {
        printf("  [VFS ERROR] NtReadFile called with NULL buffer! (handle=%p, length=%u)\n", handle, length);
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = 0xC000000DL;
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0xC000000DL;
    }

    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it = g_open_files.find(handle);
    if (it == g_open_files.end() || it->second->is_dir) {
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = 0xC0000008L;
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0xC0000008L; // STATUS_INVALID_HANDLE
    }

    int fd = it->second->fd;
    ssize_t rd = 0;
    if (byte_offset) {
        rd = pread(fd, buffer, length, (off_t)byte_offset->QuadPart);
    } else {
        rd = read(fd, buffer, length);
    }

    NTSTATUS status = 0;
    if (rd < 0) {
        status = 0xC0000001L; // STATUS_UNSUCCESSFUL
        rd = 0;
    }

    if (io_status) {
        ((XIO_STATUS_BLOCK *)io_status)->Status = status;
        ((XIO_STATUS_BLOCK *)io_status)->Information = (uint32_t)rd;
    }
    if (event) {
        NtSetEvent(event, nullptr);
    }
    return status;
}

NTSTATUS NtWriteFile(HANDLE handle, HANDLE event, void *apc_routine, void *apc_context, void *io_status,
                     const void *buffer, uint32_t length, const LARGE_INTEGER *byte_offset)
{
    (void)apc_routine; (void)apc_context;
    if (!buffer) {
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = 0xC000000DL;
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0xC000000DL;
    }

    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it = g_open_files.find(handle);
    if (it == g_open_files.end() || it->second->is_dir) {
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = 0xC0000008L;
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0xC0000008L;
    }

    int fd = it->second->fd;
    ssize_t wr = 0;
    if (byte_offset) {
        wr = pwrite(fd, buffer, length, (off_t)byte_offset->QuadPart);
    } else {
        wr = write(fd, buffer, length);
    }

    NTSTATUS status = 0;
    if (wr < 0) {
        status = 0xC0000001L;
        wr = 0;
    }

    if (io_status) {
        ((XIO_STATUS_BLOCK *)io_status)->Status = status;
        ((XIO_STATUS_BLOCK *)io_status)->Information = (uint32_t)wr;
    }
    if (event) {
        NtSetEvent(event, nullptr);
    }
    return status;
}

NTSTATUS NtQueryInformationFile(HANDLE handle, void *io_status, void *file_info, uint32_t length, uint32_t info_class)
{
    (void)length;
    if (!file_info) return 0xC000000DL;

    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it = g_open_files.find(handle);
    if (it == g_open_files.end()) return 0xC0000008L;

    struct stat st{};
    if (it->second->fd >= 0) {
        if (fstat(it->second->fd, &st) != 0) return 0xC0000001L;
    } else if (!it->second->host_path.empty()) {
        if (stat(it->second->host_path.c_str(), &st) != 0) return 0xC0000001L;
    }

    uint32_t written = 0;
    switch (info_class) {
    case 4: { // FileBasicInformation
        struct {
            int64_t CreationTime;
            int64_t LastAccessTime;
            int64_t LastWriteTime;
            int64_t ChangeTime;
            uint32_t FileAttributes;
        } *fbi = (decltype(fbi))file_info;
        int64_t t = ((int64_t)st.st_mtime + 11644473600LL) * 10000000LL;
        fbi->CreationTime = t;
        fbi->LastAccessTime = t;
        fbi->LastWriteTime = t;
        fbi->ChangeTime = t;
        fbi->FileAttributes = S_ISDIR(st.st_mode) ? 0x00000010 : 0x00000080;
        written = sizeof(*fbi);
        break;
    }
    case 5: { // FileStandardInformation
        struct {
            int64_t AllocationSize;
            int64_t EndOfFile;
            uint32_t NumberOfLinks;
            uint8_t  DeletePending;
            uint8_t  Directory;
        } *fsi = (decltype(fsi))file_info;
        fsi->AllocationSize = (int64_t)st.st_blocks * 512;
        fsi->EndOfFile = st.st_size;
        fsi->NumberOfLinks = st.st_nlink;
        fsi->DeletePending = 0;
        fsi->Directory = S_ISDIR(st.st_mode) ? 1 : 0;
        written = sizeof(*fsi);
        break;
    }
    case 9: { // FileNameInformation
        std::string rel = it->second->host_path;
        uint32_t n = (uint32_t)rel.size();
        *(uint32_t *)file_info = n;
        uint32_t copy = (n < length - 4) ? n : (length - 4);
        memcpy((uint8_t *)file_info + 4, rel.data(), copy);
        written = 4 + copy;
        break;
    }
    case 14: { // FilePositionInformation
        struct {
            int64_t CurrentByteOffset;
        } *fpi = (decltype(fpi))file_info;
        off_t cur = (it->second->fd >= 0) ? lseek(it->second->fd, 0, SEEK_CUR) : 0;
        fpi->CurrentByteOffset = (cur >= 0) ? cur : 0;
        written = sizeof(*fpi);
        break;
    }
    case 34: { // FileNetworkOpenInformation
        struct {
            int64_t CreationTime;
            int64_t LastAccessTime;
            int64_t LastWriteTime;
            int64_t ChangeTime;
            int64_t AllocationSize;
            int64_t EndOfFile;
            uint32_t FileAttributes;
        } *fni = (decltype(fni))file_info;
        int64_t t = ((int64_t)st.st_mtime + 11644473600LL) * 10000000LL;
        fni->CreationTime = t;
        fni->LastAccessTime = t;
        fni->LastWriteTime = t;
        fni->ChangeTime = t;
        fni->AllocationSize = (int64_t)st.st_blocks * 512;
        fni->EndOfFile = st.st_size;
        fni->FileAttributes = S_ISDIR(st.st_mode) ? 0x00000010 : 0x00000080;
        written = sizeof(*fni);
        break;
    }
    default:
        memset(file_info, 0, length);
        written = length;
        break;
    }

    if (io_status) {
        ((XIO_STATUS_BLOCK *)io_status)->Status = 0;
        ((XIO_STATUS_BLOCK *)io_status)->Information = written;
    }
    return 0;
}

NTSTATUS NtSetInformationFile(HANDLE handle, void *io_status, void *file_info, uint32_t length, uint32_t info_class)
{
    (void)length;
    if (!file_info) return 0xC000000DL;

    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it = g_open_files.find(handle);
    if (it == g_open_files.end() || it->second->is_dir) return 0xC0000008L;

    int fd = it->second->fd;
    if (info_class == 14) { // FilePositionInformation
        struct {
            int64_t CurrentByteOffset;
        } *fpi = (decltype(fpi))file_info;
        if (lseek(fd, (off_t)fpi->CurrentByteOffset, SEEK_SET) == (off_t)-1) {
            return 0xC0000001L;
        }
    } else if (info_class == 19) { // FileEndOfFileInformation
        struct {
            int64_t EndOfFile;
        } *fei = (decltype(fei))file_info;
        if (ftruncate(fd, (off_t)fei->EndOfFile) != 0) {
            return 0xC0000001L;
        }
    }

    if (io_status) {
        ((XIO_STATUS_BLOCK *)io_status)->Status = 0;
        ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
    }
    return 0;
}

NTSTATUS NtQueryVolumeInformationFile(HANDLE handle, void *io_status, void *fs_info, uint32_t length, uint32_t fs_info_class)
{
    (void)handle;
    if (!fs_info) return 0xC000000DL;

    uint8_t *p = (uint8_t *)fs_info;
    uint32_t written = 0;

    switch (fs_info_class) {
    case 1: { // FileFsVolumeInformation
        if (length < 18) return 0xC0000023L; // STATUS_BUFFER_TOO_SMALL
        memset(p, 0, 18);
        *(uint32_t *)(p + 8) = 0x0B1AC001; // Serial
        written = 18;
        break;
    }
    case 3: // FileFsSizeInformation
    case 7: { // FileFsFullSizeInformation
        uint32_t need = (fs_info_class == 3) ? 24 : 32;
        if (length < need) return 0xC0000023L;

        uint64_t total_clusters = 0x80000; // 512K clusters of 16KB = 8GB partition
        uint64_t avail_clusters = 0x60000; // ~6GB free

        *(uint64_t *)(p + 0) = total_clusters;
        *(uint64_t *)(p + 8) = avail_clusters;
        if (fs_info_class == 7) {
            *(uint64_t *)(p + 16) = avail_clusters;
        }
        uint32_t offset = (fs_info_class == 3) ? 16 : 24;
        *(uint32_t *)(p + offset) = 32;      // SectorsPerAllocationUnit (16KB / 512 = 32)
        *(uint32_t *)(p + offset + 4) = 512; // BytesPerSector
        written = need;
        break;
    }
    case 5: { // FileFsAttributeInformation
        const char *fs = "FATX";
        uint32_t n = 4;
        if (length < 12 + n) return 0xC0000023L;
        *(uint32_t *)(p + 0) = 0;
        *(uint32_t *)(p + 4) = 42;
        *(uint32_t *)(p + 8) = n;
        memcpy(p + 12, fs, n);
        written = 12 + n;
        break;
    }
    default:
        written = 0;
        break;
    }

    if (io_status) {
        XIO_STATUS_BLOCK *ios = (XIO_STATUS_BLOCK *)io_status;
        ios->Status = 0;
        ios->Information = written;
    }
    return 0; // STATUS_SUCCESS
}

NTSTATUS NtQueryFullAttributesFile(void *obj_attr, void *file_info)
{
    if (!obj_attr || !file_info) return 0xC000000DL;
    XOBJECT_ATTRIBUTES *oa = (XOBJECT_ATTRIBUTES *)obj_attr;
    if (!oa->ObjectName || !oa->ObjectName->Buffer) return 0xC000000DL;

    std::string raw_name(oa->ObjectName->Buffer, oa->ObjectName->Length);
    std::string host = resolve_xbox_path(raw_name);

    struct stat st;
    if (stat(host.c_str(), &st) != 0) return 0xC0000034L; // STATUS_OBJECT_NAME_NOT_FOUND

    struct {
        int64_t CreationTime;
        int64_t LastAccessTime;
        int64_t LastWriteTime;
        int64_t ChangeTime;
        int64_t EndOfFile;
        int64_t AllocationSize;
        uint32_t FileAttributes;
    } *fai = (decltype(fai))file_info;

    fai->EndOfFile = st.st_size;
    fai->AllocationSize = (int64_t)st.st_blocks * 512;
    fai->FileAttributes = S_ISDIR(st.st_mode) ? 0x10 : 0x80; // FILE_ATTRIBUTE_DIRECTORY / NORMAL
    return 0;
}

NTSTATUS NtQueryDirectoryFile(HANDLE handle, HANDLE event, void *apc_routine, void *apc_context, void *io_status,
                             void *file_info, uint32_t length, uint32_t info_class,
                             void *file_name, bool restart_scan)
{
    (void)apc_routine; (void)apc_context;
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it = g_open_files.find(handle);
    if (it == g_open_files.end() || !it->second->is_dir) {
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = (NTSTATUS)0xC0000008L; // STATUS_INVALID_HANDLE
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0xC0000008L;
    }

    FileHandle *fh = it->second;
    if (!fh->dir) {
        fh->dir = opendir(fh->host_path.c_str());
        if (!fh->dir) {
            if (io_status) {
                ((XIO_STATUS_BLOCK *)io_status)->Status = (NTSTATUS)0xC0000034L; // STATUS_OBJECT_NAME_NOT_FOUND
                ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
            }
            if (event) NtSetEvent(event, nullptr);
            return 0xC0000034L;
        }
    }

    if (restart_scan) {
        rewinddir(fh->dir);
    }

    if (file_name) {
        XSTRING *xs = (XSTRING *)file_name;
        if (xs->Buffer && xs->Length > 0) {
            fh->search_mask = std::string(xs->Buffer, xs->Length);
        } else {
            fh->search_mask.clear();
        }
    }

    struct dirent *de = nullptr;
    bool found = false;
    while ((de = readdir(fh->dir)) != nullptr) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        if (!fh->search_mask.empty() && fh->search_mask != "*" && fh->search_mask != "*.*") {
            if (!match_wildcard(fh->search_mask.c_str(), de->d_name)) {
                continue;
            }
        }
        found = true;
        break;
    }

    if (!found || !de) {
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = (NTSTATUS)0x80000006L; // STATUS_NO_MORE_FILES
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0x80000006L;
    }

    uint32_t nchars = (uint32_t)strlen(de->d_name);
    uint32_t header_size = 64;
    uint32_t name_offset = 64;

    if (info_class == 2) { // FileFullDirectoryInformation
        header_size = 68;
        name_offset = 68;
    } else if (info_class == 3) { // FileBothDirectoryInformation
        header_size = 94;
        name_offset = 94;
    } else if (info_class == 7) { // FileNamesInformation
        header_size = 12;
        name_offset = 12;
    }

    if (length < header_size) {
        if (io_status) {
            ((XIO_STATUS_BLOCK *)io_status)->Status = (NTSTATUS)0xC0000023L; // STATUS_BUFFER_TOO_SMALL
            ((XIO_STATUS_BLOCK *)io_status)->Information = 0;
        }
        if (event) NtSetEvent(event, nullptr);
        return 0xC0000023L;
    }

    uint8_t *out = (uint8_t *)file_info;
    memset(out, 0, std::min<uint32_t>(length, header_size));

    *(uint32_t *)out = 0; // NextEntryOffset: single entry
    *(uint32_t *)(out + 4) = 0; // FileIndex

    std::string full_path = fh->host_path + "/" + de->d_name;
    struct stat st;
    bool has_stat = (stat(full_path.c_str(), &st) == 0);

    if (info_class == 7) {
        *(uint32_t *)(out + 8) = nchars;
    } else {
        int64_t t = has_stat ? posix_to_filetime(st.st_mtime) : 0;
        *(int64_t *)(out + 8)  = t;
        *(int64_t *)(out + 16) = t;
        *(int64_t *)(out + 24) = t;
        *(int64_t *)(out + 32) = t;
        *(int64_t *)(out + 40) = has_stat ? st.st_size : 0;
        *(int64_t *)(out + 48) = has_stat ? ((int64_t)st.st_blocks * 512) : 0;
        uint32_t attr = (has_stat && S_ISDIR(st.st_mode)) || (de->d_type == DT_DIR) ? 0x10 : 0x80;
        *(uint32_t *)(out + 56) = attr;
        *(uint32_t *)(out + 60) = nchars;
    }

    uint32_t copy = (header_size + nchars <= length) ? nchars : (length - header_size);
    memcpy(out + name_offset, de->d_name, copy);

    NTSTATUS rs = (copy < nchars) ? (NTSTATUS)0x80000005L /* STATUS_BUFFER_OVERFLOW */ : 0 /* STATUS_SUCCESS */;
    if (io_status) {
        ((XIO_STATUS_BLOCK *)io_status)->Status = rs;
        ((XIO_STATUS_BLOCK *)io_status)->Information = header_size + copy;
    }
    if (event) {
        NtSetEvent(event, nullptr);
    }
    return rs;
}

NTSTATUS NtOpenSymbolicLinkObject(HANDLE *link_handle, void *obj_attr)
{
    if (!link_handle || !obj_attr) return 0xC000000DL;
    XOBJECT_ATTRIBUTES *oa = (XOBJECT_ATTRIBUTES *)obj_attr;
    if (!oa->ObjectName || !oa->ObjectName->Buffer) return 0xC000000DL;

    std::string name(oa->ObjectName->Buffer, oa->ObjectName->Length);
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    file_init();

    auto it = g_symlinks.find(to_lower(name));
    if (it == g_symlinks.end()) {
        return 0xC0000034L; // STATUS_OBJECT_NAME_NOT_FOUND
    }

    HANDLE h = (HANDLE)(uintptr_t)g_next_link_handle;
    g_next_link_handle += 4;
    g_link_handles[h] = LinkHandle{name, it->second};
    *link_handle = h;
    return 0; // STATUS_SUCCESS
}

NTSTATUS NtQuerySymbolicLinkObject(HANDLE link_handle, void *link_target, uint32_t *returned_length)
{
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it = g_link_handles.find(link_handle);
    if (it == g_link_handles.end()) return 0xC0000008L; // STATUS_INVALID_HANDLE

    const std::string &target = it->second.target;
    if (returned_length) *returned_length = (uint32_t)target.size();

    XSTRING *lt = (XSTRING *)link_target;
    if (!lt || !lt->Buffer || lt->MaximumLength < target.size()) {
        return 0xC0000023L; // STATUS_BUFFER_TOO_SMALL
    }

    memcpy(lt->Buffer, target.data(), target.size());
    lt->Length = (uint16_t)target.size();
    return 0; // STATUS_SUCCESS
}

bool file_close_handle(HANDLE handle)
{
    std::lock_guard<std::recursive_mutex> lock(g_file_mutex);
    auto it_link = g_link_handles.find(handle);
    if (it_link != g_link_handles.end()) {
        g_link_handles.erase(it_link);
        return true;
    }

    auto it = g_open_files.find(handle);
    if (it == g_open_files.end()) return false;

    if (it->second->is_dir && it->second->dir) {
        closedir(it->second->dir);
    } else if (it->second->fd >= 0) {
        close(it->second->fd);
    }
    delete it->second;
    g_open_files.erase(it);
    return true;
}

} // namespace xposix
