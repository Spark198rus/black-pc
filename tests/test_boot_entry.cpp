// Integration Test: Xbox Boot Entry Point Execution (CRT Startup)
// Maps default.xbe at 0x10000, 64MB RAM at 0x80000000, binds %fs KPCR, hooks 115 kernel thunks,
// and executes the Xbox CRT entry point 0x00025A3F.
#include "loader/src/platform/windefs.h"
#include "loader/src/platform/xbe_loader.h"

#include <asm/ldt.h>
#include <cassert>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#pragma pack(push, 1)
struct XBoxKPCR {
    void    *TlsData;          // +0x00
    void    *StackBase;        // +0x04
    void    *StackLimit;       // +0x08
    uint32_t Reserved[5];      // +0x0C
    void    *Prcb;             // +0x20
    uint8_t  Irql;             // +0x24
    uint8_t  Reserved2[3];     // +0x25
    void    *CurrentThread;    // +0x28
    uint32_t DpcActive;        // +0x2C
};
#pragma pack(pop)

namespace {

static sigjmp_buf g_jump_buffer;
static volatile sig_atomic_t g_crashed = 0;
static uintptr_t g_crash_ip = 0;
static uintptr_t g_crash_sp = 0;
static int g_kernel_calls_intercepted = 0;

void signal_handler(int sig, siginfo_t *info, void *ucontext)
{
    g_crashed = sig;
    ucontext_t *ctx = (ucontext_t *)ucontext;
    g_crash_ip = ctx->uc_mcontext.gregs[REG_EIP];
    g_crash_sp = ctx->uc_mcontext.gregs[REG_ESP];

    printf("\n[!] SINAL INTERCEPTADO (%s, sig=%d) no endereço IP: 0x%08X (SP: 0x%08X, Acesso: %p)\n",
           sig == SIGSEGV ? "SIGSEGV" : sig == SIGILL ? "SIGILL" : "OUTRO",
           sig, (unsigned int)g_crash_ip, (unsigned int)g_crash_sp, info->si_addr);

    siglongjmp(g_jump_buffer, 1);
}

// Xbox Kernel ordinals and argument count lookup
uint8_t get_xbox_argc(uint32_t ordinal)
{
    switch (ordinal) {
        case 1:   return 0; // AvGetSavedDataAddress
        case 2:   return 4; // AvSendTVEncoderOption
        case 3:   return 6; // AvSetDisplayMode
        case 4:   return 1; // AvSetSavedDataAddress
        case 8:   return 0; // DbgPrint (cdecl)
        case 15:  return 2; // ExAllocatePoolWithTag
        case 17:  return 1; // ExFreePool
        case 23:  return 1; // ExQueryPoolBlockSize
        case 24:  return 5; // ExQueryNonVolatileSetting
        case 44:  return 2; // HalGetInterruptVector
        case 46:  return 6; // HalReadWritePCISpace
        case 47:  return 2; // HalRegisterShutdownNotification
        case 49:  return 1; // HalReturnToFirmware
        case 67:  return 2; // IoCreateSymbolicLink
        case 69:  return 1; // IoDeleteSymbolicLink
        case 87:  return 2; // IofCompleteRequest
        case 95:  return 1; // KeBugCheck
        case 97:  return 1; // KeCancelTimer
        case 98:  return 1; // KeConnectInterrupt
        case 99:  return 3; // KeDelayExecutionThread
        case 100: return 1; // KeDisconnectInterrupt
        case 107: return 3; // KeInitializeDpc
        case 109: return 7; // KeInitializeInterrupt
        case 113: return 2; // KeInitializeTimerEx
        case 119: return 3; // KeInsertQueueDpc
        case 124: return 1; // KeQueryBasePriorityThread
        case 128: return 1; // KeQuerySystemTime
        case 129: return 0; // KeRaiseIrqlToDpcLevel
        case 137: return 1; // KeRemoveQueueDpc
        case 139: return 1; // KeRestoreFloatingPointState
        case 142: return 1; // KeSaveFloatingPointState
        case 143: return 2; // KeSetBasePriorityThread
        case 145: return 3; // KeSetEvent
        case 149: return 4; // KeSetTimer
        case 151: return 1; // KeStallExecutionProcessor
        case 153: return 3; // KeSynchronizeExecution
        case 159: return 5; // KeWaitForSingleObject
        case 160: return 1; // KfRaiseIrql
        case 161: return 1; // KfLowerIrql
        case 165: return 1; // MmAllocateContiguousMemory
        case 166: return 5; // MmAllocateContiguousMemoryEx
        case 168: return 2; // MmClaimGpuInstanceMemory
        case 171: return 1; // MmFreeContiguousMemory
        case 173: return 1; // MmGetPhysicalAddress
        case 175: return 3; // MmLockUnlockBufferPages
        case 176: return 2; // MmLockUnlockPhysicalPage
        case 178: return 3; // MmPersistContiguousMemory
        case 179: return 1; // MmQueryAddressProtect
        case 180: return 1; // MmQueryAllocationSize
        case 182: return 3; // MmSetAddressProtect
        case 184: return 5; // NtAllocateVirtualMemory
        case 187: return 1; // NtClose
        case 189: return 4; // NtCreateEvent
        case 190: return 9; // NtCreateFile
        case 193: return 4; // NtCreateSemaphore
        case 196: return 10; // NtDeviceIoControlFile
        case 197: return 3; // NtDuplicateObject
        case 198: return 2; // NtFlushBuffersFile
        case 199: return 3; // NtFreeVirtualMemory
        case 200: return 10; // NtFsControlFile
        case 202: return 6; // NtOpenFile
        case 203: return 2; // NtOpenSymbolicLinkObject
        case 207: return 10; // NtQueryDirectoryFile
        case 210: return 2; // NtQueryFullAttributesFile
        case 211: return 5; // NtQueryInformationFile
        case 215: return 3; // NtQuerySymbolicLinkObject
        case 217: return 2; // NtQueryVirtualMemory
        case 218: return 5; // NtQueryVolumeInformationFile
        case 219: return 8; // NtReadFile
        case 222: return 3; // NtReleaseSemaphore
        case 224: return 2; // NtResumeThread
        case 225: return 2; // NtSetEvent
        case 226: return 5; // NtSetInformationFile
        case 231: return 2; // NtSuspendThread
        case 233: return 3; // NtWaitForSingleObject
        case 234: return 4; // NtWaitForSingleObjectEx
        case 236: return 8; // NtWriteFile
        case 238: return 0; // NtYieldExecution
        case 246: return 3; // ObReferenceObjectByHandle
        case 250: return 1; // ObfDereferenceObject
        case 255: return 10; // PsCreateSystemThreadEx
        case 258: return 1; // PsTerminateSystemThread
        case 260: return 3; // RtlAnsiStringToUnicodeString
        case 269: return 3; // RtlCompareMemoryUlong
        case 277: return 1; // RtlEnterCriticalSection
        case 279: return 3; // RtlEqualString
        case 289: return 2; // RtlInitAnsiString
        case 291: return 1; // RtlInitializeCriticalSection
        case 294: return 1; // RtlLeaveCriticalSection
        case 301: return 1; // RtlNtStatusToDosError
        case 302: return 1; // RtlRaiseException
        case 304: return 2; // RtlTimeFieldsToTime
        case 305: return 2; // RtlTimeToTimeFields
        case 308: return 3; // RtlUnicodeStringToAnsiString
        case 312: return 4; // RtlUnwind
        case 327: return 1; // XeLoadSection
        case 328: return 1; // XeUnloadSection
        case 335: return 1; // XcSHAInit
        case 336: return 3; // XcSHAUpdate
        case 337: return 2; // XcSHAFinal
        case 340: return 7; // XcHMAC
        case 360: return 0; // HalInitiateShutdown
        default:  return 0;
    }
}

} // namespace

extern "C" uint64_t __attribute__((stdcall)) blackrt_stub_called(uint32_t ordinal, const uint32_t *args, uint32_t ecx, uint32_t edx)
{
    (void)ecx; (void)edx;
    g_kernel_calls_intercepted++;
    uint8_t argc = get_xbox_argc(ordinal);

    printf("  --> [KERNEL INTERCEPT] Ordinal %3u chamado! Argc=%u (Call #%d)\n",
           ordinal, argc, g_kernel_calls_intercepted);

    uint32_t bytes_to_drop = argc * 4;
    uint32_t ret_val = 0; // STATUS_SUCCESS

    // Mock implementations for early startup calls
    if (ordinal == 255) { // PsCreateSystemThreadEx(ThreadHandle, ExtSize, StackSize, TlsSize, ThreadId, StartRoutine, ...)
        if (args && args[0]) {
            *(uint32_t *)(uintptr_t)args[0] = 0x1000 + g_kernel_calls_intercepted; // mock handle
        }
        if (args && args[4]) {
            *(uint32_t *)(uintptr_t)args[4] = g_kernel_calls_intercepted; // mock tid
        }
        printf("      PsCreateSystemThreadEx interceptado: StartRoutine=0x%08X\n", args[5]);
    } else if (ordinal == 15) { // ExAllocatePoolWithTag
        ret_val = 0x80100000 + (g_kernel_calls_intercepted * 0x1000);
    } else if (ordinal == 165 || ordinal == 166) { // MmAllocateContiguousMemory(Ex)
        ret_val = 0x80800000 + (g_kernel_calls_intercepted * 0x1000);
    } else if (ordinal == 291) { // RtlInitializeCriticalSection
        if (args && args[0]) {
            memset((void *)(uintptr_t)args[0], 0, 24);
        }
    }
    return ((uint64_t)bytes_to_drop << 32) | (uint64_t)ret_val;
}

__attribute__((naked)) void stub_common()
{
    __asm__ volatile (
        "pushl %ebp\n\t"
        "movl  %esp, %ebp\n\t"
        "andl  $0xfffffff0, %esp\n\t"
        "pushl %edx\n\t"
        "pushl %ecx\n\t"
        "leal  12(%ebp), %eax\n\t"
        "pushl %eax\n\t"
        "pushl 4(%ebp)\n\t"
        "call  blackrt_stub_called\n\t"
        "movl  %ebp, %esp\n\t"
        "popl  %ebp\n\t"
        "movl  4(%esp), %ecx\n\t"
        "leal  8(%esp, %edx), %esp\n\t"
        "jmpl  *%ecx\n\t"
    );
}

class BootEnvironment {
public:
    BootEnvironment() {
        stub_page_ = (uint8_t *)mmap(nullptr, 0x20000,
                                     PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(stub_page_ != MAP_FAILED && "Falha ao alocar página de stubs");
    }

    ~BootEnvironment() {
        if (stub_page_) munmap(stub_page_, 0x20000);
    }

    void *make_stub(uint32_t ordinal) {
        assert(used_ + 16 <= 0x20000 && "Página de stubs cheia");
        uint8_t *p = stub_page_ + used_;
        used_ += 16;

        p[0] = 0x68; // push imm32 (ordinal)
        memcpy(p + 1, &ordinal, 4);
        p[5] = 0xE9; // jmp rel32 (stub_common)
        int32_t rel = (int32_t)((uintptr_t)&stub_common - (uintptr_t)(p + 10));
        memcpy(p + 6, &rel, 4);

        return (void *)p;
    }

    static BootEnvironment *instance;
    static void *resolve_callback(uint32_t ord) {
        return instance->make_stub(ord);
    }

private:
    uint8_t *stub_page_ = nullptr;
    size_t used_ = 0;
};

BootEnvironment *BootEnvironment::instance = nullptr;

int main(int argc, char **argv)
{
    const char *xbe_path = (argc > 1) ? argv[1] : "game/default.xbe";
    if (access(xbe_path, F_OK) != 0 && access("../game/default.xbe", F_OK) == 0) {
        xbe_path = "../game/default.xbe";
    }
    if (access(xbe_path, F_OK) != 0) {
        printf("[SKIP] %s not found (requires retail game dump)\n", xbe_path);
        return 0;
    }

    printf("==========================================================\n");
    printf("  TESTE INTEGRAÇÃO: SALTO AO XBE BOOT ENTRY POINT (CRT)   \n");
    printf("==========================================================\n");

    // 1. Carregar XBE
    XbeLoader xbe;
    std::string err;
    if (!xbe.load(xbe_path, err)) {
        fprintf(stderr, "FALHA ao carregar XBE: %s\n", err.c_str());
        return 1;
    }

    // 2. Mapear imagem em 0x00010000
    if (!xbe.map_image(err)) {
        fprintf(stderr, "FALHA ao mapear XBE: %s\n", err.c_str());
        return 1;
    }
    printf("[+] default.xbe mapeado em 0x%08X (span: %u bytes)\n", xbe.base, xbe.size_of_image);

    // 3. Mapear 64 MB de RAM em 0x80000000
    void *ram = mmap((void *)0x80000000, 64 * 1024 * 1024,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(ram == (void *)0x80000000 && "Falha ao mapear RAM em 0x80000000");
    printf("[+] 64 MB de RAM do Xbox mapeados em 0x80000000 - 0x84000000\n");

    // 4. Hookar todos os 115 thunks de kernel
    BootEnvironment env;
    BootEnvironment::instance = &env;
    int resolved = 0;
    xbe.resolve_thunks(BootEnvironment::resolve_callback, resolved);
    printf("[+] %d Thunks de Kernel interceptados e conectados ao dispatcher dinâmico!\n", resolved);
    assert(resolved >= 115 && "Todos os 115 thunks devem ser resolvidos");

    // 5. Configurar KPCR e anexar ao segmento %fs
    XBoxKPCR kpcr{};
    kpcr.StackBase = (void *)0x00700000;
    kpcr.StackLimit = (void *)0x00600000;
    kpcr.Prcb = &kpcr;
    kpcr.CurrentThread = &kpcr;

    struct user_desc u_info{};
    u_info.entry_number = -1;
    u_info.base_addr = (unsigned long)&kpcr;
    u_info.limit = sizeof(XBoxKPCR);
    u_info.seg_32bit = 1;
    u_info.contents = MODIFY_LDT_CONTENTS_DATA;
    u_info.read_exec_only = 0;
    u_info.limit_in_pages = 0;
    u_info.seg_not_present = 0;
    u_info.useable = 1;

    int ret = syscall(SYS_set_thread_area, &u_info);
    assert(ret == 0 && "SYS_set_thread_area falhou");
    unsigned short fs_selector = (u_info.entry_number << 3) | 3;
    __asm__ volatile ("movw %0, %%fs" : : "rm"(fs_selector));
    printf("[+] Segmento %%fs configurado com seletor 0x%04X apontando para KPCR (%p)\n",
           fs_selector, &kpcr);

    // 6. Instalar tratador de sinais com sigaction
    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);

    printf("\n[+] Preparando salto de execução para o Entry Point da CRT (0x%08X)...\n", xbe.entry);
    printf("    A CPU do PC executará o código nativo x86 do Xbox diretamente!\n\n");

    bool returned_cleanly = false;
    // 7. Salto de execução com proteção de checkpoint
    if (sigsetjmp(g_jump_buffer, 1) == 0) {
        typedef void (__attribute__((cdecl)) *BootEntryFn)();
        BootEntryFn entry_fn = (BootEntryFn)(uintptr_t)xbe.entry;
        printf("    [EXEC] Chamando entry_fn()...\n");
        entry_fn();
        returned_cleanly = true;
        printf("    [EXEC] entry_fn() RETORNOU NORMALMENTE COM SUCESSO!\n");
    }

    printf("\n[+] Relatório de execução do boot entry:\n");
    printf("    - Retornou normalmente:             %s\n", returned_cleanly ? "SIM (100% Sucesso)" : "NÃO");
    printf("    - Chamadas ao Kernel interceptadas: %d\n", g_kernel_calls_intercepted);
    if (!returned_cleanly) {
        printf("    - Parada da execução em EIP:        0x%08X (sinal %d)\n", (unsigned int)g_crash_ip, g_crashed);
    }

    // Validar que o código do jogo executou com sucesso e retornou limpo
    assert(returned_cleanly && g_kernel_calls_intercepted >= 2 &&
           "O ponto de entrada da CRT deve invocar o kernel e retornar normalmente sem crash!");
    printf("\n>>> [PASS] test_boot_entry: CRT startup executou nativamente no Linux! <<<\n\n");

    munmap(ram, 64 * 1024 * 1024);
    return 0;
}
