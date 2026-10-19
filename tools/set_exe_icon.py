#!/usr/bin/env python3
"""Set the icon of a Windows executable from an .ico file (Win32 resource update, Windows only).

Writes RT_ICON entries for every image in the .ico plus one RT_GROUP_ICON (id 1), replacing any existing.
Usage: set_exe_icon.py target.exe icon.ico
"""
import ctypes
import struct
import sys
from ctypes import wintypes

RT_ICON, RT_GROUP_ICON = 3, 14
LANG_NEUTRAL = 0

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.BeginUpdateResourceW.restype = wintypes.HANDLE
k32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
k32.UpdateResourceW.restype = wintypes.BOOL
k32.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
k32.EndUpdateResourceW.restype = wintypes.BOOL
k32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    exe, ico = sys.argv[1], open(sys.argv[2], "rb").read()
    reserved, kind, count = struct.unpack_from("<HHH", ico, 0)
    if reserved != 0 or kind != 1 or not count:
        print("not an .ico file")
        return 1
    h = k32.BeginUpdateResourceW(exe, False)
    if not h:
        print("BeginUpdateResource failed:", ctypes.get_last_error())
        return 1
    group = struct.pack("<HHH", 0, 1, count)
    for i in range(count):
        w, hgt, colors, res, planes, bits, size, offset = struct.unpack_from("<BBBBHHII", ico, 6 + 16 * i)
        blob = ico[offset:offset + size]
        buf = ctypes.create_string_buffer(blob, len(blob))
        if not k32.UpdateResourceW(h, RT_ICON, i + 1, LANG_NEUTRAL, buf, len(blob)):
            print("UpdateResource(RT_ICON) failed:", ctypes.get_last_error())
            k32.EndUpdateResourceW(h, True)
            return 1
        group += struct.pack("<BBBBHHIH", w, hgt, colors, res, planes, bits, size, i + 1)
    gbuf = ctypes.create_string_buffer(group, len(group))
    if not k32.UpdateResourceW(h, RT_GROUP_ICON, 1, LANG_NEUTRAL, gbuf, len(group)):
        print("UpdateResource(RT_GROUP_ICON) failed:", ctypes.get_last_error())
        k32.EndUpdateResourceW(h, True)
        return 1
    if not k32.EndUpdateResourceW(h, False):
        print("EndUpdateResource failed:", ctypes.get_last_error())
        return 1
    print(f"{exe}: icon set ({count} images)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
