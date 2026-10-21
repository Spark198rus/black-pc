# BLACK loader

Runs the original `default.xbe` natively as a 32-bit Windows process with an HLE Xbox kernel, D3D (on
Direct3D 9), DirectSound (XAudio2) and input (XInput, keyboard and mouse). The game is playable.

## Build

Needs Visual Studio 2026 (or 2022) Build Tools with the C++ x86 toolchain. CMake ships with VS
(`...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`) if it is not on PATH.

```
cmake --preset win32                 # or win32-vs2022
cmake --build build/win32 --config RelWithDebInfo
```

Output: `build/win32/RelWithDebInfo/black.exe` + `blackrt.dll` (keep them together).

## Run (from the project root)

```
loader\build\win32\RelWithDebInfo\black.exe --info game\default.xbe     # print XBE structure
loader\build\win32\RelWithDebInfo\black.exe --timeout 30 game\default.xbe
```

Options: `--hdd <dir>` (host folder for HDD partitions, default `savedata` next to black.exe), `--log <file>` (default
`black.log` next to black.exe),
`--verbose` (the whole log on the console too), `--trace` (log every kernel call), `--strict` (stop at the first unimplemented kernel call), `--multicore`,
`--symbols <functions.csv>` (default `notes\functions.csv`), `--force` (other, unsupported XBE builds),
`--skip-videos` (dev: 9-second logo video instead of the long ones). `black.exe --help` lists the debugging
options (`--timeout`, `--screenshot`, `--shot-every`, `--shot-start`, `--debug-frame`, `--press`, `--watch`,
`--watch-pad`, `--probe`, `--background`, `--wav`).

Display: widescreen 16:9 by default (the console reports a widescreen TV and BLACK switches to its own 16:9
camera and UI layouts); `--normal-aspect` for 4:3. `--scale K` renders at K x the Xbox resolution (any
factor, default 3 = 2560x1440 in widescreen, 1920x1440 in 4:3); `--resolution 1920x1080` (or just `1080`, or
`native` = the window's size, and then it follows the window when it is resized) sets the render size directly. `--window WxH` sets the window size (default: the
largest that fits the screen), `--fullscreen` starts borderless fullscreen. `--gain K` / `--gamma G` adjust the
output after the game's own gamma ramp (default 1.0 / 1.0; `--no-ramp` shows the raw framebuffer). `--mouse-sens K` (0.022 degrees x K per count, default 2.0) and
`--mouse-invert` tune mouse look; auto-aim (the game's view pull toward enemies) is off by default, `--autoaim` turns it on. A / START skip every movie, also ones the game only lets you skip once earned; `--no-movie-skip` (or unticking Game > Skip any movie) keeps the original rule. `--fov D` sets the horizontal field of view in play (50-120 degrees, default 70 = the game's; Video > Field of view, live; 86 in widescreen = the 4:3 picture's height). `--fps 30|60` (default 60; 30 is the original). `--reverb-gain K`
(default 1.0) / `--no-reverb` for the room reverb. `--no-aa` turns off the anti-aliasing, `--no-seamfill` the
filling of hairline cracks between geometry pieces (Video > Fill geometry seams).

`black.exe` is a windowed program: no console window opens. Started from a command prompt it prints a short summary
there (startup, setup errors, exit, crashes); the full log is in `black.log`.

The port keeps its own files next to black.exe (the release's `port` folder): `savedata\` (the Xbox hard disk),
`black.ini`, `black.log`, `cache\menus`, `perf\`. If the game is not found in the current folder, black.exe looks
upwards from its own folder for `game\default.xbe` and makes that folder current (so it can be started from anywhere);
relative paths given on the command line resolve against it.

The port's settings are in the game's menus (Options > PC SETTINGS, and PC SETTINGS in the pause menu; `src/fe/`):
render resolution, anti-aliasing, field of view, brightness, gamma, fullscreen, aspect ratio and frame rate (main menu
only, used from the next start), seam filling, effects at Xbox size, reverb, auto-aim, movie skipping, mouse and key
bindings. Choices are saved to `black.ini`; command-line flags override it for that run. The port has no keys of its
own and the window title is just BLACK. `--perf-at S:D` records performance for D seconds from S seconds after the
first frame: frame times, where the CPU time goes, threads, saved in `perf\<date_time>\` (summary.txt, frames.csv,
threads.csv). On a ~60 Hz display the frame clock follows the
display's refresh (smooth frame delivery); `--timer-vblank` uses a fixed 59.94 Hz timer instead. The game threads run on
one CPU like the Xbox (the last physical core, its hyperthread sibling kept free); `--game-cpu N` picks another. On weak
PCs (old laptops with Intel graphics) play at 30 fps (Video > Frame rate), the game's original rate.

Controls: every PC controller drives the Xbox pad on port 0. Keys and mouse buttons are bound to the game's actions
(`src/input/bindings.cpp`; KEY BINDINGS page, `key_<action> = Key[,Key]` in black.ini): each poll a held key looks
up its action in the game's live binding table and presses the pad input the game reads for it, so the keys follow
the game's controller layout. Defaults: WASD move, left / right mouse button = fire / aim, R reload, E action, C crouch,
G grenade, F melee, wheel down / up = next / previous weapon, Z firing mode, Q suppressor, X health pack, Space
objectives, Esc pause (always; Esc also lets go of the mouse, in menus too, and play starting or resuming takes it
again). In menus, pauses and cutscenes (no player update) bound keys do nothing; arrows, Enter and Backspace go to the
menus' own input directly (`src/fe/calls.cpp`), not through the pad, and Enter at PRESS START makes port 0 the
player's controller. Mouse = look; click the window to capture it, Alt+Tab releases it. Tests: `--press "95s:k:W*1500"` holds a bound key (black.ini spelling) without focus.

## Local launcher (icon + shortcut)

The icon comes from the user's XBE, so it is stamped into a local copy only (`work\` is git-ignored;
nothing game-derived goes into the repo or the build):

```
copy loader\build\win32\RelWithDebInfo\black.exe work\run\ & copy loader\build\win32\RelWithDebInfo\blackrt.dll work\run\
python tools\xbe_images.py game\default.xbe work\icons
python tools\set_exe_icon.py work\run\black.exe work\icons\title_image.ico
powershell -File tools\make_shortcut.ps1 -Link BLACK.lnk -Target work\run\black.exe -Arguments "--hdd work\hdd --log work\play.log"
```

`BLACK.lnk` stores the target relative to itself as well (found again after the folder moves or the drive
letter changes); its "Start in" is empty because black.exe finds the project folder itself.

## Layout

| File | What |
|---|---|
| `stub/stub.c` | `black.exe`: no CRT, linked at `0x10000` with a large BSS so its image owns the XBE range |
| `src/main.cpp` | `BlackMain`: arguments, build check (SHA-256), startup order |
| `src/image.cpp` | maps the XBE over the stub image, splices a PE header in for Windows, `XeLoadSection` |
| `src/xthread.cpp` | per-thread Xbox state; rewrites `fs:[0x04/0x20/0x24/0x28/0x58]` to TLS slots; IRQL lock |
| `src/hooks.cpp` | library hooks (tables in `*_functions.inc`), crash reports, `--watch`/`--probe` |
| `src/kernel/` | HLE kernel: `ktable.cpp` (ordinal table + logging stub), memory, threads, sync, DPC/timers, files, Rtl, misc |
| `src/d3d/` | D3D HLE on Direct3D 9: device/state, resources, textures, draws, push buffer, NV2A vertex programs and register combiners to HLSL |
| `src/audio/` | DirectSound HLE: software mixer (`dsound.cpp`), room reverb (`reverb.cpp`) and XAudio2 output (`output.cpp`) |
| `src/input/` | XInput HLE: PC controllers, keyboard and mouse; `mouselook.cpp` feeds the mouse into the game's view angles |
| `src/*_functions.inc` | generated by `tools/gen_section_table.py` from `notes/functions.csv` (addresses and names only) |

Design notes and findings are in `../NOTES.md`.
