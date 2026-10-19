#!/usr/bin/env python3
"""Print the structure of an Xbox executable (default.xbe).

Phase 1 gate: sections, entry point, kernel import table, library versions.
Usage: xbeinfo.py path/to/default.xbe [--json]
"""
import json
import os
import struct
import sys
import time

ENTRY_XOR = {"retail": 0xA8FC57AB, "debug": 0x94859D4B, "sega": 0x40B5C16E}
THUNK_XOR = {"retail": 0x5B6D40B6, "debug": 0xEFB1F152, "sega": 0x2290059D}
REGIONS = {0x1: "NA", 0x2: "Japan", 0x4: "Rest of World", 0x80000000: "Manufacturing"}
SECTION_FLAGS = [
    (0x01, "writable"), (0x02, "preload"), (0x04, "executable"), (0x08, "inserted file"),
    (0x10, "head page RO"), (0x20, "tail page RO"),
]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def cstr(b, o, limit=256):
    end = b.find(b"\0", o, o + limit)
    return b[o:end if end >= 0 else o + limit].decode("latin-1")


class Xbe:
    def __init__(self, data):
        self.d = data
        if data[:4] != b"XBEH":
            raise ValueError("not an XBE (missing XBEH magic)")
        self.base = u32(data, 0x104)
        self.size_of_headers = u32(data, 0x108)
        self.size_of_image = u32(data, 0x10C)
        self.timedate = u32(data, 0x114)
        self.cert_addr = u32(data, 0x118)
        self.num_sections = u32(data, 0x11C)
        self.sect_addr = u32(data, 0x120)
        self.init_flags = u32(data, 0x124)
        raw_entry = u32(data, 0x128)
        self.tls_addr = u32(data, 0x12C)
        self.stack_commit = u32(data, 0x130)
        self.heap_reserve = u32(data, 0x134)
        self.heap_commit = u32(data, 0x138)
        self.pe_base = u32(data, 0x13C)
        self.pe_size = u32(data, 0x140)
        self.debug_path_addr = u32(data, 0x14C)
        raw_thunk = u32(data, 0x158)
        self.libver_count = u32(data, 0x160)
        self.libver_addr = u32(data, 0x164)
        self.kernel_libver_addr = u32(data, 0x168)
        self.xapi_libver_addr = u32(data, 0x16C)

        # Entry point and kernel thunk address are XOR-encoded with a build-specific key.
        # Pick the key that puts the entry point inside the image.
        self.kind = None
        for kind in ("retail", "debug", "sega"):
            e = raw_entry ^ ENTRY_XOR[kind]
            if self.base <= e < self.base + self.size_of_image:
                self.kind, self.entry = kind, e
                self.thunk_addr = raw_thunk ^ THUNK_XOR[kind]
                break
        if self.kind is None:
            raise ValueError("entry point does not decode into the image with any known key")

        self.sections = self._sections()

    # virtual address -> file offset, via the section table (headers are mapped at base)
    def va_to_off(self, va):
        if self.base <= va < self.base + self.size_of_headers:
            return va - self.base
        for s in self.sections:
            if s["vaddr"] <= va < s["vaddr"] + max(s["vsize"], s["raw_size"]):
                return s["raw_addr"] + (va - s["vaddr"])
        return None

    def _sections(self):
        out = []
        off = self.sect_addr - self.base
        for i in range(self.num_sections):
            o = off + i * 56
            flags, vaddr, vsize, raw_addr, raw_size, name_addr = struct.unpack_from("<6I", self.d, o)
            name = cstr(self.d, name_addr - self.base, 64)
            out.append(dict(index=i, name=name, flags=flags, vaddr=vaddr, vsize=vsize,
                            raw_addr=raw_addr, raw_size=raw_size))
        return out

    def certificate(self):
        o = self.cert_addr - self.base
        d = self.d
        title = d[o + 0x0C:o + 0x0C + 80].decode("utf-16-le", "replace").split("\0")[0]
        region = u32(d, o + 0xA0)
        return dict(
            size=u32(d, o), timedate=u32(d, o + 4), title_id=u32(d, o + 8), title=title,
            allowed_media=u32(d, o + 0x9C), region=region,
            region_names=[n for bit, n in REGIONS.items() if region & bit] or ["?"],
            ratings=u32(d, o + 0xA4), disk_number=u32(d, o + 0xA8), version=u32(d, o + 0xAC),
        )

    def tls(self):
        if not self.tls_addr:
            return None
        o = self.va_to_off(self.tls_addr)
        if o is None:
            return None
        start, end, idx, callbacks, zero_fill, chars = struct.unpack_from("<6I", self.d, o)
        return dict(data_start=start, data_end=end, index_addr=idx, callbacks=callbacks,
                    zero_fill=zero_fill)

    def libraries(self):
        out = []
        o = self.va_to_off(self.libver_addr)
        for i in range(self.libver_count):
            p = o + i * 16
            name = self.d[p:p + 8].rstrip(b"\0").decode("latin-1")
            major, minor, build, flags = struct.unpack_from("<4H", self.d, p + 8)
            out.append(dict(name=name, version=f"{major}.{minor}.{build}", flags=flags,
                            approved=(flags >> 13) & 3, qfe=flags & 0x1FFF))
        return out

    def kernel_imports(self):
        o = self.va_to_off(self.thunk_addr)
        ords = []
        while True:
            v = u32(self.d, o + 4 * len(ords))
            if v == 0:
                break
            ords.append(v & 0x7FFFFFFF)
        return ords

    def debug_path(self):
        o = self.va_to_off(self.debug_path_addr) if self.debug_path_addr else None
        return cstr(self.d, o, 260) if o is not None else None


def fmt_flags(f):
    return ",".join(n for bit, n in SECTION_FLAGS if f & bit) or "-"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        sys.exit(__doc__)
    names = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "kernel_exports.json")))
    x = Xbe(open(args[0], "rb").read())
    cert = x.certificate()
    imports = x.kernel_imports()
    libs = x.libraries()
    ts = lambda t: time.strftime("%Y-%m-%d %H:%M:%S UTC", time.gmtime(t))

    if "--json" in sys.argv:
        json.dump(dict(
            kind=x.kind, base=x.base, entry=x.entry, thunk=x.thunk_addr, image_size=x.size_of_image,
            timedate=x.timedate, certificate=cert, sections=x.sections, tls=x.tls(), libraries=libs,
            kernel_imports=[dict(ordinal=o, name=names.get(str(o), "?")) for o in imports],
        ), sys.stdout, indent=1)
        return

    print(f"File            {args[0]}")
    print(f"Build kind      {x.kind}  (entry/thunk XOR keys)")
    print(f"Title           {cert['title']!r}  id {cert['title_id']:08X}  version {cert['version']:#x}  disc {cert['disk_number']}")
    print(f"Region          {'/'.join(cert['region_names'])} ({cert['region']:#x})   media {cert['allowed_media']:#x}   ratings {cert['ratings']:#x}")
    print(f"XBE timestamp   {ts(x.timedate)}   cert {ts(cert['timedate'])}")
    print(f"Debug path      {x.debug_path()}")
    print(f"Image base      {x.base:#010x}   size {x.size_of_image:#x}   headers {x.size_of_headers:#x}")
    print(f"Entry point     {x.entry:#010x}")
    print(f"Kernel thunks   {x.thunk_addr:#010x}   ({len(imports)} imports)")
    print(f"PE (orig)       base {x.pe_base:#010x}  size {x.pe_size:#x}   stack commit {x.stack_commit:#x}   heap {x.heap_reserve:#x}/{x.heap_commit:#x}")
    tls = x.tls()
    if tls:
        print(f"TLS             data {tls['data_start']:#010x}-{tls['data_end']:#010x}  index@{tls['index_addr']:#010x}  callbacks@{tls['callbacks']:#010x}  zero-fill {tls['zero_fill']:#x}")

    print(f"\nSections ({len(x.sections)})")
    print(f"  {'#':>2} {'name':<10} {'vaddr':>10} {'vsize':>9} {'raw':>9} {'rawsize':>9}  flags")
    for s in x.sections:
        print(f"  {s['index']:>2} {s['name']:<10} {s['vaddr']:#010x} {s['vsize']:#9x} {s['raw_addr']:#9x} {s['raw_size']:#9x}  {fmt_flags(s['flags'])}")

    print(f"\nStatically linked SDK libraries ({len(libs)})")
    for l in libs:
        print(f"  {l['name']:<10} {l['version']:<12} qfe {l['qfe']}  approved {l['approved']}")

    print(f"\nKernel imports ({len(imports)})")
    for o in sorted(imports):
        print(f"  {o:>3}  {names.get(str(o), '?')}")


if __name__ == "__main__":
    main()
