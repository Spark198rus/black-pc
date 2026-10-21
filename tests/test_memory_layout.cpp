// Test Suite: Memory Layout, Page Protections and Signal Exception Handling (Linux x86)
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>

static volatile sig_atomic_t g_segv_caught = 0;
static volatile uintptr_t g_fault_addr = 0;

static void segv_handler(int sig, siginfo_t *info, void *uctx)
{
    (void)sig;
    g_segv_caught = 1;
    g_fault_addr = (uintptr_t)info->si_addr;

    ucontext_t *ctx = (ucontext_t *)uctx;
    // No Linux x86 32-bit: ctx->uc_mcontext.gregs[REG_EIP] é o EIP onde ocorreu a falha
    uintptr_t eip = ctx->uc_mcontext.gregs[REG_EIP];

    // Para fins do teste, alteramos a proteção da página para RW para permitir retomar a execução
    mprotect((void*)(g_fault_addr & ~0xFFFu), 4096, PROT_READ | PROT_WRITE);
}

int main()
{
    printf("[+] Executando Testes de Layout de Memória e Proteção de Páginas (Linux x86)...\n");

    // 1. Instalar Signal Handler para SIGSEGV (equivalente ao Vectored Exception Handler do Windows)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);

    // 2. Mapeamento da Imagem do XBE em 0x00010000 (8 MB)
    constexpr uintptr_t XBE_BASE = 0x00010000;
    constexpr size_t XBE_SIZE = 0x00800000; // 8 MB

    void *xbe_mem = mmap((void*)XBE_BASE, XBE_SIZE,
                         PROT_READ | PROT_WRITE | PROT_EXEC,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (xbe_mem == MAP_FAILED) {
        perror("mmap 0x10000");
        return 1;
    }
    assert(xbe_mem == (void*)XBE_BASE);
    printf("    [PASS] Imagem XBE mapeada com sucesso em %p - %p (%zu MB)\n",
           xbe_mem, (void*)((uintptr_t)xbe_mem + XBE_SIZE), XBE_SIZE >> 20);

    // Escrever e ler na área do XBE
    uint32_t *header = (uint32_t*)xbe_mem;
    header[0] = 0x484F4F4B; // 'HOOK'
    header[1] = 0x12345678;
    assert(header[0] == 0x484F4F4B && header[1] == 0x12345678);

    // 3. Mapeamento da RAM Contígua em 0x80000000 (64 MB)
    constexpr uintptr_t CONTIG_BASE = 0x80000000;
    constexpr size_t CONTIG_SIZE = 64 * 1024 * 1024; // 64 MB

    void *contig_mem = mmap((void*)CONTIG_BASE, CONTIG_SIZE,
                            PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);

    if (contig_mem == MAP_FAILED) {
        perror("mmap 0x80000000");
        return 1;
    }
    assert(contig_mem == (void*)CONTIG_BASE);
    printf("    [PASS] RAM Contígua do Xbox mapeada em %p - %p (%zu MB)\n",
           contig_mem, (void*)((uintptr_t)contig_mem + CONTIG_SIZE), CONTIG_SIZE >> 20);

    // Validar escrita na ponta superior da RAM contígua (64 MB - 64 KB de GPU instance memory)
    uint32_t *gpu_instance = (uint32_t*)((uintptr_t)contig_mem + CONTIG_SIZE - 64 * 1024);
    gpu_instance[0] = 0xCAFEBABE;
    assert(gpu_instance[0] == 0xCAFEBABE);

    // 4. Teste de Proteção de Página e Tratamento de Exceção via Sinal
    printf("[+] Testando interceptação de violação de proteção de memória (mprotect + SIGSEGV)...\n");
    volatile uint32_t *protected_page = (volatile uint32_t*)((uintptr_t)xbe_mem + 0x20000);
    *protected_page = 0x11111111;

    // Tornar apenas leitura
    mprotect((void*)protected_page, 4096, PROT_READ);

    // Tentar escrever: deve disparar SIGSEGV e ser capturado pelo segv_handler
    *protected_page = 0x22222222;

    assert(g_segv_caught == 1);
    assert(g_fault_addr == (uintptr_t)protected_page);
    assert(*protected_page == 0x22222222);
    printf("    [PASS] SIGSEGV capturado com sucesso no endereço %p; proteção recuperada dinamicamente!\n", (void*)g_fault_addr);

    // Limpeza
    munmap(xbe_mem, XBE_SIZE);
    munmap(contig_mem, CONTIG_SIZE);

    printf("\n>>> [PASS] test_memory_layout: Espaço de endereçamento e tratamento de sinais ativos! <<<\n");
    return 0;
}
