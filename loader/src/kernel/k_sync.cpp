// Synchronization.
//
// Handle-based objects (NtCreateEvent, NtCreateSemaphore) are real Windows objects.
// Dispatcher objects that live in Xbox memory (KEVENT, KTIMER, KSEMAPHORE, critical sections) keep their
// state in the Xbox structure (SignalState is the truth, game code may read or write it directly); a host
// event per object address is only used to wake waiters.
#include "kernel/kexports.h"
#include "kernel/nt.h"
#include "xthread.h"

#include <unordered_map>

namespace {

CRITICAL_SECTION g_disp; // protects SignalState transitions and the tables below
std::unordered_map<const void *, HANDLE> g_wake;     // manual-reset wake events for dispatcher objects
std::unordered_map<const void *, uint64_t> g_gen;    // signal count per object: a notification-type signal
                                                     // releases every thread waiting at that moment
std::unordered_map<const void *, HANDLE> g_cs_event; // auto-reset events for critical sections
struct InitDisp {
    InitDisp() { InitializeCriticalSection(&g_disp); }
} g_init_disp;

HANDLE wake_event(const void *obj) // caller holds g_disp
{
    auto it = g_wake.find(obj);
    if (it != g_wake.end()) return it->second;
    HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_wake[obj] = h;
    return h;
}

HANDLE cs_event(const void *cs)
{
    EnterCriticalSection(&g_disp);
    auto it = g_cs_event.find(cs);
    HANDLE h;
    if (it != g_cs_event.end()) {
        h = it->second;
    } else {
        h = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_cs_event[cs] = h;
    }
    LeaveCriticalSection(&g_disp);
    return h;
}

// Try to satisfy a wait on obj; caller holds g_disp.
bool try_consume(XDISPATCHER_HEADER *h)
{
    switch (h->Type) {
    case XOBJ_EVENT_NOTIFICATION:
    case XOBJ_TIMER_NOTIFICATION:
        return h->SignalState > 0;
    case XOBJ_EVENT_SYNCHRONIZATION:
    case XOBJ_TIMER_SYNCHRONIZATION:
        if (h->SignalState > 0) {
            h->SignalState = 0;
            return true;
        }
        return false;
    case XOBJ_SEMAPHORE:
    case XOBJ_MUTANT:
        if (h->SignalState > 0) {
            h->SignalState--;
            return true;
        }
        return false;
    default:
        return h->SignalState > 0;
    }
}

std::string oa_name(const XOBJECT_ATTRIBUTES *oa)
{
    if (!oa || !oa->ObjectName) return {};
    std::string n = xstr(oa->ObjectName);
    for (char &c : n)
        if (c == '\\') c = '_';
    return n.empty() ? n : "xbox_" + n;
}

NTSTATUS wait_result(DWORD r)
{
    switch (r) {
    case WAIT_OBJECT_0: return XSTATUS_SUCCESS;
    case WAIT_TIMEOUT: return XSTATUS_TIMEOUT;
    case WAIT_IO_COMPLETION: return XSTATUS_USER_APC;
    case WAIT_ABANDONED: return 0x80;
    default: return XSTATUS_INVALID_HANDLE;
    }
}

} // namespace

NTSTATUS ksync_wait_object(void *object, bool alertable, const LARGE_INTEGER *timeout)
{
    auto *h = (XDISPATCHER_HEADER *)object;
    if (h->Type == XOBJ_THREAD) {
        if (XThread *t = xthread_from_kthread(object))
            return wait_result(WaitForSingleObjectEx(t->host_handle, ktimeout_ms(timeout), alertable));
    }
    DWORD ms = ktimeout_ms(timeout);
    ULONGLONG deadline = ms == INFINITE ? 0 : GetTickCount64() + ms;
    bool notification = h->Type == XOBJ_EVENT_NOTIFICATION || h->Type == XOBJ_TIMER_NOTIFICATION;
    EnterCriticalSection(&g_disp);
    uint64_t gen0 = g_gen[object];
    LeaveCriticalSection(&g_disp);
    for (;;) {
        EnterCriticalSection(&g_disp);
        if (try_consume(h) || (notification && g_gen[object] != gen0)) {
            LeaveCriticalSection(&g_disp);
            return XSTATUS_SUCCESS;
        }
        HANDLE ev = wake_event(object);
        ResetEvent(ev); // object is not signaled: any earlier wake-up is stale
        LeaveCriticalSection(&g_disp);
        DWORD wait_ms = INFINITE;
        if (ms != INFINITE) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline) return XSTATUS_TIMEOUT;
            wait_ms = (DWORD)(deadline - now);
        }
        DWORD r = WaitForSingleObjectEx(ev, wait_ms, alertable);
        if (r == WAIT_IO_COMPLETION) return XSTATUS_USER_APC;
    }
}

void ksync_signal_object(void *object)
{
    auto *h = (XDISPATCHER_HEADER *)object;
    EnterCriticalSection(&g_disp);
    h->SignalState = 1;
    g_gen[object]++;
    auto it = g_wake.find(object);
    if (it != g_wake.end()) SetEvent(it->second);
    LeaveCriticalSection(&g_disp);
}

namespace xk {

NTSTATUS __stdcall NtCreateEvent(HANDLE *EventHandle, XOBJECT_ATTRIBUTES *ObjectAttributes, uint32_t EventType,
                                 BOOLEAN InitialState)
{
    std::string name = oa_name(ObjectAttributes);
    HANDLE h = CreateEventA(nullptr, EventType == 0 /* NotificationEvent = manual reset */, InitialState,
                            name.empty() ? nullptr : name.c_str());
    DWORD err = GetLastError();
    KTRACE("%p, '%s', %u, %u) -> %p (", EventHandle, name.c_str(), EventType, InitialState, h);
    if (!h) return XSTATUS_NO_MEMORY;
    *EventHandle = h;
    return err == ERROR_ALREADY_EXISTS ? 0x40000000 /* STATUS_OBJECT_NAME_EXISTS */ : XSTATUS_SUCCESS;
}

NTSTATUS __stdcall NtSetEvent(HANDLE EventHandle, int32_t *PreviousState)
{
    KTRACE("%p", EventHandle);
    LONG prev = 0;
    NTSTATUS s = host::NtSetEvent(EventHandle, &prev);
    if (PreviousState) *PreviousState = prev;
    return s;
}

NTSTATUS __stdcall NtCreateSemaphore(HANDLE *SemaphoreHandle, XOBJECT_ATTRIBUTES *ObjectAttributes, int32_t InitialCount,
                                     int32_t MaximumCount)
{
    std::string name = oa_name(ObjectAttributes);
    HANDLE h = CreateSemaphoreA(nullptr, InitialCount, MaximumCount, name.empty() ? nullptr : name.c_str());
    KTRACE("%p, '%s', %d, %d) -> %p (", SemaphoreHandle, name.c_str(), InitialCount, MaximumCount, h);
    if (!h) return XSTATUS_INVALID_PARAMETER;
    *SemaphoreHandle = h;
    return XSTATUS_SUCCESS;
}

NTSTATUS __stdcall NtReleaseSemaphore(HANDLE SemaphoreHandle, int32_t ReleaseCount, int32_t *PreviousCount)
{
    KTRACE("%p, %d", SemaphoreHandle, ReleaseCount);
    LONG prev = 0;
    NTSTATUS s = host::NtReleaseSemaphore(SemaphoreHandle, ReleaseCount, &prev);
    if (PreviousCount) *PreviousCount = prev;
    return s;
}

NTSTATUS __stdcall NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, LARGE_INTEGER *Timeout)
{
    KTRACE("%p, %u, %lld", Handle, Alertable, Timeout ? Timeout->QuadPart : -1LL);
    return host::NtWaitForSingleObject(Handle, Alertable, Timeout);
}

NTSTATUS __stdcall NtWaitForSingleObjectEx(HANDLE Handle, uint8_t WaitMode, BOOLEAN Alertable, LARGE_INTEGER *Timeout)
{
    KTRACE("%p, %u, %u, %lld", Handle, WaitMode, Alertable, Timeout ? Timeout->QuadPart : -1LL);
    return host::NtWaitForSingleObject(Handle, Alertable, Timeout);
}

int32_t __stdcall KeSetEvent(XKEVENT *Event, int32_t Increment, BOOLEAN Wait)
{
    KTRACE("%p, %d, %u", Event, Increment, Wait);
    EnterCriticalSection(&g_disp);
    int32_t prev = Event->Header.SignalState;
    Event->Header.SignalState = 1;
    g_gen[Event]++;
    auto it = g_wake.find(Event);
    if (it != g_wake.end()) SetEvent(it->second);
    LeaveCriticalSection(&g_disp);
    return prev;
}

NTSTATUS __stdcall KeWaitForSingleObject(void *Object, uint32_t WaitReason, uint8_t WaitMode, BOOLEAN Alertable,
                                         LARGE_INTEGER *Timeout)
{
    KTRACE("%p (type %u), %u, %u, %u, %lld", Object, Object ? ((XDISPATCHER_HEADER *)Object)->Type : 0, WaitReason, WaitMode,
           Alertable, Timeout ? Timeout->QuadPart : -1LL);
    return ksync_wait_object(Object, Alertable != 0, Timeout);
}

// Critical sections use the Xbox layout (28 bytes): LockCount starts at -1, OwningThread is a KTHREAD*.

void __stdcall RtlInitializeCriticalSection(XRTL_CRITICAL_SECTION *cs)
{
    KTRACE("%p", cs);
    cs->Synchronization.Type = XOBJ_EVENT_SYNCHRONIZATION;
    cs->Synchronization.Absolute = 0;
    cs->Synchronization.Size = sizeof(XKEVENT) / 4;
    cs->Synchronization.Inserted = 0;
    cs->Synchronization.SignalState = 0;
    cs->Synchronization.WaitListHead.Flink = cs->Synchronization.WaitListHead.Blink = &cs->Synchronization.WaitListHead;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = nullptr;
    EnterCriticalSection(&g_disp);
    auto it = g_cs_event.find(cs);
    if (it != g_cs_event.end()) ResetEvent(it->second);
    LeaveCriticalSection(&g_disp);
}

void __stdcall RtlEnterCriticalSection(XRTL_CRITICAL_SECTION *cs)
{
    KTRACE("%p", cs);
    void *me = xthread_kthread(xthread_current());
    if (InterlockedIncrement((volatile LONG *)&cs->LockCount) == 0) {
        cs->OwningThread = me;
        cs->RecursionCount = 1;
        return;
    }
    if (cs->OwningThread == me) {
        cs->RecursionCount++;
        return;
    }
    WaitForSingleObject(cs_event(cs), INFINITE);
    cs->OwningThread = me;
    cs->RecursionCount = 1;
}

void __stdcall RtlLeaveCriticalSection(XRTL_CRITICAL_SECTION *cs)
{
    KTRACE("%p", cs);
    if (--cs->RecursionCount != 0) {
        InterlockedDecrement((volatile LONG *)&cs->LockCount);
        return;
    }
    cs->OwningThread = nullptr;
    if (InterlockedDecrement((volatile LONG *)&cs->LockCount) >= 0) SetEvent(cs_event(cs));
}

} // namespace xk
