#!/bin/bash
# =============================================================================
# panzer_build.sh — Panzer Dragoon Trilogy PC Builder Launcher for Wine/Linux
# =============================================================================

set -euo pipefail

# Configurable paths (defaults to ~/.wine-panzer)
WINEPREFIX="${WINEPREFIX:-$HOME/.wine-panzer}"
WINE_CMD="${WINE:-wine}"
BUILDER_DIR="$WINEPREFIX/drive_c/panzer_builder"
DAEMON_SCRIPT="$WINEPREFIX/zig_daemon.py"
LOG_FILE="$WINEPREFIX/zig-daemon.log"

export WINEPREFIX
export WINEARCH=win64
export WINEDEBUG=-all

# Ensure Zig caches use disk inside the prefix (no /tmp or tmpfs)
export ZIG_GLOBAL_CACHE_DIR="$WINEPREFIX/zig-cache/global"
export ZIG_LOCAL_CACHE_DIR="$WINEPREFIX/zig-cache/local"
mkdir -p "$ZIG_GLOBAL_CACHE_DIR" "$ZIG_LOCAL_CACHE_DIR"

# Start background Zig compiler daemon
python3 "$DAEMON_SCRIPT" > "$LOG_FILE" 2>&1 &
DAEMON_PID=$!

cleanup() {
    if kill -0 "$DAEMON_PID" 2>/dev/null; then
        kill "$DAEMON_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

sleep 0.5

echo "=========================================================="
echo " Panzer Dragoon Trilogy PC Builder (Linux Wine Bridge)"
echo "=========================================================="
echo " Wine Prefix:   $WINEPREFIX"
echo " Builder Dir:   $BUILDER_DIR"
echo " Native Zig:    $($WINEPREFIX/zig-native/zig version 2>/dev/null || echo '0.15.2')"
echo " Zig Daemon:    PID $DAEMON_PID (listening on 127.0.0.1:45678)"
echo " Daemon Log:    $LOG_FILE"
echo "=========================================================="
echo ""

# Convert any Linux path arguments to Wine Windows paths
args=()
for arg in "$@"; do
    if [[ "$arg" == /* ]] && [[ -e "$arg" ]]; then
        win_arg=$(winepath -w "$arg" 2>/dev/null || echo "$arg")
        args+=("$win_arg")
    else
        args+=("$arg")
    fi
done

cd "$BUILDER_DIR"
"$WINE_CMD" "C:\\panzer_builder\\builder.exe" "${args[@]}"
