#!/usr/bin/env bash
# Packs the built Linux native binary into a release archive: dist/black-pc-<version>-linux-x86.tar.gz
# with one folder, port/, holding black_linux, lib/ (dxvk_d3d9 and SDL2), README.md, KEYBINDS.md, and LICENSE.txt.
# Extracted next to the user's game folder, port/black_linux finds ../game and keeps what it writes in port/
# (savedata/, black.ini, black.log, cache/).
# Nothing game-derived is bundled.
# Debug symbols (black_linux.debug) go into a separate -symbols archive, kept to read crash reports of that build.
# Build first: make -j$(nproc)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION=$(grep -oP 'project\(\S+ VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$ROOT_DIR/loader/CMakeLists.txt")
if [ -z "$VERSION" ]; then
    echo "Error: Could not extract version from loader/CMakeLists.txt" >&2
    exit 1
fi

NAME="black-pc-${VERSION}-linux-x86"
DIST_DIR="$ROOT_DIR/dist"
mkdir -p "$DIST_DIR"

# Ensure binary is built
if [ ! -f "$ROOT_DIR/black_linux" ]; then
    echo "black_linux not found, building..."
    make -C "$ROOT_DIR" -j"$(nproc)"
fi

# Staged under /tmp so nothing in the project folder has to be deleted
STAGE_DIR=$(mktemp -d "/tmp/${NAME}-XXXXXX")
SYM_DIR=$(mktemp -d "/tmp/${NAME}-symbols-XXXXXX")
PORT_DIR="$STAGE_DIR/port"
LIB_DIR="$PORT_DIR/lib"

cleanup() {
    rm -rf "$STAGE_DIR" "$SYM_DIR"
}
trap cleanup EXIT

mkdir -p "$PORT_DIR" "$LIB_DIR"

# Copy binary
cp "$ROOT_DIR/black_linux" "$PORT_DIR/black_linux"
chmod +x "$PORT_DIR/black_linux"

# Separate debug symbols if objcopy and strip are available
if command -v objcopy >/dev/null 2>&1 && command -v strip >/dev/null 2>&1; then
    objcopy --only-keep-debug "$PORT_DIR/black_linux" "$SYM_DIR/black_linux.debug"
    strip --strip-unneeded "$PORT_DIR/black_linux"
    objcopy --add-gnu-debuglink="$SYM_DIR/black_linux.debug" "$PORT_DIR/black_linux" 2>/dev/null || true
fi

# Bundle runtime libraries into port/lib/
# 1. DXVK Native D3D9
DXVK_DIR="$ROOT_DIR/tools/dxvk-native/usr/lib32"
if [ -d "$DXVK_DIR" ]; then
    if [ -f "$DXVK_DIR/libdxvk_d3d9.so.0.30101" ]; then
        cp "$DXVK_DIR/libdxvk_d3d9.so.0.30101" "$LIB_DIR/libdxvk_d3d9.so.0.30101"
        (cd "$LIB_DIR" && ln -sf libdxvk_d3d9.so.0.30101 libdxvk_d3d9.so.0)
        (cd "$LIB_DIR" && ln -sf libdxvk_d3d9.so.0 libdxvk_d3d9.so)
    elif [ -f "$DXVK_DIR/libdxvk_d3d9.so" ]; then
        cp -L "$DXVK_DIR/libdxvk_d3d9.so" "$LIB_DIR/libdxvk_d3d9.so.0"
        (cd "$LIB_DIR" && ln -sf libdxvk_d3d9.so.0 libdxvk_d3d9.so)
    fi
fi

# 2. SDL2 (32-bit)
SDL_SRC=""
if [ -f "$ROOT_DIR/tools/sdl2/lib/libSDL2.so" ]; then
    SDL_SRC=$(readlink -f "$ROOT_DIR/tools/sdl2/lib/libSDL2.so" || true)
fi
if [ -z "$SDL_SRC" ] || [ ! -f "$SDL_SRC" ]; then
    if [ -f "/usr/lib/i386-linux-gnu/libSDL2-2.0.so.0" ]; then
        SDL_SRC=$(readlink -f "/usr/lib/i386-linux-gnu/libSDL2-2.0.so.0" || true)
    fi
fi

if [ -n "$SDL_SRC" ] && [ -f "$SDL_SRC" ]; then
    cp "$SDL_SRC" "$LIB_DIR/libSDL2-2.0.so.0"
    (cd "$LIB_DIR" && ln -sf libSDL2-2.0.so.0 libSDL2.so)
fi

# Copy metadata, keybinds, and license
[ -f "$ROOT_DIR/README.md" ] && cp "$ROOT_DIR/README.md" "$PORT_DIR/README.md"
[ -f "$ROOT_DIR/KEYBINDS.md" ] && cp "$ROOT_DIR/KEYBINDS.md" "$PORT_DIR/KEYBINDS.md"
[ -f "$ROOT_DIR/LICENSE" ] && cp "$ROOT_DIR/LICENSE" "$PORT_DIR/LICENSE.txt"

# Create distribution tarballs (primary for Linux)
tar -czf "$DIST_DIR/$NAME.tar.gz" -C "$STAGE_DIR" port
tar -czf "$DIST_DIR/$NAME-symbols.tar.gz" -C "$SYM_DIR" .

# Also create zip archives if zip is available
if command -v zip >/dev/null 2>&1; then
    (cd "$STAGE_DIR" && zip -q -r "$DIST_DIR/$NAME.zip" port)
    (cd "$SYM_DIR" && zip -q -r "$DIST_DIR/$NAME-symbols.zip" .)
fi

echo "================================================================================"
echo "BLACK PC Release Distribution ($VERSION - Linux x86)"
echo "================================================================================"
for f in "$DIST_DIR/$NAME"*; do
    if [ -f "$f" ]; then
        sha=$(sha256sum "$f" | cut -d' ' -f1)
        size=$(stat -c%s "$f")
        printf "%-42s %10d bytes  sha256 %s\n" "$(basename "$f")" "$size" "$sha"
    fi
done
echo "================================================================================"
