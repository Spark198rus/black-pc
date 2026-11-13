// Rtl string/time helpers. RtlUnwind and RtlRaiseException are bound directly to ntdll (see ktable.cpp).
#include "kernel/kexports.h"
#include "kernel/nt.h"

#include <cctype>

namespace xk {

// The 8-bit ("ANSI") strings are UTF-8 throughout the port: file paths (k_file.cpp resolve, widen), directory
// listings and these two conversions (issue #38: a save named in Chinese, the zh-CN fan translation's default
// profile name, was written but read back as "??" here and judged damaged; before, these mapped bytes 1:1 to
// Latin-1 and code points above 0xFF to '?', while the paths decoded the same bytes as UTF-8). ASCII is unchanged.
// Bytes that are not valid UTF-8 keep the old 1:1 mapping.
NTSTATUS __stdcall RtlAnsiStringToUnicodeString(XUNICODE_STRING *Destination, const XANSI_STRING *Source, BOOLEAN Allocate)
{
    KTRACE("%p, '%s', %u", Destination, xstr(Source).c_str(), Allocate);
    uint32_t n = Source->Length;
    int need = n ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, Source->Buffer, (int)n, nullptr, 0) : 0;
    bool utf8 = need > 0 || n == 0;
    uint32_t len = utf8 ? (uint32_t)need : n; // characters out
    if (Allocate) {
        Destination->Buffer = (wchar_t *)ExAllocatePoolWithTag((len + 1) * 2, 'grtS');
        if (!Destination->Buffer) return XSTATUS_NO_MEMORY;
        Destination->MaximumLength = (uint16_t)((len + 1) * 2);
    } else if (Destination->MaximumLength < len * 2) {
        return (NTSTATUS)0x80000005; // STATUS_BUFFER_OVERFLOW
    }
    if (utf8 && n) MultiByteToWideChar(CP_UTF8, 0, Source->Buffer, (int)n, Destination->Buffer, (int)len);
    else
        for (uint32_t i = 0; i < n; i++) Destination->Buffer[i] = (wchar_t)(unsigned char)Source->Buffer[i];
    Destination->Length = (uint16_t)(len * 2);
    if (Destination->MaximumLength >= (len + 1) * 2) Destination->Buffer[len] = 0;
    return XSTATUS_SUCCESS;
}

NTSTATUS __stdcall RtlUnicodeStringToAnsiString(XANSI_STRING *Destination, const XUNICODE_STRING *Source, BOOLEAN Allocate)
{
    KTRACE("%p, %p, %u", Destination, Source, Allocate);
    uint32_t n = Source->Length / 2;
    int need = n ? WideCharToMultiByte(CP_UTF8, 0, Source->Buffer, (int)n, nullptr, 0, nullptr, nullptr) : 0;
    uint32_t len = need > 0 ? (uint32_t)need : 0; // bytes out
    if (Allocate) {
        Destination->Buffer = (char *)ExAllocatePoolWithTag(len + 1, 'grtS');
        if (!Destination->Buffer) return XSTATUS_NO_MEMORY;
        Destination->MaximumLength = (uint16_t)(len + 1);
    } else if (Destination->MaximumLength < len) {
        return (NTSTATUS)0x80000005;
    }
    if (len) WideCharToMultiByte(CP_UTF8, 0, Source->Buffer, (int)n, Destination->Buffer, (int)len, nullptr, nullptr);
    Destination->Length = (uint16_t)len;
    if (Destination->MaximumLength > len) Destination->Buffer[len] = 0;
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
