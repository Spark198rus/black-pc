// Files, devices, symbolic links and handles.
//
// Xbox object paths are 8-bit strings: "\??\D:\media\x.bin" goes through the symbolic link table
// ("\??\D:" -> "\Device\CdRom0") to a device path, and device prefixes map to host folders:
//   \Device\CdRom0                 -> the game folder (read-only)
//   \Device\Harddisk0\PartitionN   -> <hdd>\PartitionN   (1 = E: with TDATA/UDATA, 2 = C:, 3-5 = X/Y/Z caches)
// File handles are real Windows handles opened through ntdll, so read/write/APC/event semantics carry over.
#include "kernel/kexports.h"
#include "kernel/nt.h"

#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint32_t FILE_OPEN_ = 1, FILE_OPEN_IF_ = 3;
constexpr uint32_t OPT_DIRECTORY_FILE = 0x1, OPT_NO_INTERMEDIATE_BUFFERING = 0x8, OPT_SYNC_ALERT = 0x10,
                   OPT_SYNC_NONALERT = 0x20, OPT_DELETE_ON_CLOSE = 0x1000;
constexpr ACCESS_MASK WRITE_ACCESS = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE |
                                     GENERIC_WRITE | GENERIC_ALL | WRITE_DAC | WRITE_OWNER;

struct Mount {
    std::string device; // as Xbox code spells it
    std::string key;    // lowercase
    std::wstring host;  // absolute host folder, no trailing backslash
    bool readonly;
    int partition;      // HDD partition number (0 = whole disk), -1 for the DVD
};

struct PartitionGeom {
    uint64_t start, size;
};
// Partition layout of a retail 8 GB drive (0 = the whole disk).
const PartitionGeom kPartitions[] = {
    {0, 0x1DD156000ull},
    {0xABE80000ull, 0x1312D6000ull}, // 1: E
    {0x8CA80000ull, 0x1F400000ull},  // 2: C
    {0x00080000ull, 0x2EE00000ull},  // 3: X
    {0x2EE80000ull, 0x2EE00000ull},  // 4: Y
    {0x5DC80000ull, 0x2EE00000ull},  // 5: Z
};

struct HandleInfo {
    std::string xbox_path;
    const Mount *mount;
    bool raw_volume;
};

struct LinkObject {
    std::string name, target;
};

std::recursive_mutex g_mutex;
std::vector<Mount> g_mounts;
std::wstring g_hdd; // absolute host folder for the hard disk
std::map<std::string, std::string> g_links; // lowercase name -> target
std::unordered_map<HANDLE, HandleInfo> g_handles;
std::unordered_map<HANDLE, LinkObject> g_link_handles;
uint32_t g_next_link_handle = 0x7F000000;

std::string lower(std::string s)
{
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

std::wstring full_path(const std::wstring &p);

struct Resolved {
    std::string xbox_path; // canonical device path
    const Mount *mount = nullptr;
    std::string rest;      // part after the device, starts with '\' or is empty
    std::wstring host_nt;  // \??\C:\...
    bool raw_volume = false;
};

NTSTATUS resolve(HANDLE root, const XANSI_STRING *name, Resolved &r)
{
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    std::string path = name ? xstr(name) : std::string();
    if (root == (HANDLE)(LONG_PTR)-3) { // ObDosDevicesDirectory(): XAPI passes "d:\x" relative to \??\ like this
        path = "\\??\\" + path;
        root = nullptr;
    }
    if (root) {
        auto it = g_handles.find(root);
        if (it == g_handles.end()) return XSTATUS_INVALID_HANDLE;
        std::string base = it->second.xbox_path;
        if (!path.empty() && path[0] != '\\' && (base.empty() || base.back() != '\\')) base += '\\';
        path = base + path;
    }
    for (int depth = 0; depth < 8; depth++) {
        std::string lp = lower(path);
        if (lp.rfind("\\??\\", 0) == 0) {
            path = path.substr(4);
            continue;
        }
        if (lp.rfind("\\dosdevices\\", 0) == 0) {
            path = path.substr(12);
            continue;
        }
        if (path.size() >= 2 && path[1] == ':' && isalpha((unsigned char)path[0])) {
            auto it = g_links.find("\\??\\" + lower(path.substr(0, 2)));
            if (it == g_links.end()) return XSTATUS_OBJECT_PATH_NOT_FOUND;
            path = it->second + path.substr(2);
            continue;
        }
        break;
    }
    std::string lp = lower(path);
    const Mount *best = nullptr;
    for (const auto &m : g_mounts) {
        if (lp.rfind(m.key, 0) == 0 && (lp.size() == m.key.size() || lp[m.key.size()] == '\\'))
            if (!best || m.key.size() > best->key.size()) best = &m;
    }
    if (!best) return XSTATUS_OBJECT_PATH_NOT_FOUND;
    r.mount = best;
    r.rest = path.substr(best->key.size());
    r.raw_volume = r.rest.empty();
    r.xbox_path = best->device + r.rest;
    std::string rest = r.rest;
    while (!rest.empty() && rest.back() == '\\') rest.pop_back();
    for (char &c : rest)
        if (c == '/') c = '\\';
    r.host_nt = L"\\??\\" + best->host + widen(rest);
    // --game-override, then the port's rebuilt menus: a file of the same relative path there replaces the game's
    if (best->partition == -1 && !rest.empty())
        for (const std::wstring *root : {&g_cfg.game_override, &g_cfg.menu_cache}) {
            if (root->empty()) continue;
            std::wstring alt = full_path(*root) + widen(rest);
            DWORD a = GetFileAttributesW(alt.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                r.host_nt = L"\\??\\" + alt;
                static std::set<std::wstring> told;
                if (told.insert(alt).second) logf("files: %s from %s", rest.c_str(), narrow(alt).c_str());
                break;
            }
        }
    return XSTATUS_SUCCESS;
}

void remember(HANDLE h, const Resolved &r)
{
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    g_handles[h] = HandleInfo{r.xbox_path, r.mount, r.raw_volume};
}

const HandleInfo *handle_info(HANDLE h, HandleInfo &copy)
{
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    auto it = g_handles.find(h);
    if (it == g_handles.end()) return nullptr;
    copy = it->second;
    return &copy;
}

std::string handle_desc(HANDLE h)
{
    HandleInfo hi;
    return handle_info(h, hi) ? hi.xbox_path : std::string("?");
}

bool is_link_handle(HANDLE h) { return (uint32_t)(uintptr_t)h >= 0x7F000000 && (uint32_t)(uintptr_t)h < 0x7F100000; }

void ensure_dir(const std::wstring &dir)
{
    if (CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return;
    size_t slash = dir.find_last_of(L'\\');
    if (slash != std::wstring::npos && slash > 2) {
        ensure_dir(dir.substr(0, slash));
        CreateDirectoryW(dir.c_str(), nullptr);
    }
}

std::wstring full_path(const std::wstring &p)
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, nullptr);
    std::wstring s(buf, n);
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    return s;
}

void complete(XIO_STATUS_BLOCK *iosb, NTSTATUS s, uint32_t info, HANDLE ev)
{
    if (iosb) {
        iosb->Status = s;
        iosb->Information = info;
    }
    if (ev) SetEvent(ev);
}

NTSTATUS open_common(const char *what, HANDLE *FileHandle, ACCESS_MASK access, XOBJECT_ATTRIBUTES *oa, XIO_STATUS_BLOCK *iosb,
                     LARGE_INTEGER *alloc, uint32_t attrs, uint32_t share, uint32_t disp, uint32_t opts)
{
    if (!oa || !FileHandle) return XSTATUS_INVALID_PARAMETER;
    std::string req = oa->ObjectName ? xstr(oa->ObjectName) : std::string();
    Resolved r;
    NTSTATUS s = resolve(oa->RootDirectory, oa->ObjectName, r);
    if (!NT_OK(s)) {
        logf("[k] %s('%s', root %p) -> 0x%08x (no mapping)", what, req.c_str(), oa->RootDirectory, s);
        complete(iosb, s, 0, nullptr);
        return s;
    }
    // --skip-videos: a missing video is a fatal disc error, so serve the 9-second logo video instead of
    // every other one (the title background AK_N stays).
    if (g_cfg.skip_videos && r.mount->readonly) {
        std::string lp = lower(r.rest);
        size_t slash = lp.rfind('\\');
        std::string file = slash == std::string::npos ? lp : lp.substr(slash + 1);
        if (lp.size() > 4 && lp.substr(lp.size() - 4) == ".xmv" && file != "lo_n_us.xmv" && file != "ak_n.xmv") {
            size_t hslash = r.host_nt.rfind(L'\\');
            r.host_nt = r.host_nt.substr(0, hslash + 1) + L"LO_n_US.xmv";
            logf("[k] %s('%s') -> LO_n_US.xmv (--skip-videos)", what, req.c_str());
        }
    }
    ACCESS_MASK a = access;
    uint32_t d = disp, o = opts & ~OPT_NO_INTERMEDIATE_BUFFERING, sh = share;
    if (r.raw_volume && r.mount->partition >= 0) {
        // Raw hard disk device (Partition0 = whole disk with the cache-partition table at sector 4, or a
        // partition opened for formatting). Backed by a host file that grows as sectors are written;
        // reads past its end return zeros (see NtReadFile).
        r.host_nt = L"\\??\\" + g_hdd + L"\\Partition" + std::to_wstring(r.mount->partition) + L".raw";
        a = GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE | FILE_READ_ATTRIBUTES;
        sh = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        o = (o & (OPT_SYNC_ALERT | OPT_SYNC_NONALERT)) | 0x40 /* FILE_NON_DIRECTORY_FILE */;
        d = FILE_OPEN_IF_;
    } else if (r.raw_volume) {
        // The DVD device itself: back it with the game folder so volume queries and IOCTLs have a handle.
        a = FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
        sh = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
        o = (o & (OPT_SYNC_ALERT | OPT_SYNC_NONALERT)) | OPT_DIRECTORY_FILE;
        d = FILE_OPEN_;
    } else if (r.mount->readonly) {
        if (d != FILE_OPEN_ && d != FILE_OPEN_IF_) {
            logf("[k] %s('%s') disposition %u on read-only media -> ACCESS_DENIED", what, req.c_str(), d);
            complete(iosb, XSTATUS_ACCESS_DENIED, 0, nullptr);
            return XSTATUS_ACCESS_DENIED;
        }
        if (a & GENERIC_ALL) a |= GENERIC_READ;
        a &= ~WRITE_ACCESS;
        d = FILE_OPEN_;
        o &= ~OPT_DELETE_ON_CLOSE;
    }
    if (o & (OPT_SYNC_ALERT | OPT_SYNC_NONALERT)) a |= SYNCHRONIZE; // NT requires it for synchronous handles
    // The Xbox kernel lets any handle query attributes (XAPI queries FileNetworkOpenInformation on
    // write-only handles); Windows needs FILE_READ_ATTRIBUTES for that.
    a |= FILE_READ_ATTRIBUTES;
    host::UNICODE_STRING us{(USHORT)(r.host_nt.size() * 2), (USHORT)(r.host_nt.size() * 2), r.host_nt.data()};
    host::OBJECT_ATTRIBUTES hoa;
    host::init_oa(hoa, &us);
    HANDLE h = nullptr;
    s = host::NtCreateFile(&h, a, &hoa, (host::IO_STATUS_BLOCK *)iosb, alloc, attrs, sh, d, o, nullptr, 0);
    if (NT_OK(s)) {
        *FileHandle = h;
        remember(h, r);
    }
    logf("[k] %s('%s' -> %s, access 0x%x, share %u, disp %u, opts 0x%x) -> 0x%08x handle %p", what, req.c_str(),
         r.xbox_path.c_str(), access, share, disp, opts, s, NT_OK(s) ? h : nullptr);
    if (!NT_OK(s))
        logf("[k]     attrs 0x%x, alloc %lld, host access 0x%x opts 0x%x disp %u", attrs, alloc ? alloc->QuadPart : -1LL, a, o, d);
    return s;
}

std::wstring ansi_to_wide(const char *s, size_t n)
{
    std::wstring w;
    for (size_t i = 0; i < n; i++) w += (wchar_t)(unsigned char)s[i];
    return w;
}

} // namespace

std::string xstr(const XANSI_STRING *s)
{
    if (!s || !s->Buffer) return {};
    return std::string(s->Buffer, s->Length);
}

void kfile_add_symlink(const std::string &name, const std::string &target)
{
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    g_links[lower(name)] = target;
}

bool kfile_init()
{
    std::wstring game = full_path(g_cfg.game_dir);
    std::wstring hdd = full_path(g_cfg.hdd_dir);
    ensure_dir(hdd);
    g_hdd = hdd;
    g_mounts.push_back({"\\Device\\CdRom0", lower("\\Device\\CdRom0"), game, true, -1});
    g_mounts.push_back({"\\Device\\Harddisk0\\Partition0", lower("\\Device\\Harddisk0\\Partition0"), hdd, false, 0});
    for (int p = 1; p <= 5; p++) {
        std::string dev = "\\Device\\Harddisk0\\Partition" + std::to_string(p);
        std::wstring dir = hdd + L"\\Partition" + std::to_wstring(p);
        ensure_dir(dir);
        g_mounts.push_back({dev, lower(dev), dir, false, p});
    }
    ensure_dir(hdd + L"\\Partition1\\TDATA");
    ensure_dir(hdd + L"\\Partition1\\UDATA");
    logf("files: \\Device\\CdRom0 = %s (read-only), \\Device\\Harddisk0\\Partition1-5 = %s\\PartitionN", narrow(game).c_str(),
         narrow(hdd).c_str());
    return true;
}

namespace xk {

NTSTATUS __stdcall NtCreateFile(HANDLE *FileHandle, ACCESS_MASK DesiredAccess, XOBJECT_ATTRIBUTES *ObjectAttributes,
                                XIO_STATUS_BLOCK *IoStatusBlock, LARGE_INTEGER *AllocationSize, uint32_t FileAttributes,
                                uint32_t ShareAccess, uint32_t CreateDisposition, uint32_t CreateOptions)
{
    return open_common("NtCreateFile", FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, AllocationSize, FileAttributes,
                       ShareAccess, CreateDisposition, CreateOptions);
}

NTSTATUS __stdcall NtOpenFile(HANDLE *FileHandle, ACCESS_MASK DesiredAccess, XOBJECT_ATTRIBUTES *ObjectAttributes,
                              XIO_STATUS_BLOCK *IoStatusBlock, uint32_t ShareAccess, uint32_t OpenOptions)
{
    return open_common("NtOpenFile", FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, nullptr, 0, ShareAccess, FILE_OPEN_,
                       OpenOptions);
}

NTSTATUS __stdcall NtClose(HANDLE Handle)
{
    KTRACE("%p", Handle);
    {
        std::lock_guard<std::recursive_mutex> g(g_mutex);
        if (is_link_handle(Handle)) return g_link_handles.erase(Handle) ? XSTATUS_SUCCESS : XSTATUS_INVALID_HANDLE;
        g_handles.erase(Handle);
    }
    return host::NtClose(Handle);
}

NTSTATUS __stdcall NtDuplicateObject(HANDLE SourceHandle, HANDLE *TargetHandle, uint32_t Options)
{
    KTRACE("%p, %p, 0x%x", SourceHandle, TargetHandle, Options);
    HANDLE dup = nullptr;
    DWORD opt = DUPLICATE_SAME_ACCESS | ((Options & 1) ? DUPLICATE_CLOSE_SOURCE : 0);
    if (!DuplicateHandle(GetCurrentProcess(), SourceHandle, GetCurrentProcess(), &dup, 0, FALSE, opt))
        return XSTATUS_INVALID_HANDLE;
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    auto it = g_handles.find(SourceHandle);
    if (it != g_handles.end()) {
        g_handles[dup] = it->second;
        if (Options & 1) g_handles.erase(SourceHandle);
    }
    *TargetHandle = dup;
    return XSTATUS_SUCCESS;
}

NTSTATUS __stdcall NtReadFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext, XIO_STATUS_BLOCK *IoStatusBlock,
                              void *Buffer, uint32_t Length, LARGE_INTEGER *ByteOffset)
{
    HandleInfo hi;
    if (handle_info(FileHandle, hi) && hi.raw_volume && hi.mount->partition >= 0) {
        // Raw disk: unwritten sectors read as zeros, the full length is always returned.
        memset(Buffer, 0, Length);
        host::IO_STATUS_BLOCK hio{};
        NTSTATUS s = host::NtReadFile(FileHandle, nullptr, nullptr, nullptr, &hio, Buffer, Length, ByteOffset, nullptr);
        if (s == XSTATUS_PENDING) {
            WaitForSingleObject(FileHandle, INFINITE);
            s = hio.Status;
        }
        if (s == (NTSTATUS)0xC0000011 /* END_OF_FILE */) s = XSTATUS_SUCCESS;
        logf("[k] NtReadFile(raw '%s', len 0x%x, off 0x%llx) -> 0x%08x", hi.xbox_path.c_str(), Length,
             ByteOffset ? ByteOffset->QuadPart : -1LL, s);
        if (ApcRoutine) KNOTE_ONCE("raw-disk NtReadFile with an APC routine: completed synchronously, APC not queued");
        complete(IoStatusBlock, s, NT_OK(s) ? Length : 0, Event);
        return s;
    }
    NTSTATUS s = host::NtReadFile(FileHandle, Event, ApcRoutine, ApcContext, (host::IO_STATUS_BLOCK *)IoStatusBlock, Buffer, Length,
                                  ByteOffset, nullptr);
    KTRACE("%p '%s', ev %p, apc %p, %p, len 0x%x, off %lld) -> 0x%08x (", FileHandle, handle_desc(FileHandle).c_str(), Event,
           ApcRoutine, Buffer, Length, ByteOffset ? ByteOffset->QuadPart : -1LL, s);
    if (!NT_OK(s) && s != XSTATUS_PENDING && s != (NTSTATUS)0xC0000011 /* END_OF_FILE */) {
        MEMORY_BASIC_INFORMATION mi = {}; // a buffer too small for the file: where its memory ends
        VirtualQuery(Buffer, &mi, sizeof mi);
        logf("[k] NtReadFile('%s', len 0x%x) failed 0x%08x (buffer %p, region %p + 0x%zx, state 0x%x)", handle_desc(FileHandle).c_str(),
             Length, s, Buffer, mi.BaseAddress, mi.RegionSize, (unsigned)mi.State);
    }
    return s;
}

NTSTATUS __stdcall NtWriteFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext, XIO_STATUS_BLOCK *IoStatusBlock,
                               void *Buffer, uint32_t Length, LARGE_INTEGER *ByteOffset)
{
    NTSTATUS s = host::NtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, (host::IO_STATUS_BLOCK *)IoStatusBlock, Buffer, Length,
                                   ByteOffset, nullptr);
    KTRACE("%p '%s', ev %p, apc %p, %p, len 0x%x, off %lld) -> 0x%08x (", FileHandle, handle_desc(FileHandle).c_str(), Event,
           ApcRoutine, Buffer, Length, ByteOffset ? ByteOffset->QuadPart : -1LL, s);
    if (!NT_OK(s) && s != XSTATUS_PENDING) logf("[k] NtWriteFile('%s') failed 0x%08x", handle_desc(FileHandle).c_str(), s);
    return s;
}

NTSTATUS __stdcall NtQueryInformationFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock, void *FileInformation,
                                          uint32_t Length, uint32_t FileInformationClass)
{
    KTRACE("%p '%s', %p, %u, class %u", FileHandle, handle_desc(FileHandle).c_str(), FileInformation, Length, FileInformationClass);
    switch (FileInformationClass) {
    case 4: case 5: case 6: case 7: case 8: case 14: case 16: case 17: case 34: case 35: {
        NTSTATUS s = host::NtQueryInformationFile(FileHandle, (host::IO_STATUS_BLOCK *)IoStatusBlock, FileInformation, Length,
                                                  FileInformationClass);
        if (!NT_OK(s))
            logf("[k] NtQueryInformationFile('%s', class %u) failed 0x%08x", handle_desc(FileHandle).c_str(), FileInformationClass, s);
        return s;
    }
    case 9: { // FileNameInformation, 8-bit name relative to the volume
        HandleInfo hi;
        if (!handle_info(FileHandle, hi)) return XSTATUS_INVALID_HANDLE;
        std::string rel = hi.xbox_path.substr(hi.mount->device.size());
        if (rel.empty()) rel = "\\";
        if (Length < 4) return XSTATUS_BUFFER_TOO_SMALL;
        uint32_t n = (uint32_t)rel.size();
        *(uint32_t *)FileInformation = n;
        uint32_t copy = n < Length - 4 ? n : Length - 4;
        memcpy((uint8_t *)FileInformation + 4, rel.data(), copy);
        NTSTATUS s = copy < n ? (NTSTATUS)0x80000005 /* BUFFER_OVERFLOW */ : XSTATUS_SUCCESS;
        complete(IoStatusBlock, s, 4 + copy, nullptr);
        return s;
    }
    default:
        logf("[k] NtQueryInformationFile: class %u not implemented ('%s')", FileInformationClass, handle_desc(FileHandle).c_str());
        complete(IoStatusBlock, XSTATUS_INVALID_PARAMETER, 0, nullptr);
        return XSTATUS_INVALID_PARAMETER;
    }
}

NTSTATUS __stdcall NtSetInformationFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock, void *FileInformation,
                                        uint32_t Length, uint32_t FileInformationClass)
{
    KTRACE("%p '%s', %p, %u, class %u", FileHandle, handle_desc(FileHandle).c_str(), FileInformation, Length, FileInformationClass);
    switch (FileInformationClass) {
    case 4: case 13: case 14: case 19: case 20: {
        NTSTATUS s = host::NtSetInformationFile(FileHandle, (host::IO_STATUS_BLOCK *)IoStatusBlock, FileInformation, Length,
                                                FileInformationClass);
        if (!NT_OK(s))
            logf("[k] NtSetInformationFile('%s', class %u) failed 0x%08x", handle_desc(FileHandle).c_str(), FileInformationClass, s);
        return s;
    }
    case 10: { // FileRenameInformation: { BOOLEAN Replace; HANDLE Root; OBJECT_STRING Name; }
        uint8_t *p = (uint8_t *)FileInformation;
        Resolved r;
        NTSTATUS s = resolve(*(HANDLE *)(p + 4), (XANSI_STRING *)(p + 8), r);
        if (!NT_OK(s)) {
            complete(IoStatusBlock, s, 0, nullptr);
            return s;
        }
        std::vector<uint8_t> buf(12 + r.host_nt.size() * 2 + 2);
        buf[0] = p[0];
        *(uint32_t *)&buf[8] = (uint32_t)(r.host_nt.size() * 2);
        memcpy(&buf[12], r.host_nt.data(), r.host_nt.size() * 2);
        s = host::NtSetInformationFile(FileHandle, (host::IO_STATUS_BLOCK *)IoStatusBlock, buf.data(), (ULONG)buf.size(), 10);
        logf("[k] rename '%s' -> '%s': 0x%08x", handle_desc(FileHandle).c_str(), r.xbox_path.c_str(), s);
        if (NT_OK(s)) remember(FileHandle, r);
        return s;
    }
    default:
        logf("[k] NtSetInformationFile: class %u not implemented ('%s')", FileInformationClass, handle_desc(FileHandle).c_str());
        complete(IoStatusBlock, XSTATUS_INVALID_PARAMETER, 0, nullptr);
        return XSTATUS_INVALID_PARAMETER;
    }
}

NTSTATUS __stdcall NtQueryVolumeInformationFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock, void *FsInformation,
                                                uint32_t Length, uint32_t FsInformationClass)
{
    KTRACE("%p '%s', %p, %u, class %u", FileHandle, handle_desc(FileHandle).c_str(), FsInformation, Length, FsInformationClass);
    HandleInfo hi;
    bool known = handle_info(FileHandle, hi) != nullptr;
    int part = known ? hi.mount->partition : 0;
    uint8_t *p = (uint8_t *)FsInformation;
    if ((FsInformationClass == 3 || FsInformationClass == 7) && part >= 1 && part <= 5) {
        // FATX-like numbers: 16 KB clusters, capped at the partition size.
        uint64_t total = kPartitions[part].size;
        ULARGE_INTEGER free_host{};
        GetDiskFreeSpaceExW(hi.mount->host.c_str(), &free_host, nullptr, nullptr);
        uint64_t avail = free_host.QuadPart < total ? free_host.QuadPart : total;
        uint32_t need = FsInformationClass == 3 ? 24 : 32;
        if (Length < need) return XSTATUS_BUFFER_TOO_SMALL;
        *(uint64_t *)(p + 0) = total / 0x4000;
        *(uint64_t *)(p + 8) = avail / 0x4000;
        if (FsInformationClass == 7) *(uint64_t *)(p + 16) = avail / 0x4000;
        uint32_t o = FsInformationClass == 3 ? 16 : 24;
        *(uint32_t *)(p + o) = 32;      // SectorsPerAllocationUnit
        *(uint32_t *)(p + o + 4) = 512; // BytesPerSector
        complete(IoStatusBlock, XSTATUS_SUCCESS, need, nullptr);
        return XSTATUS_SUCCESS;
    }
    switch (FsInformationClass) {
    case 3: case 7:
        return host::NtQueryVolumeInformationFile(FileHandle, (host::IO_STATUS_BLOCK *)IoStatusBlock, FsInformation, Length,
                                                  FsInformationClass);
    case 1: { // FileFsVolumeInformation { LARGE_INTEGER Created; ULONG Serial; ULONG LabelLength; BOOLEAN SupportsObjects; CHAR Label[]; }
        if (Length < 18) return XSTATUS_BUFFER_TOO_SMALL;
        memset(p, 0, 18);
        *(uint32_t *)(p + 8) = 0x0B1AC000 + part;
        complete(IoStatusBlock, XSTATUS_SUCCESS, 18, nullptr);
        return XSTATUS_SUCCESS;
    }
    case 5: { // FileFsAttributeInformation { ULONG Attributes; LONG MaxNameLength; ULONG NameLength; CHAR Name[]; }
        const char *fs = part >= 0 ? "FATX" : "UDF";
        uint32_t n = (uint32_t)strlen(fs);
        if (Length < 12 + n) return XSTATUS_BUFFER_TOO_SMALL;
        *(uint32_t *)(p + 0) = 0;
        *(uint32_t *)(p + 4) = 42;
        *(uint32_t *)(p + 8) = n;
        memcpy(p + 12, fs, n);
        complete(IoStatusBlock, XSTATUS_SUCCESS, 12 + n, nullptr);
        return XSTATUS_SUCCESS;
    }
    default:
        logf("[k] NtQueryVolumeInformationFile: class %u not implemented ('%s')", FsInformationClass,
             handle_desc(FileHandle).c_str());
        complete(IoStatusBlock, XSTATUS_INVALID_PARAMETER, 0, nullptr);
        return XSTATUS_INVALID_PARAMETER;
    }
}

NTSTATUS __stdcall NtQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext,
                                        XIO_STATUS_BLOCK *IoStatusBlock, void *FileInformation, uint32_t Length,
                                        uint32_t FileInformationClass, XANSI_STRING *FileName, BOOLEAN RestartScan)
{
    std::string mask = FileName ? xstr(FileName) : std::string();
    KTRACE("%p '%s', ev %p, apc %p, %p, %u, class %u, mask '%s', restart %u", FileHandle, handle_desc(FileHandle).c_str(), Event,
           ApcRoutine, FileInformation, Length, FileInformationClass, mask.c_str(), RestartScan);
    if (FileInformationClass != 1) {
        logf("[k] NtQueryDirectoryFile: class %u not implemented", FileInformationClass);
        complete(IoStatusBlock, XSTATUS_INVALID_PARAMETER, 0, Event);
        return XSTATUS_INVALID_PARAMETER;
    }
    if (ApcRoutine) KNOTE_ONCE("NtQueryDirectoryFile with an APC routine: completed synchronously, APC not queued");
    std::wstring wmask = ansi_to_wide(mask.data(), mask.size());
    host::UNICODE_STRING us{(USHORT)(wmask.size() * 2), (USHORT)(wmask.size() * 2), wmask.data()};
    alignas(8) uint8_t buf[sizeof(FILE_ID_BOTH_DIR_INFO) + 1024];
	BOOLEAN restart = RestartScan;
	for (;;) {
		host::IO_STATUS_BLOCK hio{};
		NTSTATUS s = host::NtQueryDirectoryFile(
			FileHandle, nullptr, nullptr, nullptr,
			&hio, buf, sizeof buf, 1, TRUE,
			wmask.empty() ? nullptr : &us, restart);

		if (s == XSTATUS_PENDING) {
			WaitForSingleObject(FileHandle, INFINITE);
			s = hio.Status;
		}

		restart = FALSE;
		if (!NT_OK(s)) {
			complete(IoStatusBlock, s, 0, Event);
			return s;
		}
        // Host FILE_DIRECTORY_INFORMATION: same fields, WCHAR name at offset 64.
        uint32_t name_bytes = *(uint32_t *)(buf + 60);
        const wchar_t *wname = (const wchar_t *)(buf + 64);
        uint32_t nchars = name_bytes / 2;
        if ((nchars == 1 && wname[0] == L'.') || (nchars == 2 && wname[0] == L'.' && wname[1] == L'.')) continue;
        uint32_t need = 64 + nchars;
        if (Length < 64) {
            complete(IoStatusBlock, XSTATUS_BUFFER_TOO_SMALL, 0, Event);
            return XSTATUS_BUFFER_TOO_SMALL;
        }
        uint8_t *out = (uint8_t *)FileInformation;
        memcpy(out, buf, 60);
        *(uint32_t *)out = 0; // NextEntryOffset: one entry per call
        *(uint32_t *)(out + 60) = nchars;
        uint32_t copy = need <= Length ? nchars : Length - 64;
        for (uint32_t i = 0; i < copy; i++) out[64 + i] = wname[i] < 256 ? (char)wname[i] : '_';
        NTSTATUS rs = copy < nchars ? (NTSTATUS)0x80000005 /* BUFFER_OVERFLOW */ : XSTATUS_SUCCESS;
        complete(IoStatusBlock, rs, 64 + copy, Event);
        return rs;
    }
}

NTSTATUS __stdcall NtQueryFullAttributesFile(XOBJECT_ATTRIBUTES *ObjectAttributes, XFILE_NETWORK_OPEN_INFORMATION *FileInformation)
{
    std::string req = ObjectAttributes && ObjectAttributes->ObjectName ? xstr(ObjectAttributes->ObjectName) : std::string();
    Resolved r;
    NTSTATUS s = resolve(ObjectAttributes ? ObjectAttributes->RootDirectory : nullptr,
                         ObjectAttributes ? ObjectAttributes->ObjectName : nullptr, r);
    if (NT_OK(s)) {
        host::UNICODE_STRING us{(USHORT)(r.host_nt.size() * 2), (USHORT)(r.host_nt.size() * 2), r.host_nt.data()};
        host::OBJECT_ATTRIBUTES hoa;
        host::init_oa(hoa, &us);
        s = host::NtQueryFullAttributesFile(&hoa, FileInformation);
    }
    logf("[k] NtQueryFullAttributesFile('%s' -> %s) -> 0x%08x", req.c_str(), r.xbox_path.c_str(), s);
    return s;
}

NTSTATUS __stdcall NtFlushBuffersFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock)
{
    KTRACE("%p", FileHandle);
    return host::NtFlushBuffersFile(FileHandle, (host::IO_STATUS_BLOCK *)IoStatusBlock);
}

NTSTATUS __stdcall NtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext,
                                         XIO_STATUS_BLOCK *IoStatusBlock, uint32_t IoControlCode, void *InputBuffer,
                                         uint32_t InputBufferLength, void *OutputBuffer, uint32_t OutputBufferLength)
{
    HandleInfo hi;
    bool known = handle_info(FileHandle, hi) != nullptr;
    int part = known ? hi.mount->partition : -1;
    uint8_t *out = (uint8_t *)OutputBuffer;
    NTSTATUS s = XSTATUS_INVALID_DEVICE_REQUEST;
    uint32_t info = 0;
    if (part >= 0 && part <= 5) {
        const PartitionGeom &pg = kPartitions[part];
        if (IoControlCode == 0x70000 && OutputBufferLength >= 24) { // IOCTL_DISK_GET_DRIVE_GEOMETRY
            memset(out, 0, 24);
            *(uint64_t *)(out + 0) = pg.size / 512; // Cylinders
            *(uint32_t *)(out + 8) = 12;            // FixedMedia
            *(uint32_t *)(out + 12) = 1;            // TracksPerCylinder
            *(uint32_t *)(out + 16) = 1;            // SectorsPerTrack
            *(uint32_t *)(out + 20) = 512;          // BytesPerSector
            s = XSTATUS_SUCCESS;
            info = 24;
        } else if (IoControlCode == 0x74004 && OutputBufferLength >= 32) { // IOCTL_DISK_GET_PARTITION_INFO
            memset(out, 0, 32);
            *(uint64_t *)(out + 0) = pg.start;
            *(uint64_t *)(out + 8) = pg.size;
            *(uint32_t *)(out + 16) = 0;          // HiddenSectors
            *(uint32_t *)(out + 20) = (uint32_t)part;
            out[24] = 0;                          // PartitionType
            out[26] = 1;                          // RecognizedPartition
            s = XSTATUS_SUCCESS;
            info = 32;
        }
    }
    logf("[k] NtDeviceIoControlFile('%s', ioctl 0x%x, in %p/%u, out %p/%u) -> 0x%08x%s", known ? hi.xbox_path.c_str() : "?",
         IoControlCode, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, s,
         s == XSTATUS_INVALID_DEVICE_REQUEST ? "  <not implemented>" : "");
    if (ApcRoutine) KNOTE_ONCE("NtDeviceIoControlFile with an APC routine: completed synchronously, APC not queued");
    complete(IoStatusBlock, s, info, Event);
    return s;
}

NTSTATUS __stdcall NtFsControlFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext,
                                   XIO_STATUS_BLOCK *IoStatusBlock, uint32_t FsControlCode, void *InputBuffer,
                                   uint32_t InputBufferLength, void *OutputBuffer, uint32_t OutputBufferLength)
{
    NTSTATUS s = FsControlCode == 0x90020 /* FSCTL_DISMOUNT_VOLUME */ ? XSTATUS_SUCCESS : XSTATUS_INVALID_DEVICE_REQUEST;
    logf("[k] NtFsControlFile('%s', fsctl 0x%x, in %u, out %u) -> 0x%08x%s", handle_desc(FileHandle).c_str(), FsControlCode,
         InputBufferLength, OutputBufferLength, s, NT_OK(s) ? "" : "  <not implemented>");
    complete(IoStatusBlock, s, 0, Event);
    return s;
}

NTSTATUS __stdcall IoCreateSymbolicLink(XANSI_STRING *SymbolicLinkName, XANSI_STRING *DeviceName)
{
    std::string name = xstr(SymbolicLinkName), target = xstr(DeviceName);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    std::string key = lower(name);
    NTSTATUS s = XSTATUS_SUCCESS;
    if (g_links.count(key))
        s = XSTATUS_OBJECT_NAME_COLLISION;
    else
        g_links[key] = target;
    logf("[k] IoCreateSymbolicLink('%s' -> '%s') -> 0x%08x", name.c_str(), target.c_str(), s);
    return s;
}

NTSTATUS __stdcall IoDeleteSymbolicLink(XANSI_STRING *SymbolicLinkName)
{
    std::string name = xstr(SymbolicLinkName);
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    NTSTATUS s = g_links.erase(lower(name)) ? XSTATUS_SUCCESS : XSTATUS_OBJECT_NAME_NOT_FOUND;
    logf("[k] IoDeleteSymbolicLink('%s') -> 0x%08x", name.c_str(), s);
    return s;
}

NTSTATUS __stdcall NtOpenSymbolicLinkObject(HANDLE *LinkHandle, XOBJECT_ATTRIBUTES *ObjectAttributes)
{
    std::string name = ObjectAttributes ? xstr(ObjectAttributes->ObjectName) : std::string();
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    auto it = g_links.find(lower(name));
    NTSTATUS s = XSTATUS_OBJECT_NAME_NOT_FOUND;
    if (it != g_links.end()) {
        HANDLE h = (HANDLE)(uintptr_t)g_next_link_handle;
        g_next_link_handle += 4;
        g_link_handles[h] = LinkObject{name, it->second};
        *LinkHandle = h;
        s = XSTATUS_SUCCESS;
    }
    logf("[k] NtOpenSymbolicLinkObject('%s') -> 0x%08x", name.c_str(), s);
    return s;
}

NTSTATUS __stdcall NtQuerySymbolicLinkObject(HANDLE LinkHandle, XANSI_STRING *LinkTarget, uint32_t *ReturnedLength)
{
    std::lock_guard<std::recursive_mutex> g(g_mutex);
    auto it = g_link_handles.find(LinkHandle);
    if (it == g_link_handles.end()) return XSTATUS_INVALID_HANDLE;
    const std::string &t = it->second.target;
    KTRACE("%p '%s' -> '%s'", LinkHandle, it->second.name.c_str(), t.c_str());
    if (ReturnedLength) *ReturnedLength = (uint32_t)t.size();
    if (LinkTarget->MaximumLength < t.size()) return XSTATUS_BUFFER_TOO_SMALL;
    memcpy(LinkTarget->Buffer, t.data(), t.size());
    LinkTarget->Length = (uint16_t)t.size();
    return XSTATUS_SUCCESS;
}

} // namespace xk

std::wstring host_full_path(const std::wstring &p) { return full_path(p); }
