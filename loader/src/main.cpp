// blackrt.dll entry: BlackMain is called by black.exe (the address-space stub) and never returns.
#include "common.h"

#include <algorithm>
#include <vector>

#include "d3d/dispatch.h"
#include "fe/calls.h"
#include "fe/pages.h"
#include "fov.h"
#include "hooks.h"
#include "image.h"
#include "timing.h"
#include "settings.h"
#include "input/autoaim.h"
#include "input/mouselook.h"
#include "kernel/ktable.h"
#include "kernel/xboxkrnl.h"
#include "xbe.h"
#include "xthread.h"

#include <bcrypt.h>
#include <shellapi.h>

namespace {

// The build the address tables (D3D hook list, expected fs patch count) were made for.
const char kKnownXbeSha256[] = "df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6";
constexpr int kKnownFsPatchCount = 34;

HANDLE g_exit_event;
std::atomic<int> g_exit_code{-1};
Xbe g_xbe;

// black.exe is a Windows-subsystem program, so a double-click shows only the game window. Started from a command
// prompt it attaches to that console for the short summary (logc lines, --help, --info); output redirected to a file
// or pipe by the parent stays there. False when there is nowhere to print.
bool console_init()
{
    if (_fileno(stdout) >= 0) return true; // the parent passed a file or pipe
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
    FILE *f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    fputs("\n", stdout); // the prompt does not wait for a windowed program: start below it
    return true;
}

bool g_console;

void usage()
{
    static const char kUsage[] =
           "BLACK PC port " BLACK_VERSION "\n"
           "usage: black.exe [options] [path\\to\\default.xbe | game folder]\n"
           "  --info            print the XBE structure and exit\n"
           "  --hdd <dir>       host folder for the Xbox hard disk (saved games; default savedata next to black.exe)\n"
           "  --log <file>      log file (default black.log next to black.exe)\n"
           "  --symbols <csv>   functions.csv for names in logs (default notes\\functions.csv if present)\n"
           "  --trace           log every kernel call, not only the first of each kind\n"
           "  --trace-d3d       log every D3D call\n"
           "  --watch <hexaddr> log every write to a dword (hardware breakpoint, up to 4)\n"
           "  --probe <hexaddr> log registers/arguments each time code reaches an address (up to 16)\n"
           "  --debug-draws <n> describe the first n draws; --debug-frame <f> describe every draw of frame f\n"
           "  --press <f:btn,...> scripted controller presses at frame f (start a b x y back up down left right)\n"
           "  --screenshot <dir> save frames as PNG (frame 1, every 60th, and when the run stops)\n"
           "  --strict          stop at the first call to an unimplemented kernel function\n"
           "  --timeout <sec>   exit after this many seconds\n"
           "  --multicore       do not pin the process to one CPU\n"
           "  --game-cpu N      run the game threads on logical CPU N (default: the last physical core)\n"
           "  --skip-videos     play the 9-second logo video instead of the others (dev: unskippable intro/briefings)\n"
           "  --shot-every N    with --screenshot: every N frames (default 60)\n"
           "  --shot-start S    with --screenshot: start the cadence S seconds after the first frame\n"
           "  --gamma G         extra output gamma after the game's ramp (default 1.0; >1 brighter; F7/F8 adjust live)\n"
           "  --scale K         render at K x the Xbox resolution, any factor 0.5-8 (default 3 = 2560x1440 widescreen)\n"
           "  --resolution R    render size instead of --scale: WxH (e.g. 1920x1080), H (width from the aspect)\n"
           "                    or native (the window's size at start, the screen's with --fullscreen)\n"
           "  --window WxH      window client size (default: largest size of the game's aspect that fits the screen)\n"
           "  --normal-aspect   4:3 like an original Xbox on a standard TV (default: widescreen 16:9)\n"
           "  --fullscreen      start in borderless fullscreen\n"
           "  --fps 30|60       frame rate (default 60; 30 is the original)\n"
           "  --no-reverb       do not mix the room reverb\n"
           "  --reverb-gain K   room reverb level (default 1.0)\n"
           "  --background      (tests) start minimised without focus, audio muted\n"
           "  --wav <file>      record the audio output as a WAV file\n"
           "  --rawvideo <file> record the picture as raw BGRX frames from --shot-start for --clip-seconds (default 4)\n"
           "  --mouse-sens K    mouse look sensitivity, K x 0.022 degrees per count (default 2.0)\n"
           "  --mouse-invert    mouse forward looks down\n"
           "  --autoaim         the game's auto-aim (the view pulling toward enemies), off by default; --no-autoaim\n"
           "  --fov D           horizontal field of view in play, 50-120 degrees (default 70, the game's; PC SETTINGS > VIDEO)\n"
           "  --no-movie-skip   movies skip only as in the original (default: A / START skip every movie)\n"
           "  --no-pc-menus     no PC SETTINGS pages in the game's Options menu (built at start into cache\\menus\n"
           "                    next to black.exe)\n"
           "  --gain K          output gain after the game's gamma ramp (default 1.0; F5/F6 adjust live)\n"
           "  --no-ramp         ignore the game's gamma ramp (no fades, half brightness: the raw framebuffer)\n"
           "  --no-aa           no anti-aliasing (default: edge-directed post filter; F9 toggles)\n"
           "  --no-seamfill     do not fill hairline cracks between geometry pieces (destructible walls)\n"
           "  --perf-at S:D     record performance for D seconds from S seconds after the first frame (perf\\)\n"
           "  --timer-vblank    vblank from a 59.94 Hz timer instead of the display's refresh (default: follow a ~60 Hz display)\n"
           "  --force           run an XBE other than the known NTSC-U build\n"
           "  --verbose         print the whole log on the console too (default: startup, errors, exit; all in the log file)\n";
    if (g_console) fputs(kUsage, stdout);
    else if (!g_cfg.background) MessageBoxW(nullptr, widen(kUsage).c_str(), L"BLACK - command-line options", MB_OK);
}

bool parse_args()
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring target;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        auto next = [&](std::wstring &out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        if (a == L"--info") g_cfg.info_only = true;
        else if (a == L"--trace") g_cfg.trace = true;
        else if (a == L"--verbose") g_cfg.verbose = true;
        else if (a == L"--trace-d3d") g_trace_d3d = true;
        else if (a == L"--screenshot") { if (!next(g_cfg.screenshot_dir)) return false; }
        else if (a == L"--watch") {
            std::wstring v;
            if (!next(v) || g_cfg.watch_count >= 4) return false;
            g_cfg.watch[g_cfg.watch_count++] = (uint32_t)wcstoul(v.c_str(), nullptr, 16);
        } else if (a == L"--debug-frame") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.debug_frame = (unsigned)_wtoi(v.c_str());
            size_t dash = v.find(L'-');
            if (dash != std::wstring::npos) g_cfg.debug_frame_end = (unsigned)_wtoi(v.c_str() + dash + 1);
        } else if (a == L"--watch-read") {
            std::wstring v;
            if (!next(v)) return false;
            const wchar_t *q = v.c_str();
            if (*q == L'w') { g_cfg.watch_write_only = true; q++; }
            if (*q == L'*') { g_cfg.watch_read_deref = true; q++; }
            wchar_t *end = nullptr;
            g_cfg.watch_read = (uint32_t)wcstoul(q, &end, 16);
            if (end && *end == L'+') g_cfg.watch_read_off = (uint32_t)wcstoul(end + 1, nullptr, 16);
        } else if (a == L"--watch-pad") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.watch_pad = (unsigned)_wtoi(v.c_str());
        } else if (a == L"--memdump") {
            if (!next(g_cfg.memdump)) return false;
        } else if (a == L"--press") {
            if (!next(g_cfg.press_script)) return false;
        } else if (a == L"--debug-draws") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.debug_draws = (unsigned)_wtoi(v.c_str());
        } else if (a == L"--probe") {
            std::wstring v;
            if (!next(v) || g_cfg.probe_count >= 16) return false;
            g_cfg.probe[g_cfg.probe_count++] = (uint32_t)wcstoul(v.c_str(), nullptr, 16);
        }
        else if (a == L"--strict") g_cfg.strict = true;
        else if (a == L"--multicore") g_cfg.multicore = true;
        else if (a == L"--game-cpu") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.game_cpu = _wtoi(v.c_str());
        }
        else if (a == L"--skip-videos") g_cfg.skip_videos = true;
        else if (a == L"--normal-aspect") { g_cfg.widescreen = false; settings_mark_cli("widescreen"); }
        else if (a == L"--fullscreen") { g_cfg.fullscreen = true; settings_mark_cli("fullscreen"); }
        else if (a == L"--no-reverb") { g_cfg.reverb = false; settings_mark_cli("reverb"); }
        else if (a == L"--no-ramp") g_cfg.ramp = false;
        else if (a == L"--no-aa") { g_cfg.aa = false; settings_mark_cli("aa"); }
        else if (a == L"--no-seamfill") { g_cfg.seam_fill = false; settings_mark_cli("seamfill"); }
        else if (a == L"--rescale-at") { if (!next(g_cfg.rescale_at)) return false; }
        else if (a == L"--reverb-gain") {
            settings_mark_cli("reverb_gain");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.reverb_gain = (float)_wtof(v.c_str());
            if (g_cfg.reverb_gain < 0.0f || g_cfg.reverb_gain > 8.0f) g_cfg.reverb_gain = 1.0f;
        }
        else if (a == L"--background") g_cfg.background = true;
        else if (a == L"--wav") { if (!next(g_cfg.wav_path)) return false; }
        else if (a == L"--rawvideo") { if (!next(g_cfg.rawvideo)) return false; }
        else if (a == L"--clip-seconds") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.clip_seconds = (uint32_t)_wtoi(v.c_str());
        }
        else if (a == L"--fps") {
            settings_mark_cli("fps");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.fps = _wtoi(v.c_str()) == 60 ? 60 : 30;
        }
        else if (a == L"--mouse-sens") {
            settings_mark_cli("mouse_sens");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.mouse_sens = (float)_wtof(v.c_str());
            if (g_cfg.mouse_sens < 0.05f || g_cfg.mouse_sens > 50.0f) g_cfg.mouse_sens = 2.0f;
        } else if (a == L"--mouse-invert") { g_cfg.mouse_invert = true; settings_mark_cli("mouse_invert"); }
        else if (a == L"--no-autoaim") { g_cfg.autoaim = false; settings_mark_cli("autoaim"); }
        else if (a == L"--autoaim") { g_cfg.autoaim = true; settings_mark_cli("autoaim"); }
        else if (a == L"--fov") {
            settings_mark_cli("fov");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.fov = std::clamp((float)_wtof(v.c_str()), 50.0f, 120.0f);
        } else if (a == L"--game-override") {
            if (!next(g_cfg.game_override)) return false;
        } else if (a == L"--press-file") {
            if (!next(g_cfg.press_file)) return false;
        } else if (a == L"--no-movie-skip") { g_cfg.skip_movies = false; settings_mark_cli("skipanymovie"); }
        else if (a == L"--no-pc-menus") g_cfg.pc_menus = false;
        else if (a == L"--timer-vblank") g_cfg.timer_vblank = true;
        else if (a == L"--display-vblank") g_cfg.display_vblank = true;
        else if (a == L"--perf-at") {
            if (!next(g_cfg.perf_at)) return false;
        }
        else if (a == L"--gamma") {
            settings_mark_cli("gamma");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.gamma = (float)_wtof(v.c_str());
            if (g_cfg.gamma < 0.2f || g_cfg.gamma > 5.0f) g_cfg.gamma = 1.0f;
        } else if (a == L"--scale") {
            settings_mark_cli("render");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.scale = (float)_wtof(v.c_str());
            if (g_cfg.scale < 0.5f || g_cfg.scale > 8.0f) g_cfg.scale = 3.0f;
        } else if (a == L"--resolution") {
            settings_mark_cli("render");
            std::wstring v;
            if (!next(v)) return false;
            if (v == L"native") g_cfg.render_native = true;
            else {
                size_t x = v.find_first_of(L"xX");
                if (x == std::wstring::npos) g_cfg.render_h = _wtoi(v.c_str()); // height only: width from the aspect
                else {
                    g_cfg.render_w = _wtoi(v.c_str());
                    g_cfg.render_h = _wtoi(v.c_str() + x + 1);
                }
                if (g_cfg.render_h < 240 || g_cfg.render_h > 8192) g_cfg.render_w = g_cfg.render_h = 0;
            }
        } else if (a == L"--window") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.window_w = _wtoi(v.c_str());
            size_t x = v.find_first_of(L"xX");
            g_cfg.window_h = x == std::wstring::npos ? 0 : _wtoi(v.c_str() + x + 1); // 0: from the aspect
        } else if (a == L"--gain") {
            settings_mark_cli("gain");
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.gain = (float)_wtof(v.c_str());
            if (g_cfg.gain < 0.2f || g_cfg.gain > 5.0f) g_cfg.gain = 1.0f;
        } else if (a == L"--shot-every") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.shot_every = _wtoi(v.c_str()) > 0 ? (uint32_t)_wtoi(v.c_str()) : 60;
        } else if (a == L"--shot-start") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.shot_start = (uint32_t)_wtoi(v.c_str());
        }
        else if (a == L"--force") g_cfg.force = true;
        else if (a == L"--hdd") { if (!next(g_cfg.hdd_dir)) return false; }
        else if (a == L"--log") { if (!next(g_cfg.log_path)) return false; }
        else if (a == L"--symbols") { if (!next(g_cfg.symbols_path)) return false; }
        else if (a == L"--timeout") {
            std::wstring v;
            if (!next(v)) return false;
            g_cfg.timeout_s = (unsigned)_wtoi(v.c_str());
        } else if (a == L"--help" || a == L"-h" || a == L"/?") return false;
        else if (!a.empty() && a[0] == L'-') return false;
        else target = a;
    }
    LocalFree(argv);
    if (target.empty()) target = L"game\\default.xbe";
    // The port keeps its own files beside black.exe (the release's port folder, next to game).
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exe_path(exe, n);
    g_cfg.port_dir = exe_path.substr(0, exe_path.find_last_of(L"\\/"));
    // Started from a shortcut or another folder: if a relative game path is not found here, look for it
    // from black.exe's own folder upwards (port -> the folder with game; work\run -> work -> project root) and make
    // that folder current, so relative paths given on the command line (--hdd, --log) and notes\ resolve against it.
    bool relative = !(target.size() >= 2 && (target[1] == L':' || (target[0] == L'\\' && target[1] == L'\\')));
    if (relative && GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring dir = exe_path;
        for (int up = 0; up < 4; up++) {
            size_t cut = dir.find_last_of(L"\\/");
            if (cut == std::wstring::npos) break;
            dir.resize(cut);
            if (GetFileAttributesW((dir + L"\\" + target).c_str()) != INVALID_FILE_ATTRIBUTES) {
                SetCurrentDirectoryW(dir.c_str());
                break;
            }
        }
    }
    DWORD attr = GetFileAttributesW(target.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) target += L"\\default.xbe";
    g_cfg.xbe_path = target;
    size_t slash = target.find_last_of(L"\\/");
    g_cfg.game_dir = slash == std::wstring::npos ? L"." : target.substr(0, slash);
    if (g_cfg.hdd_dir.empty()) g_cfg.hdd_dir = g_cfg.port_dir + L"\\savedata";
    if (g_cfg.log_path.empty()) g_cfg.log_path = g_cfg.port_dir + L"\\black.log";
    settings_load(); // black.ini in the port folder; flags given above keep their values
    if (g_cfg.symbols_path.empty() && GetFileAttributesW(L"notes\\functions.csv") != INVALID_FILE_ATTRIBUTES)
        g_cfg.symbols_path = L"notes\\functions.csv";
    return true;
}

std::string sha256_hex(const std::vector<uint8_t> &data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    uint8_t digest[32] = {};
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return "?";
    BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(), digest, 32);
    BCryptCloseAlgorithmProvider(alg, 0);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return hex;
}

// Threads that run game code share one CPU like the single-core Xbox (XAPI/DSound code assumes it); every
// Xbox thread takes this mask in xthread_bind. Host threads (audio mixer and XAudio2, the graphics driver,
// the window) stay free to use the other CPUs, so the game keeping its core busy at 60 fps does not starve
// the sound.
void pin_to_one_cpu()
{
    DWORD_PTR proc = 0, sys = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);
    // Physical cores (logical CPU masks); hyperthreads of one core share it.
    std::vector<DWORD_PTR> cores;
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    if (len && GetLogicalProcessorInformation(info.data(), &len))
        for (DWORD i = 0; i < len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); i++)
            if (info[i].Relationship == RelationProcessorCore && (info[i].ProcessorMask & proc)) cores.push_back(info[i].ProcessorMask & proc);
    DWORD_PTR one = 0, core = 0;
    if (g_cfg.game_cpu >= 0 && g_cfg.game_cpu < (int)(8 * sizeof(DWORD_PTR)) && (proc & ((DWORD_PTR)1 << g_cfg.game_cpu))) {
        one = (DWORD_PTR)1 << g_cfg.game_cpu;
        for (DWORD_PTR c : cores)
            if (c & one) core = c;
    } else if (cores.size() > 1) { // the last core: CPU 0 takes most interrupts and is where Windows puts work first
        core = cores.back();
        one = core & (~core + 1);
    }
    if (!one) one = proc & (~proc + 1); // lowest available CPU
    if (!one) return;
    g_game_cpu = one;
    SetThreadAffinityMask(GetCurrentThread(), one);
    // Keep the core's other hyperthread free: threads created from now on (driver, audio) use the other cores.
    DWORD_PTR sibling = core & ~one;
    if (sibling && (proc & ~core)) SetProcessAffinityMask(GetCurrentProcess(), proc & ~sibling);
    logf("cpu: game threads pinned to CPU mask 0x%lx like the single-core Xbox%s; host threads use the others; --game-cpu N to "
         "choose, --multicore to disable", (unsigned long)one, sibling && (proc & ~core) ? " (its hyperthread sibling left idle)" : "");
}

} // namespace

void request_exit(int code, const char *why)
{
    int expected = -1;
    if (g_exit_code.compare_exchange_strong(expected, code)) logc("exit requested: %s (code %d)", why, code);
    if (g_exit_event) SetEvent(g_exit_event);
}

bool exit_requested() { return g_exit_code.load() >= 0; }

namespace {

bool g_image_mapped; // once true, black.exe's code is gone and BlackMain must not return

// Setup errors a player can fix (no game files, another build): started by a double-click, the console closes with
// the process, so they also get a message box.
void startup_error(const std::string &text)
{
    logc("%s", text.c_str());
    if (!g_cfg.background) MessageBoxW(nullptr, widen(text).c_str(), L"BLACK can't start", MB_OK | MB_ICONERROR);
}

int run(void *reserve, unsigned reserve_size)
{
    if (!parse_args()) {
        usage();
        return 2;
    }
    if (g_cfg.info_only) {
        std::string err;
        if (!g_xbe.load(g_cfg.xbe_path, err)) {
            fprintf(stderr, "%s: %s\n", narrow(g_cfg.xbe_path).c_str(), err.c_str());
            return 1;
        }
        printf("File            %s\n", narrow(g_cfg.xbe_path).c_str());
        g_xbe.print_info(&kernel_export_name);
        return 0;
    }

    log_init(g_cfg.log_path);
    g_exit_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    logc("BLACK loader " BLACK_VERSION " (blackrt) - XBE %s, hdd %s, log %s%s%s", narrow(g_cfg.xbe_path).c_str(), narrow(g_cfg.hdd_dir).c_str(),
         narrow(g_cfg.log_path).c_str(), g_cfg.trace ? ", trace" : "", g_cfg.strict ? ", strict" : "");
    if (g_cfg.active_language == "en") logc("language: English (default)");
    else {
        const std::wstring bank = g_cfg.port_dir + L"\\language-banks\\" + widen(g_cfg.active_language) + L"\\language\\strings\\MainUS.bin";
        const DWORD attr = GetFileAttributesW(bank.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
            logc("language: %s selected, but generated MainUS.bin is missing at %s; English strings will be used",
                 g_cfg.active_language.c_str(), narrow(bank).c_str());
        else logc("language: %s (applied from %s)", g_cfg.active_language.c_str(), narrow(bank).c_str());
    }
    logf("address-space stub: reserve %p + 0x%x", reserve, reserve_size);

    std::string err;
    if (!g_xbe.load(g_cfg.xbe_path, err)) {
        wchar_t full[MAX_PATH];
        if (!GetFullPathNameW(g_cfg.xbe_path.c_str(), MAX_PATH, full, nullptr)) wcscpy_s(full, g_cfg.xbe_path.c_str());
        startup_error("cannot load " + narrow(full) + ": " + err + "\n\nPut black.exe and blackrt.dll next to the folder that "
                      "holds the game's files (game\\default.xbe), or give that folder on the command line.");
        return 1;
    }
    std::string title = narrow(g_xbe.title);
    logf("xbe: '%s' title id %08X, base 0x%08x size 0x%x entry 0x%08x, %zu sections, %zu kernel imports", title.c_str(),
         g_xbe.title_id, g_xbe.base, g_xbe.size_of_image, g_xbe.entry, g_xbe.sections.size(), g_xbe.kernel_imports().size());
    std::string hash = sha256_hex(g_xbe.data);
    bool known_build = hash == kKnownXbeSha256;
    g_cfg.known_build = known_build;
    logc("xbe: sha256 %s (%s)", hash.c_str(), known_build ? "known NTSC-U build" : "UNKNOWN build");
    if (!known_build && !g_cfg.force) {
        startup_error("This default.xbe is not the NTSC-U build of BLACK that the port supports (its SHA-256 is " + hash +
                      ", expected " + kKnownXbeSha256 + "). The port's address tables are for that build only; --force "
                      "tries anyway.");
        return 1;
    }

    if (!g_cfg.symbols_path.empty()) symbols_load(g_cfg.symbols_path);
    if (!g_cfg.multicore) pin_to_one_cpu();
    if (!kmem_init()) return 1;
    if (!xthread_init_slots()) return 1;
    g_image_mapped = true; // set before mapping: a partial copy also destroys the stub
    if (!image_map(g_xbe)) return 1;
    int patched = patch_fs_accesses(g_xbe);
    if (known_build && patched != kKnownFsPatchCount)
        logf("warning: expected %d fs patches for this build, made %d", kKnownFsPatchCount, patched);
    if (!kernel_resolve_thunks(g_xbe)) return 1;
    if (!kfile_init()) return 1;
    install_crash_handlers();
    install_probes();
    if (known_build && !xd3d::install_d3d_hooks()) return 1;
    if (known_build) install_timing_patches();
    if (known_build) install_mouselook_patches();
    if (known_build) install_autoaim_patch();
    if (known_build) install_fov_patch();
    if (known_build) install_fe_calls();
    if (known_build && g_cfg.pc_menus) { // the in-game PC SETTINGS pages, from the user's own front-end file
        std::wstring cache = g_cfg.port_dir + L"\\cache\\menus";
        if (fe_build_pages(host_full_path(g_cfg.game_dir), cache)) g_cfg.menu_cache = cache;
    }
    if (g_cfg.fps == 60 && (!known_build || !install_60fps_patches())) g_cfg.fps = 30; // other builds: no 60 fps patches
    if (!kdpc_init()) {
        logf("could not start the kernel DPC/timer thread");
        return 1;
    }

    logf("starting XBE entry point 0x%08x", g_xbe.entry);
    HANDLE boot = nullptr;
    if (!NT_OK(kthread_create(&boot, nullptr, g_xbe.stack_commit, 0, g_xbe.entry, 0, 0, false, "boot", true))) {
        logf("could not create the boot thread");
        return 1;
    }

    DWORD wait = g_cfg.timeout_s ? g_cfg.timeout_s * 1000 : INFINITE;
    if (WaitForSingleObject(g_exit_event, wait) == WAIT_TIMEOUT) {
        request_exit(5, "timeout");
        dump_threads();
    }
    return g_exit_code.load();
}

} // namespace

extern "C" __declspec(dllexport) int __cdecl BlackMain(void *reserve, unsigned reserve_size)
{
    g_console = console_init();
    int code = run(reserve, reserve_size);
    if (!g_image_mapped) return code; // --info, bad arguments, or failure before the stub was overwritten
    logc("exiting with code %d", code);
    fflush(stdout);
    // The caller's code has been replaced by the XBE, and game threads may hold locks: terminate here.
    TerminateProcess(GetCurrentProcess(), (UINT)code);
    for (;;) Sleep(INFINITE);
}
