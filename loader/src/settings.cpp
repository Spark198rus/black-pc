#include "settings.h"

#include "common.h"
#include "input/bindings.h"
#include "language.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <string>

namespace {

std::wstring ini_file() { return g_cfg.port_dir.empty() ? L"black.ini" : g_cfg.port_dir + L"\\black.ini"; }
std::set<std::string> g_cli; // keys given on the command line
std::mutex g_lock;           // the menu (window thread) and the exit path may both save

bool cli(const char *key) { return g_cli.count(key) != 0; }

} // namespace

void settings_mark_cli(const char *key) { g_cli.insert(key); }

static int s_fps_above_60; // the request, logged once the log is open (the settings are read before it)

int fps_setting(int requested)
{
    if (requested > 60) s_fps_above_60 = requested;
    return requested >= 60 ? 60 : 30;
}

void fps_setting_note()
{
    if (s_fps_above_60)
        logf("settings: frame rate %d requested; 60 is the highest (the game steps a fixed time per frame)", s_fps_above_60);
}

void settings_load()
{
    if (g_cfg.background) return;
    FILE *f = nullptr;
    if (_wfopen_s(&f, ini_file().c_str(), L"r") || !f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char key[64] = "", val[128] = "";
        if (line[0] == '#' || sscanf_s(line, " %63[^= ] = %127s", key, (unsigned)sizeof key, val, (unsigned)sizeof val) != 2) continue;
        std::string k = key;
        if (cli(k.c_str())) continue;
        float v = (float)atof(val); // the C runtime's default "C" locale: '.' decimals
        if (k == "render") {
            g_cfg.render_native = !strcmp(val, "native");
            g_cfg.render_h = g_cfg.render_native ? 0 : std::clamp(atoi(val), 0, 8192);
            g_cfg.render_w = 0;
            if (g_cfg.render_h && g_cfg.render_h < 240) g_cfg.render_h = 0;
        } else if (k == "render_factor") g_cfg.render_factor = std::clamp(v, 0.5f, 2.0f);
        else if (k == "scale") g_cfg.scale = std::clamp(v, 0.5f, 8.0f);
        else if (k == "aa") g_cfg.aa = v != 0;
        else if (k == "seamfill") g_cfg.seam_fill = v != 0;
        else if (k == "effectsnative") g_cfg.effects_native = v != 0;
        else if (k == "fps") g_cfg.fps = fps_setting(atoi(val));
        else if (k == "widescreen") g_cfg.widescreen = v != 0;
        else if (k == "language") { if (language_valid(val)) g_cfg.language = val; }
        else if (k == "fullscreen") g_cfg.fullscreen = v != 0;
        else if (k == "gain") g_cfg.gain = std::clamp(v, 0.5f, 3.0f);
        else if (k == "gamma") g_cfg.gamma = std::clamp(v, 0.5f, 3.0f);
        else if (k == "mouse_sens") g_cfg.mouse_sens = std::clamp(v, 0.1f, 50.0f);
        else if (k == "mouse_invert") g_cfg.mouse_invert = v != 0;
        else if (k == "aim_hold") g_cfg.aim_hold = v != 0;
        else if (k == "cheat_unlock") g_cfg.cheat_unlock = v != 0;
        else if (k == "cheat_god") g_cfg.cheat_god = v != 0;
        else if (k == "cheat_ammo") g_cfg.cheat_ammo = v != 0;
        else if (k == "cheat_damage") g_cfg.cheat_damage = std::clamp((int)v, 0, 3);
        else if (k == "autoaim") g_cfg.autoaim = v != 0;
        else if (k == "reloadblur") g_cfg.reload_blur = v != 0;
        else if (k == "fast_menus") g_cfg.fast_menus = v != 0;
        else if (k == "weapon_buffers_mb") g_cfg.buf_mb = (uint32_t)std::clamp((int)v, 0, 16);
        else if (k == "lean_angle") g_cfg.lean_angle = std::clamp(v, -45.0f, 45.0f);
        else if (k == "sprint") g_cfg.sprint = v != 0;
        else if (k == "lean_offset") g_cfg.lean_offset = std::clamp(v, -2.0f, 2.0f);
        else if (k == "fov") g_cfg.fov = std::clamp(v, 50.0f, 120.0f);
        else if (k == "skipanymovie") g_cfg.skip_movies = v != 0;
        else if (k == "reverb") g_cfg.reverb = v != 0;
        else if (k == "reverb_gain") g_cfg.reverb_gain = std::clamp(v, 0.0f, 8.0f);
        else if (k == "speakers") g_cfg.speakers = !strcmp(val, "5.1") ? 6 : !strcmp(val, "stereo") ? 2 : 0;
        else bindings::load_ini(k.c_str(), val); // key_<action> = Key[,Key]
    }
    fclose(f);
}

void settings_save()
{
    if (g_cfg.background) return;
    std::lock_guard<std::mutex> g(g_lock);
    FILE *f = nullptr;
    if (_wfopen_s(&f, ini_file().c_str(), L"w") || !f) return;
    fprintf(f, "# BLACK PC port settings (written by PC SETTINGS and on exit; command-line flags override them)\n");
    if (g_cfg.render_native) fprintf(f, "render = native\n");
    else if (g_cfg.render_h) fprintf(f, "render = %d\n", g_cfg.render_h);
    else fprintf(f, "render = scale\n");
    fprintf(f, "render_factor = %.2f\nscale = %.3f\n", g_cfg.render_factor, g_cfg.scale);
    int fps = g_cfg.fps_next ? g_cfg.fps_next : g_cfg.fps; // a menu choice for the next run wins
    bool wide = g_cfg.widescreen_next >= 0 ? g_cfg.widescreen_next != 0 : g_cfg.widescreen;
    fprintf(f, "aa = %d\nfps = %d\nwidescreen = %d\nfullscreen = %d\n", g_cfg.aa ? 1 : 0, fps, wide ? 1 : 0, g_cfg.fullscreen ? 1 : 0);
    fprintf(f, "# auto = the Windows user interface language; or: en es de fr it ja ko zh\n");
    fprintf(f, "language = %s\n", g_cfg.language.c_str());
    fprintf(f, "gain = %.2f\ngamma = %.2f\n", g_cfg.gain, g_cfg.gamma);
    fprintf(f, "mouse_sens = %.3f\nmouse_invert = %d\n", g_cfg.mouse_sens, g_cfg.mouse_invert ? 1 : 0);
    fprintf(f, "aim_hold = %d\n", g_cfg.aim_hold ? 1 : 0);
    fprintf(f, "cheat_unlock = %d\ncheat_god = %d\ncheat_ammo = %d\ncheat_damage = %d\n", g_cfg.cheat_unlock ? 1 : 0,
            g_cfg.cheat_god ? 1 : 0, g_cfg.cheat_ammo ? 1 : 0, g_cfg.cheat_damage);
    fprintf(f, "reverb = %d\nreverb_gain = %.2f\n", g_cfg.reverb ? 1 : 0, g_cfg.reverb_gain);
    fprintf(f, "speakers = %s\n", g_cfg.speakers == 6 ? "5.1" : g_cfg.speakers == 2 ? "stereo" : "auto");
    fprintf(f, "autoaim = %d\n", g_cfg.autoaim ? 1 : 0);
    fprintf(f, "reloadblur = %d\n", g_cfg.reload_blur ? 1 : 0);
    fprintf(f, "fast_menus = %d\n", g_cfg.fast_menus ? 1 : 0);
    fprintf(f, "weapon_buffers_mb = %u\n", g_cfg.buf_mb);
    fprintf(f, "lean_angle = %.1f\nlean_offset = %.3f\n", g_cfg.lean_angle, g_cfg.lean_offset);
    fprintf(f, "sprint = %d\n", g_cfg.sprint ? 1 : 0);
    fprintf(f, "fov = %.1f\n", g_cfg.fov);
    fprintf(f, "skipanymovie = %d\n", g_cfg.skip_movies ? 1 : 0);
    fprintf(f, "seamfill = %d\n", g_cfg.seam_fill ? 1 : 0);
    fprintf(f, "effectsnative = %d\n", g_cfg.effects_native ? 1 : 0);
    bindings::save_ini(f);
    fclose(f);
}
