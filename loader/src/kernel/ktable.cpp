// Kernel export table: ordinal -> implementation, and the logging stub used for anything not implemented.
#include "kernel/ktable.h"

#include "kernel/kexports.h"
#include "xbe.h"

#include <cstring>
#include <vector>

namespace {

enum KConv : uint8_t { K_STD, K_FAST, K_CDECL, K_VAR };

struct KExport {
    uint16_t ordinal;
    const char *name;
    KConv conv;
    uint8_t argc; // stack+register arguments (for the stub's stack cleanup and argument logging)
    void *impl;   // function or variable; nullptr = logging stub
};

#define F(ord, name, argc) {ord, #name, K_STD, argc, (void *)&xk::name}
#define FF(ord, name, argc) {ord, #name, K_FAST, argc, (void *)&xk::name}
#define FC(ord, name) {ord, #name, K_CDECL, 0, (void *)&xk::name}
#define V(ord, name) {ord, #name, K_VAR, 0, (void *)&xk::name}
#define STUB(ord, name, argc) {ord, #name, K_STD, argc, nullptr}

// Every ordinal default.xbe imports (115), plus room for more. Argument counts are from the Xbox kernel ABI.
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
    F(149, KeSetTimer, 4), // LARGE_INTEGER by value counts as two
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
    {302, "RtlRaiseException", K_STD, 1, nullptr}, // bound to ntdll at startup (same ABI, shared SEH chain)
    F(304, RtlTimeFieldsToTime, 2),
    F(305, RtlTimeToTimeFields, 2),
    F(308, RtlUnicodeStringToAnsiString, 3),
    {312, "RtlUnwind", K_STD, 4, nullptr}, // bound to ntdll at startup
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
    for (const auto &e : kExports)
        if (e.ordinal == ordinal) return &e;
    return nullptr;
}

std::atomic<uint32_t> g_stub_calls[400];

} // namespace

// Called by the stub thunk. Returns (result in eax, bytes of stack arguments to drop in edx).
extern "C" uint64_t __stdcall blackrt_stub_called(uint32_t ordinal, const uint32_t *args, uint32_t ecx, uint32_t edx)
{
    const KExport *e = find_export(ordinal);
    uint32_t n = ordinal < 400 ? ++g_stub_calls[ordinal] : 1;
    char argbuf[256] = "";
    int pos = 0;
    if (e) {
        for (uint32_t i = 0; i < e->argc && pos < 200; i++) {
            uint32_t v = (e->conv == K_FAST && i == 0) ? ecx : (e->conv == K_FAST && i == 1) ? edx
                         : args[e->conv == K_FAST ? i - 2 : i];
            pos += snprintf(argbuf + pos, sizeof argbuf - pos, "%s0x%x", i ? ", " : "", v);
        }
    }
    if (n == 1 || g_cfg.trace)
        logf("[k] UNIMPLEMENTED %s(%s) ordinal %u, call #%u, from %s", e ? e->name : "?", argbuf, ordinal, n,
             sym_name(args[-1]).c_str());
    if (g_cfg.strict) {
        request_exit(2, "unimplemented kernel call (--strict)");
        Sleep(INFINITE);
    }
    uint32_t bytes = 0;
    if (e) {
        if (e->conv == K_STD) bytes = e->argc * 4;
        else if (e->conv == K_FAST && e->argc > 2) bytes = (e->argc - 2) * 4;
    }
    return ((uint64_t)bytes << 32) | 0u;
}

namespace {

// Entered from a per-ordinal thunk: push <ordinal>; jmp stub_common.
// Stack on entry: [esp] ordinal, [esp+4] return address, [esp+8] first stack argument.
__declspec(naked) void stub_common()
{
    __asm {
        push ebp
        mov ebp, esp            ; [ebp+4] ordinal, [ebp+8] return address, [ebp+12] args
        push edx
        push ecx
        lea eax, [ebp+12]
        push eax
        push dword ptr [ebp+4]
        call blackrt_stub_called ; eax = result, edx = bytes to drop
        mov esp, ebp
        pop ebp                 ; [esp] ordinal, [esp+4] return address
        mov ecx, [esp+4]
        lea esp, [esp+edx+8]
        jmp ecx
    }
}

uint8_t *g_stub_page;
size_t g_stub_used;

void *make_stub(uint32_t ordinal)
{
    if (!g_stub_page) g_stub_page = (uint8_t *)VirtualAlloc(nullptr, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    uint8_t *p = g_stub_page + g_stub_used;
    g_stub_used += 16;
    p[0] = 0x68; // push imm32
    memcpy(p + 1, &ordinal, 4);
    p[5] = 0xE9; // jmp rel32
    int32_t rel = (int32_t)((uintptr_t)&stub_common - (uintptr_t)(p + 10));
    memcpy(p + 6, &rel, 4);
    return p;
}

} // namespace

const char *kernel_export_name(uint32_t ordinal)
{
    const KExport *e = find_export(ordinal);
    return e ? e->name : "?";
}

bool kernel_resolve_thunks(const Xbe &xbe)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    void *rtl_unwind = (void *)GetProcAddress(ntdll, "RtlUnwind");
    void *rtl_raise = (void *)GetProcAddress(ntdll, "RtlRaiseException");

    uint32_t *thunk = (uint32_t *)(uintptr_t)xbe.thunk_addr;
    int implemented = 0, stubbed = 0, vars = 0;
    std::string stubs;
    for (int i = 0; thunk[i]; i++) {
        uint32_t ord = thunk[i] & 0x7FFFFFFF;
        const KExport *e = find_export(ord);
        if (!e) {
            logf("kernel: ordinal %u is not in the export table; add it to ktable.cpp", ord);
            return false;
        }
        void *target = e->impl;
        if (ord == 312) target = rtl_unwind;
        if (ord == 302) target = rtl_raise;
        if (!target) {
            target = make_stub(ord);
            stubbed++;
            stubs += std::string(stubs.empty() ? "" : ", ") + e->name;
        } else if (e->conv == K_VAR) {
            vars++;
        } else {
            implemented++;
        }
        thunk[i] = (uint32_t)(uintptr_t)target;
    }
    logf("kernel: %d imports implemented, %d data exports, %d logging stubs%s%s", implemented, vars, stubbed,
         stubbed ? ": " : "", stubs.c_str());
    return true;
}
