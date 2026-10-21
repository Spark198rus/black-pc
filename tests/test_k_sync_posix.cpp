// Unit Test: POSIX Synchronization Primitives and Concurrency Stress
#include "loader/src/platform/k_sync_posix.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>

static XDISPATCHER_HEADER g_event;
static XRTL_CRITICAL_SECTION g_cs;
static bool g_worker_ran = false;

void *sync_worker(void *arg)
{
    (void)arg;
    usleep(50000); // 50ms delay
    g_worker_ran = true;
    xposix::KeSetEvent(&g_event, 0, false);
    return nullptr;
}

int main()
{
    printf("==========================================================\n");
    printf("     TESTE FASE 2: SUBSISTEMA DE SINCRONIZAÇÃO POSIX      \n");
    printf("==========================================================\n");

    xposix::sync_init();

    // 1. Testar Critical Section Recursiva
    printf("[+] Testando Critical Section com reentrância recursiva...\n");
    xposix::RtlInitializeCriticalSection(&g_cs);
    xposix::RtlEnterCriticalSection(&g_cs);
    xposix::RtlEnterCriticalSection(&g_cs);
    assert(g_cs.RecursionCount == 2 && "RecursionCount deve ser 2");
    xposix::RtlLeaveCriticalSection(&g_cs);
    xposix::RtlLeaveCriticalSection(&g_cs);
    printf("    [PASS] Critical Section suporta recursão perfeitamente!\n");

    // 2. Testar Evento de Sincronização entre Threads
    printf("\n[+] Testando KeInitializeEvent, KeSetEvent e KeWaitForSingleObject...\n");
    xposix::KeInitializeEvent(&g_event, XOBJ_SYNCHRONIZATION_EVENT, false);
    assert(g_event.SignalState == 0);

    pthread_t th;
    pthread_create(&th, nullptr, sync_worker, nullptr);

    printf("    Aguardando sinal da thread trabalhadora...\n");
    NTSTATUS status = xposix::KeWaitForSingleObject(&g_event, 0, 0, false, nullptr);
    assert(status == 0 && "KeWaitForSingleObject deve retornar STATUS_SUCCESS (0)");
    assert(g_worker_ran && "Worker thread deve ter executado");
    assert(g_event.SignalState == 0 && "Evento de sincronização deve resetar automaticamente para 0 após consumo");
    printf("    [PASS] Evento sinalizado e consumido com sucesso!\n");

    pthread_join(th, nullptr);

    // 3. Testar Timeout com NtCreateEvent e NtWaitForSingleObject
    printf("\n[+] Testando NtCreateEvent e timeout em NtWaitForSingleObject...\n");
    HANDLE h_event = nullptr;
    status = xposix::NtCreateEvent(&h_event, 0, nullptr, 0, false);
    assert(status == 0 && h_event != nullptr);

    LARGE_INTEGER timeout;
    timeout.QuadPart = -1000000LL; // 100ms relativo
    status = xposix::NtWaitForSingleObject(h_event, false, &timeout);
    assert(status == 0x00000102L && "Deve retornar STATUS_TIMEOUT (0x102)");
    printf("    [PASS] Timeout de 100ms expirou corretamente com STATUS_TIMEOUT!\n");

    xposix::NtSetEvent(h_event, nullptr);
    status = xposix::NtWaitForSingleObject(h_event, false, &timeout);
    assert(status == 0 && "Deve retornar STATUS_SUCCESS (0) imediatamente");
    printf("    [PASS] Evento sinalizado acordou imediatamente!\n");

    xposix::NtClose(h_event);

    // 4. Testar Contenção Real Multithread com 4 threads na Critical Section
    printf("\n[+] Testando contenção real multithread com 4 threads na Critical Section...\n");
    static volatile int g_shared_counter = 0;
    const int kIterations = 25000;
    const int kNumThreads = 4;
    pthread_t threads[kNumThreads];

    auto contention_worker = [](void *) -> void * {
        for (int i = 0; i < kIterations; ++i) {
            xposix::RtlEnterCriticalSection(&g_cs);
            int cur = g_shared_counter;
            g_shared_counter = cur + 1;
            xposix::RtlLeaveCriticalSection(&g_cs);
        }
        return nullptr;
    };

    for (int i = 0; i < kNumThreads; ++i) {
        pthread_create(&threads[i], nullptr, contention_worker, nullptr);
    }
    for (int i = 0; i < kNumThreads; ++i) {
        pthread_join(threads[i], nullptr);
    }
    printf("    Contador compartilhado final: %d (esperado: %d)\n",
           g_shared_counter, kNumThreads * kIterations);
    assert(g_shared_counter == kNumThreads * kIterations &&
           "Contador deve bater 100% sem perda de atualizações sob exclusão mútua!");
    printf("    [PASS] Exclusão mútua concorrente confirmada sem race conditions!\n");

    printf("\n>> TESTE DE SINCRONIZAÇÃO POSIX PASSOU COM SUCESSO!\n\n");
    return 0;
}
