# Windows PC setup

Developer setup for building, running and analysing the port on Windows (players only need `README.md`). Written as
the handoff from the Mac, where the disc was extracted and analysed, to the PC.

## 0. First session prompt (paste into Claude Code on the PC, with this folder open)

> Read CLAUDE.md, NOTES.md and docs/ROADMAP.md. Run the checks in PC_SETUP.md section 2. Then start Phase 4: create the CMake
> loader in `loader/` and work through the "First milestone" checklist at the end of the roadmap. Do not commit or git init until I say so.

## 1. Install (Windows 10/11 x64, a GPU with current drivers)

PowerShell. The winget IDs are from memory: if one fails, `winget search <name>` and use the closest match.

```powershell
winget install Git.Git
winget install Kitware.CMake
winget install Python.Python.3.12
winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install EclipseAdoptium.Temurin.21.JDK
winget install x64dbg.x64dbg
winget install BaldurKarlsson.RenderDoc
```

- Ghidra (only to browse the analysis): download the release zip from https://github.com/NationalSecurityAgency/ghidra/releases (12.1.4 or newer), unzip, run `ghidraRun.bat`.
  Import `ghidra/default.xbe.gzf` with File > Import File. It already holds every label and the symbol matches; no extension needed just to view it.
- xemu (Phase 2, reference): https://xemu.app. It needs the MCPX boot ROM, BIOS and HDD image from your own console; keep them out of the repo.
- Build target is **32-bit**: use the "x86" / "Win32" toolchain (`cmake -A Win32`, or the "x86 Native Tools" prompt).

## 2. Checks before writing code

Run from the project root (the drive may be mounted as any letter, e.g. `E:\black-pc`).

```powershell
cmake --version; git --version; python --version; java -version
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
python tools\xbeinfo.py game\default.xbe | Select-Object -First 12
Get-FileHash game\default.xbe -Algorithm SHA256
```

Expected: `xbeinfo.py` prints title `Black`, id `45410083`, region NA, entry `0x00025a3f`, 115 kernel imports.
SHA-256 of `game\default.xbe` must be `df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6`.
Full manifest of all 574 game files: `notes\game-manifest.sha256` (`shasum` format, paths with `/`). Hash of `ghidra\default.xbe.gzf`: `notes\handoff-checksums.txt`.

## 3. Known snags

- The drive is **exFAT**. Windows reads it fine. Git works on it but can be slow; if it misbehaves, clone/copy the repo (not `game/`) to an NTFS disk and keep `game/` here, passing its path to the loader.
- macOS left `._*` junk files in places. If any show up, delete them: `Get-ChildItem -Recurse -Force -Filter "._*" | Remove-Item -Force`. They also break CMake globbing.
- `tools/bin/` holds macOS-built binaries (`extract-xiso`, `XbSymbolDatabaseCLI`); ignore them on Windows. Nothing in the game needs re-extracting: `game/` is complete.
- Running 32-bit Windows code is fine on 64-bit Windows. Do not use Windows-on-ARM or a VM without GPU passthrough for this project.
