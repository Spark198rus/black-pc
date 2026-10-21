// POSIX Xbox Kernel Exports Resolver and Dynamic Thunk Dispatcher
#include "k_exports_posix.h"

#include "k_file_posix.h"
#include "k_memory_posix.h"
#include "k_sync_posix.h"
#include "k_thread_posix.h"
#include "k_timing_posix.h"
#include "loader/src/sha1.h"

#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sched.h>
#include <sys/mman.h>
#include <unistd.h>

namespace xposix {

namespace {

enum KConv : uint8_t { K_STD, K_FAST, K_CDECL, K_VAR };

struct KExport {
    uint16_t ordinal;
    const char *name;
    KConv conv;
    uint8_t argc;
};

#define F(ord, name, argc) {ord, #name, K_STD, argc}
#define FF(ord, name, argc) {ord, #name, K_FAST, argc}
#define FC(ord, name) {ord, #name, K_CDECL, 0}
#define V(ord, name) {ord, #name, K_VAR, 0}

const KExport kExports[] = {
    F(1, AvGetSavedDataAddress, 0),
    F(2, AvSendTVEncoderOption, 4),
    F(3, AvSetDisplayMode, 6),
    F(4, AvSetSavedDataAddress, 1),
    FC(8, DbgPrint),
    F(15, ExAllocatePoolWithTag, 2),
    V(16, ExEventObjectType),
    F(17, ExFreePool, 1),
    F(23, ExQueryPoolBlockSize, 1),
    F(24, ExQueryNonVolatileSetting, 5),
    V(40, HalDiskCachePartitionCount),
    F(44, HalGetInterruptVector, 2),
    F(46, HalReadWritePCISpace, 6),
    F(47, HalRegisterShutdownNotification, 2),
    F(49, HalReturnToFirmware, 1),
    F(67, IoCreateSymbolicLink, 2),
    F(69, IoDeleteSymbolicLink, 1),
    V(71, IoFileObjectType),
    FF(87, IofCompleteRequest, 2),
    F(95, KeBugCheck, 1),
    F(97, KeCancelTimer, 1),
    F(98, KeConnectInterrupt, 1),
    F(99, KeDelayExecutionThread, 3),
    F(100, KeDisconnectInterrupt, 1),
    F(107, KeInitializeDpc, 3),
    F(109, KeInitializeInterrupt, 7),
    F(113, KeInitializeTimerEx, 2),
    F(119, KeInsertQueueDpc, 3),
    F(124, KeQueryBasePriorityThread, 1),
    F(128, KeQuerySystemTime, 1),
    F(129, KeRaiseIrqlToDpcLevel, 0),
    F(137, KeRemoveQueueDpc, 1),
    F(139, KeRestoreFloatingPointState, 1),
    F(142, KeSaveFloatingPointState, 1),
    F(143, KeSetBasePriorityThread, 2),
    F(145, KeSetEvent, 3),
    F(149, KeSetTimer, 4),
    F(151, KeStallExecutionProcessor, 1),
    F(153, KeSynchronizeExecution, 3),
    F(159, KeWaitForSingleObject, 5),
    FF(160, KfRaiseIrql, 1),
    FF(161, KfLowerIrql, 1),
    V(164, LaunchDataPage),
    F(165, MmAllocateContiguousMemory, 1),
    F(166, MmAllocateContiguousMemoryEx, 5),
    F(168, MmClaimGpuInstanceMemory, 2),
    F(171, MmFreeContiguousMemory, 1),
    F(173, MmGetPhysicalAddress, 1),
    F(175, MmLockUnlockBufferPages, 3),
    F(176, MmLockUnlockPhysicalPage, 2),
    F(178, MmPersistContiguousMemory, 3),
    F(179, MmQueryAddressProtect, 1),
    F(180, MmQueryAllocationSize, 1),
    F(182, MmSetAddressProtect, 3),
    F(184, NtAllocateVirtualMemory, 5),
    F(187, NtClose, 1),
    F(189, NtCreateEvent, 4),
    F(190, NtCreateFile, 9),
    F(193, NtCreateSemaphore, 4),
    F(196, NtDeviceIoControlFile, 10),
    F(197, NtDuplicateObject, 3),
    F(198, NtFlushBuffersFile, 2),
    F(199, NtFreeVirtualMemory, 3),
    F(200, NtFsControlFile, 10),
    F(202, NtOpenFile, 6),
    F(203, NtOpenSymbolicLinkObject, 2),
    F(207, NtQueryDirectoryFile, 10),
    F(210, NtQueryFullAttributesFile, 2),
    F(211, NtQueryInformationFile, 5),
    F(215, NtQuerySymbolicLinkObject, 3),
    F(217, NtQueryVirtualMemory, 2),
    F(218, NtQueryVolumeInformationFile, 5),
    F(219, NtReadFile, 8),
    F(222, NtReleaseSemaphore, 3),
    F(224, NtResumeThread, 2),
    F(225, NtSetEvent, 2),
    F(226, NtSetInformationFile, 5),
    F(231, NtSuspendThread, 2),
    F(233, NtWaitForSingleObject, 3),
    F(234, NtWaitForSingleObjectEx, 4),
    F(236, NtWriteFile, 8),
    F(238, NtYieldExecution, 0),
    F(246, ObReferenceObjectByHandle, 3),
    FF(250, ObfDereferenceObject, 1),
    F(255, PsCreateSystemThreadEx, 10),
    F(258, PsTerminateSystemThread, 1),
    V(259, PsThreadObjectType),
    F(260, RtlAnsiStringToUnicodeString, 3),
    F(269, RtlCompareMemoryUlong, 3),
    F(277, RtlEnterCriticalSection, 1),
    F(279, RtlEqualString, 3),
    F(289, RtlInitAnsiString, 2),
    F(291, RtlInitializeCriticalSection, 1),
    F(294, RtlLeaveCriticalSection, 1),
    F(301, RtlNtStatusToDosError, 1),
    F(302, RtlRaiseException, 1),
    F(304, RtlTimeFieldsToTime, 2),
    F(305, RtlTimeToTimeFields, 2),
    F(308, RtlUnicodeStringToAnsiString, 3),
    F(312, RtlUnwind, 4),
    V(322, XboxHardwareInfo),
    V(323, XboxHDKey),
    V(324, XboxKrnlVersion),
    V(325, XboxSignatureKey),
    V(326, XeImageFileName),
    F(327, XeLoadSection, 1),
    F(328, XeUnloadSection, 1),
    F(335, XcSHAInit, 1),
    F(336, XcSHAUpdate, 3),
    F(337, XcSHAFinal, 2),
    F(340, XcHMAC, 7),
    V(354, XboxAlternateSignatureKeys),
    V(356, HalBootSMCVideoMode),
    V(357, IdexChannelObject),
    F(360, HalInitiateShutdown, 0),
};

const KExport *find_export(uint32_t ordinal)
{
    for (const auto &e : kExports) {
        if (e.ordinal == ordinal) return &e;
    }
    return nullptr;
}

// Data exports
uint8_t g_ExEventObjectType[64] = {0};
uint8_t g_IoFileObjectType[64] = {0};
uint8_t g_PsThreadObjectType[64] = {0};
uint32_t g_HalDiskCachePartitionCount = 3;
uint32_t g_HalBootSMCVideoMode = 0;
void *g_LaunchDataPage = nullptr;

struct {
    uint32_t Flags;
    uint8_t  GpuRevision;
    uint8_t  McpxRevision;
    uint8_t  Reserved[2];
} g_XboxHardwareInfo = {0, 0xA2, 0xD3, {0, 0}};

uint8_t g_XboxHDKey[16] = {0};
struct {
    uint16_t Major;
    uint16_t Minor;
    uint16_t Build;
    uint16_t Qfe;
} g_XboxKrnlVersion = {1, 0, 5838, 1};

uint8_t g_XboxSignatureKey[16] = {0};
uint8_t g_XboxAlternateSignatureKeys[16][16] = {{0}};
char g_image_file_name[] = "\\Device\\CdRom0\\default.xbe";

#pragma pack(push, 1)
struct XANSI_STRING {
    uint16_t Length;
    uint16_t MaximumLength;
    char    *Buffer;
};

struct XUNICODE_STRING {
    uint16_t Length;
    uint16_t MaximumLength;
    uint16_t *Buffer;
};

struct XOBJECT_ATTRIBUTES {
    void         *RootDirectory;
    XANSI_STRING *ObjectName;
    uint32_t      Attributes;
};

struct XTIME_FIELDS {
    int16_t Year;
    int16_t Month;
    int16_t Day;
    int16_t Hour;
    int16_t Minute;
    int16_t Second;
    int16_t Milliseconds;
    int16_t Weekday;
};
#pragma pack(pop)

XANSI_STRING g_XeImageFileName = {
    (uint16_t)(sizeof(g_image_file_name) - 1),
    (uint16_t)sizeof(g_image_file_name),
    g_image_file_name
};

uint8_t g_IdexChannelObject[256] = {0};
void *g_av_saved_data = nullptr;

std::atomic<uint32_t> g_call_counts[400]{};
bool g_trace_all = false;
uint8_t *g_stub_page = nullptr;
size_t g_stub_used = 0;

} // namespace

void exports_init()
{
    if (!g_stub_page) {
        g_stub_page = (uint8_t *)mmap(nullptr, 0x10000,
                                      PROT_READ | PROT_WRITE | PROT_EXEC,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(g_stub_page != MAP_FAILED && "Failed to allocate executable stub page");
    }
}

const char *kposix_export_name(uint32_t ordinal)
{
    const KExport *e = find_export(ordinal);
    return e ? e->name : "UnknownExport";
}

uint32_t kposix_get_call_count(uint32_t ordinal)
{
    return (ordinal < 400) ? g_call_counts[ordinal].load() : 0;
}

void kposix_set_trace(bool enable)
{
    g_trace_all = enable;
}

// Forward declare assembly stub_common
extern "C" void stub_common();

extern "C" uint64_t __attribute__((stdcall)) kposix_stub_called(uint32_t ordinal, const uint32_t *args, uint32_t ecx, uint32_t edx)
{
    (void)ecx; (void)edx;
    const KExport *e = find_export(ordinal);
    uint32_t n = (ordinal < 400) ? ++g_call_counts[ordinal] : 1;

    if (g_trace_all || n == 1) {
        printf("  [KRNL DISPATCH] Ordinal %3u (%-28s) Call #%u\n",
               ordinal, e ? e->name : "Unknown", n);
    }

    uint32_t ret_val = 0;

    switch (ordinal) {
    // --- AV & Settings ---
    case 1: // AvGetSavedDataAddress
        ret_val = (uint32_t)(uintptr_t)g_av_saved_data;
        break;
    case 2: // AvSendTVEncoderOption
        if (args[3]) {
            uint32_t *res = (uint32_t *)(uintptr_t)args[3];
            *res = (args[1] == 6) ? (0x00000100 | 0x00000001) : 0;
        }
        ret_val = 0;
        break;
    case 3: // AvSetDisplayMode
        ret_val = 0;
        break;
    case 4: // AvSetSavedDataAddress
        g_av_saved_data = (void *)(uintptr_t)args[0];
        ret_val = 0;
        break;
    case 8: // DbgPrint
        if (args[0]) {
            printf("[DbgPrint] %s\n", (const char *)(uintptr_t)args[0]);
        }
        ret_val = 0;
        break;
    case 24: // ExQueryNonVolatileSetting
        {
            uint32_t val_idx = args[0];
            uint32_t *type_ptr = (uint32_t *)(uintptr_t)args[1];
            void *val_buf = (void *)(uintptr_t)args[2];
            uint32_t val_len = args[3];
            uint32_t *res_len = (uint32_t *)(uintptr_t)args[4];

            uint32_t dword = 0, size = 4, type = 4;
            bool known = true;

            switch (val_idx) {
            case 0x000: dword = 0; break;          // XC_TIMEZONE_BIAS
            case 0x001: case 0x004: type = 3; break;
            case 0x002: case 0x005: type = 3; break;
            case 0x003: case 0x006: dword = 0; break;
            case 0x007: dword = 1; break;          // XC_LANGUAGE: English
            case 0x008: dword = 0x00010000; break; // XC_VIDEO: Widescreen
            case 0x009: dword = 0; break;          // XC_AUDIO: Stereo
            case 0x00A: case 0x00B: case 0x00C: case 0x011: dword = 0; break;
            case 0x012: dword = 1; break;          // XC_DVD_REGION: 1
            case 0x103: dword = 0x00400100; break; // XC_FACTORY_AV_REGION: NTSC-M 60Hz
            case 0x104: dword = 1; break;          // XC_FACTORY_GAME_REGION: North America
            case 0x0FF: case 0xFFFF: type = 3; size = (val_len < 256) ? val_len : 256; break;
            default: known = false; break;
            }

            if (!known) {
                ret_val = 0xC0000034L; // STATUS_OBJECT_NAME_NOT_FOUND
            } else if (val_len < size) {
                ret_val = 0xC0000023L; // STATUS_BUFFER_TOO_SMALL
            } else {
                if (type_ptr) *type_ptr = type;
                memset(val_buf, 0, size);
                if (type == 4) memcpy(val_buf, &dword, 4);
                if (res_len) *res_len = size;
                ret_val = 0; // STATUS_SUCCESS
            }
        }
        break;

    // --- HAL ---
    case 44: // HalGetInterruptVector
        if (args[1]) {
            uint8_t *irql = (uint8_t *)(uintptr_t)args[1];
            *irql = (args[0] < 27) ? (27 - args[0]) : 1;
        }
        ret_val = args[0] + 0x30;
        break;
    case 46: // HalReadWritePCISpace
        if (!args[5] && args[3]) { // Read
            memset((void *)(uintptr_t)args[3], 0, args[4]);
        }
        ret_val = 0;
        break;
    case 47: // HalRegisterShutdownNotification
        ret_val = 0;
        break;
    case 49: // HalReturnToFirmware
    case 360: // HalInitiateShutdown
        fprintf(stderr, "\n[FATAL KERNEL HALT] HalReturnToFirmware/Shutdown called! Routine=%u (0x%X), caller EBP=%p\n",
                args[0], args[0], __builtin_frame_address(0));
        fprintf(stderr, "A execução do jogo foi abortada pelo kernel do Xbox devido a um erro fatal ou requisição de desligamento.\n");
        _exit(99);
        ret_val = 0;
        break;

    // --- Memory ---
    case 15: // ExAllocatePoolWithTag
        ret_val = (uint32_t)(uintptr_t)ExAllocatePoolWithTag(0, args[0], args[1]);
        break;
    case 17: // ExFreePool
        ExFreePool((void *)(uintptr_t)args[0]);
        ret_val = 0;
        break;
    case 23: // ExQueryPoolBlockSize
        if (args[0]) {
            uint8_t *p = (uint8_t *)(uintptr_t)args[0] - 16;
            ret_val = *(uint32_t *)p;
        } else {
            ret_val = 0;
        }
        break;
    case 165: // MmAllocateContiguousMemory
        ret_val = (uint32_t)(uintptr_t)MmAllocateContiguousMemory(args[0]);
        break;
    case 166: // MmAllocateContiguousMemoryEx
        ret_val = (uint32_t)(uintptr_t)MmAllocateContiguousMemoryEx(args[0], args[1], args[2], args[3], args[4]);
        break;
    case 168: // MmClaimGpuInstanceMemory
        ret_val = (uint32_t)(uintptr_t)MmAllocateContiguousMemory(args[0]);
        if (args[1]) *(uint32_t *)(uintptr_t)args[1] = 0;
        break;
    case 171: // MmFreeContiguousMemory
        MmFreeContiguousMemory((void *)(uintptr_t)args[0]);
        ret_val = 0;
        break;
    case 173: // MmGetPhysicalAddress
        ret_val = MmGetPhysicalAddress((void *)(uintptr_t)args[0]);
        break;
    case 175: // MmLockUnlockBufferPages
    case 176: // MmLockUnlockPhysicalPage
    case 178: // MmPersistContiguousMemory
        ret_val = 0;
        break;
    case 179: // MmQueryAddressProtect
        ret_val = 0x04; // PAGE_READWRITE
        break;
    case 180: // MmQueryAllocationSize
        ret_val = 0x1000;
        break;
    case 182: // MmSetAddressProtect
        ret_val = 0;
        break;
    case 184: // NtAllocateVirtualMemory
        ret_val = (uint32_t)NtAllocateVirtualMemory((void **)(uintptr_t)args[0], args[1],
                                                    (uint32_t *)(uintptr_t)args[2], args[3], args[4]);
        break;
    case 199: // NtFreeVirtualMemory
        ret_val = (uint32_t)NtFreeVirtualMemory((void **)(uintptr_t)args[0], (uint32_t *)(uintptr_t)args[1], args[2]);
        break;
    case 217: // NtQueryVirtualMemory
        ret_val = 0;
        break;

    // --- Synchronization & Timing ---
    case 97: // KeCancelTimer
    case 100: // KeDisconnectInterrupt
        ret_val = 1;
        break;
    case 98: // KeConnectInterrupt
        ret_val = 1;
        break;
    case 99: // KeDelayExecutionThread
        if (args[2]) {
            int64_t interval = *(int64_t *)(uintptr_t)args[2];
            if (interval < 0) { // relative 100ns units
                usleep((useconds_t)(-interval / 10));
            }
        }
        ret_val = 0;
        break;
    case 107: // KeInitializeDpc
    case 109: // KeInitializeInterrupt
    case 113: // KeInitializeTimerEx
    case 119: // KeInsertQueueDpc
    case 137: // KeRemoveQueueDpc
        ret_val = 1;
        break;
    case 128: // KeQuerySystemTime
        if (args[0]) {
            *(int64_t *)(uintptr_t)args[0] = ktime_now_100ns();
        }
        ret_val = 0;
        break;
    case 129: // KeRaiseIrqlToDpcLevel
    case 160: // KfRaiseIrql
        ret_val = 2; // DISPATCH_LEVEL
        break;
    case 161: // KfLowerIrql
        ret_val = 0;
        break;
    case 139: // KeRestoreFloatingPointState
    case 142: // KeSaveFloatingPointState
        ret_val = 0;
        break;
    case 145: // KeSetEvent
        ret_val = (uint32_t)KeSetEvent((XDISPATCHER_HEADER *)(uintptr_t)args[0], (int32_t)args[1], (bool)args[2]);
        break;
    case 149: // KeSetTimer
        ret_val = 0;
        break;
    case 151: // KeStallExecutionProcessor
        usleep(args[0]);
        ret_val = 0;
        break;
    case 153: // KeSynchronizeExecution
        if (args[1]) {
            typedef uint32_t (__stdcall *SyncFn)(void *ctx);
            ret_val = ((SyncFn)(uintptr_t)args[1])((void *)(uintptr_t)args[2]);
        }
        break;
    case 159: // KeWaitForSingleObject
        ret_val = (uint32_t)KeWaitForSingleObject((void *)(uintptr_t)args[0], args[1], (uint8_t)args[2],
                                                  (bool)args[3], (const LARGE_INTEGER *)(uintptr_t)args[4]);
        break;
    case 189: // NtCreateEvent
        ret_val = (uint32_t)NtCreateEvent((HANDLE *)(uintptr_t)args[0], (void *)(uintptr_t)args[1],
                                          args[2], (bool)args[3]);
        break;
    case 193: // NtCreateSemaphore
        ret_val = (uint32_t)NtCreateSemaphore((HANDLE *)(uintptr_t)args[0], (void *)(uintptr_t)args[1],
                                              (int32_t)args[2], (int32_t)args[3]);
        break;
    case 222: // NtReleaseSemaphore
        ret_val = 0;
        break;
    case 225: // NtSetEvent
        ret_val = (uint32_t)NtSetEvent((HANDLE)(uintptr_t)args[0], (int32_t *)(uintptr_t)args[1]);
        break;
    case 233: // NtWaitForSingleObject
        ret_val = (uint32_t)NtWaitForSingleObject((HANDLE)(uintptr_t)args[0], (bool)args[1],
                                                  (const LARGE_INTEGER *)(uintptr_t)args[2]);
        break;
    case 234: // NtWaitForSingleObjectEx
        ret_val = (uint32_t)NtWaitForSingleObjectEx((HANDLE)(uintptr_t)args[0], (uint8_t)args[1],
                                                    (bool)args[2], (const LARGE_INTEGER *)(uintptr_t)args[3]);
        break;
    case 277: // RtlEnterCriticalSection
        RtlEnterCriticalSection((XRTL_CRITICAL_SECTION *)(uintptr_t)args[0]);
        ret_val = 0;
        break;
    case 291: // RtlInitializeCriticalSection
        RtlInitializeCriticalSection((XRTL_CRITICAL_SECTION *)(uintptr_t)args[0]);
        ret_val = 0;
        break;
    case 294: // RtlLeaveCriticalSection
        RtlLeaveCriticalSection((XRTL_CRITICAL_SECTION *)(uintptr_t)args[0]);
        ret_val = 0;
        break;

    // --- Threads ---
    case 124: // KeQueryBasePriorityThread
        ret_val = 8;
        break;
    case 143: // KeSetBasePriorityThread
        ret_val = 8;
        break;
    case 224: // NtResumeThread
    case 231: // NtSuspendThread
        ret_val = 0;
        break;
    case 238: // NtYieldExecution
        sched_yield();
        ret_val = 0;
        break;
    case 246: // ObReferenceObjectByHandle
        if (args[2]) *(void **)(uintptr_t)args[2] = (void *)(uintptr_t)args[0];
        ret_val = 0;
        break;
    case 250: // ObfDereferenceObject
        ret_val = 0;
        break;
    case 255: // PsCreateSystemThreadEx
        ret_val = (uint32_t)PsCreateSystemThreadEx((HANDLE *)(uintptr_t)args[0], args[1], args[2], args[3],
                                                   (uint32_t *)(uintptr_t)args[4], (void *)(uintptr_t)args[5],
                                                   (void *)(uintptr_t)args[6], (bool)args[7],
                                                   (bool)args[8], (void *)(uintptr_t)args[9]);
        break;
    case 258: // PsTerminateSystemThread
        PsTerminateSystemThread((NTSTATUS)args[0]);
        ret_val = 0;
        break;

    // --- Filesystem & I/O ---
    case 67: // IoCreateSymbolicLink
        {
            XANSI_STRING *src = (XANSI_STRING *)(uintptr_t)args[0];
            XANSI_STRING *dst = (XANSI_STRING *)(uintptr_t)args[1];
            std::string s_name(src->Buffer, src->Length);
            std::string d_name(dst->Buffer, dst->Length);
            ret_val = (uint32_t)IoCreateSymbolicLink(s_name.c_str(), d_name.c_str());
        }
        break;
    case 69: // IoDeleteSymbolicLink
        {
            XANSI_STRING *src = (XANSI_STRING *)(uintptr_t)args[0];
            std::string s_name(src->Buffer, src->Length);
            ret_val = (uint32_t)IoDeleteSymbolicLink(s_name.c_str());
        }
        break;
    case 87: // IofCompleteRequest
        ret_val = 0;
        break;
    case 187: // NtClose
        {
            HANDLE h = (HANDLE)(uintptr_t)args[0];
            if (file_close_handle(h)) {
                ret_val = 0;
            } else {
                ret_val = (uint32_t)NtClose(h);
            }
        }
        break;
    case 190: // NtCreateFile
        ret_val = (uint32_t)NtCreateFile((HANDLE *)(uintptr_t)args[0], (ACCESS_MASK)args[1],
                                         (void *)(uintptr_t)args[2], (void *)(uintptr_t)args[3],
                                         (const LARGE_INTEGER *)(uintptr_t)args[4], args[5],
                                         args[6], args[7], args[8]);
        break;
    case 196: // NtDeviceIoControlFile
    case 198: // NtFlushBuffersFile
    case 200: // NtFsControlFile
        ret_val = 0;
        break;
    case 202: // NtOpenFile
        {
            XOBJECT_ATTRIBUTES *oa = (XOBJECT_ATTRIBUTES *)(uintptr_t)args[2];
            std::string opath = (oa && oa->ObjectName && oa->ObjectName->Buffer) ?
                                std::string(oa->ObjectName->Buffer, oa->ObjectName->Length) : "";
            printf("  [VFS] NtOpenFile requisitado: '%s'\n", opath.c_str());
            ret_val = (uint32_t)NtOpenFile((HANDLE *)(uintptr_t)args[0], (ACCESS_MASK)args[1],
                                           (void *)(uintptr_t)args[2], (void *)(uintptr_t)args[3],
                                           args[4], args[5]);
            printf("  [VFS] NtOpenFile resultado: 0x%08X (Handle=%p)\n", ret_val, *(void **)(uintptr_t)args[0]);
        }
        break;
    case 203: // NtOpenSymbolicLinkObject
        ret_val = (uint32_t)NtOpenSymbolicLinkObject((HANDLE *)(uintptr_t)args[0], (void *)(uintptr_t)args[1]);
        break;
    case 207: // NtQueryDirectoryFile
        ret_val = (uint32_t)NtQueryDirectoryFile((HANDLE)(uintptr_t)args[0], (HANDLE)(uintptr_t)args[1],
                                                 (void *)(uintptr_t)args[2], (void *)(uintptr_t)args[3],
                                                 (void *)(uintptr_t)args[4], (void *)(uintptr_t)args[5],
                                                 args[6], args[7], (void *)(uintptr_t)args[8],
                                                 (bool)(args[9] != 0));
        break;
    case 210: // NtQueryFullAttributesFile
        ret_val = (uint32_t)NtQueryFullAttributesFile((void *)(uintptr_t)args[0], (void *)(uintptr_t)args[1]);
        break;
    case 211: // NtQueryInformationFile
        ret_val = (uint32_t)NtQueryInformationFile((HANDLE)(uintptr_t)args[0], (void *)(uintptr_t)args[1],
                                                   (void *)(uintptr_t)args[2], args[3], args[4]);
        break;
    case 215: // NtQuerySymbolicLinkObject
        ret_val = (uint32_t)NtQuerySymbolicLinkObject((HANDLE)(uintptr_t)args[0], (void *)(uintptr_t)args[1],
                                                      (uint32_t *)(uintptr_t)args[2]);
        break;
    case 218: // NtQueryVolumeInformationFile
        ret_val = (uint32_t)NtQueryVolumeInformationFile((HANDLE)(uintptr_t)args[0], (void *)(uintptr_t)args[1],
                                                         (void *)(uintptr_t)args[2], args[3], args[4]);
        break;
    case 219: // NtReadFile
        ret_val = (uint32_t)NtReadFile((HANDLE)(uintptr_t)args[0], (HANDLE)(uintptr_t)args[1],
                                       (void *)(uintptr_t)args[2], (void *)(uintptr_t)args[3],
                                       (void *)(uintptr_t)args[4], (void *)(uintptr_t)args[5],
                                       args[6], (const LARGE_INTEGER *)(uintptr_t)args[7]);
        printf("  [VFS] NtReadFile handle=%p buf=%p length=%u -> status=0x%08X\n",
               (void *)(uintptr_t)args[0], (void *)(uintptr_t)args[5], args[6], ret_val);
        break;
    case 226: // NtSetInformationFile
        ret_val = (uint32_t)NtSetInformationFile((HANDLE)(uintptr_t)args[0], (void *)(uintptr_t)args[1],
                                                 (void *)(uintptr_t)args[2], args[3], args[4]);
        break;
    case 236: // NtWriteFile
        ret_val = (uint32_t)NtWriteFile((HANDLE)(uintptr_t)args[0], (HANDLE)(uintptr_t)args[1],
                                        (void *)(uintptr_t)args[2], (void *)(uintptr_t)args[3],
                                        (void *)(uintptr_t)args[4], (const void *)(uintptr_t)args[5],
                                        args[6], (const LARGE_INTEGER *)(uintptr_t)args[7]);
        break;

    // --- RTL & Strings ---
    case 260: // RtlAnsiStringToUnicodeString
        {
            XUNICODE_STRING *dst = (XUNICODE_STRING *)(uintptr_t)args[0];
            XANSI_STRING *src = (XANSI_STRING *)(uintptr_t)args[1];
            bool alloc = (args[2] != 0);
            uint32_t n = src->Length;
            if (alloc) {
                dst->Buffer = (uint16_t *)ExAllocatePoolWithTag(0, (n + 1) * 2, 0x53747267);
                if (!dst->Buffer) return 0xC0000017L;
                dst->MaximumLength = (uint16_t)((n + 1) * 2);
            }
            for (uint32_t i = 0; i < n; i++) dst->Buffer[i] = (uint8_t)src->Buffer[i];
            dst->Length = (uint16_t)(n * 2);
            if (dst->MaximumLength >= (n + 1) * 2) dst->Buffer[n] = 0;
            ret_val = 0;
        }
        break;
    case 269: // RtlCompareMemoryUlong
        {
            const uint32_t *p = (const uint32_t *)(uintptr_t)args[0];
            uint32_t len = args[1];
            uint32_t pat = args[2];
            uint32_t i = 0;
            for (; i < len / 4; i++) {
                if (p[i] != pat) break;
            }
            ret_val = i * 4;
        }
        break;
    case 279: // RtlEqualString
        {
            XANSI_STRING *s1 = (XANSI_STRING *)(uintptr_t)args[0];
            XANSI_STRING *s2 = (XANSI_STRING *)(uintptr_t)args[1];
            bool case_insensitive = (args[2] != 0);
            if (s1->Length != s2->Length) {
                ret_val = 0;
            } else {
                bool eq = true;
                for (uint32_t i = 0; i < s1->Length; i++) {
                    char c1 = s1->Buffer[i];
                    char c2 = s2->Buffer[i];
                    if (case_insensitive) {
                        c1 = (char)tolower((unsigned char)c1);
                        c2 = (char)tolower((unsigned char)c2);
                    }
                    if (c1 != c2) { eq = false; break; }
                }
                ret_val = eq ? 1 : 0;
            }
        }
        break;
    case 289: // RtlInitAnsiString
        {
            XANSI_STRING *dst = (XANSI_STRING *)(uintptr_t)args[0];
            const char *src = (const char *)(uintptr_t)args[1];
            dst->Buffer = (char *)src;
            if (src) {
                size_t l = strlen(src);
                dst->Length = (uint16_t)l;
                dst->MaximumLength = (uint16_t)(l + 1);
            } else {
                dst->Length = dst->MaximumLength = 0;
            }
            ret_val = 0;
        }
        break;
    case 301: // RtlNtStatusToDosError
        ret_val = 0;
        break;
    case 302: // RtlRaiseException
        printf("k_exports_posix: RtlRaiseException called!\n");
        ret_val = 0;
        break;
    case 304: // RtlTimeFieldsToTime
    case 305: // RtlTimeToTimeFields
        ret_val = 1;
        break;
    case 308: // RtlUnicodeStringToAnsiString
        {
            XANSI_STRING *dst = (XANSI_STRING *)(uintptr_t)args[0];
            const XUNICODE_STRING *src = (const XUNICODE_STRING *)(uintptr_t)args[1];
            bool alloc = (args[2] != 0);

            if (!dst) {
                ret_val = 0xC000000DL; // STATUS_INVALID_PARAMETER
                break;
            }
            if (!src || !src->Buffer || src->Length == 0) {
                if (alloc) {
                    dst->Buffer = (char *)ExAllocatePoolWithTag(0, 1, 0x53747267);
                    if (dst->Buffer) dst->Buffer[0] = '\0';
                    dst->Length = 0;
                    dst->MaximumLength = 1;
                } else {
                    dst->Length = 0;
                    if (dst->MaximumLength > 0 && dst->Buffer) dst->Buffer[0] = '\0';
                }
                ret_val = 0;
                break;
            }

            uint32_t n = src->Length / 2;
            if (alloc) {
                dst->Buffer = (char *)ExAllocatePoolWithTag(0, n + 1, 0x53747267);
                if (!dst->Buffer) {
                    ret_val = 0xC0000017L; // STATUS_NO_MEMORY
                    break;
                }
                dst->MaximumLength = (uint16_t)(n + 1);
            } else if (dst->MaximumLength < n) {
                ret_val = 0x80000005L; // STATUS_BUFFER_OVERFLOW
                break;
            }

            for (uint32_t i = 0; i < n; i++) {
                uint16_t c = src->Buffer[i];
                dst->Buffer[i] = (c < 256) ? (char)c : '?';
            }
            dst->Length = (uint16_t)n;
            if (dst->MaximumLength > n && dst->Buffer) {
                dst->Buffer[n] = '\0';
            }

            printf("  [kposix] RtlUnicodeStringToAnsiString: converted %u chars: '%.*s'\n",
                   n, (int)std::min<uint32_t>(n, 64), dst->Buffer ? dst->Buffer : "");
            ret_val = 0; // STATUS_SUCCESS
        }
        break;
    case 312: // RtlUnwind
        ret_val = 0;
        break;

    // --- Crypto ---
    case 335: // XcSHAInit
        sha1_init((Sha1Ctx *)(uintptr_t)args[0]);
        ret_val = 0;
        break;
    case 336: // XcSHAUpdate
        sha1_update((Sha1Ctx *)(uintptr_t)args[0], (const uint8_t *)(uintptr_t)args[1], args[2]);
        ret_val = 0;
        break;
    case 337: // XcSHAFinal
        sha1_final((Sha1Ctx *)(uintptr_t)args[0], (uint8_t *)(uintptr_t)args[1]);
        ret_val = 0;
        break;
    case 340: // XcHMAC
        hmac_sha1((const uint8_t *)(uintptr_t)args[0], args[1],
                  (const uint8_t *)(uintptr_t)args[2], args[3],
                  (const uint8_t *)(uintptr_t)args[4], args[5],
                  (uint8_t *)(uintptr_t)args[6]);
        ret_val = 0;
        break;

    // --- Sections ---
    case 327: // XeLoadSection
    case 328: // XeUnloadSection
        ret_val = 0;
        break;

    default:
        ret_val = 0;
        break;
    }

    uint32_t bytes_to_drop = 0;
    if (e) {
        if (e->conv == K_STD) {
            bytes_to_drop = e->argc * 4;
        } else if (e->conv == K_FAST && e->argc > 2) {
            bytes_to_drop = (e->argc - 2) * 4;
        }
    }

    return ((uint64_t)bytes_to_drop << 32) | (uint32_t)ret_val;
}

void *make_stub(uint32_t ordinal)
{
    exports_init();
    uint8_t *p = g_stub_page + g_stub_used;
    g_stub_used += 16;

    p[0] = 0x68; // push imm32 (ordinal)
    memcpy(p + 1, &ordinal, 4);
    p[5] = 0xE9; // jmp rel32 (stub_common)
    int32_t rel = (int32_t)((uintptr_t)&stub_common - (uintptr_t)(p + 10));
    memcpy(p + 6, &rel, 4);

    return p;
}

bool kposix_resolve_thunks(uint32_t *thunk_table)
{
    if (!thunk_table) return false;
    exports_init();

    int implemented = 0, vars = 0, stubs = 0;

    for (int i = 0; thunk_table[i]; i++) {
        uint32_t ord = thunk_table[i] & 0x7FFFFFFF;
        const KExport *e = find_export(ord);

        void *target = nullptr;
        if (e && e->conv == K_VAR) {
            switch (ord) {
            case 16:  target = g_ExEventObjectType; break;
            case 40:  target = &g_HalDiskCachePartitionCount; break;
            case 71:  target = g_IoFileObjectType; break;
            case 164: target = &g_LaunchDataPage; break;
            case 259: target = g_PsThreadObjectType; break;
            case 322: target = &g_XboxHardwareInfo; break;
            case 323: target = g_XboxHDKey; break;
            case 324: target = &g_XboxKrnlVersion; break;
            case 325: target = g_XboxSignatureKey; break;
            case 326: target = &g_XeImageFileName; break;
            case 354: target = g_XboxAlternateSignatureKeys; break;
            case 356: target = &g_HalBootSMCVideoMode; break;
            case 357: target = g_IdexChannelObject; break;
            default:  target = g_ExEventObjectType; break;
            }
            vars++;
        } else {
            target = make_stub(ord);
            if (e) implemented++;
            else stubs++;
        }

        thunk_table[i] = (uint32_t)(uintptr_t)target;
    }

    printf("  [KRNL RESOLVER] %d funções resolvidas, %d variáveis de dados mapeadas, %d stubs.\n",
           implemented, vars, stubs);
    return true;
}

} // namespace xposix

// Raw assembly dispatcher
extern "C" void __attribute__((naked)) stub_common()
{
    __asm__ volatile (
        "pushl %ebp\n\t"
        "movl %esp, %ebp\n\t"
        "pushl %esi\n\t"
        "pushl %edi\n\t"
        "pushl %ebx\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "leal 12(%ebp), %eax\n\t"
        "subl $8, %esp\n\t"
        "andl $0xfffffff0, %esp\n\t" // 16-byte glibc stack alignment
        "pushl %edx\n\t"             // fastcall edx
        "pushl %ecx\n\t"             // fastcall ecx
        "pushl %eax\n\t"             // args pointer
        "pushl 4(%ebp)\n\t"          // ordinal
        "call kposix_stub_called\n\t"
        "movl %ebp, %esp\n\t"
        "popl %ebp\n\t"
        "movl 4(%esp), %ecx\n\t"     // return address
        "leal 8(%esp,%edx), %esp\n\t"// drop args and ordinal
        "jmpl *%ecx\n\t"
    );
}
