// The port's settings as rows of the in-game PC SETTINGS pages (fe/pages.cpp builds the pages, fe/calls.cpp receives
// "PCChange(row, -1 / +1)" from their list boxes and "PCSlider<row>(0-100)" from their sliders). Each row is a list of choices: the current one is found from
// g_cfg, a step applies the next one and saves black.ini. Aspect ratio, frame rate and the language are
// read by the game at boot: a new choice is saved for the next run and shown with "(RESTART)" until then
// (the value box wraps at about 18 letters and shows only the first line, so "NEXT START" did not fit).
#include "fe/pc_settings.h"

#include "common.h"
#include "d3d/device.h"
#include "d3d/host.h"
#include "fe/bind_list.h"
#include "fe/vars.h"
#include "fov.h"
#include "input/autoaim.h"
#include "bigbuf.h"
#include "language.h"
#include "reload_blur.h"
#include "cheats.h"
#include "input/sprint.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace pcset {
namespace {

bool close_to(float a, float b) { return std::fabs(a - b) < 0.01f; }
std::wstring on_off(bool on) { return on ? L"ON" : L"OFF"; }
std::wstring number(float v, const wchar_t *fmt = L"%.1f")
{
    wchar_t s[32];
    swprintf(s, 32, fmt, v);
    return s;
}

// Render size (issue #40; user 2026-11-13). RESOLUTION: WINDOW SIZE (the picture in the window, times RENDER SCALE)
// or a fixed height (width from the aspect). The default (--scale 3: 3 x the Xbox's 480 lines) is the same size as
// 1440P and shows as it. 480P is the Xbox's own size; 576P and 624P are 1024x576 and 1109x624 in 16:9, 768P is
// 1024x768 in 4:3 (the issue's sizes for an iGPU). Fullscreen at a fixed height also switches the display to a mode
// of that height when it has one (host3d::update_display_mode). RENDER SCALE: a share of the window, 50-200%;
// moving it sets RESOLUTION to WINDOW SIZE. It keeps its value while a fixed height is chosen (render_factor).
struct Res {
    int height; // 0 = WINDOW SIZE
    const wchar_t *text;
};
const Res kRes[] = {{0, L"WINDOW SIZE"}, {480, L"480P"},   {576, L"576P"},   {624, L"624P"},   {720, L"720P"},
                    {768, L"768P"},      {900, L"900P"},   {1080, L"1080P"}, {1440, L"1440P"}, {2160, L"2160P"}};
constexpr int kResN = sizeof kRes / sizeof kRes[0];
int res_get()
{
    if (g_cfg.render_native) return 0;
    if (g_cfg.render_w) return -1;
    for (int i = 1; i < kResN; i++)
        if (g_cfg.render_h == kRes[i].height || (!g_cfg.render_h && kRes[i].height == 1440 && close_to(g_cfg.scale, 3.0f)))
            return i;
    return -1; // a size from the command line
}
void render_changed()
{
    xd3d::request_render_change();
    host3d::update_display_mode();
}
void res_set(int i)
{
    g_cfg.render_native = kRes[i].height == 0; // WINDOW SIZE keeps the slider's share
    g_cfg.render_w = 0;
    g_cfg.render_h = kRes[i].height;
    render_changed();
}
// RENDER SCALE positions (the slider clip has 26): 50-150% in steps of 5, then 160-200% in steps of 10.
const float kScales[] = {50,  55,  60,  65,  70,  75,  80,  85,  90,  95,  100, 105, 110,
                         115, 120, 125, 130, 135, 140, 145, 150, 160, 170, 180, 190, 200};
static_assert(sizeof kScales / sizeof kScales[0] == 26, "the slider has 26 positions");
void picture_size(int *w, int *h)
{
    RECT frame, pic;
    host3d::frame_layout(&frame, &pic);
    *w = pic.right - pic.left;
    *h = pic.bottom - pic.top;
}
int scale_get();
void scale_set(int i)
{
    g_cfg.render_native = true;
    g_cfg.render_factor = kScales[i] / 100.0f;
    g_cfg.render_w = g_cfg.render_h = 0;
    render_changed();
}
std::wstring scale_text(int i)
{
    wchar_t s[16];
    swprintf(s, 16, L"%.0f%%", kScales[i]);
    return s;
}
// A size from the command line shows as its pixels.
std::wstring res_text(int i)
{
    if (i >= 0) return kRes[i].text;
    wchar_t s[32];
    swprintf(s, 32, L"%dX%d", (int)xd3d::host_w(640), (int)xd3d::host_h(480));
    return s;
}

// A slider row has the 26 positions of the game's slider clip (Sound Setup's bullet bar): field of view 70-120 in
// steps of 2, brightness and gamma 0.5-3.0 in steps of 0.1, mouse sensitivity 0.25-6.5 in steps of 0.25, lean angle
// 0-25 degrees in steps of 1, lean distance 0-0.625 m in steps of 0.025.
constexpr int kSteps = 26;
int step_of(float v, float first, float step) { return std::clamp((int)std::lround((v - first) / step), 0, kSteps - 1); }
const float kReverb[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f}; // 0 = off

template <size_t N> int nearest(const float (&list)[N], float v)
{
    int best = 0;
    for (int i = 1; i < (int)N; i++)
        if (std::fabs(list[i] - v) < std::fabs(list[best] - v)) best = i;
    return best;
}
int scale_get() { return nearest(kScales, g_cfg.render_factor * 100.0f); }

int shown_fps() { return g_cfg.fps_next ? g_cfg.fps_next : g_cfg.fps; }

// The game's language (language.cpp): the PAL disc carries English, German, French, Spanish and Italian, chosen at
// boot; AUTO follows Windows. The NTSC-U disc has English only, so its row has that one choice.
struct Lang {
    const char *name;
    const wchar_t *text;
};
const Lang kLangs[] = {{"auto", L"AUTO (WINDOWS)"}, {"en", L"ENGLISH"}, {"de", L"DEUTSCH"},
                       {"fr", L"FRAN\u00C7AIS"},   {"es", L"ESPA\u00D1OL"}, {"it", L"ITALIANO"}};
constexpr int kLangN = sizeof kLangs / sizeof kLangs[0];
int lang_of(const std::string &name)
{
    for (int i = 0; i < kLangN; i++)
        if (name == kLangs[i].name) return i;
    return 0;
}
std::wstring lang_text(int i)
{
    if (!g_cfg.pal_build) return L"ENGLISH (THIS DISC)";
    if (lang_of(language_at_start()) == i) return kLangs[i].text;
    return std::wstring(i == 0 ? L"AUTO" : kLangs[i].text) + L" (RESTART)";
}
bool shown_wide() { return g_cfg.widescreen_next >= 0 ? g_cfg.widescreen_next != 0 : g_cfg.widescreen; }

struct Row {
    const char *label;
    int choices;
    int (*get)();
    void (*set)(int);
    std::wstring (*text)(int);
    bool slider = false;
};

const Row kRows[] = {
    // VIDEO
    {"RESOLUTION", kResN, res_get, res_set, res_text},
    {"ANTI-ALIASING", 2, [] { return (int)g_cfg.aa; }, [](int i) { g_cfg.aa = i != 0; }, [](int i) { return on_off(i); }},
    {"FIELD OF VIEW", kSteps, [] { return step_of(g_cfg.fov, 70, 2); },
     [](int i) {
         g_cfg.fov = 70.0f + 2 * i;
         fov_set(g_cfg.fov);
     },
     [](int i) { return number(70.0f + 2 * i, L"%.0f") + (i == 0 ? L" (ORIGINAL)" : L""); }, true},
    {"BRIGHTNESS", kSteps, [] { return step_of(g_cfg.gain, 0.5f, 0.1f); }, [](int i) { g_cfg.gain = 0.5f + i * 0.1f; },
     [](int i) { return number(0.5f + i * 0.1f); }, true},
    {"GAMMA", kSteps, [] { return step_of(g_cfg.gamma, 0.5f, 0.1f); }, [](int i) { g_cfg.gamma = 0.5f + i * 0.1f; },
     [](int i) { return number(0.5f + i * 0.1f); }, true},
    {"FULLSCREEN", 2, [] { return (int)host3d::fullscreen(); }, [](int i) { host3d::set_fullscreen(i != 0); },
     [](int i) { return on_off(i); }},
    // DISPLAY
    {"ASPECT RATIO", 2, [] { return (int)!shown_wide(); },
     [](int i) {
         bool wide = i == 0;
         g_cfg.widescreen_next = wide == g_cfg.widescreen ? -1 : wide;
     },
     [](int i) { return std::wstring(i == 0 ? L"16:9" : L"4:3") + ((i == 0) != g_cfg.widescreen ? L" (RESTART)" : L""); }},
    {"FRAME RATE", 2, [] { return (int)(shown_fps() == 60); },
     [](int i) {
         int fps = i ? 60 : 30;
         g_cfg.fps_next = fps == g_cfg.fps ? 0 : fps;
     },
     [](int i) {
         if ((i ? 60 : 30) != g_cfg.fps) return std::wstring(i ? L"60 FPS (RESTART)" : L"30 FPS (RESTART)");
         return std::wstring(i ? L"60 FPS" : L"30 FPS (ORIGINAL)");
     }},
    {"FILL GEOMETRY SEAMS", 2, [] { return (int)g_cfg.seam_fill; }, [](int i) { g_cfg.seam_fill = i != 0; }, [](int i) { return on_off(i); }},
    {"EFFECTS AT XBOX SIZE", 2, [] { return (int)g_cfg.effects_native; },
     [](int i) {
         g_cfg.effects_native = i != 0;
         xd3d::request_render_rebuild();
     },
     [](int i) { return on_off(i); }},
    // AUDIO AND MOVIES (10, 12), TWEAKS (11)
    {"ROOM REVERB", 5, [] { return g_cfg.reverb ? std::max(1, nearest(kReverb, g_cfg.reverb_gain)) : 0; },
     [](int i) {
         g_cfg.reverb = i != 0;
         if (i) g_cfg.reverb_gain = kReverb[i];
     },
     [](int i) { return i ? number(kReverb[i] * 100, L"%.0f%%") : std::wstring(L"OFF"); }},
    {"AUTO-AIM", 2, [] { return (int)g_cfg.autoaim; }, [](int i) { autoaim_set(i != 0); }, [](int i) { return on_off(i); }},
    {"SKIP ANY MOVIE", 2, [] { return (int)g_cfg.skip_movies; }, [](int i) { g_cfg.skip_movies = i != 0; }, [](int i) { return on_off(i); }},
    // CONTROLS
    {"MOUSE SENSITIVITY", kSteps, [] { return step_of(g_cfg.mouse_sens, 0.25f, 0.25f); },
     [](int i) { g_cfg.mouse_sens = 0.25f * (i + 1); }, [](int i) { return number(0.25f * (i + 1), L"%.2f"); }, true},
    {"INVERT MOUSE", 2, [] { return (int)g_cfg.mouse_invert; }, [](int i) { g_cfg.mouse_invert = i != 0; }, [](int i) { return on_off(i); }},
    {"KEY BINDINGS", 1, [] { return 0; }, [](int) {}, [](int) { return std::wstring(); }}, // opens the bindings page
    // added after the others, so the rows before keep their numbers: AUDIO AND MOVIES (16), TWEAKS (17-19)
    {"LANGUAGE", kLangN, [] { return g_cfg.pal_build ? lang_of(g_cfg.language) : 0; },
     [](int i) {
         if (g_cfg.pal_build) g_cfg.language = kLangs[i].name;
     },
     lang_text},
    {"RELOAD BLUR", 2, [] { return (int)g_cfg.reload_blur; }, [](int i) { reload_blur_set(i != 0); }, [](int i) { return on_off(i); }},
    // tactical lean (input/lean.cpp reads g_cfg every frame)
    {"LEAN ANGLE", kSteps, [] { return step_of(g_cfg.lean_angle, 0, 1); }, [](int i) { g_cfg.lean_angle = (float)i; },
     [](int i) { return i ? number((float)i, L"%.0f") : std::wstring(L"0 (NO ROLL)"); }, true},
    {"LEAN DISTANCE", kSteps, [] { return step_of(g_cfg.lean_offset, 0, 0.025f); }, [](int i) { g_cfg.lean_offset = 0.025f * i; },
     [](int i) { return i ? number(0.025f * i, L"%.2f M") : std::wstring(L"0 (NO SHIFT)"); }, true},
    // weapon-data buffers for modded weapon files (bigbuf.cpp), from the next start
    {"WEAPON BUFFERS", 4, [] { return g_cfg.buf_mb >= 8 ? 3 : g_cfg.buf_mb >= 4 ? 2 : g_cfg.buf_mb >= 2 ? 1 : 0; },
     [](int i) { g_cfg.buf_mb = i ? 1u << i : 0; },
     [](int i) {
         uint32_t mb = i ? 1u << i : 0;
         std::wstring s = mb ? number((float)mb, L"%.0f MB") : std::wstring(L"ORIGINAL");
         return mb != bigbuf_active_mb() ? s + L" (RESTART)" : s;
     }},
    // CONTROLS (21): the game's own hold path (input/mouselook.cpp), from the next frame in play
    {"AIM", 2, [] { return (int)g_cfg.aim_hold; }, [](int i) { g_cfg.aim_hold = i != 0; },
     [](int i) { return std::wstring(i ? L"HOLD" : L"TOGGLE (ORIGINAL)"); }},
    // CHEATS (22-25, cheats.cpp), live
    {"UNLOCK ALL MISSIONS", 2, [] { return (int)g_cfg.cheat_unlock; },
     [](int i) { g_cfg.cheat_unlock = i != 0; cheats_apply(); }, [](int i) { return on_off(i); }},
    {"GOD MODE", 2, [] { return (int)g_cfg.cheat_god; }, [](int i) { g_cfg.cheat_god = i != 0; cheats_apply(); },
     [](int i) { return on_off(i); }},
    {"INFINITE AMMO", 2, [] { return (int)g_cfg.cheat_ammo; }, [](int i) { g_cfg.cheat_ammo = i != 0; cheats_apply(); },
     [](int i) { return on_off(i); }},
    {"DAMAGE TO ENEMIES", 4, [] { return g_cfg.cheat_damage; }, [](int i) { g_cfg.cheat_damage = i; cheats_apply(); },
     [](int i) { return std::wstring(i == 3 ? L"ONE-HIT KILL" : i == 2 ? L"X4" : i == 1 ? L"X2" : L"X1 (ORIGINAL)"); }},
    // CONTROLS (26): the Sprint key (input/sprint.cpp), live
    {"SPRINT", 2, [] { return (int)g_cfg.sprint; }, [](int i) { g_cfg.sprint = i != 0; }, [](int i) { return on_off(i); }},
    // TWEAKS (27): menu animations twice as fast (fe/menu_speed.cpp), live
    {"FAST MENUS", 2, [] { return (int)g_cfg.fast_menus; }, [](int i) { g_cfg.fast_menus = i != 0; }, [](int i) { return on_off(i); }},
    // VIDEO (28): the render size as a share of the window (issue #40), live
    {"RENDER SCALE", kSteps, scale_get, scale_set, scale_text, true},
};
constexpr int kRowN = sizeof kRows / sizeof kRows[0];
constexpr int kKeyBindings = 15;
constexpr int kLanguage = 16;
constexpr int kWeaponBuffers = 20;
constexpr int kAimMode = 21;

} // namespace

const std::vector<Group> &groups()
{
    static const std::vector<Group> g = {
        {"PCVideo", "VIDEO", {0, 28, 1, 2, 3, 4}},
        {"PCDisplay", "DISPLAY", {5, 6, 7, 8, 9}},
        {"PCTweaks", "TWEAKS", {11, 17, 18, 19, 27, kWeaponBuffers}},
        {"PCAudio", "AUDIO AND MOVIES", {10, 12, kLanguage}},
        {"PCControls", "CONTROLS", {13, 14, kAimMode, 26, kKeyBindings}},
        {"PCCheats", "CHEATS", {22, 23, 24, 25}},
    };
    return g;
}

int count() { return kRowN; }
const char *label(int n) { return n >= 0 && n < kRowN ? kRows[n].label : ""; }

std::wstring value(int n)
{
    if (n < 0 || n >= kRowN) return L"";
    const Row &r = kRows[n];
    int i = std::min(r.get(), r.choices - 1);
    if (i < 0 && r.get != res_get) i = 0; // only the render size has a value outside the list (CUSTOM)
    return r.text(i);
}

void step(int n, int dir)
{
    if (n < 0 || n >= kRowN || !dir) return;
    const Row &r = kRows[n];
    int choices = n == kLanguage && !g_cfg.pal_build ? 1 : r.choices;
    int i = std::min(r.get(), choices - 1);
    i = i < 0 ? 0 : (i + (dir > 0 ? 1 : choices - 1)) % choices;
    r.set(i);
    settings_save();
    logf("fe: %s -> %s", r.label, narrow(r.text(i)).c_str());
}

bool is_slider(int n) { return n >= 0 && n < kRowN && kRows[n].slider; }

bool restart_only(int n) { return n == 6 || n == 7 || n == kLanguage || n == kWeaponBuffers; } // read by the game at boot

const char *link(int n) { return n == kKeyBindings ? "PCBindings" : nullptr; }

int slider_pos(int n)
{
    if (!is_slider(n)) return 0;
    return std::clamp(kRows[n].get(), 0, kSteps - 1) * 100 / (kSteps - 1);
}

void slide(int n, float pos)
{
    if (!is_slider(n)) return;
    const Row &r = kRows[n];
    int i = std::clamp((int)std::lround(pos * (kSteps - 1) / 100.0f), 0, kSteps - 1);
    if (i == r.get()) return;
    r.set(i);
    settings_save();
    logf("fe: %s -> %s", r.label, narrow(r.text(i)).c_str());
}

void fill_page(int group)
{
    const std::vector<Group> &gs = groups();
    if (group < 0 || group >= (int)gs.size()) return;
    const std::vector<int> &rows = gs[group].rows;
    for (int i = 0; i < fevar::kRows; i++) {
        int n = i < (int)rows.size() ? rows[i] : -1;
        fe_var_set(fevar::kValue + i, n >= 0 ? value(n) : L"");
    }
}

// a slider takes its start from externObject.FEPCS<setting> when its page is prepared, before the page reports itself,
// so these follow the settings all the time
void refresh_sliders()
{
    for (int n = 0; n < kRowN; n++)
        if (kRows[n].slider) fe_int_set(("FEPCS" + std::to_string(n)).c_str(), slider_pos(n));
}

} // namespace pcset
