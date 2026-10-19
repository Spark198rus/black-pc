// Xbox kernel exports implemented by the HLE kernel. Signatures follow the Xbox kernel ABI:
// stdcall unless marked __fastcall / __cdecl. Each is placed directly in the XBE's kernel thunk table.
#pragma once

#include "kernel/xboxkrnl.h"

namespace xk {

// Av / Hal / Ke hardware-facing (k_misc.cpp)
void *__stdcall AvGetSavedDataAddress();
void __stdcall AvSendTVEncoderOption(void *RegisterBase, uint32_t Option, uint32_t Param, uint32_t *Result);
uint32_t __stdcall AvSetDisplayMode(void *RegisterBase, uint32_t Step, uint32_t Mode, uint32_t Format, uint32_t Pitch,
                                    uint32_t FrameBuffer);
void __stdcall AvSetSavedDataAddress(void *Address);
uint32_t __cdecl DbgPrint(const char *Format, ...);
NTSTATUS __stdcall ExQueryNonVolatileSetting(uint32_t ValueIndex, uint32_t *Type, void *Value, uint32_t ValueLength,
                                             uint32_t *ResultLength);
uint32_t __stdcall HalGetInterruptVector(uint32_t BusInterruptLevel, uint8_t *Irql);
void __stdcall HalReadWritePCISpace(uint32_t BusNumber, uint32_t SlotNumber, uint32_t RegisterNumber, void *Buffer,
                                    uint32_t Length, BOOLEAN WritePCISpace);
void __stdcall HalRegisterShutdownNotification(void *ShutdownRegistration, BOOLEAN Register);
void __stdcall HalReturnToFirmware(uint32_t Routine);
void __stdcall HalInitiateShutdown();
void __fastcall IofCompleteRequest(void *Irp, char PriorityBoost);
void __stdcall KeBugCheck(uint32_t BugCheckCode);
BOOLEAN __stdcall KeConnectInterrupt(XKINTERRUPT *Interrupt);
BOOLEAN __stdcall KeDisconnectInterrupt(XKINTERRUPT *Interrupt);
void __stdcall KeInitializeInterrupt(XKINTERRUPT *Interrupt, void *ServiceRoutine, void *ServiceContext, uint32_t Vector,
                                     uint8_t Irql, uint32_t InterruptMode, BOOLEAN ShareVector);
BOOLEAN __stdcall KeSynchronizeExecution(XKINTERRUPT *Interrupt, void *SynchronizeRoutine, void *SynchronizeContext);
void __stdcall KeQuerySystemTime(LARGE_INTEGER *CurrentTime);
NTSTATUS __stdcall KeSaveFloatingPointState(void *FloatSave);
NTSTATUS __stdcall KeRestoreFloatingPointState(void *FloatSave);
NTSTATUS __stdcall XeLoadSection(void *Section);
NTSTATUS __stdcall XeUnloadSection(void *Section);
void __stdcall XcSHAInit(uint8_t *Context);
void __stdcall XcSHAUpdate(uint8_t *Context, const uint8_t *Input, uint32_t InputLength);
void __stdcall XcSHAFinal(uint8_t *Context, uint8_t *Digest);
void __stdcall XcHMAC(const uint8_t *Key, uint32_t KeyLength, const uint8_t *Data, uint32_t DataLength, const uint8_t *Data2,
                      uint32_t Data2Length, uint8_t *Digest);

// Memory (k_memory.cpp)
void *__stdcall ExAllocatePoolWithTag(uint32_t NumberOfBytes, uint32_t Tag);
void __stdcall ExFreePool(void *P);
uint32_t __stdcall ExQueryPoolBlockSize(void *PoolBlock);
void *__stdcall MmAllocateContiguousMemory(uint32_t NumberOfBytes);
void *__stdcall MmAllocateContiguousMemoryEx(uint32_t NumberOfBytes, uint32_t LowestAcceptableAddress,
                                             uint32_t HighestAcceptableAddress, uint32_t Alignment, uint32_t ProtectionType);
void *__stdcall MmClaimGpuInstanceMemory(uint32_t NumberOfBytes, uint32_t *NumberOfPaddingBytes);
void __stdcall MmFreeContiguousMemory(void *BaseAddress);
uint32_t __stdcall MmGetPhysicalAddress(void *BaseAddress);
void __stdcall MmLockUnlockBufferPages(void *BaseAddress, uint32_t NumberOfBytes, BOOLEAN UnlockPages);
void __stdcall MmLockUnlockPhysicalPage(uint32_t PhysicalAddress, BOOLEAN UnlockPage);
void __stdcall MmPersistContiguousMemory(void *BaseAddress, uint32_t NumberOfBytes, BOOLEAN Persist);
uint32_t __stdcall MmQueryAddressProtect(void *VirtualAddress);
uint32_t __stdcall MmQueryAllocationSize(void *BaseAddress);
void __stdcall MmSetAddressProtect(void *BaseAddress, uint32_t NumberOfBytes, uint32_t NewProtect);
NTSTATUS __stdcall NtAllocateVirtualMemory(void **BaseAddress, uint32_t ZeroBits, uint32_t *RegionSize, uint32_t AllocationType,
                                           uint32_t Protect);
NTSTATUS __stdcall NtFreeVirtualMemory(void **BaseAddress, uint32_t *RegionSize, uint32_t FreeType);
NTSTATUS __stdcall NtQueryVirtualMemory(void *BaseAddress, MEMORY_BASIC_INFORMATION *Buffer);

// Threads (k_thread.cpp)
NTSTATUS __stdcall PsCreateSystemThreadEx(HANDLE *ThreadHandle, uint32_t ThreadExtensionSize, uint32_t KernelStackSize,
                                          uint32_t TlsDataSize, uint32_t *ThreadId, void *StartRoutine, void *StartContext,
                                          BOOLEAN CreateSuspended, BOOLEAN DebuggerThread, void *SystemRoutine);
void __stdcall PsTerminateSystemThread(NTSTATUS ExitStatus);
int32_t __stdcall KeQueryBasePriorityThread(void *Thread);
int32_t __stdcall KeSetBasePriorityThread(void *Thread, int32_t Increment);
NTSTATUS __stdcall KeDelayExecutionThread(uint8_t WaitMode, BOOLEAN Alertable, LARGE_INTEGER *Interval);
void __stdcall KeStallExecutionProcessor(uint32_t MicroSeconds);
NTSTATUS __stdcall NtResumeThread(HANDLE ThreadHandle, uint32_t *PreviousSuspendCount);
NTSTATUS __stdcall NtSuspendThread(HANDLE ThreadHandle, uint32_t *PreviousSuspendCount);
NTSTATUS __stdcall NtYieldExecution();
NTSTATUS __stdcall ObReferenceObjectByHandle(HANDLE Handle, void *ObjectType, void **ReturnedObject);
void __fastcall ObfDereferenceObject(void *Object);

// Synchronization (k_sync.cpp)
NTSTATUS __stdcall NtCreateEvent(HANDLE *EventHandle, XOBJECT_ATTRIBUTES *ObjectAttributes, uint32_t EventType,
                                 BOOLEAN InitialState);
NTSTATUS __stdcall NtSetEvent(HANDLE EventHandle, int32_t *PreviousState);
NTSTATUS __stdcall NtCreateSemaphore(HANDLE *SemaphoreHandle, XOBJECT_ATTRIBUTES *ObjectAttributes, int32_t InitialCount,
                                     int32_t MaximumCount);
NTSTATUS __stdcall NtReleaseSemaphore(HANDLE SemaphoreHandle, int32_t ReleaseCount, int32_t *PreviousCount);
NTSTATUS __stdcall NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, LARGE_INTEGER *Timeout);
NTSTATUS __stdcall NtWaitForSingleObjectEx(HANDLE Handle, uint8_t WaitMode, BOOLEAN Alertable, LARGE_INTEGER *Timeout);
int32_t __stdcall KeSetEvent(XKEVENT *Event, int32_t Increment, BOOLEAN Wait);
NTSTATUS __stdcall KeWaitForSingleObject(void *Object, uint32_t WaitReason, uint8_t WaitMode, BOOLEAN Alertable,
                                         LARGE_INTEGER *Timeout);
void __stdcall RtlInitializeCriticalSection(XRTL_CRITICAL_SECTION *CriticalSection);
void __stdcall RtlEnterCriticalSection(XRTL_CRITICAL_SECTION *CriticalSection);
void __stdcall RtlLeaveCriticalSection(XRTL_CRITICAL_SECTION *CriticalSection);

// IRQL, DPCs, timers (k_dpc.cpp)
uint8_t __stdcall KeRaiseIrqlToDpcLevel();
uint8_t __fastcall KfRaiseIrql(uint8_t NewIrql);
void __fastcall KfLowerIrql(uint8_t NewIrql);
void __stdcall KeInitializeDpc(XKDPC *Dpc, XKDEFERRED_ROUTINE DeferredRoutine, void *DeferredContext);
BOOLEAN __stdcall KeInsertQueueDpc(XKDPC *Dpc, void *SystemArgument1, void *SystemArgument2);
BOOLEAN __stdcall KeRemoveQueueDpc(XKDPC *Dpc);
void __stdcall KeInitializeTimerEx(XKTIMER *Timer, uint32_t Type);
BOOLEAN __stdcall KeSetTimer(XKTIMER *Timer, LARGE_INTEGER DueTime, XKDPC *Dpc);
BOOLEAN __stdcall KeCancelTimer(XKTIMER *Timer);

// Files, devices, symbolic links, handles (k_file.cpp)
NTSTATUS __stdcall NtClose(HANDLE Handle);
NTSTATUS __stdcall NtDuplicateObject(HANDLE SourceHandle, HANDLE *TargetHandle, uint32_t Options);
NTSTATUS __stdcall NtCreateFile(HANDLE *FileHandle, ACCESS_MASK DesiredAccess, XOBJECT_ATTRIBUTES *ObjectAttributes,
                                XIO_STATUS_BLOCK *IoStatusBlock, LARGE_INTEGER *AllocationSize, uint32_t FileAttributes,
                                uint32_t ShareAccess, uint32_t CreateDisposition, uint32_t CreateOptions);
NTSTATUS __stdcall NtOpenFile(HANDLE *FileHandle, ACCESS_MASK DesiredAccess, XOBJECT_ATTRIBUTES *ObjectAttributes,
                              XIO_STATUS_BLOCK *IoStatusBlock, uint32_t ShareAccess, uint32_t OpenOptions);
NTSTATUS __stdcall NtReadFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext, XIO_STATUS_BLOCK *IoStatusBlock,
                              void *Buffer, uint32_t Length, LARGE_INTEGER *ByteOffset);
NTSTATUS __stdcall NtWriteFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext, XIO_STATUS_BLOCK *IoStatusBlock,
                               void *Buffer, uint32_t Length, LARGE_INTEGER *ByteOffset);
NTSTATUS __stdcall NtQueryInformationFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock, void *FileInformation,
                                          uint32_t Length, uint32_t FileInformationClass);
NTSTATUS __stdcall NtSetInformationFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock, void *FileInformation,
                                        uint32_t Length, uint32_t FileInformationClass);
NTSTATUS __stdcall NtQueryVolumeInformationFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock, void *FsInformation,
                                                uint32_t Length, uint32_t FsInformationClass);
NTSTATUS __stdcall NtQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext,
                                        XIO_STATUS_BLOCK *IoStatusBlock, void *FileInformation, uint32_t Length,
                                        uint32_t FileInformationClass, XANSI_STRING *FileName, BOOLEAN RestartScan);
NTSTATUS __stdcall NtQueryFullAttributesFile(XOBJECT_ATTRIBUTES *ObjectAttributes, XFILE_NETWORK_OPEN_INFORMATION *FileInformation);
NTSTATUS __stdcall NtFlushBuffersFile(HANDLE FileHandle, XIO_STATUS_BLOCK *IoStatusBlock);
NTSTATUS __stdcall NtDeviceIoControlFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext,
                                         XIO_STATUS_BLOCK *IoStatusBlock, uint32_t IoControlCode, void *InputBuffer,
                                         uint32_t InputBufferLength, void *OutputBuffer, uint32_t OutputBufferLength);
NTSTATUS __stdcall NtFsControlFile(HANDLE FileHandle, HANDLE Event, void *ApcRoutine, void *ApcContext,
                                   XIO_STATUS_BLOCK *IoStatusBlock, uint32_t FsControlCode, void *InputBuffer,
                                   uint32_t InputBufferLength, void *OutputBuffer, uint32_t OutputBufferLength);
NTSTATUS __stdcall IoCreateSymbolicLink(XANSI_STRING *SymbolicLinkName, XANSI_STRING *DeviceName);
NTSTATUS __stdcall IoDeleteSymbolicLink(XANSI_STRING *SymbolicLinkName);
NTSTATUS __stdcall NtOpenSymbolicLinkObject(HANDLE *LinkHandle, XOBJECT_ATTRIBUTES *ObjectAttributes);
NTSTATUS __stdcall NtQuerySymbolicLinkObject(HANDLE LinkHandle, XANSI_STRING *LinkTarget, uint32_t *ReturnedLength);

// Rtl (k_rtl.cpp)
NTSTATUS __stdcall RtlAnsiStringToUnicodeString(XUNICODE_STRING *Destination, const XANSI_STRING *Source, BOOLEAN Allocate);
NTSTATUS __stdcall RtlUnicodeStringToAnsiString(XANSI_STRING *Destination, const XUNICODE_STRING *Source, BOOLEAN Allocate);
uint32_t __stdcall RtlCompareMemoryUlong(void *Source, uint32_t Length, uint32_t Pattern);
BOOLEAN __stdcall RtlEqualString(const XANSI_STRING *String1, const XANSI_STRING *String2, BOOLEAN CaseInSensitive);
void __stdcall RtlInitAnsiString(XANSI_STRING *Destination, const char *Source);
uint32_t __stdcall RtlNtStatusToDosError(NTSTATUS Status);
BOOLEAN __stdcall RtlTimeFieldsToTime(void *TimeFields, LARGE_INTEGER *Time);
void __stdcall RtlTimeToTimeFields(LARGE_INTEGER *Time, void *TimeFields);

// Data exports (k_misc.cpp)
extern uint8_t ExEventObjectType[64];
extern uint8_t IoFileObjectType[64];
extern uint8_t PsThreadObjectType[64];
extern uint32_t HalDiskCachePartitionCount;
extern uint32_t HalBootSMCVideoMode;
extern XLAUNCH_DATA_PAGE *LaunchDataPage;
extern XXBOX_HARDWARE_INFO XboxHardwareInfo;
extern uint8_t XboxHDKey[16];
extern XXBOX_KRNL_VERSION XboxKrnlVersion;
extern uint8_t XboxSignatureKey[16];
extern uint8_t XboxAlternateSignatureKeys[16][16];
extern XANSI_STRING XeImageFileName;
extern uint8_t IdexChannelObject[256];

} // namespace xk
