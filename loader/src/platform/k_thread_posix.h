// POSIX Thread Management and %fs KPCR Context for Xbox kernel
#pragma once

#include "loader/src/platform/windefs.h"

#include <cstdint>
#include <pthread.h>
#include <vector>

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

struct XThread {
    alignas(16) uint8_t ethread[0x200];
    alignas(16) uint8_t prcb[0x300];
    XBoxKPCR kpcr;
    uint8_t *tls_data = nullptr;
    uint32_t tls_size = 0;
    void    *tls_array[64] = {};
    pthread_t host_thread = 0;
    uint32_t host_tid = 0;
    uint32_t start_routine = 0;
    uint32_t start_context = 0;
    uint32_t system_routine = 0;
    bool cdecl_entry = false;
    bool holds_dispatch_lock = false;
    int  base_priority = 0;
    char name[32] = {};
};

namespace xposix {

void thread_subsystem_init();
void set_tls_index_ptr(uint32_t ptr);

NTSTATUS kthread_create(HANDLE *out_handle, uint32_t *out_tid, uint32_t stack_size, uint32_t tls_size,
                        uint32_t start_routine, uint32_t start_context, uint32_t system_routine,
                        bool suspended, const char *name, bool cdecl_entry = false);

XThread *kthread_current();
bool pin_to_cpu(int cpu_id);
NTSTATUS PsCreateSystemThreadEx(HANDLE *out_handle, uint32_t ext_size, uint32_t stack_size, uint32_t tls_size,
                                uint32_t *out_tid, void *start_routine, void *start_context,
                                bool suspended, bool debugger_thread, void *system_routine);

void PsTerminateSystemThread(NTSTATUS exit_status);

} // namespace xposix
