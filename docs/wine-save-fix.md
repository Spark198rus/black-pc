# Saving under Wine and Proton

The port's HLE `NtQueryDirectoryFile` used to create an event and pass it to the host call. Wine and Proton
answer a directory query that carries an event with `STATUS_NOT_IMPLEMENTED` (`0xC0000002`), so XAPI's
`FindFirstFile` failed with an error the game's save code has no case for. The memory card state machine then
stayed in state 11, and confirming a save dereferenced a null pointer:

```
first-chance ACCESS_VIOLATION (0xc0000005) at 000c49fe  (write of address 00000000)
```

Windows is unaffected; only Wine-based runtimes (Proton, Wine, Steam Deck) hit it. Upstream issue:
<https://github.com/Spark198rus/black-pc/issues/6>.

## The fix

Do not give the host call an event. The host file handles are synchronous, so the query completes without one; if
a call ever pends, wait on the file handle.

Source change (already applied in `loader/src/kernel/k_file.cpp`; a local copy of the diff sits in
`patches/wine-ntquerydirectoryfile.patch`, which is not tracked by git):

```cpp
- NTSTATUS s = host::NtQueryDirectoryFile(FileHandle, ev, nullptr, nullptr, &hio, ...);
+ NTSTATUS s = host::NtQueryDirectoryFile(FileHandle, nullptr, nullptr, nullptr, &hio, ...);
  if (s == XSTATUS_PENDING) {
-     WaitForSingleObject(ev, INFINITE);
+     WaitForSingleObject(FileHandle, INFINITE);
```

A build from the current source needs no extra step.

## Patching a release build

The released `blackrt.dll` (0.2.0) still has the bug, and building the loader needs MSVC on Windows. For a local
install under Proton, patch the shipped DLL instead:

```bash
python3 tools/patch_blackrt.py          # patch blackrt.dll next to the repo root
python3 tools/patch_blackrt.py --check  # report the state only
python3 tools/patch_blackrt.py --revert # undo
```

The script checks the file's SHA-256, finds the host `NtQueryDirectoryFile` call by its surrounding instructions
and changes the argument push from `push edi` (the event handle) to `push ecx` (which holds 0 there), so the host
call receives no event. The original file is kept as `blackrt.dll.bak`.

| file | SHA-256 |
| --- | --- |
| `blackrt.dll` 0.2.0 | `fcda603bc75d5cfb51237acdd15c8b73acfa8a46c36686ca393ce61fb0b93627` |
| patched | `2b2cb28bdbd66642ef3d059a01c27518f3520d02b512aa4f87d13916deb23732` |

## Checking the result

Run the game under Proton, finish a mission and confirm the save. The log should show the directory query
without a crash, and the profile lands in the port's `savedata` folder:

```
savedata/Partition1/UDATA/<title id>/<profile id>/Profile
savedata/Partition1/UDATA/<title id>/<profile id>/SaveMeta.xbx
```
