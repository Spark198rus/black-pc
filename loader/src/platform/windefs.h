// Platform abstraction and Win32/NT type definitions for Linux x86 native build.
#pragma once

#if defined(_WIN32)
#include <windows.h>
#else

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <algorithm>

#if __has_include(<windows_base.h>)
#include <windows_base.h>
#endif

#undef __stdcall
#define __stdcall __attribute__((stdcall))

#ifndef __cdecl
#define __cdecl __attribute__((cdecl))
#endif

#ifndef __fastcall
#define __fastcall __attribute__((fastcall))
#endif

#ifndef __declspec
#define __declspec(x)
#endif

#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif

typedef struct _FILETIME {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef uint64_t  QWORD;
typedef int32_t   LONG;
typedef uint32_t  ULONG;
typedef uint32_t  UINT;
typedef int16_t   SHORT;
typedef uint16_t  USHORT;
typedef char      CHAR;
typedef wchar_t   WCHAR;
typedef int32_t   BOOL;
typedef uint8_t   BOOLEAN;
typedef const char* LPCCH;
typedef BOOL* LPBOOL;
typedef void*     HANDLE;
typedef void*     PVOID;
typedef void*     LPVOID;
typedef const void* LPCVOID;
typedef char*     LPSTR;
typedef const char* LPCSTR;
typedef wchar_t*  LPWSTR;
typedef const wchar_t* LPCWSTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef intptr_t  LONG_PTR;
typedef size_t    SIZE_T;
typedef int64_t   LONGLONG;
typedef uint64_t  ULONGLONG;
typedef uint32_t  ACCESS_MASK;
typedef LONG      NTSTATUS;
typedef LONG      HRESULT;
typedef DWORD*    PDWORD;
typedef DWORD*    LPDWORD;
typedef DWORD_PTR* PDWORD_PTR;
typedef ULONG*    PULONG;
typedef HANDLE*   PHANDLE;
typedef void*     HMODULE;
typedef void*     HWND;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef S_OK
#define S_OK ((HRESULT)0L)
#endif

#ifndef S_FALSE
#define S_FALSE ((HRESULT)1L)
#endif

#ifndef FAILED
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#endif

#ifndef SUCCEEDED
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif
#ifndef ERROR_IO_PENDING
#define ERROR_IO_PENDING 997L
#endif
#ifndef ERROR_DEVICE_NOT_CONNECTED
#define ERROR_DEVICE_NOT_CONNECTED 1167L
#endif

#ifndef GENERIC_READ
#define GENERIC_READ 0x80000000L
#define GENERIC_WRITE 0x40000000L
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002
#define FILE_SHARE_DELETE 0x00000004
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#endif

// Win32 Virtual-Key Codes for keyboard/mouse abstraction
#ifndef VK_LBUTTON
#define VK_LBUTTON        0x01
#define VK_RBUTTON        0x02
#define VK_CANCEL         0x03
#define VK_MBUTTON        0x04
#define VK_XBUTTON1       0x05
#define VK_XBUTTON2       0x06
#define VK_BACK           0x08
#define VK_TAB            0x09
#define VK_CLEAR          0x0C
#define VK_RETURN         0x0D
#define VK_SHIFT          0x10
#define VK_CONTROL        0x11
#define VK_MENU           0x12
#define VK_PAUSE          0x13
#define VK_CAPITAL        0x14
#define VK_ESCAPE         0x1B
#define VK_SPACE          0x20
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_SELECT         0x29
#define VK_PRINT          0x2A
#define VK_EXECUTE        0x2B
#define VK_SNAPSHOT       0x2C
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_HELP           0x2F
#define VK_LWIN           0x5B
#define VK_RWIN           0x5C
#define VK_APPS           0x5D
#define VK_NUMPAD0        0x60
#define VK_NUMPAD9        0x69
#define VK_MULTIPLY       0x6A
#define VK_ADD            0x6B
#define VK_SEPARATOR      0x6C
#define VK_SUBTRACT       0x6D
#define VK_DECIMAL        0x6E
#define VK_DIVIDE         0x6F
#define VK_F1             0x70
#define VK_F12            0x7B
#define VK_F24            0x87
#define VK_NUMLOCK        0x90
#define VK_SCROLL         0x91
#define VK_LSHIFT         0xA0
#define VK_RSHIFT         0xA1
#define VK_LCONTROL       0xA2
#define VK_RCONTROL       0xA3
#define VK_LMENU          0xA4
#define VK_RMENU          0xA5
#endif

#ifndef GetAsyncKeyState
extern "C" SHORT GetAsyncKeyState(int vKey);
#endif

#include <strings.h>
#ifndef _stricmp
#define _stricmp strcasecmp
#endif
#ifndef _strnicmp
#define _strnicmp strncasecmp
#endif

#ifndef MAPVK_VK_TO_VSC
#define MAPVK_VK_TO_VSC 0
#endif

inline UINT MapVirtualKeyW(UINT uCode, UINT) { return uCode; }
inline int GetKeyNameTextW(LONG, LPWSTR lpString, int cchSize) {
    if (!lpString || cchSize <= 0) return 0;
    lpString[0] = L'\0';
    return 0;
}

#ifndef GUID_DEFINED
typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    struct {
        DWORD LowPart;
        LONG  HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
#else
typedef LARGE_INTEGER *PLARGE_INTEGER;
#endif

typedef union _ULARGE_INTEGER {
    struct {
        DWORD LowPart;
        DWORD HighPart;
    };
    struct {
        DWORD LowPart;
        DWORD HighPart;
    } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _MEMORY_BASIC_INFORMATION {
    PVOID  BaseAddress;
    PVOID  AllocationBase;
    DWORD  AllocationProtect;
    SIZE_T RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
} MEMORY_BASIC_INFORMATION, *PMEMORY_BASIC_INFORMATION;

#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_WRITECOPY         0x08
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80
#define PAGE_GUARD            0x100
#define PAGE_NOCACHE          0x200
#define PAGE_WRITECOMBINE     0x400

#define MEM_COMMIT             0x1000
#define MEM_RESERVE            0x2000
#define MEM_DECOMMIT           0x4000
#define MEM_RELEASE            0x8000
#define MEM_FREE              0x10000
#define MEM_PRIVATE           0x20000
#define MEM_MAPPED            0x40000
#define MEM_RESET             0x80000
#define MEM_TOP_DOWN         0x100000

#include <pthread.h>

struct CRITICAL_SECTION {
    pthread_mutex_t mutex;
};

inline void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}

inline void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutex_destroy(&cs->mutex);
}

inline void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutex_lock(&cs->mutex);
}

inline void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutex_unlock(&cs->mutex);
}

inline BOOL TryEnterCriticalSection(CRITICAL_SECTION *cs)
{
    return pthread_mutex_trylock(&cs->mutex) == 0;
}

typedef pthread_rwlock_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_RWLOCK_INITIALIZER
inline void AcquireSRWLockExclusive(SRWLOCK *lock) { pthread_rwlock_wrlock(lock); }
inline void ReleaseSRWLockExclusive(SRWLOCK *lock) { pthread_rwlock_unlock(lock); }
inline void AcquireSRWLockShared(SRWLOCK *lock) { pthread_rwlock_rdlock(lock); }
inline void ReleaseSRWLockShared(SRWLOCK *lock) { pthread_rwlock_unlock(lock); }

#include <sched.h>
#include <unistd.h>
#include <time.h>

#ifndef Sleep
inline void Sleep(DWORD dwMilliseconds) {
    if (dwMilliseconds == 0) {
        sched_yield();
    } else {
        struct timespec req;
        req.tv_sec = dwMilliseconds / 1000;
        req.tv_nsec = (dwMilliseconds % 1000) * 1000000L;
        nanosleep(&req, nullptr);
    }
}
#endif

#ifndef QueryPerformanceFrequency
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
    if (!lpFrequency) return FALSE;
    lpFrequency->QuadPart = 1000000000LL;
    return TRUE;
}
#endif

#ifndef QueryPerformanceCounter
inline BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    if (!lpPerformanceCount) return FALSE;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
#endif

#ifndef GetCurrentThread
inline HANDLE GetCurrentThread() { return (HANDLE)(uintptr_t)-2; }
#endif

#ifndef GetCurrentThreadId
inline DWORD GetCurrentThreadId() { return (DWORD)pthread_self(); }
#endif

#ifndef GetCurrentProcess
inline HANDLE GetCurrentProcess() { return (HANDLE)(uintptr_t)-1; }
#endif

#ifndef TerminateProcess
inline BOOL TerminateProcess(HANDLE, UINT uExitCode) { _exit((int)uExitCode); return TRUE; }
#endif

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

inline int WideCharToMultiByte(UINT, DWORD, LPCWSTR lpWideCharStr, int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH, LPBOOL) {
    if (!lpWideCharStr) return 0;
    int len = cchWideChar < 0 ? (int)wcslen(lpWideCharStr) : cchWideChar;
    if (cbMultiByte == 0 || !lpMultiByteStr) return len;
    int to_copy = std::min(len, cbMultiByte);
    for (int i = 0; i < to_copy; i++) lpMultiByteStr[i] = (char)lpWideCharStr[i];
    if (to_copy < cbMultiByte) lpMultiByteStr[to_copy] = '\0';
    return to_copy;
}

inline int MultiByteToWideChar(UINT, DWORD, LPCSTR lpMultiByteStr, int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar) {
    if (!lpMultiByteStr) return 0;
    int len = cbMultiByte < 0 ? (int)strlen(lpMultiByteStr) : cbMultiByte;
    if (cchWideChar == 0 || !lpWideCharStr) return len;
    int to_copy = std::min(len, cchWideChar);
    for (int i = 0; i < to_copy; i++) lpWideCharStr[i] = (wchar_t)(unsigned char)lpMultiByteStr[i];
    if (to_copy < cchWideChar) lpWideCharStr[to_copy] = L'\0';
    return to_copy;
}

#ifndef SetThreadPriority
inline BOOL SetThreadPriority(HANDLE, int) { return TRUE; }
#endif

#ifndef THREAD_PRIORITY_HIGHEST
#define THREAD_PRIORITY_HIGHEST 2
#endif

#ifndef THREAD_PRIORITY_TIME_CRITICAL
#define THREAD_PRIORITY_TIME_CRITICAL 15
#endif

#ifndef GetProcessAffinityMask
inline BOOL GetProcessAffinityMask(HANDLE, PDWORD_PTR lpProcessAffinityMask, PDWORD_PTR lpSystemAffinityMask) {
    if (lpProcessAffinityMask) *lpProcessAffinityMask = 1;
    if (lpSystemAffinityMask) *lpSystemAffinityMask = 1;
    return TRUE;
}
#endif

#ifndef SetThreadAffinityMask
inline DWORD_PTR SetThreadAffinityMask(HANDLE, DWORD_PTR) { return 1; }
#endif

inline void timeBeginPeriod(uint32_t) {}
inline void timeEndPeriod(uint32_t) {}

typedef struct _DEVMODEW {
    WCHAR dmDeviceName[32];
    WORD  dmSpecVersion;
    WORD  dmDriverVersion;
    WORD  dmSize;
    WORD  dmDriverExtra;
    DWORD dmFields;
    short dmOrientation;
    short dmPaperSize;
    short dmPaperLength;
    short dmPaperWidth;
    short dmScale;
    short dmCopies;
    short dmDefaultSource;
    short dmPrintQuality;
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    WCHAR dmFormName[32];
    WORD  dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    DWORD dmDisplayFlags;
    DWORD dmDisplayFrequency;
} DEVMODEW, *PDEVMODEW, *LPDEVMODEW;

inline BOOL EnumDisplaySettingsW(LPCWSTR, DWORD, DEVMODEW*) { return FALSE; }
inline HMODULE LoadLibraryW(LPCWSTR) { return nullptr; }
inline void* GetProcAddress(HMODULE, LPCSTR) { return nullptr; }

inline ULONGLONG GetTickCount64() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ULONGLONG)ts.tv_sec * 1000ULL + (ts.tv_nsec / 1000000ULL);
}

#include <sys/mman.h>

inline LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect) {
    (void)flAllocationType;
    int prot = PROT_READ | PROT_WRITE;
    if (flProtect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) prot |= PROT_EXEC;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (lpAddress) flags |= MAP_FIXED;
    void *p = mmap(lpAddress, dwSize, prot, flags, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
}

inline BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    (void)dwFreeType;
    return munmap(lpAddress, dwSize) == 0;
}

struct Win32EventObj {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool manual_reset;
    bool state;
};

inline HANDLE CreateEventW(void*, BOOL bManualReset, BOOL bInitialState, LPCWSTR) {
    auto *ev = new Win32EventObj();
    pthread_mutex_init(&ev->mutex, nullptr);
    pthread_cond_init(&ev->cond, nullptr);
    ev->manual_reset = bManualReset != 0;
    ev->state = bInitialState != 0;
    return (HANDLE)ev;
}

inline BOOL SetEvent(HANDLE hEvent) {
    if (!hEvent) return FALSE;
    auto *ev = (Win32EventObj*)hEvent;
    pthread_mutex_lock(&ev->mutex);
    ev->state = true;
    if (ev->manual_reset) {
        pthread_cond_broadcast(&ev->cond);
    } else {
        pthread_cond_signal(&ev->cond);
    }
    pthread_mutex_unlock(&ev->mutex);
    return TRUE;
}

inline BOOL ResetEvent(HANDLE hEvent) {
    if (!hEvent) return FALSE;
    auto *ev = (Win32EventObj*)hEvent;
    pthread_mutex_lock(&ev->mutex);
    ev->state = false;
    pthread_mutex_unlock(&ev->mutex);
    return TRUE;
}

inline DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    if (!hHandle) return 0xFFFFFFFF;
    auto *ev = (Win32EventObj*)hHandle;
    pthread_mutex_lock(&ev->mutex);
    DWORD ret = 0;
    if (!ev->state) {
        if (dwMilliseconds == 0) {
            ret = 0x00000102; // WAIT_TIMEOUT
        } else if (dwMilliseconds == 0xFFFFFFFF) {
            while (!ev->state) {
                pthread_cond_wait(&ev->cond, &ev->mutex);
            }
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += dwMilliseconds / 1000;
            ts.tv_nsec += (dwMilliseconds % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            while (!ev->state) {
                int r = pthread_cond_timedwait(&ev->cond, &ev->mutex, &ts);
                if (r != 0) {
                    ret = 0x00000102; // WAIT_TIMEOUT
                    break;
                }
            }
        }
    }
    if (ret == 0 && !ev->manual_reset) {
        ev->state = false;
    }
    pthread_mutex_unlock(&ev->mutex);
    return ret;
}

#include <sys/stat.h>
#include <string>
#include <cstdio>

inline BOOL CreateDirectoryW(LPCWSTR lpPathName, void*) {
    if (!lpPathName) return FALSE;
    std::wstring ws(lpPathName);
    std::string s(ws.begin(), ws.end());
    for (char &c : s) { if (c == '\\') c = '/'; }
    return mkdir(s.c_str(), 0755) == 0;
}

inline BOOL DeleteFileW(LPCWSTR lpFileName) {
    if (!lpFileName) return FALSE;
    std::wstring ws(lpFileName);
    std::string s(ws.begin(), ws.end());
    for (char &c : s) { if (c == '\\') c = '/'; }
    return unlink(s.c_str()) == 0;
}

inline int posix_sscanf_s(const char *buf, const char *fmt, char *k, unsigned, char *v, unsigned) {
    return sscanf(buf, fmt, k, v);
}
#ifndef sscanf_s
#define sscanf_s posix_sscanf_s
#endif

inline int _wfopen_s(FILE **pfile, const wchar_t *filename, const wchar_t *mode) {
    if (!pfile || !filename || !mode) return 22;
    std::wstring wfn(filename), wm(mode);
    std::string sfn(wfn.begin(), wfn.end()), sm(wm.begin(), wm.end());
    for (char &c : sfn) { if (c == '\\') c = '/'; }
    *pfile = fopen(sfn.c_str(), sm.c_str());
    return *pfile ? 0 : 2;
}

inline FILE *_wfopen(const wchar_t *filename, const wchar_t *mode) {
    if (!filename || !mode) return nullptr;
    std::wstring wfn(filename), wm(mode);
    std::string sfn(wfn.begin(), wfn.end()), sm(wm.begin(), wm.end());
    for (char &c : sfn) { if (c == '\\') c = '/'; }
    return fopen(sfn.c_str(), sm.c_str());
}

inline DWORD GetLastError() { return 0; }
inline int gmtime_s(struct tm *result, const time_t *timep) {
    return gmtime_r(timep, result) ? 0 : -1;
}

#include <fcntl.h>
#include <unistd.h>

inline HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD, void*, DWORD dwCreationDisposition, DWORD, HANDLE) {
    if (!lpFileName) return (HANDLE)(intptr_t)-1;
    std::wstring ws(lpFileName);
    std::string s(ws.begin(), ws.end());
    for (char &c : s) { if (c == '\\') c = '/'; }

    int flags = 0;
    if ((dwDesiredAccess & GENERIC_READ) && (dwDesiredAccess & GENERIC_WRITE)) flags = O_RDWR;
    else if (dwDesiredAccess & GENERIC_WRITE) flags = O_WRONLY;
    else flags = O_RDONLY;

    if (dwCreationDisposition == CREATE_ALWAYS) flags |= O_CREAT | O_TRUNC;
    else if (dwCreationDisposition == CREATE_NEW) flags |= O_CREAT | O_EXCL;
    else if (dwCreationDisposition == OPEN_ALWAYS) flags |= O_CREAT;

    int fd = open(s.c_str(), flags, 0666);
    return (fd < 0) ? (HANDLE)(intptr_t)-1 : (HANDLE)(intptr_t)(fd + 100);
}

inline BOOL CloseHandle(HANDLE hObject) {
    if (!hObject || hObject == (HANDLE)(intptr_t)-1) return FALSE;
    intptr_t val = (intptr_t)hObject;
    if (val >= 100) {
        close((int)(val - 100));
        return TRUE;
    }
    return TRUE;
}

inline BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, void*) {
    if (!hFile || hFile == (HANDLE)(intptr_t)-1) return FALSE;
    int fd = (int)((intptr_t)hFile - 100);
    ssize_t rd = read(fd, lpBuffer, nNumberOfBytesToRead);
    if (rd < 0) return FALSE;
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = (DWORD)rd;
    return TRUE;
}

inline BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, void*) {
    if (!hFile || hFile == (HANDLE)(intptr_t)-1) return FALSE;
    int fd = (int)((intptr_t)hFile - 100);
    ssize_t wr = write(fd, lpBuffer, nNumberOfBytesToWrite);
    if (wr < 0) return FALSE;
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = (DWORD)wr;
    return TRUE;
}

inline BOOL GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize) {
    if (!hFile || hFile == (HANDLE)(intptr_t)-1 || !lpFileSize) return FALSE;
    int fd = (int)((intptr_t)hFile - 100);
    struct stat st;
    if (fstat(fd, &st) != 0) return FALSE;
    lpFileSize->QuadPart = st.st_size;
    return TRUE;
}

inline BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod) {
    if (!hFile || hFile == (HANDLE)(intptr_t)-1) return FALSE;
    int fd = (int)((intptr_t)hFile - 100);
    int whence = SEEK_SET;
    if (dwMoveMethod == FILE_CURRENT) whence = SEEK_CUR;
    else if (dwMoveMethod == FILE_END) whence = SEEK_END;
    off_t pos = lseek(fd, (off_t)liDistanceToMove.QuadPart, whence);
    if (pos == (off_t)-1) return FALSE;
    if (lpNewFilePointer) lpNewFilePointer->QuadPart = pos;
    return TRUE;
}

struct NT_TIB {
    void *ExceptionList;
    void *StackBase;
    void *StackLimit;
    void *SubSystemTib;
    void *FiberData;
    void *ArbitraryUserPointer;
    struct NT_TIB *Self;
};

inline void* NtCurrentTeb() {
    void *teb = nullptr;
#if defined(__i386__)
    __asm__ volatile ("movl %%fs:0, %0" : "=r" (teb));
#endif
    return teb;
}

#ifndef _AddressOfReturnAddress
#define _AddressOfReturnAddress() __builtin_frame_address(0)
#endif

#endif // _WIN32
