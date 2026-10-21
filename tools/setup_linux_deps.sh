#!/usr/bin/env bash
# Sets up 32-bit third-party development dependencies for building black_linux
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

DXVK_DIR="$ROOT_DIR/tools/dxvk-native"
DXVK_URL="https://github.com/doitsujin/dxvk/releases/download/v3.1.1/dxvk-native-3.1.1-steamrt-sniper.tar.gz"

echo "================================================================================"
echo "Setting up Linux x86 (32-bit) build dependencies for BLACK PC"
echo "================================================================================"

# 1. Fetch DXVK-Native 3.1.1 if not present
if [ ! -f "$DXVK_DIR/usr/lib32/libdxvk_d3d9.so" ]; then
    echo "[+] Downloading and extracting DXVK-Native 3.1.1..."
    mkdir -p "$DXVK_DIR"
    curl -sL "$DXVK_URL" | tar -zx -C "$DXVK_DIR/"
    echo "[+] DXVK-Native ready."
else
    echo "[✓] DXVK-Native 3.1.1 already present."
fi

echo "================================================================================"
echo "Dependencies setup complete. You can now build with:"
echo "    make -j\$(nproc)"
echo "================================================================================"
