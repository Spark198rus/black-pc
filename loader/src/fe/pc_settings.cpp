// The port's settings as rows of the in-game PC SETTINGS pages (fe/pages.cpp builds the pages, fe/calls.cpp receives
// "PCChange(row, -1 / +1)" from their list boxes and "PCSlider<row>(0-100)" from their sliders). Each row is a list of choices: the current one is found from
// g_cfg, a step applies the next one and saves black.ini. Aspect ratio and frame rate are
// read by the game at boot: a new choice is saved for the next run and shown with "NEXT START" until then.
#include "fe/pc_settings.h"

#include "common.h"
#include "d3d/device.h"
#include "d3d/host.h"
#include "fe/bind_list.h"
#include "fe/vars.h"
#include "fov.h"
#include "input/autoaim.h"
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

// render size: native multiples of the window, fixed heights, or the default (3 x Xbox)
struct Res {
    bool native;
    float factor;
    int height;
    const wchar_t *text;
};
const Res kRes[] = {{true, 1.0f, 0, L"WINDOW SIZE"}, {true, 1.5f, 0, L"1.5 X WINDOW"}, {true, 2.0f, 0, L"2 X WINDOW"},
                    {false, 1.0f, 720, L"720P"},     {false, 1.0f, 1080, L"1080P"},   {false, 1.0f, 1440, L"1440P"},
                    {false, 1.0f, 2160, L"2160P"},   {false, 1.0f, 0, L"3 X ORIGINAL"}};
constexpr int kResN = sizeof kRes / sizeof kRes[0];
int res_get()
{
    for (int i = 0; i < kResN; i++) {
        const Res &r = kRes[i];
        if (r.native ? g_cfg.render_native && close_to(g_cfg.render_factor, r.factor)
                     : !g_cfg.render_native && !g_cfg.render_w && g_cfg.render_h == r.height)
            return i;
    }
    return -1; // a size from the command line
}
void res_set(int i)
{
    g_cfg.render_native = kRes[i].native;
    g_cfg.render_factor = kRes[i].factor;
    g_cfg.render_w = 0;
    g_cfg.render_h = kRes[i].height;
    xd3d::request_render_change();
}

// A slider row has the 26 positions of the game's slider clip (Sound Setup's bullet bar): field of view 70-120 in
// steps of 2, brightness and gamma 0.5-3.0 in steps of 0.1, mouse sensitivity 0.25-6.5 in steps of 0.25.
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

int shown_fps() { return g_cfg.fps_next ? g_cfg.fps_next : g_cfg.fps; }
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
    {"RESOLUTION", kResN, res_get, res_set, [](int i) { return std::wstring(i < 0 ? L"CUSTOM" : kRes[i].text); }},
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
     [](int i) { return std::wstring(i == 0 ? L"16:9" : L"4:3") + ((i == 0) != g_cfg.widescreen ? L" (NEXT START)" : L""); }},
    {"FRAME RATE", 2, [] { return (int)(shown_fps() == 60); },
     [](int i) {
         int fps = i ? 60 : 30;
         g_cfg.fps_next = fps == g_cfg.fps ? 0 : fps;
     },
     [](int i) { return std::wstring(i ? L"60 FPS" : L"30 FPS (ORIGINAL)") + ((i ? 60 : 30) != g_cfg.fps ? L" (NEXT START)" : L""); }},
    {"FILL GEOMETRY SEAMS", 2, [] { return (int)g_cfg.seam_fill; }, [](int i) { g_cfg.seam_fill = i != 0; }, [](int i) { return on_off(i); }},
    {"EFFECTS AT XBOX SIZE", 2, [] { return (int)g_cfg.effects_native; },
     [](int i) {
         g_cfg.effects_native = i != 0;
         xd3d::request_render_rebuild();
     },
     [](int i) { return on_off(i); }},
    // AUDIO AND GAME
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
};
constexpr int kRowN = sizeof kRows / sizeof kRows[0];
constexpr int kKeyBindings = 15;

} // namespace

const std::vector<Group> &groups()
{
    static const std::vector<Group> g = {
        {"PCVideo", "VIDEO", {0, 1, 2, 3, 4, 5}},
        {"PCDisplay", "DISPLAY", {6, 7, 8, 9}},
        {"PCGame", "AUDIO AND GAME", {10, 11, 12}},
        {"PCControls", "CONTROLS", {13, 14, kKeyBindings}},
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
    int i = std::min(r.get(), r.choices - 1);
    i = i < 0 ? 0 : (i + (dir > 0 ? 1 : r.choices - 1)) % r.choices;
    r.set(i);
    settings_save();
    logf("fe: %s -> %s", r.label, narrow(r.text(i)).c_str());
}

bool is_slider(int n) { return n >= 0 && n < kRowN && kRows[n].slider; }

bool restart_only(int n) { return n == 6 || n == 7; } // aspect ratio, frame rate: read by the game at boot

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
