"""Look up blackrt.dll offsets (as printed in crash logs, e.g. blackrt.dll+0x3e15d) in the linker map.

usage: python tools/mapsym.py [path/to/blackrt.map] 0x3e15d [0x1234 ...]
"""
import re
import sys

args = sys.argv[1:]
path = r"loader\build\win32\RelWithDebInfo\blackrt.map"
if args and not args[0].lower().startswith("0x"):
    path = args.pop(0)
text = open(path, encoding="latin-1").read()
base = int(re.search(r"Preferred load address is ([0-9a-fA-F]+)", text).group(1), 16)
syms = []
for m in re.finditer(r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s", text, re.M):
    syms.append((int(m.group(2), 16) - base, m.group(1)))
syms.sort()
for a in args:
    off = int(a, 16)
    best = None
    for rva, name in syms:
        if rva > off:
            break
        best = (rva, name)
    print(a, "->", "%s+0x%x" % (best[1], off - best[0]) if best else "?")
