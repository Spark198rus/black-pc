// Xbox kernel types used by the HLE kernel. Layouts follow the Xbox (NT-derived) kernel ABI as publicly
// documented by the homebrew community (e.g. nxdk headers); written for this project, no code copied.
// Names are prefixed X to avoid clashing with Windows SDK types.
#pragma once

#include "common.h"

#pragma pack(push, 4)

struct XANSI_STRING { // OBJECT_STRING / STRING / ANSI_STRING
    uint16_t Length;
    uint16_t MaximumLength;
    char *Buffer;
};

struct XUNICODE_STRING {
    uint16_t Length;
    uint16_t MaximumLength;
    wchar_t *Buffer;
};

struct XOBJECT_ATTRIBUTES {
    HANDLE RootDirectory;
    XANSI_STRING *ObjectName;
    uint32_t Attributes;
};

struct XIO_STATUS_BLOCK {
    union {
        NTSTATUS Status;
        void *Pointer;
    };
    uint32_t Information;
};

struct XLIST_ENTRY {
    XLIST_ENTRY *Flink, *Blink;
};

struct XDISPATCHER_HEADER {
    uint8_t Type;
    uint8_t Absolute;
    uint8_t Size;
    uint8_t Inserted;
    int32_t SignalState;
    XLIST_ENTRY WaitListHead;
};
static_assert(sizeof(XDISPATCHER_HEADER) == 16, "");

enum : uint8_t {
    XOBJ_EVENT_NOTIFICATION = 0,
    XOBJ_EVENT_SYNCHRONIZATION = 1,
    XOBJ_MUTANT = 2,
    XOBJ_PROCESS = 3,
    XOBJ_QUEUE = 4,
    XOBJ_SEMAPHORE = 5,
    XOBJ_THREAD = 6,
    XOBJ_TIMER_NOTIFICATION = 8,
    XOBJ_TIMER_SYNCHRONIZATION = 9,
    XOBJ_DPC = 19,
};

struct XKEVENT {
    XDISPATCHER_HEADER Header;
};

struct XKSEMAPHORE {
    XDISPATCHER_HEADER Header;
    int32_t Limit;
};

struct XKDPC;
typedef void(__stdcall *XKDEFERRED_ROUTINE)(XKDPC *Dpc, void *DeferredContext, void *SystemArgument1, void *SystemArgument2);

struct XKDPC {
    int16_t Type;
    uint8_t Inserted;
    uint8_t Padding;
    XLIST_ENTRY DpcListEntry;
    XKDEFERRED_ROUTINE DeferredRoutine;
    void *DeferredContext;
    void *SystemArgument1;
    void *SystemArgument2;
};
static_assert(sizeof(XKDPC) == 28, "");

struct XKTIMER {
    XDISPATCHER_HEADER Header;
    ULARGE_INTEGER DueTime;
    XLIST_ENTRY TimerListEntry;
    XKDPC *Dpc;
    int32_t Period;
};
static_assert(sizeof(XKTIMER) == 40, "");

struct XKINTERRUPT {
    void *ServiceRoutine;
    void *ServiceContext;
    uint32_t BusInterruptLevel;
    uint32_t Irql;
    uint8_t Connected;
    uint8_t ShareVector;
    uint8_t Mode;
    uint8_t Padding;
    uint32_t ServiceCount;
    uint32_t DispatchCode[22];
};

struct XRTL_CRITICAL_SECTION {
    XDISPATCHER_HEADER Synchronization;
    int32_t LockCount;
    int32_t RecursionCount;
    void *OwningThread;
};
static_assert(sizeof(XRTL_CRITICAL_SECTION) == 28, "");

struct XFILE_NETWORK_OPEN_INFORMATION {
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER AllocationSize, EndOfFile;
    uint32_t FileAttributes;
    uint32_t Reserved;
};

// FILE_DIRECTORY_INFORMATION with an 8-bit name (Xbox uses OCHAR = char).
struct XFILE_DIRECTORY_INFORMATION {
    uint32_t NextEntryOffset;
    uint32_t FileIndex;
    LARGE_INTEGER CreationTime, LastAccessTime, LastWriteTime, ChangeTime;
    LARGE_INTEGER EndOfFile, AllocationSize;
    uint32_t FileAttributes;
    uint32_t FileNameLength;
    char FileName[1];
};

struct XLAUNCH_DATA_PAGE {
    uint8_t Header[0x1000];
};

struct XXBOX_HARDWARE_INFO {
    uint32_t Flags;
    uint8_t GpuRevision;
    uint8_t McpRevision;
    uint8_t Unknown3;
    uint8_t Unknown4;
};

struct XXBOX_KRNL_VERSION {
    uint16_t Major, Minor, Build, Qfe;
};

#pragma pack(pop)

typedef void(__stdcall *XPKSTART_ROUTINE)(void *StartContext);
typedef void(__stdcall *XPKSYSTEM_ROUTINE)(XPKSTART_ROUTINE StartRoutine, void *StartContext);

// --- HLE kernel internals shared between k_*.cpp ---

// Xbox ANSI string helpers
std::string xstr(const XANSI_STRING *s);

// Path translation (k_file.cpp)
bool kfile_init();
void kfile_add_symlink(const std::string &name, const std::string &target);

// Contiguous memory (k_memory.cpp)
bool kmem_init();
constexpr uint32_t CONTIGUOUS_BASE = 0x80000000;
constexpr uint32_t CONTIGUOUS_SIZE = 64u << 20;

// Kernel timers / DPCs (k_dpc.cpp)
bool kdpc_init();
bool kdpc_insert(XKDPC *dpc, void *arg1, void *arg2);

// Dispatcher-object waits (k_sync.cpp)
NTSTATUS ksync_wait_object(void *object, bool alertable, const LARGE_INTEGER *timeout);
void ksync_signal_object(void *object);

// Threads (k_thread.cpp)
NTSTATUS kthread_create(HANDLE *out_handle, DWORD *out_tid, uint32_t stack_size, uint32_t tls_size, uint32_t start_routine,
                        uint32_t start_context, uint32_t system_routine, bool suspended, const char *name,
                        bool cdecl_entry = false);

// Image sections (image.cpp)
NTSTATUS image_load_section(struct XbeSectionHeader *section);
NTSTATUS image_unload_section(struct XbeSectionHeader *section);

// Time
#ifdef _WIN32
inline int64_t ktime_now_100ns()
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}
#else
namespace xposix { int64_t ktime_now_100ns(); }
inline int64_t ktime_now_100ns()
{
    return xposix::ktime_now_100ns();
}
#endif

// Converts an NT timeout (negative = relative 100ns units, positive = absolute) to milliseconds.
inline DWORD ktimeout_ms(const LARGE_INTEGER *timeout)
{
    if (!timeout) return INFINITE;
    int64_t t = timeout->QuadPart;
    if (t < 0) {
        int64_t ms = (-t + 9999) / 10000;
        return ms >= 0xFFFFFFFELL ? 0xFFFFFFFE : (DWORD)ms;
    }
    int64_t now = ktime_now_100ns();
    if (t <= now) return 0;
    int64_t ms = (t - now + 9999) / 10000;
    return ms >= 0xFFFFFFFELL ? 0xFFFFFFFE : (DWORD)ms;
}
