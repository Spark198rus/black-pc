// Native Linux x86 Executable Entry Point for BLACK PC (black_linux)
// Combines POSIX VFS, KPCR %fs, HLE Kernel Thunks, SDL2 Host & Input, and DXVK-Native D3D9.
#include "common.h"
#include "platform/windefs.h"
#include "platform/xbe_loader.h"
#include "platform/k_exports_posix.h"
#include "platform/k_file_posix.h"
#include "platform/k_memory_posix.h"
#include "platform/k_sync_posix.h"
#include "platform/k_thread_posix.h"
#include "platform/k_timing_posix.h"
#include "platform/host_sdl.h"
#include "platform/input_sdl.h"
#include "platform/audio_sdl.h"
#include "d3d/host.h"
#include "d3d/dispatch.h"
#include "fe/pages.h"
#include "fe/calls.h"
#include "fov.h"
#include "hooks.h"
#include "settings.h"
#include "timing.h"
#include "input/autoaim.h"
#include "input/bindings.h"
#include "input/mouselook.h"
#include "sha1.h"

#include <SDL2/SDL.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <atomic>


namespace {

const char kKnownXbeSha256[] = "df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6";

std::atomic<bool> g_running{true};
std::atomic<int>  g_exit_code{0};

void sig_handler(int sig)
{
    logc("[main_linux] Signal %d received, requesting exit...", sig);
    g_running = false;
}

struct SHA256_CTX {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

static inline uint32_t rotr(uint32_t a, uint32_t b) { return (a >> b) | (a << (32 - b)); }

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[])
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };

    uint32_t m[64];
    for (int i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + k[i] + m[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[])
{
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = ctx->bitlen;
    ctx->data[62] = ctx->bitlen >> 8;
    ctx->data[61] = ctx->bitlen >> 16;
    ctx->data[60] = ctx->bitlen >> 24;
    ctx->data[59] = ctx->bitlen >> 32;
    ctx->data[58] = ctx->bitlen >> 40;
    ctx->data[57] = ctx->bitlen >> 48;
    ctx->data[56] = ctx->bitlen >> 56;
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

std::string compute_sha256(const std::vector<uint8_t> &data)
{
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data.data(), data.size());
    uint8_t hash[32];
    sha256_final(&ctx, hash);

    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", hash[i]);
    hex[64] = '\0';
    return std::string(hex);
}

void print_usage(const char *prog)
{
    printf("BLACK PC Native Linux Port (x86)\n");
    printf("Usage: %s [options] [path/to/default.xbe | game_folder]\n", prog);
    printf("Options:\n");
    printf("  --info            Print XBE header info and exit\n");
    printf("  --hdd <dir>       Path to savedata directory (default: ./savedata)\n");
    printf("  --log <file>      Path to log file (default: ./black.log)\n");
    printf("  --symbols <csv>   Load symbols from CSV (default: notes/functions.csv)\n");
    printf("  --timeout <sec>   Automatically exit after N seconds (useful for tests)\n");
    printf("  --background      Run in background/hidden mode (audio/display quiet)\n");
    printf("  --fullscreen      Launch in fullscreen mode\n");
    printf("  --fps 30|60       Target frame rate (default: 60)\n");
    printf("  --scale <factor>  Render scaling factor (default: 1.0)\n");
    printf("  --resolution WxH  Internal render resolution (e.g. 1280x720, 1920x1080, native)\n");
    printf("  --window WxH      Host window dimensions (default: 1280x720)\n");
    printf("  --hud, --show-fps Enable DXVK on-screen Vulkan overlay HUD\n");
    printf("  --skip-movies     Allow skipping movies/cutscenes with Enter/Space (default: true)\n");
    printf("  --no-movie-skip   Do not allow skipping unearned movies\n");
    printf("  --fov <degrees>   Field of view (50 to 120, default: 70)\n");
    printf("  --autoaim         Enable auto-aim (default: disabled)\n");
    printf("  --no-autoaim      Disable auto-aim\n");
    printf("  --no-pc-menus     Do not build/inject PC SETTINGS frontend pages\n");
    printf("  --force           Bypass SHA-256 verification of default.xbe\n");
    printf("  --keybinds, --list-binds  Print current keybindings and controls table and exit\n");
    printf("  --trace           Log kernel exports on call\n");
    printf("  --trace-d3d       Log D3D API calls\n");
    printf("  --help, -h        Show this help message\n");
}

} // namespace

void request_exit(int code, const char *why)
{
    logc("[main_linux] Exit requested: %s (code %d)", why, code);
    g_exit_code = code;
    g_running = false;
}

bool exit_requested()
{
    return !g_running;
}

int main(int argc, char **argv)
{
    std::string target_xbe = "game/default.xbe";
    std::string hdd_dir    = "savedata";
    std::string log_path   = "black.log";
    std::string sym_path   = "notes/functions.csv";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next_arg = [&]() -> std::string {
            if (i + 1 < argc) return argv[++i];
            return "";
        };

        if (arg == "--keybinds" || arg == "--list-binds") {
            settings_load();
            bindings::print_table();
            return 0;
        } else if (arg == "--info") {
            g_cfg.info_only = true;
        } else if (arg == "--hdd") {
            hdd_dir = next_arg();
        } else if (arg == "--log") {
            log_path = next_arg();
        } else if (arg == "--symbols") {
            sym_path = next_arg();
        } else if (arg == "--timeout") {
            g_cfg.timeout_s = (unsigned)atoi(next_arg().c_str());
        } else if (arg == "--press") {
            g_cfg.press_script = widen(next_arg());
        } else if (arg == "--press-file") {
            g_cfg.press_file = widen(next_arg());
        } else if (arg == "--background") {
            g_cfg.background = true;
            setenv("BLACK_HEADLESS", "1", 1);
        } else if (arg == "--fullscreen") {
            settings_mark_cli("fullscreen");
            g_cfg.fullscreen = true;
        } else if (arg == "--fps") {
            settings_mark_cli("fps");
            int fps = atoi(next_arg().c_str());
            g_cfg.fps = (fps == 30) ? 30 : 60;
        } else if (arg == "--scale") {
            settings_mark_cli("scale");
            g_cfg.scale = (float)atof(next_arg().c_str());
            if (g_cfg.scale < 0.5f) g_cfg.scale = 0.5f;
            if (g_cfg.scale > 8.0f) g_cfg.scale = 8.0f;
        } else if (arg == "--resolution") {
            settings_mark_cli("render");
            std::string res = next_arg();
            if (res == "native") {
                g_cfg.render_native = true;
                g_cfg.render_w = 0;
                g_cfg.render_h = 0;
            } else {
                size_t x = res.find_first_of("xX");
                if (x != std::string::npos) {
                    g_cfg.render_w = atoi(res.substr(0, x).c_str());
                    g_cfg.render_h = atoi(res.substr(x + 1).c_str());
                    g_cfg.render_native = false;
                }
            }
        } else if (arg == "--window") {
            std::string win = next_arg();
            size_t x = win.find_first_of("xX");
            if (x != std::string::npos) {
                g_cfg.window_w = atoi(win.substr(0, x).c_str());
                g_cfg.window_h = atoi(win.substr(x + 1).c_str());
            }
        } else if (arg == "--hud" || arg == "--show-fps") {
            setenv("DXVK_HUD", "fps,frametimes", 1);
        } else if (arg == "--skip-movies") {
            settings_mark_cli("skipanymovie");
            g_cfg.skip_movies = true;
        } else if (arg == "--no-movie-skip") {
            settings_mark_cli("skipanymovie");
            g_cfg.skip_movies = false;
        } else if (arg == "--fov") {
            settings_mark_cli("fov");
            g_cfg.fov = (float)atof(next_arg().c_str());
            g_cfg.fov = std::clamp(g_cfg.fov, 50.0f, 120.0f);
        } else if (arg == "--autoaim") {
            settings_mark_cli("autoaim");
            g_cfg.autoaim = true;
        } else if (arg == "--no-autoaim") {
            settings_mark_cli("autoaim");
            g_cfg.autoaim = false;
        } else if (arg == "--no-pc-menus") {
            g_cfg.pc_menus = false;
        } else if (arg == "--force") {
            g_cfg.force = true;
        } else if (arg == "--trace") {
            g_cfg.trace = true;
            xposix::kposix_set_trace(true);
        } else if (arg == "--trace-d3d") {
            g_trace_d3d = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            target_xbe = arg;
        }
    }

    // The port keeps its own files beside black_linux (the release's port folder, next to game).
    char exe_buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    std::string exe_path;
    if (len > 0) {
        exe_buf[len] = '\0';
        exe_path = exe_buf;
        size_t slash = exe_path.find_last_of("/\\");
        g_cfg.port_dir = widen(slash == std::string::npos ? "." : exe_path.substr(0, slash));
    } else {
        g_cfg.port_dir = L".";
    }

    // Started from a shortcut or another folder: if a relative game path is not found here, look for it
    // from black_linux's own folder upwards (port -> the folder with game; work/run -> work -> project root) and make
    // that folder current, so relative paths given on the command line (--hdd, --log) and notes/ resolve against it.
    bool relative = !(target_xbe.size() >= 1 && target_xbe[0] == '/');
    struct stat st;
    if (relative && stat(target_xbe.c_str(), &st) != 0 && !exe_path.empty()) {
        std::string dir = exe_path;
        for (int up = 0; up < 4; up++) {
            size_t cut = dir.find_last_of("/\\");
            if (cut == std::string::npos) break;
            dir.resize(cut);
            std::string cand = dir + "/" + target_xbe;
            if (stat(cand.c_str(), &st) == 0) {
                if (chdir(dir.c_str()) == 0) {
                    break;
                }
            }
        }
    }

    // Adjust target_xbe if directory given
    if (stat(target_xbe.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (target_xbe.back() == '/') target_xbe += "default.xbe";
        else target_xbe += "/default.xbe";
    }

    // Determine game directory
    size_t slash = target_xbe.find_last_of("/\\");
    std::string game_dir = (slash == std::string::npos) ? "." : target_xbe.substr(0, slash);

    g_cfg.xbe_path = widen(target_xbe);
    g_cfg.game_dir = widen(game_dir);

    std::string port_dir_narrow = narrow(g_cfg.port_dir);
    if (hdd_dir == "savedata" && !port_dir_narrow.empty() && port_dir_narrow != ".") {
        hdd_dir = port_dir_narrow + "/savedata";
    }
    if (log_path == "black.log" && !port_dir_narrow.empty() && port_dir_narrow != ".") {
        log_path = port_dir_narrow + "/black.log";
    }

    g_cfg.hdd_dir  = widen(hdd_dir);
    g_cfg.log_path = widen(log_path);

    settings_load(); // load black.ini from port_dir if present; CLI flags above retain precedence


    // Initialize Logging
    log_init(g_cfg.log_path);
    logc("================================================================================");
    logc("BLACK PC Native Linux Port (32-bit x86 ELF)");
    logc("Target XBE:  %s", target_xbe.c_str());
    logc("Game Dir:    %s", game_dir.c_str());
    logc("SaveData:    %s", hdd_dir.c_str());
    logc("Log File:    %s", log_path.c_str());
    logc("Target FPS:  %d", g_cfg.fps);
    logc("================================================================================");

    // Handle signals for graceful termination
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    // Parse XBE
    XbeLoader loader;
    std::string err;
    if (!loader.load(target_xbe, err)) {
        logc("[FATAL] Could not load XBE '%s': %s", target_xbe.c_str(), err.c_str());
        return 1;
    }

    if (g_cfg.info_only) {
        printf("XBE Title:       %s (Title ID: %08X)\n", loader.title.c_str(), loader.title_id);
        printf("Base Address:    0x%08X (Size: 0x%08X)\n", loader.base, loader.size_of_image);
        printf("Entry Point:     0x%08X\n", loader.entry);
        printf("Sections:        %zu\n", loader.sections.size());
        for (const auto &s : loader.sections) {
            printf("  - %-12s: VA 0x%08X, Size 0x%08X, Raw 0x%08X, Flags 0x%08X\n",
                   s.name.c_str(), s.vaddr, s.vsize, s.raw_addr, s.flags);
        }
        return 0;
    }

    // Verify SHA-256
    std::string sha256 = compute_sha256(loader.data);
    bool known_build = (sha256 == kKnownXbeSha256);
    g_cfg.known_build = known_build;
    logc("[main_linux] XBE SHA-256: %s (%s)", sha256.c_str(), known_build ? "known NTSC-U build" : "unknown build");
    if (!known_build && !g_cfg.force) {
        logc("[FATAL] Unknown XBE build. Pass --force to bypass verification.");
        return 1;
    }

    // Load symbols if available
    struct stat st_sym;
    if (stat(sym_path.c_str(), &st_sym) == 0) {
        symbols_load(widen(sym_path));
    }

    // Initialize Subsystems
    xposix::timing_init();
    xposix::sync_init();
    xposix::memory_init();

    // Initialize Threading Subsystem & %fs KPCR LDT
    xposix::thread_subsystem_init();

    // Initialize VFS (Mounts \Device\CdRom0 and \Device\Harddisk0\Partition1)
    xposix::file_init(game_dir, hdd_dir);

    // Map XBE sections to 0x00010000..
    if (!loader.map_image(err)) {
        logc("[FATAL] Failed to map XBE image: %s", err.c_str());
        return 1;
    }
    logc("[main_linux] XBE image mapped successfully at 0x%08x.", loader.base);

    // Patch Ring 0 privileged instructions:
    // Black executes 'wbinvd' (0x0F 0x09) at 0x00209F79 to invalidate CPU cache for NV2A.
    // In Ring 3 on PC Linux x86, wbinvd triggers a General Protection Fault (SIGSEGV).
    uint8_t *wbinvd_ptr = (uint8_t *)(uintptr_t)0x00209F79;
    if (wbinvd_ptr[0] == 0x0F && wbinvd_ptr[1] == 0x09) {
        wbinvd_ptr[0] = 0x90; // NOP
        wbinvd_ptr[1] = 0x90; // NOP
        logc("[main_linux] Patched Ring 0 instruction 'wbinvd' at 0x00209F79 to NOP.");
    }

    // Resolve Xbox Kernel Thunks
    uint32_t *thunk_table = (uint32_t *)(uintptr_t)loader.thunk_addr;
    if (!xposix::kposix_resolve_thunks(thunk_table)) {
        logc("[FATAL] Failed to resolve Xbox kernel thunks.");
        return 1;
    }

    // Front-end Flash/Apt UI injection
    if (g_cfg.pc_menus) {
        std::wstring cache = L"cache/menus";
        if (fe_build_pages(widen(game_dir), cache)) {
            g_cfg.menu_cache = cache;
            xposix::set_cache_override("cache/menus");
            logc("[main_linux] Patched PC SETTINGS menus built in 'cache/menus' and mounted to VFS.");
        } else {
            logc("[main_linux] Frontend menu patch skipped; original game menus retained.");
        }
    }

    // Install Game Patches and Subsystems
    if (known_build) {
        if (!xd3d::install_d3d_hooks()) {
            logc("[FATAL] Failed to install Direct3D HLE hooks.");
            return 1;
        }
        install_timing_patches();
        install_mouselook_patches();
        install_autoaim_patch();
        install_fov_patch();
        install_fe_calls();
        if (g_cfg.fps == 60) install_60fps_patches();
    }

    // Initialize Host Display & Window (SDL2 + DXVK D3D9)
    int win_w = (g_cfg.window_w > 0) ? g_cfg.window_w : 1280;
    int win_h = (g_cfg.window_h > 0) ? g_cfg.window_h : 720;
    if (!host3d::open_window(win_w, win_h, 16, 9)) {
        logc("[FATAL] Failed to open SDL2 host window.");
        return 1;
    }

    if (g_cfg.fullscreen) {
        host_sdl::set_fullscreen(true);
    }

    if (!host3d::create_device()) {
        logc("[FATAL] Failed to create DXVK Direct3D9 device.");
        return 1;
    }

    // Initialize Audio & Input Subsystems
    if (!audio_sdl::open()) {
        logc("[WARN] Audio output initialization failed; running silent.");
    }
    if (!input_sdl::init()) {
        logc("[WARN] Input initialization failed.");
    }

    // Launch Xbox Boot Thread at Entry Point
    logc("[main_linux] Creating and launching Xbox boot thread at 0x%08X...", loader.entry);
    HANDLE boot_thread = nullptr;
    NTSTATUS status = xposix::kthread_create(&boot_thread, nullptr, loader.stack_commit, 0,
                                            loader.entry, 0, 0, false, "boot", true);
    if (!NT_OK(status)) {
        logc("[FATAL] Failed to create Xbox boot thread (status 0x%08X).", status);
        return 1;
    }

    // Main Host Loop
    logc("[main_linux] Entering main host execution loop.");
    uint32_t start_ticks = SDL_GetTicks();
    while (g_running && !host_sdl::window_closed()) {
        host_sdl::pump_events();

        if (g_cfg.timeout_s > 0) {
            uint32_t elapsed_s = (SDL_GetTicks() - start_ticks) / 1000;
            if (elapsed_s >= g_cfg.timeout_s) {
                logc("[main_linux] Execution timeout of %u seconds reached. Shutting down cleanly.", g_cfg.timeout_s);
                break;
            }
        }
        SDL_Delay(2);
    }

    g_running = false;
    logc("[main_linux] Shutting down subsystems...");
    settings_save();
    input_sdl::shutdown();
    audio_sdl::close();
    host_sdl::close_window();

    logc("[main_linux] BLACK Linux execution finished with exit code %d.", g_exit_code.load());
    _exit(g_exit_code.load());
}
