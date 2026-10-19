# BLACK PC port

An unofficial native Windows port of **BLACK** (Criterion Games, 2006), made from the original Xbox version (NTSC-U).

`black.exe` loads your own copy of the game's `default.xbe` and runs the original x86 game code directly on the
PC.

**You need your own copy of the game.** This repository and the release downloads contain no game code or data.

## Status

The game is fully playable. Please report problems in the issue tracker and include
`port\black.log`.

- Any render resolution (720p to 4K and beyond, or a multiple of the window size), anti-aliasing
- Widescreen 16:9 (the game's own widescreen mode) or 4:3, adjustable field of view
- 60 fps (the default) or the original 30 fps
- Mouse look, with the weapon swaying as it does with the stick; keys bound to the game's actions; XInput
  controllers
- The port's settings in the game's own menus (Options > PC SETTINGS, and in the pause menu), including borderless
  fullscreen
- Room reverb, the game's own brightness and gamma ramp; auto-aim off by default; every movie can be skipped
- Fixes for things the Xbox hides by its low resolution: flickering seams between pieces of destructible objects,
  a grid pattern in the blur effects

## Requirements

- Windows 10 or 11. The program is 32-bit and runs on 64-bit Windows.
- A graphics card with Direct3D 9 (Shader Model 3). On old laptops with Intel graphics (HD 4400 class) play at 30 fps.
- The NTSC-U (North America) Xbox disc of BLACK. Other regions and builds have different code addresses and are not
  supported.

Nothing else needs installing: the C runtime is built in, and Direct3D 9, the shader compiler and XAudio2 ship with
Windows.

## Setting up

1. Dump your NTSC-U version of the game to an ISO and extract it with [extract-xiso](https://github.com/XboxDev/extract-xiso)
   (`extract-xiso -x Black.iso`). Rename the extracted folder to `game`.
2. Check that you have the supported build. In PowerShell:

   ```powershell
   Get-FileHash game\default.xbe -Algorithm SHA256
   ```

   It must print `DF2739C372D254A90AEECCF5971D12097F22D89FBB90DB021FDA96DD4DEB68F6`.
3. Download the zip from [Releases](../../releases) and extract it next to the `game` folder, so the `port` folder
   sits beside it:

   ```
   BLACK\
     game\
       default.xbe
       ...
     port\
       black.exe
       blackrt.dll
   ```

4. Start `port\black.exe`. 

The port keeps everything it writes in the `port` folder: `savedata\` holds the Xbox hard disk (saved games and the
game's settings), `black.ini` the port's settings, `black.log` the log of the last run and `cache\menus` the game's menu
files with the PC SETTINGS pages added.

## Controls

Keys and mouse buttons are bound to the game's own actions, not to controller buttons, and can be changed in
PC SETTINGS > CONTROLS > KEY BINDINGS (two keys per action, saved to `black.ini`). Controllers work as well. Click
the window to capture the mouse; Esc lets go of it (in play it also pauses), and play starting or resuming takes it
again.

| Action | Default |
|---|---|
| Move | W A S D |
| Look | mouse |
| Fire / Aim | left / right mouse button |
| Reload | R |
| Action (use, pick up) | E |
| Crouch | C |
| Grenade | G |
| Melee | F |
| Next / previous weapon | mouse wheel down / up |
| Change firing mode | Z |
| Toggle suppressor | Q |
| Health pack | X |
| Show objectives | Space |
| Pause | Esc |

In menus the arrow keys move (held, they repeat), Enter selects and Backspace goes back; Enter also passes PRESS START,
skips movies and a mission's intro text, and closes the game's hints (PRESS A TO CONTINUE). QUIT at the bottom of the
main menu closes the game. On the KEY BINDINGS page left / right choose the first or second key and Enter waits for the new one
(Esc cancels, Delete clears it). Any key can be bound, F1-F12 included: the port has no keys of its own.

## Settings

The port's settings are in the game's menus: Options > PC SETTINGS on the main menu, and PC SETTINGS in the pause
menu (below OPTIONS).

| Page | Settings |
|---|---|
| VIDEO | render resolution, anti-aliasing, field of view, brightness, gamma, fullscreen |
| DISPLAY | aspect ratio, frame rate (main menu only: used from the next start), filling geometry seams, effects at Xbox size |
| AUDIO AND GAME | room reverb, auto-aim, skipping any movie |
| CONTROLS | mouse sensitivity, mouse inversion, key bindings |

Left / right change a setting; field of view, brightness, gamma and mouse sensitivity are sliders. Changes apply at
once and are saved to `black.ini`. `--no-pc-menus` leaves the game's menus as they are.

`black.exe --help` lists the command-line options. They override `black.ini` for that run, for example
`--fullscreen`, `--resolution 1080`, `--fps 30`, `--fov 86`, `--normal-aspect`, `--autoaim`, `--mouse-sens 2.5`.

Field of view (PC SETTINGS > VIDEO, `--fov`) is horizontal, in degrees; the game's is 70. The widescreen picture
keeps that width and shows less height than 4:3; 86 shows the same height as the 4:3 picture. Aiming zooms by the
same factor as before, cutscenes keep their own framing, and the gun and hands keep the game's own view (they were
made for 70 and their animations would show their ends at a wider view).

The game lets you skip a movie only once you have earned it (the intro and a mission's movies after completing that
mission, the logos never). The port lets A, START or Enter skip every movie; set PC SETTINGS > AUDIO AND GAME > SKIP ANY
MOVIE to off (or start with `--no-movie-skip`) for the original rule.

## Known issues

- The game was made for 30 fps; at 60 (the default) something may still run at the wrong speed (PC SETTINGS > DISPLAY >
  FRAME RATE), but I didn't notice.
- The game's on-screen prompts show Xbox controller buttons, also when you play with the keyboard.
- Audio: the reverb is an approximation of the Xbox's, some effect sends are not mixed, there is no subwoofer
  channel, and 3D sound uses distance and panning only.
- **Only the NTSC-U build is supported** (`--force` tries another build, which will most likely fail).

## Building from source

Visual Studio 2022 or 2026 (Build Tools are enough) with the C++ x86 tools, and CMake 3.24 or newer (it ships with
Visual Studio).

```
cd loader
cmake --preset win32            # Visual Studio 2026; use win32-vs2022 for Visual Studio 2022
cmake --build build/win32 --config RelWithDebInfo
```

This builds `build\win32\RelWithDebInfo\black.exe` and `blackrt.dll`. `loader/README.md` has the developer
options and the source layout.

## How it works

`black.exe` is a small stub linked at the XBE's base address, so the game's address range belongs to it before
Windows can put anything there. `blackrt.dll` maps `default.xbe` into that range, provides the 115 Xbox kernel
functions the game imports, and redirects the Xbox libraries that are linked into the game (Direct3D 8,
DirectSound, XAPI) to PC code by patching their entry points. Graphics run on Direct3D 9: the NV2A vertex programs
and register combiners are translated to HLSL. Audio is a software DirectSound mixer on XAudio2.

The game's code is never changed on disk; the few patches (60 fps timing, mouse look, auto-aim switch) are applied
in memory at start. Design notes and findings (addresses, structures, the boot sequence) are in `NOTES.md`; the
original plan is `docs/ROADMAP.md`. `notes/` holds analysis results used to build the address tables: function
addresses and symbol names, the XBE header report and checksums of the disc's files. It contains no game code.

## Legal

This project is not affiliated with or endorsed by Criterion Games or Electronic Arts. BLACK is a trademark of
Electronic Arts Inc.; Xbox is a trademark of Microsoft Corporation. No code or data from the game and no code from
the Microsoft Xbox Development Kit is included. You need your own legally obtained copy of the game.

The port is free software under the GNU General Public License, version 2 or (at your option) any later version.
See `LICENSE`.

## Thanks

[Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded) and [xemu](https://xemu.app) served as
references for how the Xbox kernel and the NV2A behave (no code was copied).
[XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase) identified the Xbox library functions in the
game, [Ghidra](https://ghidra-sre.org) with [ghidra-xbe](https://github.com/XboxDev/ghidra-xbe) was used for the
analysis, and the [nxdk](https://github.com/XboxDev/nxdk) headers document the kernel's types.

Developed by Spark198rus with Claude Code.
