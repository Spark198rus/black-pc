// POSIX Synchronization implementation for Xbox kernel
#include "k_sync_posix.h"

#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <unordered_map>

namespace xposix {

namespace {

pthread_mutex_t g_disp_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_disp_cond  = PTHREAD_COND_INITIALIZER;
pthread_mutexattr_t g_rec_attr;
bool g_initialized = false;

struct HandleEvent {
    uint32_t type; // 0 = notification, 1 = synchronization
    int32_t  state;
};

std::unordered_map<HANDLE, HandleEvent*> g_handle_events;
uint32_t g_next_event_handle = 0x2000;

bool try_consume(XDISPATCHER_HEADER *h)
{
    if (!h) return false;
    switch (h->Type) {
    case XOBJ_NOTIFICATION_EVENT:
    case XOBJ_NOTIFICATION_TIMER:
        return h->SignalState > 0;
    case XOBJ_SYNCHRONIZATION_EVENT:
    case XOBJ_SYNCHRONIZATION_TIMER:
        if (h->SignalState > 0) {
            h->SignalState = 0; // auto-reset
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

timespec xbox_timeout_to_timespec(int64_t quad_part)
{
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    if (quad_part < 0) { // Relative timeout in 100-nanosecond units
        int64_t nanos = -quad_part * 100;
        int64_t sec = nanos / 1000000000LL;
        int64_t nsec = nanos % 1000000000LL;
        now.tv_sec += sec;
        now.tv_nsec += nsec;
        if (now.tv_nsec >= 1000000000L) {
            now.tv_sec++;
            now.tv_nsec -= 1000000000L;
        }
    }
    return now;
}

} // namespace

void sync_init()
{
    if (g_initialized) return;
    pthread_mutexattr_init(&g_rec_attr);
    pthread_mutexattr_settype(&g_rec_attr, PTHREAD_MUTEX_RECURSIVE);
    g_initialized = true;
}

void KeInitializeEvent(XDISPATCHER_HEADER *event, uint32_t type, bool state)
{
    if (!event) return;
    event->Type = (uint8_t)type; // 0 = Notification, 1 = Synchronization
    event->Absolute = 0;
    event->Size = sizeof(XDISPATCHER_HEADER) / 4;
    event->Inserted = 0;
    event->SignalState = state ? 1 : 0;
    event->WaitListHead.Flink = 0;
    event->WaitListHead.Blink = 0;
}

int32_t KeSetEvent(XDISPATCHER_HEADER *event, int32_t increment, bool wait)
{
    (void)increment; (void)wait;
    if (!event) return 0;

    pthread_mutex_lock(&g_disp_mutex);
    int32_t old_state = event->SignalState;
    event->SignalState = 1;
    pthread_cond_broadcast(&g_disp_cond);
    pthread_mutex_unlock(&g_disp_mutex);

    return old_state;
}

int32_t KeResetEvent(XDISPATCHER_HEADER *event)
{
    if (!event) return 0;

    pthread_mutex_lock(&g_disp_mutex);
    int32_t old_state = event->SignalState;
    event->SignalState = 0;
    pthread_mutex_unlock(&g_disp_mutex);

    return old_state;
}

NTSTATUS KeWaitForSingleObject(void *object, uint32_t wait_reason, uint8_t wait_mode, bool alertable, const LARGE_INTEGER *timeout)
{
    (void)wait_reason; (void)wait_mode; (void)alertable;
    XDISPATCHER_HEADER *h = (XDISPATCHER_HEADER *)object;
    if (!h) return 0; // STATUS_SUCCESS

    pthread_mutex_lock(&g_disp_mutex);

    if (timeout && timeout->QuadPart == 0) {
        bool ok = try_consume(h);
        pthread_mutex_unlock(&g_disp_mutex);
        return ok ? 0 : 0x00000102L; // STATUS_WAIT_0 or STATUS_TIMEOUT
    }

    if (!timeout) { // Infinite wait
        while (!try_consume(h)) {
            pthread_cond_wait(&g_disp_cond, &g_disp_mutex);
        }
        pthread_mutex_unlock(&g_disp_mutex);
        return 0; // STATUS_WAIT_0
    }

    // Timed wait
    timespec ts = xbox_timeout_to_timespec(timeout->QuadPart);
    while (!try_consume(h)) {
        int r = pthread_cond_timedwait(&g_disp_cond, &g_disp_mutex, &ts);
        if (r == ETIMEDOUT) {
            bool ok = try_consume(h);
            pthread_mutex_unlock(&g_disp_mutex);
            return ok ? 0 : 0x00000102L; // STATUS_TIMEOUT
        }
    }

    pthread_mutex_unlock(&g_disp_mutex);
    return 0; // STATUS_WAIT_0
}

static std::mutex g_cs_mutex;
static std::condition_variable g_cs_cv;

void RtlInitializeCriticalSection(XRTL_CRITICAL_SECTION *cs)
{
    if (!cs) return;
    cs->Synchronization.Type = XOBJ_SYNCHRONIZATION_EVENT;
    cs->Synchronization.Absolute = 0;
    cs->Synchronization.Size = sizeof(XDISPATCHER_HEADER) / 4;
    cs->Synchronization.Inserted = 0;
    cs->Synchronization.SignalState = 0;
    cs->Synchronization.WaitListHead.Flink = 0;
    cs->Synchronization.WaitListHead.Blink = 0;
    cs->LockCount = -1;
    cs->RecursionCount = 0;
    cs->OwningThread = nullptr;
}

void RtlEnterCriticalSection(XRTL_CRITICAL_SECTION *cs)
{
    if (!cs) return;
    void *me = nullptr;
#ifdef __i386__
    unsigned short fs_val = 0;
    __asm__ volatile ("movw %%fs, %0" : "=r"(fs_val));
    if (fs_val != 0) {
        __asm__ volatile ("movl %%fs:0x28, %0" : "=r"(me));
    }
#endif
    if (!me) {
        me = (void *)(uintptr_t)pthread_self();
    }

    std::unique_lock<std::mutex> lock(g_cs_mutex);
    if (cs->OwningThread == me) {
        cs->RecursionCount++;
        cs->LockCount++;
        return;
    }

    while (cs->OwningThread != nullptr) {
        g_cs_cv.wait(lock);
    }
    cs->OwningThread = me;
    cs->RecursionCount = 1;
    cs->LockCount = 0;
}

void RtlLeaveCriticalSection(XRTL_CRITICAL_SECTION *cs)
{
    if (!cs) return;
    std::unique_lock<std::mutex> lock(g_cs_mutex);
    if (--cs->RecursionCount == 0) {
        cs->OwningThread = nullptr;
        cs->LockCount = -1;
        g_cs_cv.notify_one();
    } else {
        cs->LockCount--;
    }
}

NTSTATUS NtCreateEvent(HANDLE *out_handle, void *obj_attr, uint32_t event_type, bool initial_state)
{
    return NtCreateEvent(out_handle, 0, obj_attr, event_type, initial_state);
}

NTSTATUS NtCreateEvent(HANDLE *out_handle, ACCESS_MASK desired_access, void *obj_attr, uint32_t event_type, bool initial_state)
{
    (void)desired_access; (void)obj_attr;
    if (!out_handle) return 0xC000000DL; // STATUS_INVALID_PARAMETER

    pthread_mutex_lock(&g_disp_mutex);
    HandleEvent *ev = new HandleEvent{event_type, initial_state ? 1 : 0};
    HANDLE h = (HANDLE)(uintptr_t)(g_next_event_handle += 4);
    g_handle_events[h] = ev;
    *out_handle = h;
    pthread_mutex_unlock(&g_disp_mutex);

    return 0; // STATUS_SUCCESS
}

NTSTATUS NtCreateSemaphore(HANDLE *out_handle, void *obj_attr, int32_t initial_count, int32_t maximum_count)
{
    (void)obj_attr; (void)maximum_count;
    if (!out_handle) return 0xC000000DL;

    pthread_mutex_lock(&g_disp_mutex);
    HandleEvent *ev = new HandleEvent{XOBJ_SEMAPHORE, initial_count};
    HANDLE h = (HANDLE)(uintptr_t)(g_next_event_handle += 4);
    g_handle_events[h] = ev;
    *out_handle = h;
    pthread_mutex_unlock(&g_disp_mutex);

    return 0;
}

NTSTATUS NtSetEvent(HANDLE handle, int32_t *previous_state)
{
    pthread_mutex_lock(&g_disp_mutex);
    auto it = g_handle_events.find(handle);
    if (it == g_handle_events.end()) {
        pthread_mutex_unlock(&g_disp_mutex);
        return 0xC0000008L; // STATUS_INVALID_HANDLE
    }
    if (previous_state) *previous_state = it->second->state;
    it->second->state = 1;
    pthread_cond_broadcast(&g_disp_cond);
    pthread_mutex_unlock(&g_disp_mutex);

    return 0;
}

NTSTATUS NtResetEvent(HANDLE handle, int32_t *previous_state)
{
    pthread_mutex_lock(&g_disp_mutex);
    auto it = g_handle_events.find(handle);
    if (it == g_handle_events.end()) {
        pthread_mutex_unlock(&g_disp_mutex);
        return 0xC0000008L; // STATUS_INVALID_HANDLE
    }
    if (previous_state) *previous_state = it->second->state;
    it->second->state = 0;
    pthread_mutex_unlock(&g_disp_mutex);

    return 0;
}

NTSTATUS NtWaitForSingleObject(HANDLE handle, bool alertable, const LARGE_INTEGER *timeout)
{
    (void)alertable;
    pthread_mutex_lock(&g_disp_mutex);
    auto it = g_handle_events.find(handle);
    if (it == g_handle_events.end()) {
        pthread_mutex_unlock(&g_disp_mutex);
        return 0xC0000008L; // STATUS_INVALID_HANDLE
    }
    HandleEvent *ev = it->second;

    if (timeout && timeout->QuadPart == 0) {
        bool ok = false;
        if (ev->state > 0) {
            if (ev->type == 1) ev->state = 0; // auto-reset
            ok = true;
        }
        pthread_mutex_unlock(&g_disp_mutex);
        return ok ? 0 : 0x00000102L;
    }

    if (!timeout) {
        while (ev->state <= 0) {
            pthread_cond_wait(&g_disp_cond, &g_disp_mutex);
        }
        if (ev->type == 1) ev->state = 0; // auto-reset
        pthread_mutex_unlock(&g_disp_mutex);
        return 0;
    }

    timespec ts = xbox_timeout_to_timespec(timeout->QuadPart);
    while (ev->state <= 0) {
        int r = pthread_cond_timedwait(&g_disp_cond, &g_disp_mutex, &ts);
        if (r == ETIMEDOUT) {
            bool ok = false;
            if (ev->state > 0) {
                if (ev->type == 1) ev->state = 0;
                ok = true;
            }
            pthread_mutex_unlock(&g_disp_mutex);
            return ok ? 0 : 0x00000102L;
        }
    }

    if (ev->type == 1) ev->state = 0;
    pthread_mutex_unlock(&g_disp_mutex);
    return 0;
}

NTSTATUS NtWaitForSingleObjectEx(HANDLE handle, uint8_t wait_mode, bool alertable, const LARGE_INTEGER *timeout)
{
    (void)wait_mode;
    return NtWaitForSingleObject(handle, alertable, timeout);
}

NTSTATUS NtClose(HANDLE handle)
{
    pthread_mutex_lock(&g_disp_mutex);
    auto it = g_handle_events.find(handle);
    if (it != g_handle_events.end()) {
        delete it->second;
        g_handle_events.erase(it);
        pthread_mutex_unlock(&g_disp_mutex);
        return 0;
    }
    pthread_mutex_unlock(&g_disp_mutex);
    return 0;
}

} // namespace xposix

void ksync_signal_object(void *object)
{
    if (object) {
        xposix::KeSetEvent((XDISPATCHER_HEADER *)object, 0, false);
    }
}
