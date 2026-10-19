#!/usr/bin/env python3
"""Wrap an XBE's mapped image in a minimal PE32 file so MSVC `dumpbin /disasm` can read it.

The output is derived from the game binary: write it under work/ (git-ignored), never commit it.
Usage: xbe2pe.py game/default.xbe work/default.pe.exe
Then:  dumpbin /disasm /range:0x11000,0x282fe0 work/default.pe.exe > work/code.asm
Addresses in the listing are the XBE's own virtual addresses (image base kept). The range also covers data
embedded in code sections, which disassembles as garbage (e.g. fs:[eax] hits inside jump tables).
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbeinfo import Xbe  # noqa: E402

PAGE = 0x1000


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    x = Xbe(data)
    size = (x.size_of_image + PAGE - 1) & ~(PAGE - 1)
    img = bytearray(size)
    img[:x.size_of_headers] = data[:x.size_of_headers]
    for s in x.sections:
        off = s["vaddr"] - x.base
        raw = data[s["raw_addr"]:s["raw_addr"] + s["raw_size"]]
        img[off:off + len(raw)] = raw
    body = bytes(img[PAGE:])
    hdr = bytearray(PAGE)
    struct.pack_into("<2s58xI", hdr, 0, b"MZ", 0x40)
    pe = 0x40
    struct.pack_into("<4sHHIIIHH", hdr, pe, b"PE\0\0", 0x14C, 1, x.timedate, 0, 0, 0xE0, 0x0103)
    opt = pe + 24
    struct.pack_into("<HBBIIIIIIIIIHHHHHHIIIIHHIIIIII", hdr, opt,
                     0x10B, 14, 0, len(body), 0, 0, x.entry - x.base, PAGE, PAGE,
                     x.base, PAGE, PAGE, 4, 0, 0, 0, 4, 0, 0, size, PAGE, 0, 3, 0,
                     0x100000, 0x1000, 0x100000, 0x1000, 0, 16)
    sect = opt + 0xE0
    struct.pack_into("<8sIIIIIIHHI", hdr, sect, b".image\0\0", len(body), PAGE, len(body), PAGE,
                     0, 0, 0, 0, 0xE0000020)
    with open(sys.argv[2], "wb") as f:
        f.write(hdr)
        f.write(body)
    print(f"wrote {sys.argv[2]}: base {x.base:#x}, size {size:#x}")


if __name__ == "__main__":
    main()
