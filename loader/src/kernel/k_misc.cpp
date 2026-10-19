// Hardware-facing and miscellaneous kernel exports: AV, HAL, interrupts, EEPROM settings, crypto, data exports.
// Most hardware calls only matter to the statically linked D3D/DSound code, which is being replaced; they
// log and return values that keep XAPI going.
#include "hooks.h"
#include "kernel/kexports.h"
#include "sha1.h"

#include <intrin.h>
#include "xbe.h"
#include "xthread.h"

namespace {

void *g_av_saved_data;
char g_image_file_name[] = "\\Device\\CdRom0\\default.xbe";

} // namespace

namespace xk {

// --- data exports ---
uint8_t ExEventObjectType[64];
uint8_t IoFileObjectType[64];
uint8_t PsThreadObjectType[64];
uint32_t HalDiskCachePartitionCount = 3;
uint32_t HalBootSMCVideoMode = 0;
XLAUNCH_DATA_PAGE *LaunchDataPage = nullptr; // cold boot: no launch data
XXBOX_HARDWARE_INFO XboxHardwareInfo = {0, 0xA2, 0xD3, 0, 0}; // retail, not arcade (flag 8), not devkit (flag 2)
uint8_t XboxHDKey[16];
XXBOX_KRNL_VERSION XboxKrnlVersion = {1, 0, 5838, 1};
uint8_t XboxSignatureKey[16];
uint8_t XboxAlternateSignatureKeys[16][16];
XANSI_STRING XeImageFileName = {(uint16_t)(sizeof g_image_file_name - 1), (uint16_t)sizeof g_image_file_name, g_image_file_name};
uint8_t IdexChannelObject[256];

// --- AV ---

void *__stdcall AvGetSavedDataAddress()
{
    KTRACE("");
    return g_av_saved_data;
}

void __stdcall AvSetSavedDataAddress(void *Address)
{
    KTRACE("%p", Address);
    g_av_saved_data = Address;
}

void __stdcall AvSendTVEncoderOption(void *RegisterBase, uint32_t Option, uint32_t Param, uint32_t *Result)
{
    KTRACE("%p, %u, 0x%x, %p", RegisterBase, Option, Param, Result);
    if (!Result) return;
    switch (Option) {
    case 6: // AV_QUERY_AV_CAPABILITIES: standard AV pack, NTSC-M
        *Result = 0x00000100 | 0x00000001;
        break;
    default:
        *Result = 0;
        break;
    }
}

uint32_t __stdcall AvSetDisplayMode(void *RegisterBase, uint32_t Step, uint32_t Mode, uint32_t Format, uint32_t Pitch,
                                    uint32_t FrameBuffer)
{
    KTRACE("%p, %u, 0x%x, 0x%x, %u, 0x%x", RegisterBase, Step, Mode, Format, Pitch, FrameBuffer);
    return 0;
}

uint32_t __cdecl DbgPrint(const char *Format, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, Format);
    vsnprintf(msg, sizeof msg, Format, ap);
    va_end(ap);
    size_t n = strlen(msg);
    while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = 0;
    logf("[DbgPrint] %s", msg);
    return 0;
}

// --- EEPROM settings ---

NTSTATUS __stdcall ExQueryNonVolatileSetting(uint32_t ValueIndex, uint32_t *Type, void *Value, uint32_t ValueLength,
                                             uint32_t *ResultLength)
{
    uint32_t dword = 0, size = 4, type = 4 /* REG_DWORD */;
    bool known = true;
    switch (ValueIndex) {
    case 0x000: dword = 0; break;                  // XC_TIMEZONE_BIAS (UTC)
    case 0x001: case 0x004: type = 3; break;       // XC_TZ_STD_NAME / XC_TZ_DLT_NAME (4 bytes)
    case 0x002: case 0x005: type = 3; break;       // XC_TZ_STD_DATE / XC_TZ_DLT_DATE
    case 0x003: case 0x006: dword = 0; break;      // XC_TZ_STD_BIAS / XC_TZ_DLT_BIAS
    case 0x007: dword = 1; break;                  // XC_LANGUAGE: English
    case 0x008: dword = g_cfg.widescreen ? 0x00010000 : 0; break; // XC_VIDEO: bit 16 = widescreen TV, no HD modes
    case 0x009: dword = 0; break;                  // XC_AUDIO: stereo
    case 0x00A: dword = 0; break;                  // XC_P_CONTROL_GAMES: no limit
    case 0x00B: dword = 0; break;                  // XC_P_CONTROL_PASSWORD
    case 0x00C: dword = 0; break;                  // XC_P_CONTROL_MOVIES
    case 0x011: dword = 0; break;                  // XC_MISC
    case 0x012: dword = 1; break;                  // XC_DVD_REGION
    case 0x103: dword = 0x00400100; break;         // XC_FACTORY_AV_REGION: NTSC-M, 60 Hz
    case 0x104: dword = 1; break;                  // XC_FACTORY_GAME_REGION: North America
    case 0x0FF: case 0xFFFF: type = 3; size = ValueLength < 256 ? ValueLength : 256; break; // whole sections: zeros
    default: known = false; break;
    }
    logf("[k] ExQueryNonVolatileSetting(0x%x, len %u) -> %s 0x%x", ValueIndex, ValueLength, known ? "" : "UNKNOWN, ", dword);
    if (!known) return XSTATUS_OBJECT_NAME_NOT_FOUND;
    if (ValueLength < size) return XSTATUS_BUFFER_TOO_SMALL;
    if (Type) *Type = type;
    memset(Value, 0, size);
    if (type == 4) memcpy(Value, &dword, 4);
    if (ResultLength) *ResultLength = size;
    return XSTATUS_SUCCESS;
}

// --- HAL ---

uint32_t __stdcall HalGetInterruptVector(uint32_t BusInterruptLevel, uint8_t *Irql)
{
    KTRACE("%u, %p", BusInterruptLevel, Irql);
    if (Irql) *Irql = (uint8_t)(BusInterruptLevel < 27 ? 27 - BusInterruptLevel : 1);
    return BusInterruptLevel + 0x30;
}

void __stdcall HalReadWritePCISpace(uint32_t BusNumber, uint32_t SlotNumber, uint32_t RegisterNumber, void *Buffer,
                                    uint32_t Length, BOOLEAN WritePCISpace)
{
    KTRACE("bus %u, slot 0x%x, reg 0x%x, %p, %u, write %u", BusNumber, SlotNumber, RegisterNumber, Buffer, Length, WritePCISpace);
    if (!WritePCISpace && Buffer) memset(Buffer, 0, Length);
}

void __stdcall HalRegisterShutdownNotification(void *ShutdownRegistration, BOOLEAN Register)
{
    KTRACE("%p, %u", ShutdownRegistration, Register);
}

void __stdcall HalReturnToFirmware(uint32_t Routine)
{
    logf("[k] HalReturnToFirmware(%u): the game asked to leave (0 halt, 1 reboot, 2 quick reboot, 3 fatal error)", Routine);
    if (LaunchDataPage) {
        const uint32_t *ld = (const uint32_t *)LaunchDataPage;
        logf("    launch data page %p: type %u, title %08X, flags 0x%x; data %08x %08x %08x", LaunchDataPage, ld[0], ld[1], ld[2],
             ld[0x100 / 4], ld[0x104 / 4], ld[0x108 / 4]);
    }
    log_stack_trace((const uint32_t *)_AddressOfReturnAddress());
    request_exit(Routine == 3 ? 1 : 0, "HalReturnToFirmware");
    Sleep(INFINITE);
}

void __stdcall HalInitiateShutdown()
{
    logf("[k] HalInitiateShutdown()");
    request_exit(0, "HalInitiateShutdown");
    Sleep(INFINITE);
}

void __fastcall IofCompleteRequest(void *Irp, char PriorityBoost) { KTRACE("%p, %d", Irp, PriorityBoost); }

void __stdcall KeBugCheck(uint32_t BugCheckCode)
{
    logf("[k] KeBugCheck(0x%08x)", BugCheckCode);
    log_stack_trace((const uint32_t *)_AddressOfReturnAddress());
    request_exit(4, "KeBugCheck");
    Sleep(INFINITE);
}

// --- interrupts: recorded, never delivered (GPU/APU interrupts belong to the libraries being replaced) ---

void __stdcall KeInitializeInterrupt(XKINTERRUPT *Interrupt, void *ServiceRoutine, void *ServiceContext, uint32_t Vector,
                                     uint8_t Irql, uint32_t InterruptMode, BOOLEAN ShareVector)
{
    KTRACE("%p, %s, %p, 0x%x, %u, %u, %u", Interrupt, sym_name((uint32_t)(uintptr_t)ServiceRoutine).c_str(), ServiceContext, Vector,
           Irql, InterruptMode, ShareVector);
    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->BusInterruptLevel = Vector - 0x30;
    Interrupt->Irql = Irql;
    Interrupt->Mode = (uint8_t)InterruptMode;
    Interrupt->ShareVector = ShareVector;
    Interrupt->Connected = 0;
    Interrupt->ServiceCount = 0;
}

BOOLEAN __stdcall KeConnectInterrupt(XKINTERRUPT *Interrupt)
{
    logf("[k] KeConnectInterrupt(%p, level %u, isr %s): recorded, will never fire", Interrupt, Interrupt->BusInterruptLevel,
         sym_name((uint32_t)(uintptr_t)Interrupt->ServiceRoutine).c_str());
    if (Interrupt->Connected) return FALSE;
    Interrupt->Connected = 1;
    return TRUE;
}

BOOLEAN __stdcall KeDisconnectInterrupt(XKINTERRUPT *Interrupt)
{
    KTRACE("%p", Interrupt);
    bool was = Interrupt->Connected != 0;
    Interrupt->Connected = 0;
    return was;
}

BOOLEAN __stdcall KeSynchronizeExecution(XKINTERRUPT *Interrupt, void *SynchronizeRoutine, void *SynchronizeContext)
{
    KTRACE("%p, %s, %p", Interrupt, sym_name((uint32_t)(uintptr_t)SynchronizeRoutine).c_str(), SynchronizeContext);
    uint8_t old = xthread_raise_irql(Interrupt ? (uint8_t)Interrupt->Irql : 2);
    BOOLEAN r = ((BOOLEAN(__stdcall *)(void *))SynchronizeRoutine)(SynchronizeContext);
    xthread_lower_irql(old);
    return r;
}

// --- time, FPU ---

void __stdcall KeQuerySystemTime(LARGE_INTEGER *CurrentTime)
{
    KTRACE("%p", CurrentTime);
    CurrentTime->QuadPart = ktime_now_100ns();
}

NTSTATUS __stdcall KeSaveFloatingPointState(void *FloatSave)
{
    KTRACE("%p", FloatSave);
    return XSTATUS_SUCCESS; // user-mode threads keep their own FPU state on Windows
}

NTSTATUS __stdcall KeRestoreFloatingPointState(void *FloatSave)
{
    KTRACE("%p", FloatSave);
    return XSTATUS_SUCCESS;
}

// --- XBE sections ---

NTSTATUS __stdcall XeLoadSection(void *Section) { return image_load_section((XbeSectionHeader *)Section); }

NTSTATUS __stdcall XeUnloadSection(void *Section) { return image_unload_section((XbeSectionHeader *)Section); }

// --- crypto ---

void __stdcall XcSHAInit(uint8_t *Context)
{
    KTRACE("%p", Context);
    sha1_init((Sha1Ctx *)Context);
}

void __stdcall XcSHAUpdate(uint8_t *Context, const uint8_t *Input, uint32_t InputLength)
{
    KTRACE("%p, %p, %u", Context, Input, InputLength);
    sha1_update((Sha1Ctx *)Context, Input, InputLength);
}

void __stdcall XcSHAFinal(uint8_t *Context, uint8_t *Digest)
{
    KTRACE("%p, %p", Context, Digest);
    sha1_final((Sha1Ctx *)Context, Digest);
}

void __stdcall XcHMAC(const uint8_t *Key, uint32_t KeyLength, const uint8_t *Data, uint32_t DataLength, const uint8_t *Data2,
                      uint32_t Data2Length, uint8_t *Digest)
{
    KTRACE("%p, %u, %p, %u, %p, %u, %p", Key, KeyLength, Data, DataLength, Data2, Data2Length, Digest);
    hmac_sha1(Key, KeyLength, Data, DataLength, Data2, Data2Length, Digest);
}

} // namespace xk
