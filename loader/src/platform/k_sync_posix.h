// POSIX Synchronization implementation for Xbox kernel dispatcher objects
#pragma once

#include "loader/src/platform/windefs.h"

#include <cstdint>
#include <pthread.h>

enum XObjectType : uint8_t {
    XOBJ_NOTIFICATION_EVENT = 0,
    XOBJ_SYNCHRONIZATION_EVENT = 1,
    XOBJ_MUTANT = 2,
    XOBJ_PROCESS = 3,
    XOBJ_QUEUE = 4,
    XOBJ_SEMAPHORE = 5,
    XOBJ_THREAD = 6,
    XOBJ_NOTIFICATION_TIMER = 8,
    XOBJ_SYNCHRONIZATION_TIMER = 9,
};

#pragma pack(push, 1)
struct XLIST_ENTRY {
    uint32_t Flink;
    uint32_t Blink;
};

struct XDISPATCHER_HEADER {
    uint8_t  Type;
    uint8_t  Absolute;
    uint8_t  Size;
    uint8_t  Inserted;
    int32_t  SignalState;
    XLIST_ENTRY WaitListHead;
};

struct XRTL_CRITICAL_SECTION {
    XDISPATCHER_HEADER Synchronization; // +0x00 (16 bytes)
    int32_t  LockCount;                 // +0x10 (4 bytes)
    int32_t  RecursionCount;            // +0x14 (4 bytes)
    void    *OwningThread;              // +0x18 (4 bytes)
};
#pragma pack(pop)

static_assert(sizeof(XDISPATCHER_HEADER) == 16, "XDISPATCHER_HEADER must be 16 bytes");
static_assert(sizeof(XRTL_CRITICAL_SECTION) == 28, "XRTL_CRITICAL_SECTION must be 28 bytes");

namespace xposix {

void sync_init();

// Xbox Dispatcher Objects (Events, Timers, Semaphores)
void     KeInitializeEvent(XDISPATCHER_HEADER *event, uint32_t type, bool state);
int32_t  KeSetEvent(XDISPATCHER_HEADER *event, int32_t increment, bool wait);
int32_t  KeResetEvent(XDISPATCHER_HEADER *event);
NTSTATUS KeWaitForSingleObject(void *object, uint32_t wait_reason, uint8_t wait_mode, bool alertable, const LARGE_INTEGER *timeout);

// Xbox Critical Sections
void RtlInitializeCriticalSection(XRTL_CRITICAL_SECTION *cs);
void RtlEnterCriticalSection(XRTL_CRITICAL_SECTION *cs);
void RtlLeaveCriticalSection(XRTL_CRITICAL_SECTION *cs);

// Handle-based events (NtCreateEvent, NtSetEvent, etc.)
NTSTATUS NtCreateEvent(HANDLE *out_handle, void *obj_attr, uint32_t event_type, bool initial_state);
NTSTATUS NtCreateEvent(HANDLE *out_handle, ACCESS_MASK desired_access, void *obj_attr, uint32_t event_type, bool initial_state);
NTSTATUS NtCreateSemaphore(HANDLE *out_handle, void *obj_attr, int32_t initial_count, int32_t maximum_count);
NTSTATUS NtSetEvent(HANDLE handle, int32_t *previous_state);
NTSTATUS NtResetEvent(HANDLE handle, int32_t *previous_state);
NTSTATUS NtWaitForSingleObject(HANDLE handle, bool alertable, const LARGE_INTEGER *timeout);
NTSTATUS NtWaitForSingleObjectEx(HANDLE handle, uint8_t wait_mode, bool alertable, const LARGE_INTEGER *timeout);
NTSTATUS NtClose(HANDLE handle);

} // namespace xposix
