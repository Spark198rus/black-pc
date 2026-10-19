// HLE replacement for the Xbox input library (XPP section: USB host controller + XID gamepads).
// Xbox port 0 is fed by every PC XInput controller, merged with keyboard and mouse while the game window has
// focus: the mouse rotates the view directly (input/mouselook.cpp; a click in the window captures the mouse),
// keys and mouse buttons are bound to the game's actions (input/bindings.cpp, PC SETTINGS > CONTROLS > KEY BINDINGS).
#include "audio/output.h"
#include "d3d/dispatch.h"
#include "d3d/draw.h"
#include "d3d/host.h"
#include "input/bindings.h"
#include "input/mouselook.h"
#include "input/movies.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

uint32_t g_pad_watch;

namespace xinput {
namespace {

#pragma pack(push, 1)
struct XGamepad { // Xbox XINPUT_GAMEPAD
    uint16_t wButtons;
    uint8_t bAnalogButtons[8]; // A B X Y BLACK WHITE LTRIGGER RTRIGGER
    int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct XState {
    uint32_t dwPacketNumber;
    XGamepad Gamepad;
};
#pragma pack(pop)

// PC XInput (loaded dynamically: xinput1_4.dll ships with Windows 10)
struct PcGamepad {
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};
struct PcState {
    DWORD dwPacketNumber;
    PcGamepad Gamepad;
};
struct PcVibration {
    WORD wLeftMotorSpeed, wRightMotorSpeed;
};
typedef DWORD(WINAPI *PFN_GetState)(DWORD, PcState *);
typedef DWORD(WINAPI *PFN_SetState)(DWORD, PcVibration *);
PFN_GetState g_pc_get;
PFN_SetState g_pc_set;

struct Device {
    uint32_t magic;
    uint32_t port;
    uint32_t packet;
    XGamepad last;
};
constexpr uint32_t DEV_MAGIC = 0x44495858; // 'XXID'

bool g_reported_insertion;

void load_xinput()
{
    static bool tried;
    if (tried) return;
    tried = true;
    HMODULE m = LoadLibraryW(L"xinput1_4.dll");
    if (!m) m = LoadLibraryW(L"xinput9_1_0.dll");
    if (m) {
        g_pc_get = (PFN_GetState)GetProcAddress(m, "XInputGetState");
        g_pc_set = (PFN_SetState)GetProcAddress(m, "XInputSetState");
    }
    logf("[input] PC XInput %s; keyboard mapping active when the game window has focus", g_pc_get ? "loaded" : "not available");
}

// --press "frame:button,..." or "SECONDSs:button,...": scripted presses for unattended test runs, held for
// 10 frames, or 300 ms when timed in seconds since the first input poll. "button*MS" holds for MS
// milliseconds; lx/ly/rx/ry+NN or -NN pushes a stick to NN percent (e.g. "80s:rx+25*2000"); mx/my+NN or -NN
// moves the mouse NN counts per poll.
struct Press {
    uint32_t frame;   // or milliseconds when timed
    bool timed;
    std::string button;
    uint32_t hold_ms;
};
std::vector<Press> g_presses;
ULONGLONG g_first_poll_ms;
int g_script_mx, g_script_my; // scripted mouse counts for this poll

void parse_presses()
{
    static bool done;
    if (done) return;
    done = true;
    std::string s = narrow(g_cfg.press_script);
    size_t pos = 0;
    while (pos < s.size()) {
        size_t comma = s.find(',', pos);
        std::string item = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        size_t colon = item.find(':');
        if (colon != std::string::npos) {
            bool timed = colon > 0 && item[colon - 1] == 's';
            uint32_t at = timed ? (uint32_t)(atof(item.c_str()) * 1000.0) : (uint32_t)atoi(item.c_str());
            std::string b = item.substr(colon + 1);
            size_t star = b.find('*');
            uint32_t hold = 300;
            if (star != std::string::npos) {
                hold = (uint32_t)atoi(b.c_str() + star + 1);
                b.resize(star);
            }
            g_presses.push_back({at, timed, b, hold});
        }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    if (!g_presses.empty()) logf("[input] %zu scripted presses", g_presses.size());
}

// Tests: show in-game hint N as the game does (FUN_0012B3C0: eax = N, arguments the HUD (*0x2D12E4 + 0xC) and a
// 16-byte vector it keeps at +0x22B0; FUN_0012C110 is the game's own caller). Hints 12, 13, 23, 87 and 24 stop the game
// until A (PRESS A TO CONTINUE).
void show_hint(int id)
{
    uint32_t root = *(const uint32_t *)(uintptr_t)0x002D12E4;
    uint32_t hud = root ? *(const uint32_t *)(uintptr_t)(root + 0xC) : 0;
    if (!hud) {
        logf("[input] tests: no HUD for hint %d", id);
        return;
    }
    alignas(16) float vec[4];
    memcpy(vec, (const void *)(uintptr_t)(hud + 0x22B0), sizeof vec);
    float *v = vec;
    uint32_t fn = 0x0012B3C0;
    logf("[input] tests: showing hint %d", id);
    __asm {
        push v
        push hud
        mov eax, id
        call fn
    }
}

// --press-file: a test driver appends lines while the game runs: a press as in --press without the time ("k:Enter",
// "a", "lx+80*500"), "shot:NAME" for a screenshot of the next whole frame, "reset" (a Direct3D device Reset as after
// sleep), "audio-lost" (the XAudio2 device gone) or "hint:N" (show_hint). Presses read together are 250 ms apart.
void read_press_file(uint32_t now_ms)
{
    static ULONGLONG next_check;
    static long long pos;
    static uint32_t queue_end;
    if (g_cfg.press_file.empty() || GetTickCount64() < next_check) return;
    next_check = GetTickCount64() + 50;
    HANDLE h = CreateFileW(g_cfg.press_file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    GetFileSizeEx(h, &size);
    if (size.QuadPart > pos) {
        std::string buf((size_t)(size.QuadPart - pos), '\0');
        LARGE_INTEGER p;
        p.QuadPart = pos;
        SetFilePointerEx(h, p, nullptr, FILE_BEGIN);
        DWORD got = 0;
        ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr);
        buf.resize(got);
        size_t last = buf.rfind('\n');
        if (last != std::string::npos) {
            pos += (long long)last + 1;
            uint32_t at = std::max(now_ms, queue_end);
            for (size_t s = 0; s <= last;) {
                size_t e = buf.find('\n', s);
                std::string line = buf.substr(s, e - s);
                s = e + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                if (line.empty()) continue;
                if (line.rfind("shot:", 0) == 0) {
                    xd3d::request_screenshot(line.substr(5).c_str());
                    continue;
                }
                if (line == "reset") {
                    xd3d::request_device_reset();
                    continue;
                }
                if (line == "audio-lost") {
                    audio_out::simulate_lost();
                    continue;
                }
                if (line.rfind("hint:", 0) == 0) {
                    show_hint(atoi(line.c_str() + 5));
                    continue;
                }
                uint32_t hold = 150;
                size_t star = line.find('*');
                if (star != std::string::npos) {
                    hold = (uint32_t)atoi(line.c_str() + star + 1);
                    line.resize(star);
                }
                g_presses.push_back({at, true, line, hold});
                logf("[input] press-file '%s' at %u ms", line.c_str(), at);
                at += std::max(250u, hold + 100);
            }
            queue_end = at;
        }
    }
    CloseHandle(h);
}

void apply_presses(XGamepad &g)
{
    parse_presses();
    g_script_mx = g_script_my = 0;
    uint32_t f = g_frame_count;
    if (!g_first_poll_ms) g_first_poll_ms = GetTickCount64();
    uint32_t ms = (uint32_t)(GetTickCount64() - g_first_poll_ms);
    read_press_file(ms);
    for (const Press &p : g_presses) {
        if (p.timed ? (ms < p.frame || ms >= p.frame + p.hold_ms) : (f < p.frame || f >= p.frame + 10)) continue;
        const std::string &b = p.button;
        if (b.size() > 3 && (b[0] == 'l' || b[0] == 'r') && (b[1] == 'x' || b[1] == 'y') && (b[2] == '+' || b[2] == '-')) {
            int16_t v = (int16_t)std::clamp(atoi(b.c_str() + 2) * 32767 / 100, -32767, 32767);
            int16_t &axis = b[0] == 'l' ? (b[1] == 'x' ? g.sThumbLX : g.sThumbLY) : (b[1] == 'x' ? g.sThumbRX : g.sThumbRY);
            axis = v;
        }
        if (b.size() > 3 && b[0] == 'm' && (b[1] == 'x' || b[1] == 'y') && (b[2] == '+' || b[2] == '-'))
            (b[1] == 'x' ? g_script_mx : g_script_my) += atoi(b.c_str() + 2);
        if (b.size() > 2 && b[0] == 'k' && b[1] == ':') bindings::script_hold(b.substr(2)); // a bound key
        if (b == "up") g.wButtons |= 0x01;
        else if (b == "down") g.wButtons |= 0x02;
        else if (b == "left") g.wButtons |= 0x04;
        else if (b == "right") g.wButtons |= 0x08;
        else if (b == "start") g.wButtons |= 0x10;
        else if (b == "back") g.wButtons |= 0x20;
        else if (b == "a") g.bAnalogButtons[0] = 255;
        else if (b == "b") g.bAnalogButtons[1] = 255;
        else if (b == "x") g.bAnalogButtons[2] = 255;
        else if (b == "y") g.bAnalogButtons[3] = 255;
        else if (b == "black") g.bAnalogButtons[4] = 255;
        else if (b == "white") g.bAnalogButtons[5] = 255;
        else if (b == "lt") g.bAnalogButtons[6] = 255;
        else if (b == "rt") g.bAnalogButtons[7] = 255;
        else if (b == "lthumb") g.wButtons |= 0x40;
        else if (b == "rthumb") g.wButtons |= 0x80;
        static uint32_t logged_frame;
        if (logged_frame != p.frame) {
            logged_frame = p.frame;
            logf("[input] scripted press '%s' at frame %u", b.c_str(), f);
        }
    }
}

uint8_t max8(uint8_t a, uint8_t b) { return a > b ? a : b; }

// Keep whichever stick is pushed further.
void merge_stick(int16_t &x, int16_t &y, int16_t px, int16_t py)
{
    if ((int64_t)px * px + (int64_t)py * py > (int64_t)x * x + (int64_t)y * y) {
        x = px;
        y = py;
    }
}

// Mouse: movement since the previous poll (the game polls once per frame) becomes this frame's rotation
// (input/mouselook.cpp); buttons and the wheel are bindable keys (input/bindings.cpp). Returns the wheel notches.
int mouse_look(bool live, int *raw_wheel)
{
    int dx, dy, wheel;
    host3d::mouse_take(&dx, &dy, &wheel);
    *raw_wheel = wheel; // the key bindings page takes the wheel without the mouse captured
    if (!live) dx = dy = wheel = 0;
    dx += g_script_mx; // scripted moves work without focus
    dy += g_script_my;
    float k = 0.022f * g_cfg.mouse_sens; // degrees per count
    // The game's yaw rate is positive to the left and its pitch rate positive downwards (mouse dy > 0 = towards
    // the user = look down).
    mouselook_set_frame(-dx * k, (g_cfg.mouse_invert ? -dy : dy) * k);
    return wheel;
}

// The game plays on Xbox port 0; every connected PC controller (any XInput user index) feeds it, so a
// pad that Windows put on index 1-3 works without setup.
void read_pad(uint32_t port, XGamepad &g)
{
    memset(&g, 0, sizeof g);
    if (port != 0) return;
    static uint32_t polls, absent[4]; // absent: the poll count when a slot was last found empty
    polls++;
    for (DWORD i = 0; g_pc_get && i < 4; i++) {
        if (absent[i] && polls - absent[i] < 120) continue;
        PcState pc = {};
        if (g_pc_get(i, &pc) != ERROR_SUCCESS) {
            absent[i] = polls;
            continue;
        }
        absent[i] = 0;
        const PcGamepad &p = pc.Gamepad;
        g.wButtons |= p.wButtons & 0x00FF; // D-pad, START, BACK, thumb clicks map 1:1
        if (p.wButtons & 0x1000) g.bAnalogButtons[0] = 255; // A
        if (p.wButtons & 0x2000) g.bAnalogButtons[1] = 255; // B
        if (p.wButtons & 0x4000) g.bAnalogButtons[2] = 255; // X
        if (p.wButtons & 0x8000) g.bAnalogButtons[3] = 255; // Y
        if (p.wButtons & 0x0200) g.bAnalogButtons[4] = 255; // BLACK = right shoulder
        if (p.wButtons & 0x0100) g.bAnalogButtons[5] = 255; // WHITE = left shoulder
        g.bAnalogButtons[6] = max8(g.bAnalogButtons[6], p.bLeftTrigger);
        g.bAnalogButtons[7] = max8(g.bAnalogButtons[7], p.bRightTrigger);
        merge_stick(g.sThumbLX, g.sThumbLY, p.sThumbLX, p.sThumbLY);
        merge_stick(g.sThumbRX, g.sThumbRY, p.sThumbRX, p.sThumbRY);
    }
    apply_presses(g);
    bool focus = GetForegroundWindow() == host3d::window();
    bool live = focus && host3d::mouse_captured();
    int raw_wheel;
    int wheel = mouse_look(live, &raw_wheel);
    if (!bindings::capture_poll(focus, raw_wheel)) // while the key bindings page waits for a key, keys bind instead
        bindings::apply(g.wButtons, g.bAnalogButtons, g.sThumbLX, g.sThumbLY, g.sThumbRX, g.sThumbRY, focus, live, wheel);
    movies_poll(g.wButtons, g.bAnalogButtons, bindings::menu_select_held());
}

} // namespace

void __stdcall XInitDevices(uint32_t dwPreallocTypeCount, void *PreallocTypes)
{
    DTRACE("%u, %p", dwPreallocTypeCount, PreallocTypes);
    load_xinput();
}

// Port bitmasks: bit n = port n (gamepads). One gamepad on port 0, reported once as an insertion.
BOOL __stdcall XGetDeviceChanges(void *DeviceType, uint32_t *pdwInsertions, uint32_t *pdwRemovals)
{
    DTRACE("%p", DeviceType);
    *pdwInsertions = 0;
    *pdwRemovals = 0;
    if (DeviceType == (void *)(uintptr_t)0x0027cf4c && !g_reported_insertion) { // XDEVICE_TYPE_GAMEPAD
        g_reported_insertion = true;
        *pdwInsertions = 1;
        return TRUE;
    }
    return FALSE;
}

HANDLE __stdcall XInputOpen(void *DeviceType, uint32_t dwPort, uint32_t dwSlot, void *pPollingParameters)
{
    auto *d = new Device();
    d->magic = DEV_MAGIC;
    d->port = dwPort;
    logf("[input] XInputOpen(type %p, port %u, slot %u) -> %p", DeviceType, dwPort, dwSlot, d);
    return d;
}

void __stdcall XInputClose(Device *h)
{
    DTRACE("%p", h);
    if (h && h->magic == DEV_MAGIC) h->magic = 0;
}

uint32_t __stdcall XInputGetState(Device *h, XState *pState)
{
    if (!h || h->magic != DEV_MAGIC) return ERROR_DEVICE_NOT_CONNECTED;
    if (g_cfg.watch_pad && !g_pad_watch && g_frame_count >= g_cfg.watch_pad) {
        // Debug: hardware read/write breakpoint (DR3, 2 bytes) on this thread over sThumbRX of the game's buffer.
        uint32_t base = g_cfg.watch_read_deref ? *(uint32_t *)(uintptr_t)g_cfg.watch_read : g_cfg.watch_read;
        g_pad_watch = g_cfg.watch_read ? base + g_cfg.watch_read_off : (uint32_t)(uintptr_t)&pState->Gamepad.sThumbRX;
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(GetCurrentThread(), &c);
        c.Dr3 = g_pad_watch;
        uint32_t len = g_cfg.watch_read ? 0x3u : 0x1u; // LEN3: 4 bytes for --watch-read, else 2
        uint32_t rw = g_cfg.watch_write_only ? 0x1u : 0x3u;
        c.Dr7 = (c.Dr7 & ~(0xFu << 28)) | (1u << 6) | (rw << 28) | (len << 30); // L3, RW3 = write or read/write
        BOOL ok = SetThreadContext(GetCurrentThread(), &c);
        logf("[input] watching right-stick reads at %08x (%s)", g_pad_watch, ok ? "set" : "FAILED");
    }
    static std::atomic<uint32_t> polls{0};
    if ((++polls % 600) == 1) logf("[input] XInputGetState poll #%u (frame %u)", polls.load(), g_frame_count.load());
    XGamepad g;
    read_pad(h->port, g);
    if (memcmp(&g, &h->last, sizeof g)) {
        h->last = g;
        h->packet++;
    }
    pState->dwPacketNumber = h->packet;
    pState->Gamepad = g;
    return ERROR_SUCCESS;
}

// XINPUT_FEEDBACK: header {dwStatus, hEvent, Reserved[58]} (68 bytes with padding), then rumble.
uint32_t __stdcall XInputSetState(Device *h, uint8_t *pFeedback)
{
    if (!h || h->magic != DEV_MAGIC) return ERROR_DEVICE_NOT_CONNECTED;
    PcVibration v = {*(uint16_t *)(pFeedback + 68), *(uint16_t *)(pFeedback + 70)};
    for (DWORD i = 0; g_pc_set && h->port == 0 && i < 4; i++) g_pc_set(i, &v); // port 0 = every PC pad
    *(uint32_t *)pFeedback = ERROR_SUCCESS; // completed immediately
    if (HANDLE ev = *(HANDLE *)(pFeedback + 4)) SetEvent(ev);
    return ERROR_IO_PENDING;
}

#define H(addr, fn, params, ret) {addr, #fn, (void *)&fn, params, ret}

static const xd3d::Hle kTable[] = {
    H(0x0027dd24, XInitDevices, "s s", false),
    H(0x0027dd29, XGetDeviceChanges, "s s s", true),
    H(0x0027dd96, XInputOpen, "s s s s", true),
    H(0x0027ddec, XInputClose, "s", false),
    H(0x0027ddf8, XInputGetState, "s s", true),
    H(0x0027de6b, XInputSetState, "s s", true),
};

} // namespace xinput

namespace xd3d {
const Hle *hle_input_table(size_t *count)
{
    *count = sizeof xinput::kTable / sizeof xinput::kTable[0];
    return xinput::kTable;
}
} // namespace xd3d
