// Unit Test: POSIX Memory Management and Contiguous Physical Allocator
#include "loader/src/platform/k_memory_posix.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main()
{
    printf("==========================================================\n");
    printf("     TESTE KERNEL: GERENCIAMENTO DE MEMÓRIA POSIX & RAM   \n");
    printf("==========================================================\n");

    xposix::memory_init();

    // 1. Testar ExAllocatePoolWithTag e ExFreePool
    printf("[+] Testando ExAllocatePoolWithTag e ExFreePool (Heap)...\n");
    void *ptr1 = xposix::ExAllocatePoolWithTag(0, 1024, 0x41424344);
    assert(ptr1 != nullptr && "ExAllocatePoolWithTag deve alocar memória com sucesso");
    assert(((uintptr_t)ptr1 & 15) == 0 && "Memória deve ter alinhamento de 16 bytes");

    // Verificar se a memória foi zerada
    uint8_t zero_buf[64] = {0};
    assert(memcmp(ptr1, zero_buf, sizeof(zero_buf)) == 0 && "Memória alocada deve estar zerada");

    // Escrever dados e liberar
    memset(ptr1, 0x5A, 1024);
    xposix::ExFreePool(ptr1);
    printf("    [PASS] ExAllocatePoolWithTag & ExFreePool funcionando perfeitamente!\n");

    // 2. Testar MmAllocateContiguousMemory e MmGetPhysicalAddress
    printf("\n[+] Testando MmAllocateContiguousMemory (RAM Física em 0x80000000)...\n");
    void *phys_ptr = xposix::MmAllocateContiguousMemory(64 * 1024); // 64 KB
    assert(phys_ptr != nullptr && "MmAllocateContiguousMemory deve ter sucesso");
    assert((uintptr_t)phys_ptr >= 0x80000000 && (uintptr_t)phys_ptr < 0x84000000 &&
           "Ponteiro deve residir no range da RAM de 64MB do Xbox (0x80000000 - 0x84000000)");

    uint32_t phys_addr = xposix::MmGetPhysicalAddress(phys_ptr);
    printf("    Endereço Virtual: %p -> Endereço Físico Xbox: 0x%08X\n", phys_ptr, phys_addr);
    assert(phys_addr < (64 * 1024 * 1024) && "Endereço físico deve estar dentro dos 64MB");
    assert(phys_addr == ((uintptr_t)phys_ptr - 0x80000000) && "Endereço físico é offset relativo a 0x80000000");

    // Testar leitura/escrita na RAM mapeada
    memset(phys_ptr, 0xAA, 64 * 1024);
    assert(*(uint8_t *)phys_ptr == 0xAA);
    printf("    [PASS] Alocador contíguo de RAM validado com sucesso!\n");

    // 3. Testar NtAllocateVirtualMemory e NtFreeVirtualMemory
    printf("\n[+] Testando NtAllocateVirtualMemory e NtFreeVirtualMemory...\n");
    void *virt_ptr = nullptr;
    uint32_t region_size = 4096;
    NTSTATUS status = xposix::NtAllocateVirtualMemory(&virt_ptr, 0, &region_size, 0x1000, 0x04);
    assert(status == 0 && virt_ptr != nullptr);
    assert(region_size >= 4096);
    printf("    Memória virtual alocada em %p (tamanho %u bytes)\n", virt_ptr, region_size);

    *(uint32_t *)virt_ptr = 0xDEADBEEF;
    assert(*(uint32_t *)virt_ptr == 0xDEADBEEF);

    status = xposix::NtFreeVirtualMemory(&virt_ptr, &region_size, 0x8000);
    assert(status == 0 && virt_ptr != nullptr);
    printf("    [PASS] NtAllocateVirtualMemory & NtFreeVirtualMemory validados!\n");

    printf("\n>> TESTE DE MEMÓRIA POSIX PASSOU COM SUCESSO!\n\n");
    return 0;
}
