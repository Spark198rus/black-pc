// Test Suite: KPCR and %fs Segment Register Concurrency Validation on Linux x86
#include <cstdio>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <vector>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/syscall.h>
#include <asm/ldt.h>

struct XboxKPCR {
    uint8_t  padding1[4];
    uint32_t tls_array;         // fs:[0x04]
    uint8_t  padding2[24];
    uint32_t prcb;              // fs:[0x20]
    uint8_t  irql;              // fs:[0x24]
    uint8_t  padding3[3];
    uint32_t current_thread;    // fs:[0x28]
    uint8_t  padding4[44];
    uint32_t dpc_active;        // fs:[0x58]
    uint8_t  padding5[100];
};

void worker_thread(int thread_id, std::atomic<bool> *start_flag, std::atomic<int> *errors)
{
    // Esperar sinal de largada para máxima concorrência
    while (!start_flag->load()) {
        std::this_thread::yield();
    }

    XboxKPCR kpcr;
    memset(&kpcr, 0, sizeof(kpcr));

    uint32_t expected_thread_ptr = 0x10000000 + thread_id * 0x1000;
    uint32_t expected_tls = 0x20000000 + thread_id * 0x1000;
    uint8_t expected_irql = (uint8_t)(thread_id + 1);

    kpcr.tls_array = expected_tls;
    kpcr.irql = expected_irql;
    kpcr.current_thread = expected_thread_ptr;
    kpcr.prcb = (uint32_t)(uintptr_t)&kpcr;

    struct user_desc u_info;
    memset(&u_info, 0, sizeof(u_info));
    u_info.entry_number = -1; // Alocar nova entrada GDT
    u_info.base_addr = (unsigned long)&kpcr;
    u_info.limit = 0x1000;
    u_info.seg_32bit = 1;
    u_info.contents = MODIFY_LDT_CONTENTS_DATA;
    u_info.read_exec_only = 0;
    u_info.limit_in_pages = 0;
    u_info.seg_not_present = 0;
    u_info.useable = 1;

    int ret = syscall(SYS_set_thread_area, &u_info);
    if (ret != 0) {
        perror("set_thread_area");
        errors->fetch_add(1);
        return;
    }

    unsigned short fs_selector = (u_info.entry_number << 3) | 3;
    __asm__ volatile ("mov %0, %%fs" : : "r"(fs_selector));

    // Executar 200.000 iterações com preempções concorrentes
    for (int i = 0; i < 200000; i++) {
        uint32_t read_tls = 0;
        uint32_t read_thread = 0;
        uint32_t read_irql = 0;

        __asm__ volatile (
            "mov %%fs:0x04, %0\n"
            "mov %%fs:0x28, %1\n"
            "movzbl %%fs:0x24, %2\n"
            : "=r"(read_tls), "=r"(read_thread), "=r"(read_irql)
        );

        if (read_tls != expected_tls || read_thread != expected_thread_ptr || (uint8_t)read_irql != expected_irql) {
            printf("[ERROR] Thread %d corrompida na iteração %d! Esperado: (tls=0x%x, thread=0x%x, irql=%u), Lido: (tls=0x%x, thread=0x%x, irql=%u)\n",
                   thread_id, i, expected_tls, expected_thread_ptr, expected_irql, read_tls, read_thread, read_irql);
            errors->fetch_add(1);
            return;
        }

        // Alterar o IRQL e checar se persiste após preempção
        uint8_t new_irql = (uint8_t)((i % 4));
        __asm__ volatile ("movb %0, %%fs:0x24" : : "q"(new_irql));

        if (i % 10000 == 0) {
            std::this_thread::yield();
        }

        uint32_t check_irql = 0;
        __asm__ volatile ("movzbl %%fs:0x24, %0" : "=r"(check_irql));
        if ((uint8_t)check_irql != new_irql) {
            printf("[ERROR] Thread %d perdeu modificação de IRQL! Esperado: %u, Lido: %u\n", thread_id, new_irql, check_irql);
            errors->fetch_add(1);
            return;
        }

        // Restaurar IRQL esperado
        __asm__ volatile ("movb %0, %%fs:0x24" : : "q"(expected_irql));
    }
}

int main()
{
    printf("[+] Executando Teste de Concorrência e Isolamento do Segmento %%fs (Linux x86)...\n");

    const int kThreadCount = 8;
    std::vector<std::thread> threads;
    std::atomic<bool> start_flag{false};
    std::atomic<int> errors{0};

    for (int i = 0; i < kThreadCount; i++) {
        threads.emplace_back(worker_thread, i, &start_flag, &errors);
    }

    printf("    Disparando %d threads concorrentes com KPCRs exclusivos via set_thread_area...\n", kThreadCount);
    start_flag.store(true);

    for (auto &t : threads) {
        t.join();
    }

    if (errors.load() == 0) {
        printf("    [PASS] Todas as %d threads completaram 200.000 iterações com 100%% de isolamento em %%fs!\n", kThreadCount);
        printf("\n>>> [PASS] test_kpcr_fs: O kernel Linux preserva %%fs perfeitamente entre context switches! <<<\n");
        return 0;
    } else {
        printf("    [FAIL] Falhas detectadas durante o teste de %%fs: %d erros.\n", errors.load());
        return 1;
    }
}
