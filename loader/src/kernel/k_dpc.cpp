// IRQL, DPCs and kernel timers. One worker thread (an Xbox-context thread, so Xbox code it calls sees
// the patched fs slots) fires expired timers and runs queued DPCs at DISPATCH_LEVEL.
#include "kernel/kexports.h"
#include "xthread.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace {

CRITICAL_SECTION g_q;
HANDLE g_wake;
std::deque<XKDPC *> g_dpcs;
std::vector<XKTIMER *> g_timers;

void remove_timer(XKTIMER *t) // caller holds g_q
{
    g_timers.erase(std::remove(g_timers.begin(), g_timers.end(), t), g_timers.end());
    t->Header.Inserted = 0;
}

void __stdcall dpc_worker(void *)
{
    xthread_current()->base_priority = 15;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        std::vector<std::pair<XKDPC *, int64_t>> run;
        DWORD wait_ms = INFINITE;
        EnterCriticalSection(&g_q);
        int64_t now = ktime_now_100ns();
        for (size_t i = 0; i < g_timers.size();) {
            XKTIMER *t = g_timers[i];
            if ((int64_t)t->DueTime.QuadPart <= now) {
                if (t->Period > 0) {
                    t->DueTime.QuadPart += (uint64_t)t->Period * 10000;
                    if ((int64_t)t->DueTime.QuadPart < now) t->DueTime.QuadPart = now + (uint64_t)t->Period * 10000;
                    i++;
                } else {
                    g_timers.erase(g_timers.begin() + i);
                    t->Header.Inserted = 0;
                }
                ksync_signal_object(t);
                if (t->Dpc && !t->Dpc->Inserted) {
                    t->Dpc->Inserted = 1;
                    t->Dpc->SystemArgument1 = (void *)(uintptr_t)(uint32_t)now;
                    t->Dpc->SystemArgument2 = (void *)(uintptr_t)(uint32_t)(now >> 32);
                    g_dpcs.push_back(t->Dpc);
                }
            } else {
                i++;
            }
        }
        for (XKTIMER *t : g_timers) {
            int64_t ms = ((int64_t)t->DueTime.QuadPart - now + 9999) / 10000;
            if (ms < 0) ms = 0;
            if ((DWORD)ms < wait_ms) wait_ms = (DWORD)ms;
        }
        while (!g_dpcs.empty()) {
            XKDPC *d = g_dpcs.front();
            g_dpcs.pop_front();
            d->Inserted = 0;
            run.push_back({d, 0});
        }
        LeaveCriticalSection(&g_q);

        for (auto &r : run) {
            XKDPC *d = r.first;
            uint8_t old = xthread_raise_irql(2);
            xthread_set_dpc_active(true);
            d->DeferredRoutine(d, d->DeferredContext, d->SystemArgument1, d->SystemArgument2);
            xthread_set_dpc_active(false);
            xthread_lower_irql(old);
        }
        if (run.empty()) WaitForSingleObject(g_wake, wait_ms);
    }
}

} // namespace

bool kdpc_init()
{
    InitializeCriticalSection(&g_q);
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return NT_OK(kthread_create(nullptr, nullptr, 0, 0, (uint32_t)(uintptr_t)&dpc_worker, 0, 0, false, "kernel-dpc"));
}

bool kdpc_insert(XKDPC *dpc, void *arg1, void *arg2)
{
    EnterCriticalSection(&g_q);
    bool ok = !dpc->Inserted;
    if (ok) {
        dpc->Inserted = 1;
        dpc->SystemArgument1 = arg1;
        dpc->SystemArgument2 = arg2;
        g_dpcs.push_back(dpc);
    }
    LeaveCriticalSection(&g_q);
    if (ok) SetEvent(g_wake);
    return ok;
}

namespace xk {

uint8_t __stdcall KeRaiseIrqlToDpcLevel()
{
    KTRACE("");
    return xthread_raise_irql(2);
}

uint8_t __fastcall KfRaiseIrql(uint8_t NewIrql)
{
    KTRACE("%u", NewIrql);
    return xthread_raise_irql(NewIrql);
}

void __fastcall KfLowerIrql(uint8_t NewIrql)
{
    KTRACE("%u", NewIrql);
    xthread_lower_irql(NewIrql);
}

void __stdcall KeInitializeDpc(XKDPC *Dpc, XKDEFERRED_ROUTINE DeferredRoutine, void *DeferredContext)
{
    KTRACE("%p, %s, %p", Dpc, sym_name((uint32_t)(uintptr_t)DeferredRoutine).c_str(), DeferredContext);
    Dpc->Type = XOBJ_DPC;
    Dpc->Inserted = 0;
    Dpc->Padding = 0;
    Dpc->DpcListEntry.Flink = Dpc->DpcListEntry.Blink = nullptr;
    Dpc->DeferredRoutine = DeferredRoutine;
    Dpc->DeferredContext = DeferredContext;
    Dpc->SystemArgument1 = Dpc->SystemArgument2 = nullptr;
}

BOOLEAN __stdcall KeInsertQueueDpc(XKDPC *Dpc, void *SystemArgument1, void *SystemArgument2)
{
    KTRACE("%p, %p, %p", Dpc, SystemArgument1, SystemArgument2);
    return kdpc_insert(Dpc, SystemArgument1, SystemArgument2);
}

BOOLEAN __stdcall KeRemoveQueueDpc(XKDPC *Dpc)
{
    KTRACE("%p", Dpc);
    EnterCriticalSection(&g_q);
    bool was = Dpc->Inserted != 0;
    if (was) {
        g_dpcs.erase(std::remove(g_dpcs.begin(), g_dpcs.end(), Dpc), g_dpcs.end());
        Dpc->Inserted = 0;
    }
    LeaveCriticalSection(&g_q);
    return was;
}

void __stdcall KeInitializeTimerEx(XKTIMER *Timer, uint32_t Type)
{
    KTRACE("%p, %u", Timer, Type);
    Timer->Header.Type = (uint8_t)(XOBJ_TIMER_NOTIFICATION + Type);
    Timer->Header.Absolute = 0;
    Timer->Header.Size = sizeof(XKTIMER) / 4;
    Timer->Header.Inserted = 0;
    Timer->Header.SignalState = 0;
    Timer->Header.WaitListHead.Flink = Timer->Header.WaitListHead.Blink = &Timer->Header.WaitListHead;
    Timer->DueTime.QuadPart = 0;
    Timer->TimerListEntry.Flink = Timer->TimerListEntry.Blink = nullptr;
    Timer->Dpc = nullptr;
    Timer->Period = 0;
}

BOOLEAN __stdcall KeSetTimer(XKTIMER *Timer, LARGE_INTEGER DueTime, XKDPC *Dpc)
{
    KTRACE("%p, %lld, %p", Timer, DueTime.QuadPart, Dpc);
    EnterCriticalSection(&g_q);
    bool was = Timer->Header.Inserted != 0;
    if (was) remove_timer(Timer);
    Timer->Header.SignalState = 0;
    Timer->Dpc = Dpc;
    Timer->Period = 0;
    int64_t now = ktime_now_100ns();
    Timer->DueTime.QuadPart = DueTime.QuadPart < 0 ? now - DueTime.QuadPart : DueTime.QuadPart;
    Timer->Header.Inserted = 1;
    g_timers.push_back(Timer);
    LeaveCriticalSection(&g_q);
    SetEvent(g_wake);
    return was;
}

BOOLEAN __stdcall KeCancelTimer(XKTIMER *Timer)
{
    KTRACE("%p", Timer);
    EnterCriticalSection(&g_q);
    bool was = Timer->Header.Inserted != 0;
    if (was) remove_timer(Timer);
    LeaveCriticalSection(&g_q);
    return was;
}

} // namespace xk
