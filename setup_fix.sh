#!/bin/bash
# =============================================================================
# setup_fix.sh — Automated Setup for Panzer Dragoon PC Builder on Linux / Wine
# =============================================================================
#
# Usage:
#   ./setup_fix.sh /path/to/extracted/pdt-recompile-builder-2.0.0-win64
#
# =============================================================================

set -euo pipefail

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 <path_to_extracted_pdt_builder_folder>"
    exit 1
fi

SRC_DIR="$1"
if [ ! -f "$SRC_DIR/builder.exe" ]; then
    echo "Error: Could not find builder.exe in '$SRC_DIR'"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-panzer}"
BUILDER_DEST="$WINEPREFIX/drive_c/panzer_builder"

echo "=== 1. Initializing 64-bit Wine prefix at $WINEPREFIX ==="
WINEARCH=win64 WINEPREFIX="$WINEPREFIX" wineboot --init

echo "=== 2. Copying builder package into Wine prefix ==="
mkdir -p "$BUILDER_DEST"
cp -r "$SRC_DIR"/* "$BUILDER_DEST/"

echo "=== 3. Downloading native Linux Zig 0.15.2 ==="
mkdir -p "$WINEPREFIX/zig-native"
if [ ! -f "$WINEPREFIX/zig-native/zig" ]; then
    TMP_TAR="/tmp/zig-0.15.2-linux-x86_64.tar.xz"
    curl -L -o "$TMP_TAR" "https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz"
    tar -xf "$TMP_TAR" -C "$WINEPREFIX/zig-native" --strip-components=1
    rm -f "$TMP_TAR"
fi
echo "Native Zig version: $("$WINEPREFIX/zig-native/zig" version)"

echo "=== 4. Binary patching builder.exe (/zig/zig.exe -> \zig\zig.exe) ==="
python3 -c "
with open('$BUILDER_DEST/builder.exe', 'rb') as f:
    data = f.read()
if b'/zig/zig.exe' in data:
    data = data.replace(b'/zig/zig.exe', b'\\\\zig\\\\zig.exe')
    with open('$BUILDER_DEST/builder.exe', 'wb') as f:
        f.write(data)
    print('Patched /zig/zig.exe to \\zig\\zig.exe')
else:
    print('Already patched or not found.')
"

echo "=== 5. Compiling and installing Windows PE zig.exe proxy ==="
"$WINEPREFIX/zig-native/zig" cc -O2 -target x86_64-windows \
    "$SCRIPT_DIR/zig_proxy.c" -lws2_32 \
    -o "$BUILDER_DEST/zig/zig.exe"

echo "=== 6. Installing launcher scripts ==="
cp "$SCRIPT_DIR/zig_daemon.py" "$WINEPREFIX/zig_daemon.py"
cp "$SCRIPT_DIR/panzer_build.sh" "$WINEPREFIX/panzer_build.sh"
chmod +x "$WINEPREFIX/zig_daemon.py" "$WINEPREFIX/panzer_build.sh"

echo ""
echo "=========================================================="
echo " Setup complete! You can now run:"
echo "   $WINEPREFIX/panzer_build.sh"
echo "=========================================================="
