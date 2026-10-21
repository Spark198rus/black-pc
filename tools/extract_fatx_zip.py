#!/usr/bin/env python3
"""
Direct Streaming FATX Extractor for Xbox ISOs inside ZIP archives.
Extracts game files directly from Black (USA).zip into game/ without unpacking the ISO.
"""

import hashlib
import os
import struct
import sys
import time
import zipfile

ZIP_PATH = "/home/nubi/Downloads/Black (USA).zip"
ISO_NAME = "Black (USA).iso"
TARGET_DIR = "/home/nubi/Programacao/black-pc/game"
SYMLINK_PATH = "/home/nubi/Programacao/black-pc/games"
EXPECTED_XBE_SHA256 = "df2739c372d254a90aeeccf5971d12097f22d89fbb90db021fda96dd4deb68f6"
PART_OFFSET = 0x18300000  # Xbox Redump partition offset

def main():
    if not os.path.exists(ZIP_PATH):
        print(f"Error: ZIP file not found at {ZIP_PATH}", file=sys.stderr)
        sys.exit(1)

    print(f"Opening archive: {ZIP_PATH}")
    t0 = time.time()

    with zipfile.ZipFile(ZIP_PATH, "r") as z:
        if ISO_NAME not in z.namelist():
            print(f"Error: {ISO_NAME} not found inside {ZIP_PATH}", file=sys.stderr)
            sys.exit(1)

        with z.open(ISO_NAME) as f:
            f.seek(PART_OFFSET + 0x10000)
            hdr = f.read(2048)
            if not hdr.startswith(b"MICROSOFT*XBOX*MEDIA"):
                print("Error: Xbox volume header magic not found!", file=sys.stderr)
                sys.exit(1)

            root_sector, root_size = struct.unpack("<II", hdr[20:28])
            print(f"FATX volume found. Root sector: {root_sector}, size: {root_size}")

            os.makedirs(TARGET_DIR, exist_ok=True)

            # Discover all files and directories
            items_to_extract = []  # (rel_path, is_dir, sector, size)

            def scan_dir(sector, size, current_path):
                f.seek(PART_OFFSET + sector * 2048)
                dir_data = f.read(size)
                queue = [0]
                visited = set()
                subdirs = []

                while queue:
                    node = queue.pop()
                    if node in visited or node == 0xFFFF or node * 4 >= len(dir_data):
                        continue
                    visited.add(node)
                    entry_off = node * 4
                    if entry_off + 14 > len(dir_data):
                        continue
                    left, right, sec, fsize, attr, name_len = struct.unpack("<HHIIBB", dir_data[entry_off:entry_off+14])
                    if name_len > 0 and entry_off + 14 + name_len <= len(dir_data):
                        name = dir_data[entry_off+14:entry_off+14+name_len].decode("latin-1", "replace")
                        is_dir = bool(attr & 0x10)
                        rel_path = os.path.join(current_path, name) if current_path else name
                        if is_dir:
                            items_to_extract.append((rel_path, True, sec, 2048 if fsize == 0 else fsize))
                            subdirs.append((sec, 2048 if fsize == 0 else fsize, rel_path))
                        else:
                            items_to_extract.append((rel_path, False, sec, fsize))
                    if left != 0xFFFF and left not in visited:
                        queue.append(left)
                    if right != 0xFFFF and right not in visited:
                        queue.append(right)

                for sub_sec, sub_sz, sub_p in subdirs:
                    scan_dir(sub_sec, sub_sz, sub_p)

            print("Indexing FATX filesystem hierarchy...")
            scan_dir(root_sector, root_size, "")

            files = [item for item in items_to_extract if not item[1]]
            dirs = [item for item in items_to_extract if item[1]]
            total_bytes = sum(f[3] for f in files)
            print(f"Found {len(dirs)} directories and {len(files)} files ({total_bytes / (1024**3):.2f} GiB).")

            # Create all directories
            for d in dirs:
                os.makedirs(os.path.join(TARGET_DIR, d[0]), exist_ok=True)

            # Extract files
            extracted_bytes = 0
            chunk_size = 1024 * 1024  # 1 MB chunk

            print("\nExtracting files directly to target directory...")
            for idx, (rel_path, _, sector, fsize) in enumerate(files, 1):
                out_file_path = os.path.join(TARGET_DIR, rel_path)
                os.makedirs(os.path.dirname(out_file_path), exist_ok=True)

                f.seek(PART_OFFSET + sector * 2048)
                bytes_left = fsize
                with open(out_file_path, "wb") as out_f:
                    while bytes_left > 0:
                        to_read = min(chunk_size, bytes_left)
                        buf = f.read(to_read)
                        if not buf:
                            break
                        out_f.write(buf)
                        bytes_left -= len(buf)

                extracted_bytes += fsize
                pct = (extracted_bytes / total_bytes) * 100 if total_bytes else 100
                if idx % 25 == 0 or idx == len(files):
                    print(f"[{idx:3d}/{len(files):3d}] ({pct:5.1f}%) {rel_path} ({fsize:,} bytes)")

    # Create games -> game symlink
    if not os.path.exists(SYMLINK_PATH):
        try:
            os.symlink("game", SYMLINK_PATH)
            print(f"\nCreated symlink: {SYMLINK_PATH} -> game")
        except Exception as e:
            print(f"Warning: Could not create symlink: {e}")

    # Verify default.xbe hash
    xbe_path = os.path.join(TARGET_DIR, "default.xbe")
    if os.path.exists(xbe_path):
        hasher = hashlib.sha256()
        with open(xbe_path, "rb") as f:
            while chunk := f.read(1024 * 1024):
                hasher.update(chunk)
        calc_sha256 = hasher.hexdigest()
        elapsed = time.time() - t0
        print(f"\n==========================================")
        print(f"Extraction completed in {elapsed:.1f} seconds!")
        print(f"Target location: {TARGET_DIR}")
        print(f"default.xbe SHA-256: {calc_sha256}")
        if calc_sha256 == EXPECTED_XBE_SHA256:
            print("STATUS: VERIFIED MATCH! Official NTSC-U Retail Build.")
        else:
            print("STATUS: HASH MISMATCH! Expected:", EXPECTED_XBE_SHA256)
        print(f"==========================================")
    else:
        print("Error: default.xbe was not extracted!", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
