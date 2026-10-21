// Unit Test: POSIX Thread Management and %fs KPCR Context
#include "loader/src/platform/k_thread_posix.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

static volatile int g_thread_res = 0;
static volatile bool g_thread_done = false;

extern "C" void __stdcall worker_routine(void *ctx)
{
    uint32_t val = (uint32_t)(uintptr_t)ctx;
    printf("    [WORKER] Iniciando rotina da thread com argumento: 0x%08X\n", val);

    // Ler campo Prcb de %fs:[0x20]
    void *fs_prcb = nullptr;
    __asm__ volatile ("movl %%fs:0x20, %0" : "=r"(fs_prcb));
    printf("    [WORKER] Valor lido de %%fs:[0x20]: %p\n", fs_prcb);
    assert(fs_prcb != nullptr && "KPCR.Prcb em %fs:[0x20] deve ser um ponteiro válido");

    g_thread_res = val * 2;
    g_thread_done = true;
}

int main()
{
    printf("==========================================================\n");
    printf("     TESTE FASE 2: GERENCIAMENTO DE THREADS POSIX & %%fs  \n");
    printf("==========================================================\n");

    xposix::thread_subsystem_init();

    HANDLE h_thread = nullptr;
    uint32_t tid = 0;

    printf("[+] Criando thread de trabalho via kthread_create...\n");
    NTSTATUS status = xposix::kthread_create(&h_thread, &tid, 1024 * 1024, 0x1000,
                                            (uint32_t)(uintptr_t)&worker_routine,
                                            0x1234, 0, false, "test-worker");

    assert(status == 0 && h_thread != nullptr);
    printf("    [PASS] Thread criada com sucesso! Handle: %p, TID: %u\n", h_thread, tid);

    // Aguardar execução
    for (int i = 0; i < 50 && !g_thread_done; ++i) {
        usleep(10000); // 10ms
    }

    assert(g_thread_done && "Worker thread deve ter executado");
    assert(g_thread_res == 0x1234 * 2 && "Resultado da thread deve ser 0x2468");
    printf("    [PASS] Thread concluiu com sucesso! Resultado: 0x%04X (esperado: 0x%04X)\n",
           g_thread_res, 0x1234 * 2);

    printf("\n>> TESTE DE THREADS POSIX PASSOU COM SUCESSO!\n\n");
    return 0;
}
