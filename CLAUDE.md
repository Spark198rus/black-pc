# BLACK (Xbox, NTSC-U) native PC port

Read `NOTES.md` (findings, build facts, environment) and `docs/ROADMAP.md` (7-phase plan) first. This file is the short version plus rules.

## What this project is

A 32-bit Windows program that loads the user's own `game/default.xbe` and runs the original x86 code natively, with the Xbox kernel,
D3D8, DSound and XAPI replaced by PC code (HLE, like Cxbx-Reloaded). Decompiling to C++ is incremental and secondary: first get the
original code running, then replace subsystems with decompiled C++ in priority order. Decomp = functionally equivalent C++, not
byte-matching (no XDK compiler). The work was started on a Mac and is being continued on a Windows PC.

## State (2026-10-04)

Done on the Mac: disc extracted to `game/`; Phase 1 gate passed; Ghidra analysis done (7,595 functions in memory); push-buffer risk assessed.
Done on the PC: **Phase 4 gate passed**. `loader/` (CMake, Win32) builds `black.exe` + `blackrt.dll`. Build/run: `loader/README.md`; design and boot sequence: NOTES.md.
Phase 5 (evening): D3D, DSound and input are HLE (Direct3D 9, NV2A vertex programs and register combiners translated to HLSL, XAudio2 mixer).
The game runs through logos, title, menus and videos into the **first mission, which renders and plays with sound** at 30 fps.
Fast test loop: `--skip-videos --press "20s:start,50s:a,56s:a,62s:a"` reaches gameplay in ~75 s. Findings and next steps: "Phase 5 progress" in NOTES.md.
Setup for this machine: `PC_SETUP.md` (CMake is bundled with VS 18 Build Tools, not on PATH; see NOTES.md environment notes).

## Hard rules

- **Never commit, upload or share**: `game/`, `*.xbe`, `*.iso`, `work/` (decompiler output), `ghidra/` (analysed binary), `notes/ghidra-strings.txt`.
  They are in `.gitignore`; check it still is before any `git add`. No Microsoft XDK headers/libs/code in the repo.
- Do not run `git commit`/`push` or `git init` without the user asking. Publishing is the user's call.
- The user supplies the dump; the loader reads `default.xbe` and assets from a path at runtime. Nothing game-derived is embedded in the loader.
- Cxbx-Reloaded and xemu are GPL: read them as reference; do not paste their code into this project without the user deciding the license first.
- Everything under `tools/` third-party clones and `tools/bin/` is local only (also git-ignored). macOS binaries in `tools/bin/` do not run on Windows.

## Known facts that shape the design (details and addresses in NOTES.md)

- Image base `0x10000`, image size `0x577a40` (occupies `0x10000-0x587a40`), entry `0x25a3f`, 115 kernel imports (`notes/phase1-xbe-report.txt`).
- Reserve the XBE's address range **at link time** if possible (a linker-reserved section at that base), not only with `VirtualAlloc` at startup:
  Windows may already have put stacks/heaps low. Also plan for Xbox contiguous-memory/physical aliases (`0x80000000`, `0xF0000000`);
  the game imports `MmAllocateContiguousMemoryEx`, `MmClaimGpuInstanceMemory`, `MmGetPhysicalAddress`. Cxbx-Reloaded's approach is the reference.
- D3D is statically linked, LTCG-compiled (`D3D8LTCG`), and talks to hardware, so it is replaced **per function** (hook the entry address with a jump).
  Many LTCG functions take args in registers (XbSymbolDatabase names encode them: `_4__LTCG_eax1` etc.), so hooks need naked asm thunks.
- The game calls 70 distinct D3D functions (`notes/ghidra-summary.txt`); 9 of them are unnamed (listed in NOTES.md).
- The game **writes into the D3D library's data** (render-state shadow array `0x27c1xx-0x27c2xx`, dirty flags `0x27c000`): keep that memory in place, read it at draw time.
- Only 3 callers use raw push buffers (`0x2bac0`, `0x37e90`, `0x3e1b0`), always `NV097_SET_BEGIN_END` (`0x17fc`) + `NV097_INLINE_ARRAY` (`0x1818`).
  A mini interpreter for those 2 methods is enough.
- Audio needs more than DSound: `WMADEC` (WMA decoder) and `DOLBY` sections are linked in, videos are `.xmv`. None of those have symbols.
- Ghidra's decompiler output is analysis input, not compilable source.

## Tools on this side

`python tools/xbeinfo.py game/default.xbe` prints the XBE structure. Ghidra project: import `ghidra/default.xbe.gzf` (File > Import File);
it already contains all analysis and labels. `notes/functions.csv` lists every function (address, name, size, section, refs).
`ghidra_scripts/*.java` are headless-capable report/decompile scripts (`BlackSummary`, `DumpDecomp`, `ExportForPc`).

## Working style

Gate each phase with a concrete check and say plainly when a gate is not met. Log every unimplemented kernel/D3D call on first use rather than guessing.
Keep `NOTES.md` updated with findings (addresses, structures) so the next session does not rediscover them.
