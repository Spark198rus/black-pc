// Unit Test: Kernel Thunk Trampoline and Dispatcher for Linux x86
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>

namespace {

struct StubLog {
    uint32_t ordinal;
    uint32_t args[8];
    uint32_t ecx;
    uint32_t edx;
    int call_count;
};

static StubLog g_last_stub{};

// Standard Xbox stdcall argument counts for common ordinals
uint8_t get_xbox_argc(uint32_t ordinal)
{
    switch (ordinal) {
        case 1:   return 0; // AvGetSavedDataAddress
        case 15:  return 2; // ExAllocatePoolWithTag(type, size)
        case 17:  return 1; // ExFreePool(ptr)
        case 95:  return 1; // KeBugCheck(code)
        case 99:  return 3; // KeDelayExecutionThread
        case 145: return 3; // KeSetEvent(event, increment, wait)
        case 159: return 5; // KeWaitForSingleObject
        case 289: return 2; // RtlInitAnsiString
        case 291: return 1; // RtlInitializeCriticalSection
        case 294: return 1; // RtlLeaveCriticalSection
        default:  return 0;
    }
}

} // namespace

// Called by stub_common trampoline.
// Returns: lower 32-bits in EAX (function return value), upper 32-bits in EDX (bytes to drop from stack).
extern "C" uint64_t __attribute__((stdcall)) blackrt_stub_called(uint32_t ordinal, const uint32_t *args, uint32_t ecx, uint32_t edx)
{
    g_last_stub.ordinal = ordinal;
    g_last_stub.call_count++;
    g_last_stub.ecx = ecx;
    g_last_stub.edx = edx;
    uint8_t argc = get_xbox_argc(ordinal);
    for (int i = 0; i < argc && i < 8; ++i) {
        g_last_stub.args[i] = args[i];
    }

    printf("  [THUNK DISPATCH] Ordinal %3u chamado! Argc=%u, Args: [", ordinal, argc);
    for (int i = 0; i < argc; ++i) {
        printf("%s0x%08X", i ? ", " : "", args[i]);
    }
    printf("]\n");

    uint32_t bytes_to_drop = argc * 4;
    uint32_t return_val = 0; // STATUS_SUCCESS
    return ((uint64_t)bytes_to_drop << 32) | (uint64_t)return_val;
}

// Low-level assembly trampoline for Linux x86 with 16-byte glibc stack alignment
__attribute__((naked)) void stub_common()
{
    __asm__ volatile (
        "pushl %ebp\n\t"
        "movl  %esp, %ebp\n\t"
        "andl  $0xfffffff0, %esp\n\t" // 16-byte stack alignment for Linux glibc
        "pushl %edx\n\t"              // arg 4: edx
        "pushl %ecx\n\t"              // arg 3: ecx
        "leal  12(%ebp), %eax\n\t"
        "pushl %eax\n\t"              // arg 2: pointer to stack arguments
        "pushl 4(%ebp)\n\t"           // arg 1: ordinal
        "call  blackrt_stub_called\n\t" // returns eax = result, edx = bytes to drop
        "movl  %ebp, %esp\n\t"        // restore stack pointer from ebp
        "popl  %ebp\n\t"
        "movl  4(%esp), %ecx\n\t"     // ecx = caller return address
        "leal  8(%esp, %edx), %esp\n\t" // drop ordinal (4), retaddr (4), and stack args (edx)
        "jmpl  *%ecx\n\t"             // return to caller with eax = result
    );
}

class ThunkManager {
public:
    ThunkManager() {
        stub_page_ = (uint8_t *)mmap(nullptr, 0x10000,
                                     PROT_READ | PROT_WRITE | PROT_EXEC,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(stub_page_ != MAP_FAILED && "Falha ao alocar página de stubs");
    }

    ~ThunkManager() {
        if (stub_page_) munmap(stub_page_, 0x10000);
    }

    void *make_stub(uint32_t ordinal) {
        assert(used_ + 16 <= 0x10000 && "Página de stubs cheia");
        uint8_t *p = stub_page_ + used_;
        used_ += 16;

        p[0] = 0x68; // push imm32 (ordinal)
        memcpy(p + 1, &ordinal, 4);
        p[5] = 0xE9; // jmp rel32 (stub_common)
        int32_t rel = (int32_t)((uintptr_t)&stub_common - (uintptr_t)(p + 10));
        memcpy(p + 6, &rel, 4);

        return (void *)p;
    }

private:
    uint8_t *stub_page_ = nullptr;
    size_t used_ = 0;
};

int main()
{
    printf("==========================================================\n");
    printf("     TESTE 3: DISPATCHER DINÂMICO DE THUNKS DO KERNEL     \n");
    printf("==========================================================\n");

    ThunkManager mgr;

    // Criar stubs para ordinais conhecidos
    void *stub_ex_alloc = mgr.make_stub(15);  // ExAllocatePoolWithTag (2 args)
    void *stub_ke_event = mgr.make_stub(145); // KeSetEvent (3 args)
    void *stub_ke_wait  = mgr.make_stub(159); // KeWaitForSingleObject (5 args)

    printf("[+] Stubs dinâmicos gerados em memória executável:\n");
    printf("    - Ordinal  15 (ExAllocatePoolWithTag): %p\n", stub_ex_alloc);
    printf("    - Ordinal 145 (KeSetEvent):            %p\n", stub_ke_event);
    printf("    - Ordinal 159 (KeWaitForSingleObject): %p\n", stub_ke_wait);

    // 1. Testar chamada a KeSetEvent(0x12345678, 0x01, 0x00)
    typedef uint32_t (__attribute__((stdcall)) *KeSetEventFn)(uint32_t ev, uint32_t inc, uint32_t wait);
    KeSetEventFn call_ke_event = (KeSetEventFn)stub_ke_event;

    printf("\n[+] Executando chamada simulada a KeSetEvent(0xAABBCCDD, 0x00000001, 0x00000000)...\n");

    register void *fn_ke_event __asm__("edi") = stub_ke_event;
    uint32_t esp_before = 0, esp_after = 0, res1 = 0;
    __asm__ volatile (
        "movl %%esp, %0\n\t"
        "pushl $0x00000000\n\t"
        "pushl $0x00000001\n\t"
        "pushl $0xAABBCCDD\n\t"
        "call *%3\n\t"
        "movl %%esp, %1\n\t"
        : "=&r"(esp_before), "=&r"(esp_after), "=a"(res1)
        : "r"(fn_ke_event)
        : "ecx", "edx", "memory"
    );

    printf("[+] %%esp antes: 0x%08X, %%esp depois: 0x%08X (delta: %d bytes)\n",
           esp_before, esp_after, (int)(esp_after - esp_before));
    assert(res1 == 0 && "Retorno de KeSetEvent deve ser 0");
    assert(g_last_stub.ordinal == 145 && "Ordinal 145 deve ser registrado");
    assert(g_last_stub.args[0] == 0xAABBCCDD && "Arg 0 deve ser 0xAABBCCDD");
    assert(g_last_stub.args[1] == 0x00000001 && "Arg 1 deve ser 1");
    assert(g_last_stub.args[2] == 0x00000000 && "Arg 2 deve ser 0");
    assert(esp_before == esp_after && "Stack pointer (%%esp) deve estar perfeitamente balanceado após stdcall!");
    printf("[+] %%esp preservado com precisão: 0x%08X == 0x%08X\n", esp_before, esp_after);

    // 2. Testar chamada a ExAllocatePoolWithTag(0x00000000, 0x00001000)
    typedef uint32_t (__attribute__((stdcall)) *ExAllocFn)(uint32_t pool_type, uint32_t size);
    ExAllocFn call_ex_alloc = (ExAllocFn)stub_ex_alloc;

    printf("\n[+] Executando chamada simulada a ExAllocatePoolWithTag(0x00000001, 0x00002000)...\n");
    call_ex_alloc(0x00000001, 0x00002000);

    assert(g_last_stub.ordinal == 15 && "Ordinal 15 deve ser registrado");
    assert(g_last_stub.args[0] == 0x00000001 && "Pool type deve ser 1");
    assert(g_last_stub.args[1] == 0x00002000 && "Tamanho deve ser 0x2000");

    printf("\n>> TESTE 3 (THUNK DISPATCHER) PASSOU COM SUCESSO!\n\n");
    return 0;
}
