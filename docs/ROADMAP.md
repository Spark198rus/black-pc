<!--
Copy of the project's planning doc "BLACK Xbox to PC Port Roadmap", taken 2026-10-04.
Findings made after this was written are in ../NOTES.md ("Findings that change the plan"), not edited in here.
-->

# BLACK Xbox to PC Port Roadmap

Oct 4, 2026

## Goal: a native Windows build of BLACK from the original Xbox version

The target is a 32-bit Windows program that loads the original Xbox `default.xbe` from a user-supplied dump and runs it natively, with the Xbox kernel and SDK libraries replaced by PC code. No existing public BLACK port project was found as of this date, so this starts from scratch.

Why Xbox and not PS2:

|  | Original Xbox | PlayStation 2 |
| --- | --- | --- |
| CPU | Pentium III, x86: game code runs on a PC CPU as-is | MIPS R5900 + VU0/VU1 microcode: every instruction must be translated |
| GPU | NVIDIA NV2A, Direct3D 8-style API | Graphics Synthesizer, nothing like a PC GPU |
| Tooling | Ghidra XBE loader, XbSymbolDatabase, xemu, Cxbx-Reloaded as reference | [PS2Recomp](https://github.com/ran-j/ps2recomp), experimental, no playable games yet |
| Main work | Replace kernel + D3D8/DSound/XAPI libraries | Recompile CPU code, then write a GS/VU runtime |

The Xbox route skips CPU translation entirely. The bulk of the work is the graphics layer.

## Roadmap: seven phases, each with an exit gate

Each phase ends with a concrete check. Do not start the next phase until the gate passes.

1. **Dump and extract**
   - Dump your own BLACK disc to an XISO.
   - Extract `default.xbe` and the asset tree with `extract-xiso`.
   - Note the exact region and build (title ID, XBE timestamp); addresses are build-specific.
   - Gate: `default.xbe` parses and the section list, entry point and kernel import table can be printed.
2. **Observe in xemu**
   - Play through the menu and the first level in xemu.
   - Use xemu's GDB stub to break on interesting addresses and log kernel calls.
   - Gate: you know which kernel functions are called at boot and roughly in what order.
3. **Static analysis in Ghidra**
   - Load the XBE with an Xbox loader plugin.
   - Apply XbSymbolDatabase signatures to label statically linked SDK code (D3D8, DSound, XAPI, XGraphics).
   - Separate game code from SDK code; start naming game subsystems (main loop, renderer, file loader).
   - Gate: every SDK function the game calls is labelled, and the list of D3D functions used is written down.
4. **Loader and kernel (HLE)**
   - 32-bit C++ Windows executable, linked at a high base address so the XBE's own range is free.
   - Map XBE sections at their original virtual addresses; zero-fill BSS; set up TLS.
   - Resolve kernel imports (xboxkrnl.exe ordinals) to your own implementations: memory, threads, events, timers, file I/O, `D:\` and `T:\` path mapping.
   - Start with every import as a logging stub that reports its first call; implement on demand.
   - Gate: the game reaches its first D3D call without crashing.
5. **Graphics**
   - Hook the labelled XDK D3D8 functions and forward them to PC Direct3D 8, then use d3d8to9 or DXVK to reach a modern API.
   - Handle the Xbox-only parts: NV2A vertex shader format, pixel shaders and register combiners, swizzled textures, push buffers, tiling, `D3DFMT` differences.
   - Gate 1: the main menu renders, even with wrong colours. Gate 2: the first level renders and is playable.
6. **Audio, input, saves**
   - Input: map XAPI `XInputGetState` to PC XInput (nearly identical API).
   - Audio: replace Xbox DirectSound (and its hardware effects) with XAudio2 or a mixer like miniaudio.
   - Saves: redirect the save partition to a folder.
   - Gate: full campaign mission playable start to finish with sound and saving.
7. **Enhancements and cleanup**
   - Widescreen, higher resolution, unlocked frame rate (check game logic is not tied to 30 FPS), mouse and keyboard.
   - Over time, decompile hot or buggy functions into C++ and replace them.
   - Gate: the game is completable on PC with the enhancement options working.

## Tools and references

All free. Names are given so they can be searched; check each project's current repo before relying on it.

| Tool | Used in phase | What for |
| --- | --- | --- |
| extract-xiso | 1 | Unpack the disc image |
| xemu | 2 | Reference emulator with a GDB stub |
| Ghidra + an XBE loader plugin | 3 | Disassembly and decompilation |
| XbSymbolDatabase | 3, 4, 5 | Signatures for statically linked XDK functions |
| Cxbx-Reloaded (source) | 4, 5 | Reference for HLE kernel and D3D8 patching on Xbox |
| xemu (source) | 5 | Reference for NV2A behaviour: textures, shaders, combiners |
| Visual Studio or clang-cl, CMake | 4+ | 32-bit Windows build |
| d3d8to9, DXVK | 5 | Translate D3D8 to modern APIs |
| RenderDoc | 5 | Inspect frames once rendering starts |
| x64dbg | 4+ | Debug the running loader |

Background reading: the N64Recomp, XenonRecomp and ReXGlue projects show how others structure a recompiled port and its runtime, even though their CPU translation step is not needed here.

## Risks, skills and legal rules

The biggest risk is the graphics layer; budget most of the project's time for phase 5.

**Risks**

- The game may write NV2A push buffers or registers directly instead of going through D3D, which needs low-level GPU emulation for those paths.
- Custom vertex shaders and register-combiner setups must be translated to PC shaders one by one.
- Code that assumes 64 MB RAM, physical addresses, or contiguous memory can break when mapped into a Windows process.
- Game logic tied to a fixed frame rate makes unlocked FPS harder.
- Scope: comparable ports are usually multi-month team efforts. A working main menu is a realistic first big win.

**Skills to build**

- C and C++ (the loader and runtime), x86 assembly reading, Ghidra.
- Win32 memory and threading APIs, Direct3D 8/9 basics, shader models.
- C# helps for side tools (XBE parser, asset viewers) but not for the runtime itself.

**Legal rules**

- Publish only your own code.
- Never commit or distribute the XBE, game assets, or Microsoft XDK code.
- Users supply their own dump; the loader reads it at runtime.

## First milestone: XBE loads and reaches its first D3D call

Hand this checklist to Claude Code as the first task. It covers phases 1 and 4 at their smallest useful size.

- [ ] Create the repo: CMake, 32-bit (Win32) target, `.gitignore` that excludes `*.xbe`, `*.iso` and any `game/` folder
- [ ] Write an XBE header parser: image header, certificate, section headers, TLS directory, kernel import thunk table (with the XOR-encoded entry point and thunk address decoded)
- [ ] Add a CLI that prints sections, entry point, title ID and the list of imported kernel ordinals for a given `default.xbe`
- [ ] Set the loader's own image base high enough that the XBE's address range is free; reserve that range with `VirtualAlloc` at startup
- [ ] Map each section to its virtual address, zero the uninitialized tail, apply protection flags
- [ ] Generate a stub for every kernel ordinal the game imports: log name and arguments on first call, then return a safe default
- [ ] Implement the first real kernel calls the log shows (likely memory allocation, threads, events, `NtCreateFile` path mapping)
- [ ] Jump to the entry point on a dedicated thread; catch crashes with a vectored exception handler that logs the faulting address
- [ ] Stop condition: the log shows the first call into the game's D3D8 code (address found in Ghidra, phase 3)
