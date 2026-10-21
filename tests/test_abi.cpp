// Unit Test: 32-bit x86 ABI Compatibility, Struct Layout and Dynamic Thunks
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <sys/mman.h>

#include "loader/src/platform/windefs.h"

// ---- Xbox Structures Definitions ----

#pragma pack(push, 4)
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

struct XKTIMER {
    XDISPATCHER_HEADER Header;
    ULARGE_INTEGER DueTime;
    XLIST_ENTRY TimerListEntry;
    XKDPC *Dpc;
    int32_t Period;
};

// Xbox critical sections are 28 bytes with KTHREAD* owner (unlike Win32 layout)
struct XRTL_CRITICAL_SECTION {
    XDISPATCHER_HEADER Header;
    LONG LockCount;
    LONG RecursionCount;
    void *OwningThread;
};

struct XANSI_STRING {
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
#pragma pack(pop)

#pragma pack(push, 1)
struct WaveFormatEx {
    uint16_t wFormatTag, nChannels;
    uint32_t nSamplesPerSec, nAvgBytesPerSec;
    uint16_t nBlockAlign, wBitsPerSample, cbSize;
};

struct XGamepad {
    uint16_t wButtons;
    uint8_t bAnalogButtons[8]; // A B X Y BLACK WHITE LTRIGGER RTRIGGER
    int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};

struct XState {
    uint32_t dwPacketNumber;
    XGamepad Gamepad;
};
#pragma pack(pop)

struct Vec3 { float x, y, z; };

struct Ds3dBuffer {
    uint32_t dwSize;
    Vec3 vPosition, vVelocity;
    uint32_t dwInsideConeAngle, dwOutsideConeAngle;
    Vec3 vConeOrientation;
    int32_t lConeOutsideVolume;
    float flMinDistance, flMaxDistance;
    uint32_t dwMode;
    float flDistanceFactor, flRolloffFactor, flDopplerFactor;
};

// Target function to be called through dynamic thunk
static uint32_t g_last_arg1 = 0, g_last_arg2 = 0, g_last_arg3 = 0;
extern "C" uint32_t __stdcall test_target_func(uint32_t a, uint32_t b, uint32_t c)
{
    g_last_arg1 = a;
    g_last_arg2 = b;
    g_last_arg3 = c;
    return a + b * 2 + c * 3;
}

int main()
{
    printf("[+] Executando Testes de ABI e Estruturas (Linux x86)...\n");

    // 1. Verificação de Tamanhos e Alinhamentos
    static_assert(sizeof(XDISPATCHER_HEADER) == 16, "XDISPATCHER_HEADER size mismatch");
    static_assert(sizeof(XKDPC) == 28, "XKDPC size mismatch");
    static_assert(sizeof(XKTIMER) == 40, "XKTIMER size mismatch");
    static_assert(sizeof(XRTL_CRITICAL_SECTION) == 28, "XRTL_CRITICAL_SECTION size mismatch");
    static_assert(sizeof(XANSI_STRING) == 8, "XANSI_STRING size mismatch");
    static_assert(sizeof(XUNICODE_STRING) == 8, "XUNICODE_STRING size mismatch");
    static_assert(sizeof(XOBJECT_ATTRIBUTES) == 12, "XOBJECT_ATTRIBUTES size mismatch");
    static_assert(sizeof(XIO_STATUS_BLOCK) == 8, "XIO_STATUS_BLOCK size mismatch");

    static_assert(sizeof(WaveFormatEx) == 18, "WaveFormatEx size mismatch");
    static_assert(sizeof(XGamepad) == 18, "XGamepad size mismatch");
    static_assert(sizeof(XState) == 22, "XState size mismatch");
    static_assert(sizeof(Ds3dBuffer) == 76, "Ds3dBuffer size mismatch");

    printf("    [PASS] Todos os static_asserts de estruturas do Xbox foram validados com sucesso.\n");

    // 2. Verificação de Emissão e Execução de Thunk Dinâmico em Assembly
    printf("[+] Testando emissão dinâmica de thunks (LTCG register preservation e stack cleanup)...\n");
    void *exec_mem = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(exec_mem != MAP_FAILED);

    uint8_t *code = (uint8_t *)exec_mem;
    int n = 0;
    // O thunk salva ecx e edx, passa 3 argumentos da pilha para stdcall target e faz ret 12
    code[n++] = 0x51; // push ecx
    code[n++] = 0x52; // push edx

    // Pilha antes do push dos args: [esp] = edx, [esp+4] = ecx, [esp+8] = ret_addr, [esp+12] = arg1, [esp+16] = arg2, [esp+20] = arg3
    // Como cada push diminui o esp em 4 bytes, o deslocamento relativo de cada argumento se mantém em 20 bytes:
    code[n++] = 0xFF; code[n++] = 0x74; code[n++] = 0x24; code[n++] = 20; // push [esp+20] (arg3)
    code[n++] = 0xFF; code[n++] = 0x74; code[n++] = 0x24; code[n++] = 20; // push [esp+20] (arg2)
    code[n++] = 0xFF; code[n++] = 0x74; code[n++] = 0x24; code[n++] = 20; // push [esp+20] (arg1)

    code[n++] = 0xB8; // mov eax, target_func
    uint32_t target_addr = (uint32_t)(uintptr_t)&test_target_func;
    memcpy(code + n, &target_addr, 4);
    n += 4;

    code[n++] = 0xFF; code[n++] = 0xD0; // call eax
    code[n++] = 0x5A;                   // pop edx
    code[n++] = 0x59;                   // pop ecx
    code[n++] = 0xC2; code[n++] = 12; code[n++] = 0; // ret 12

    typedef uint32_t (__stdcall *ThunkFn)(uint32_t, uint32_t, uint32_t);
    ThunkFn thunk = (ThunkFn)(uintptr_t)code;

    // Testar com registradores ecx e edx contendo valores conhecidos
    uint32_t ecx_before = 0xDEADBEEF, edx_before = 0xCAFEBABE;
    uint32_t ecx_after = 0, edx_after = 0;
    uint32_t res = 0;

    __asm__ volatile (
        "mov %4, %%ecx\n"
        "mov %5, %%edx\n"
        "push $30\n"
        "push $20\n"
        "push $10\n"
        "call *%3\n"
        "mov %%ecx, %1\n"
        "mov %%edx, %2\n"
        "mov %%eax, %0\n"
        : "=r"(res), "=r"(ecx_after), "=r"(edx_after)
        : "r"(thunk), "r"(ecx_before), "r"(edx_before)
        : "eax", "ecx", "edx", "memory"
    );

    assert(res == (10 + 20 * 2 + 30 * 3));
    assert(g_last_arg1 == 10);
    assert(g_last_arg2 == 20);
    assert(g_last_arg3 == 30);
    assert(ecx_after == ecx_before);
    assert(edx_after == edx_before);

    printf("    [PASS] Thunk executado com sucesso: res = %u, ecx/edx preservados.\n", res);
    munmap(exec_mem, 4096);

    printf("\n>>> [PASS] test_abi: ABI, struct layouts and 32-bit thunks operational!\n");
    return 0;
}
