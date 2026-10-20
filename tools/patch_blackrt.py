#!/usr/bin/env python3
"""Patch the release blackrt.dll so saving works under Wine/Proton.

The loader's HLE NtQueryDirectoryFile always creates an event and passes it to the
host call. Wine and Proton answer a call with a non-null event with
STATUS_NOT_IMPLEMENTED (0xC0000002), so the game's save/profile directory
enumeration fails; later, confirming a save dereferences a null pointer and the
game crashes with an access violation at 0x000C49FE.

The fix is to pass no event to the host call. The host file handles are
synchronous, so the query completes without one; the source fix
(loader/src/kernel/k_file.cpp) additionally waits on the file handle if the call
ever pends.

The release DLL is machine code, so the change is one byte: the argument push in
front of the host call, "push edi" (the event handle) becomes "push ecx" (which
holds 0 at that point). This script finds that instruction by its surroundings
instead of trusting a fixed offset, and checks the file's SHA-256 first.

Usage:
    python3 tools/patch_blackrt.py            # patch blackrt.dll next to the repo root
    python3 tools/patch_blackrt.py --check    # only report the state
    python3 tools/patch_blackrt.py --revert   # undo the patch
    python3 tools/patch_blackrt.py --dll path/to/blackrt.dll
"""

import argparse
import hashlib
import shutil
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DLL = REPO_ROOT / "blackrt.dll"

# Release 0.2.0 (2026-10-14) and the same file after the patch.
SHA_ORIGINAL = "fcda603bc75d5cfb51237acdd15c8b73acfa8a46c36686ca393ce61fb0b93627"
SHA_PATCHED = "2b2cb28bdbd66642ef3d059a01c27518f3520d02b512aa4f87d13916deb23732"

# "push edi; push [ebp-0x4d8]; call [NtQueryDirectoryFile]" and its patched form.
PATCHED_PATTERN = bytes.fromhex("51ffb528fbffffff1594f52f10")
ORIGINAL_PATTERN = bytes.fromhex("57ffb528fbffffff1594f52f10")

# The byte that carries the event argument.
ARG_OFFSET = 0
ARG_ORIGINAL = 0x57
ARG_PATCHED = 0x51


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def find_pattern(data: bytes, pattern: bytes) -> int:
    hits = []
    start = 0
    while True:
        i = data.find(pattern, start)
        if i < 0:
            break
        hits.append(i)
        start = i + 1
    if not hits:
        return -1
    if len(hits) > 1:
        raise SystemExit(f"pattern found {len(hits)} times ({[hex(h) for h in hits]}); refusing to patch")
    return hits[0]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dll", type=Path, default=DEFAULT_DLL, help="blackrt.dll to patch (default: %(default)s)")
    ap.add_argument("--check", action="store_true", help="only report whether the DLL is patched")
    ap.add_argument("--revert", action="store_true", help="undo the patch")
    args = ap.parse_args()

    dll: Path = args.dll.expanduser().resolve()
    if not dll.is_file():
        print(f"error: {dll} not found")
        return 1

    data = bytearray(dll.read_bytes())
    digest = sha256(bytes(data))

    if digest == SHA_PATCHED:
        state = "patched"
    elif digest == SHA_ORIGINAL:
        state = "original"
    else:
        state = "unknown build"

    print(f"{dll}")
    print(f"  sha256 : {digest} ({state})")
    if state == "unknown build":
        print("  warning: this is not the release 0.2.0 blackrt.dll; the pattern is still checked below")

    if state == "patched":
        off = find_pattern(data, PATCHED_PATTERN)
    else:
        off = find_pattern(data, ORIGINAL_PATTERN)
    if off < 0:
        print("  error: could not find the host NtQueryDirectoryFile call; nothing changed")
        return 1

    arg = off + ARG_OFFSET
    print(f"  patch  : file offset 0x{arg:X} (byte 0x{data[arg]:02X} -> "
          f"0x{ARG_PATCHED if not args.revert else ARG_ORIGINAL:02X})")
    print(f"  context: {bytes(data[arg:arg + 13]).hex(' ')}")

    if args.check:
        return 0
    if state == "patched" and not args.revert:
        print("  already patched; nothing to do")
        return 0
    if state == "original" and args.revert:
        print("  not patched; nothing to do")
        return 0

    backup = dll.with_suffix(dll.suffix + ".bak")
    if not backup.exists():
        shutil.copy2(dll, backup)
        print(f"  backup : {backup}")

    data[arg] = ARG_ORIGINAL if args.revert else ARG_PATCHED
    dll.write_bytes(bytes(data))
    print(f"  result : {sha256(bytes(data))} (written)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
