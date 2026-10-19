// Rtl string/time helpers. RtlUnwind and RtlRaiseException are bound directly to ntdll (see ktable.cpp).
#include "kernel/kexports.h"
#include "kernel/nt.h"

#include <cctype>

namespace xk {

NTSTATUS __stdcall RtlAnsiStringToUnicodeString(XUNICODE_STRING *Destination, const XANSI_STRING *Source, BOOLEAN Allocate)
{
    KTRACE("%p, '%s', %u", Destination, xstr(Source).c_str(), Allocate);
    uint32_t n = Source->Length;
    if (Allocate) {
        Destination->Buffer = (wchar_t *)ExAllocatePoolWithTag((n + 1) * 2, 'grtS');
        if (!Destination->Buffer) return XSTATUS_NO_MEMORY;
        Destination->MaximumLength = (uint16_t)((n + 1) * 2);
    } else if (Destination->MaximumLength < n * 2) {
        return (NTSTATUS)0x80000005; // STATUS_BUFFER_OVERFLOW
    }
    for (uint32_t i = 0; i < n; i++) Destination->Buffer[i] = (wchar_t)(unsigned char)Source->Buffer[i];
    Destination->Length = (uint16_t)(n * 2);
    if (Destination->MaximumLength >= (n + 1) * 2) Destination->Buffer[n] = 0;
    return XSTATUS_SUCCESS;
}

NTSTATUS __stdcall RtlUnicodeStringToAnsiString(XANSI_STRING *Destination, const XUNICODE_STRING *Source, BOOLEAN Allocate)
{
    KTRACE("%p, %p, %u", Destination, Source, Allocate);
    uint32_t n = Source->Length / 2;
    if (Allocate) {
        Destination->Buffer = (char *)ExAllocatePoolWithTag(n + 1, 'grtS');
        if (!Destination->Buffer) return XSTATUS_NO_MEMORY;
        Destination->MaximumLength = (uint16_t)(n + 1);
    } else if (Destination->MaximumLength < n) {
        return (NTSTATUS)0x80000005;
    }
    for (uint32_t i = 0; i < n; i++) {
        wchar_t c = Source->Buffer[i];
        Destination->Buffer[i] = c < 256 ? (char)c : '?';
    }
    Destination->Length = (uint16_t)n;
    if (Destination->MaximumLength > n) Destination->Buffer[n] = 0;
    return XSTATUS_SUCCESS;
}

uint32_t __stdcall RtlCompareMemoryUlong(void *Source, uint32_t Length, uint32_t Pattern)
{
    KTRACE("%p, %u, 0x%x", Source, Length, Pattern);
    const uint32_t *p = (const uint32_t *)Source;
    uint32_t i = 0;
    for (; i < Length / 4; i++)
        if (p[i] != Pattern) break;
    return i * 4;
}

BOOLEAN __stdcall RtlEqualString(const XANSI_STRING *String1, const XANSI_STRING *String2, BOOLEAN CaseInSensitive)
{
    KTRACE("'%s', '%s', %u", xstr(String1).c_str(), xstr(String2).c_str(), CaseInSensitive);
    if (String1->Length != String2->Length) return FALSE;
    for (uint32_t i = 0; i < String1->Length; i++) {
        char a = String1->Buffer[i], b = String2->Buffer[i];
        if (CaseInSensitive) {
            a = (char)toupper((unsigned char)a);
            b = (char)toupper((unsigned char)b);
        }
        if (a != b) return FALSE;
    }
    return TRUE;
}

void __stdcall RtlInitAnsiString(XANSI_STRING *Destination, const char *Source)
{
    KTRACE("%p, '%s'", Destination, Source ? Source : "(null)");
    Destination->Buffer = (char *)Source;
    if (Source) {
        size_t n = strlen(Source);
        Destination->Length = (uint16_t)n;
        Destination->MaximumLength = (uint16_t)(n + 1);
    } else {
        Destination->Length = Destination->MaximumLength = 0;
    }
}

uint32_t __stdcall RtlNtStatusToDosError(NTSTATUS Status)
{
    KTRACE("0x%08x", Status);
    return host::RtlNtStatusToDosError(Status);
}

BOOLEAN __stdcall RtlTimeFieldsToTime(void *TimeFields, LARGE_INTEGER *Time)
{
    KTRACE("%p, %p", TimeFields, Time);
    return host::RtlTimeFieldsToTime((host::TIME_FIELDS *)TimeFields, Time);
}

void __stdcall RtlTimeToTimeFields(LARGE_INTEGER *Time, void *TimeFields)
{
    KTRACE("%p, %p", Time, TimeFields);
    host::RtlTimeToTimeFields(Time, (host::TIME_FIELDS *)TimeFields);
}

} // namespace xk
