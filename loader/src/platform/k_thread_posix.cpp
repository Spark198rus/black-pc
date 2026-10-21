// POSIX Thread Management and %fs KPCR Context Implementation for Xbox kernel
#include "k_thread_posix.h"

#include <asm/ldt.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unordered_map>

namespace xposix {

namespace {

pthread_key_t g_thread_key;
bool g_thread_key_init = false;
std::mutex g_threads_mutex;
std::unordered_map<HANDLE, XThread*> g_active_threads;
uint32_t g_next_thread_handle = 0x4000;
uint32_t g_thread_id_counter = 1;
uint32_t g_xapi_tls_index_ptr = 0;

void *posix_thread_trampoline(void *arg)
{
    XThread *t = (XThread *)arg;

    // 1. Setup per-thread KPCR segment via SYS_modify_ldt (avoids GDT TLS exhaustion)
    uint32_t ldt_idx = (t->host_tid - 1) % 8192;
    struct user_desc u_info{};
    u_info.entry_number = ldt_idx;
    u_info.base_addr = (unsigned long)&t->kpcr;
    u_info.limit = 0x1000;
    u_info.seg_32bit = 1;
    u_info.contents = MODIFY_LDT_CONTENTS_DATA;
    u_info.read_exec_only = 0;
    u_info.limit_in_pages = 0;
    u_info.seg_not_present = 0;
    u_info.useable = 1;

    int r = syscall(SYS_modify_ldt, 1, &u_info, sizeof(u_info));
    if (r == 0) {
        unsigned short fs_sel = (ldt_idx << 3) | 7; // LDT selector, RPL 3
        __asm__ volatile ("movw %0, %%fs" : : "rm"(fs_sel));
    } else {
        fprintf(stderr, "k_thread_posix: SYS_modify_ldt failed for thread '%s' (errno=%d: %s, ldt_idx=%u)\n",
                t->name, errno, strerror(errno), ldt_idx);
    }

    // 2. Initialize KPCR references
    t->kpcr.Prcb = &t->prcb;
    t->kpcr.CurrentThread = &t->ethread;
    t->kpcr.TlsData = t->tls_data;

    // Recalculate StackBase offset based on XapiTlsIndex if registered
    int32_t tls_idx = -37;
    if (g_xapi_tls_index_ptr != 0) {
        volatile int32_t *p_idx = (volatile int32_t *)(uintptr_t)g_xapi_tls_index_ptr;
        if (*p_idx < 0) {
            tls_idx = *p_idx;
        }
    }
    uint32_t sb_off = (uint32_t)(-tls_idx * 4);
    uint8_t *sb = t->tls_data + sb_off;
    for (int i = 0; i < 64; i++) {
        ((void **)sb)[i] = t->tls_data + 4;
    }
    t->kpcr.StackBase = sb;

    if (g_thread_key_init) {
        pthread_setspecific(g_thread_key, t);
    }

    printf("  [THREAD START] Thread '%s' (TID %u) iniciada! Routine: 0x%08X\n",
           t->name, t->host_tid, t->start_routine);

    // 3. Jump to the Xbox Start Routine
    if (t->cdecl_entry) {
        ((void (__cdecl *)())(uintptr_t)t->start_routine)();
    } else if (t->system_routine) {
        typedef void (__stdcall *SystemRoutineFn)(void *start_routine, void *start_ctx);
        ((SystemRoutineFn)(uintptr_t)t->system_routine)((void *)(uintptr_t)t->start_routine, (void *)(uintptr_t)t->start_context);
    } else {
        typedef void (__stdcall *StartRoutineFn)(void *start_ctx);
        ((StartRoutineFn)(uintptr_t)t->start_routine)((void *)(uintptr_t)t->start_context);
    }

    printf("  [THREAD END] Thread '%s' (TID %u) finalizou execução limpa.\n", t->name, t->host_tid);
    return nullptr;
}

} // namespace

void thread_subsystem_init()
{
    if (!g_thread_key_init) {
        pthread_key_create(&g_thread_key, nullptr);
        g_thread_key_init = true;
    }
}

void set_tls_index_ptr(uint32_t ptr)
{
    g_xapi_tls_index_ptr = ptr;
}

NTSTATUS kthread_create(HANDLE *out_handle, uint32_t *out_tid, uint32_t stack_size, uint32_t tls_size,
                        uint32_t start_routine, uint32_t start_context, uint32_t system_routine,
                        bool suspended, const char *name, bool cdecl_entry)
{
    (void)suspended;
    thread_subsystem_init();

    XThread *t = new XThread{};

    t->start_routine = start_routine;
    t->start_context = start_context;
    t->system_routine = system_routine;
    t->cdecl_entry = cdecl_entry;

    size_t actual_tls = (tls_size < 4096) ? 4096 : tls_size;
    t->tls_size = (uint32_t)actual_tls;
    t->tls_data = (uint8_t *)calloc(1, actual_tls + 4096);

    t->host_tid = ++g_thread_id_counter;
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "xbox-worker");

    // Initialize ETHREAD / KTHREAD structure
    t->ethread[0] = 6;                     // DISPATCHER_HEADER.Type = ThreadObject
    t->ethread[2] = (uint8_t)(0x110 / 4);  // Size of KTHREAD in dwords
    *(uint32_t *)t->tls_data = (uint32_t)(uintptr_t)(t->tls_data + 4);
    *(uint32_t *)(t->ethread + 0x28)  = (uint32_t)(uintptr_t)t->tls_data;
    *(uint32_t *)(t->ethread + 0x12C) = t->host_tid;
    *(uint32_t *)(t->ethread + 0x130) = t->start_routine;

    // Initialize KPRCB
    *(uint32_t *)(t->prcb + 0) = (uint32_t)(uintptr_t)t->ethread;

    // Determine TLS negative offset from XapiTlsIndex if registered, or default to 148 (0x94)
    int32_t tls_idx = -37;
    if (g_xapi_tls_index_ptr != 0) {
        volatile int32_t *p_idx = (volatile int32_t *)(uintptr_t)g_xapi_tls_index_ptr;
        if (*p_idx < 0) {
            tls_idx = *p_idx;
        }
    }
    uint32_t stack_base_offset = (uint32_t)(-tls_idx * 4);
    uint8_t *stack_base = t->tls_data + stack_base_offset;

    // Populate pointers for both positive and negative index lookups
    for (int i = 0; i < 64; i++) {
        t->tls_array[i] = t->tls_data + 4;
        ((void **)stack_base)[i] = t->tls_data + 4;
    }

    // Initialize KPCR
    t->kpcr.TlsData = t->tls_data;
    t->kpcr.StackBase = stack_base;
    t->kpcr.Prcb = t->prcb;
    t->kpcr.CurrentThread = t->ethread;

    // Allocate host stack (minimum 1 MB)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    size_t stack = stack_size < (1024 * 1024) ? (1024 * 1024) : stack_size;
    pthread_attr_setstacksize(&attr, stack);

    int r = pthread_create(&t->host_thread, &attr, posix_thread_trampoline, t);
    pthread_attr_destroy(&attr);

    if (r != 0) {
        delete t;
        return 0xC0000017L; // STATUS_NO_MEMORY
    }

    std::lock_guard<std::mutex> lock(g_threads_mutex);
    HANDLE h = (HANDLE)(uintptr_t)(g_next_thread_handle += 4);
    g_active_threads[h] = t;

    if (out_handle) *out_handle = h;
    if (out_tid) *out_tid = t->host_tid;

    return 0; // STATUS_SUCCESS
}

XThread *kthread_current()
{
    if (!g_thread_key_init) return nullptr;
    return (XThread *)pthread_getspecific(g_thread_key);
}

bool pin_to_cpu(int cpu_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
}

NTSTATUS PsCreateSystemThreadEx(HANDLE *out_handle, uint32_t ext_size, uint32_t stack_size, uint32_t tls_size,
                                uint32_t *out_tid, void *start_routine, void *start_context,
                                bool suspended, bool debugger_thread, void *system_routine)
{
    (void)ext_size; (void)debugger_thread;
    return kthread_create(out_handle, out_tid, stack_size, tls_size,
                          (uint32_t)(uintptr_t)start_routine, (uint32_t)(uintptr_t)start_context,
                          (uint32_t)(uintptr_t)system_routine, suspended, "xbox-sys-thread", false);
}

void PsTerminateSystemThread(NTSTATUS exit_status)
{
    (void)exit_status;
    pthread_exit(nullptr);
}

} // namespace xposix

XThread *xthread_current_or_null()
{
    return xposix::kthread_current();
}

XThread *xthread_current()
{
    return xposix::kthread_current();
}

static std::recursive_mutex g_dispatch_lock;

uint8_t xthread_raise_irql(uint8_t new_irql)
{
    XThread *t = xposix::kthread_current();
    if (!t) return 0;
    uint8_t old = t->kpcr.Irql;
    if (new_irql >= 2 && !t->holds_dispatch_lock) {
        g_dispatch_lock.lock();
        t->holds_dispatch_lock = true;
    }
    t->kpcr.Irql = new_irql;
    return old;
}

void xthread_lower_irql(uint8_t new_irql)
{
    XThread *t = xposix::kthread_current();
    if (!t) return;
    t->kpcr.Irql = new_irql;
    if (new_irql < 2 && t->holds_dispatch_lock) {
        t->holds_dispatch_lock = false;
        g_dispatch_lock.unlock();
    }
}

NTSTATUS kthread_create(HANDLE *out_handle, uint32_t *out_tid, uint32_t stack_size, uint32_t tls_size,
                        uint32_t start_routine, uint32_t start_context, uint32_t system_routine,
                        bool suspended, const char *name, bool cdecl_entry)
{
    return xposix::kthread_create(out_handle, out_tid, stack_size, tls_size,
                                 start_routine, start_context, system_routine,
                                 suspended, name, cdecl_entry);
}
