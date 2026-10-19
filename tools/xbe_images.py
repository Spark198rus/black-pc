#!/usr/bin/env python3
"""Extract the title and save icons embedded in an Xbox executable as PNG (and the title image as .ico).

The XBE carries them as inserted-file sections: $$XTIMAGE (title image, shown by the dashboard) and
$$XSIMAGE (default save-game image). Each is an XPR0 packed resource holding one D3D texture, normally
DXT1. The title image is also written as a Windows icon (16-128 px) for the local loader shortcut/exe.
Usage: xbe_images.py path/to/default.xbe output_dir
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbeinfo import Xbe  # noqa: E402

SECTIONS = {"$$XTIMAGE": "title_image", "$$XSIMAGE": "save_image"}
FMT_DXT1, FMT_DXT3, FMT_DXT5 = 0x0C, 0x0E, 0x0F  # Xbox D3DFORMAT values (DXT2/4 share 0x0E/0x0F)


def rgb565(c):
    r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
    return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)


def color_block(blk, opaque):
    c0, c1 = struct.unpack_from("<HH", blk, 0)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or opaque:
        pal = [p0 + (255,), p1 + (255,),
               tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,),
               tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)]
    else:
        pal = [p0 + (255,), p1 + (255,), tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    bits = struct.unpack_from("<I", blk, 4)[0]
    return [pal[(bits >> (2 * i)) & 3] for i in range(16)]


def alpha_dxt3(blk):
    bits = int.from_bytes(blk[:8], "little")
    return [((bits >> (4 * i)) & 15) * 17 for i in range(16)]


def alpha_dxt5(blk):
    a0, a1 = blk[0], blk[1]
    if a0 > a1:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1) // 7 for i in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - i) * a0 + i * a1) // 5 for i in range(1, 5)] + [0, 255]
    bits = int.from_bytes(blk[2:8], "little")
    return [pal[(bits >> (3 * i)) & 7] for i in range(16)]


def decode(fmt, data, w, h):
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    bsize = 8 if fmt == FMT_DXT1 else 16
    o = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            blk = data[o:o + bsize]
            o += bsize
            if fmt == FMT_DXT1:
                texels = color_block(blk, False)
            else:
                alpha = alpha_dxt3(blk) if fmt == FMT_DXT3 else alpha_dxt5(blk)
                texels = [c[:3] + (a,) for c, a in zip(color_block(blk[8:], True), alpha)]
            for i, t in enumerate(texels):
                x, y = bx + i % 4, by + i // 4
                if x < w and y < h:
                    px[y][x] = t
    return px


def write_png(path, px):
    h, w = len(px), len(px[0])
    raw = b"".join(b"\0" + bytes(c for t in row for c in t) for row in px)
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def downscale(px, size):
    """Area-average an RGBA image (square, power-of-two) down to size x size."""
    n = len(px) // size
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(y * n, y * n + n):
                for sx in range(x * n, x * n + n):
                    r, g, b, a = px[sy][sx]
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            a = acc[3]
            row.append((acc[0] // a, acc[1] // a, acc[2] // a, a // (n * n)) if a else (0, 0, 0, 0))
        out.append(row)
    return out


def write_ico(path, px, sizes=(16, 24, 32, 48, 64, 128)):
    """Windows .ico with 32-bit DIB images (BGRA bottom-up + AND mask) for each size."""
    images = []
    for s in sizes:
        img = px if s == len(px) else downscale(px, s)
        bih = struct.pack("<IiiHHIIiiII", 40, s, 2 * s, 1, 32, 0, 0, 0, 0, 0, 0)
        xor = b"".join(bytes((t[2], t[1], t[0], t[3])) for row in reversed(img) for t in row)
        mask = bytes(((s + 31) // 32) * 4 * s)  # all zero: 32-bit icons take transparency from the alpha channel
        images.append((s, bih + xor + mask))
    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for s, blob in images:
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(blob), offset + len(blobs))
        blobs += blob
    with open(path, "wb") as f:
        f.write(header + entries + blobs)


def extract(xpr, name, outdir):
    if xpr[:4] != b"XPR0":
        print(f"{name}: not an XPR0 resource (magic {xpr[:4]!r})")
        return
    total, header_size = struct.unpack_from("<II", xpr, 4)
    common, data_off, lock, fmt_word, size_word = struct.unpack_from("<5I", xpr, 12)
    fmt = (fmt_word >> 8) & 0xFF
    w, h = 1 << ((fmt_word >> 20) & 15), 1 << ((fmt_word >> 24) & 15)
    if fmt not in (FMT_DXT1, FMT_DXT3, FMT_DXT5):
        print(f"{name}: texture format 0x{fmt:02x} not supported (only DXT1/3/5)")
        return
    data = xpr[header_size + data_off:]
    path = os.path.join(outdir, f"{name}.png")
    px = decode(fmt, data, w, h)
    write_png(path, px)
    print(f"{name}: {w}x{h} DXT{ {FMT_DXT1: 1, FMT_DXT3: 3, FMT_DXT5: 5}[fmt] } -> {path}")
    if name == "title_image" and w == h:
        ico = os.path.join(outdir, f"{name}.ico")
        write_ico(ico, px, tuple(s for s in (16, 24, 32, 48, 64, 128) if s <= w))
        print(f"{name}: icon -> {ico}")


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    xbe = Xbe(open(sys.argv[1], "rb").read())
    os.makedirs(sys.argv[2], exist_ok=True)
    found = 0
    for s in xbe.sections:
        if s["name"] in SECTIONS:
            found += 1
            extract(xbe.d[s["raw_addr"]:s["raw_addr"] + s["raw_size"]], SECTIONS[s["name"]], sys.argv[2])
    if not found:
        print("no $$XTIMAGE / $$XSIMAGE sections in this XBE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
