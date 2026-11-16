# Panzer Dragoon Trilogy PC Builder on Linux

Scripts to run the Panzer Dragoon Trilogy PC Builder under Wine and build the native PC recompilations of *Panzer Dragoon*, *Panzer Dragoon II Zwei*, and *Panzer Dragoon Saga*.

## Why this is needed

The bundled Windows version of Zig 0.15.2 has trouble finding its executable path under Wine. The builder also expects a Windows-style path to Zig, and its linker response files contain Windows paths. This project replaces the bundled compiler with a small Windows proxy that forwards build commands to native Zig on Linux. The bridge translates paths and handles parallel compiler requests.

## Requirements

- Linux
- Wine 8 or later
- Python 3
- `curl` and `tar`

## Quick setup

1. Extract the builder package:
   ```bash
   unzip pdt-recompile-builder-2.0.0-win64.zip
   ```

2. Run the setup script with the extracted folder:
   ```bash
   chmod +x setup_fix.sh
   ./setup_fix.sh /path/to/pdt-recompile-builder-2.0.0-win64
   ```

   The script creates a Wine prefix at `~/.wine-panzer`, installs the builder and native Zig 0.15.2, and sets up the proxy and launcher.

3. Start the builder:
   ```bash
   ~/.wine-panzer/panzer_build.sh
   ```

## Manual setup

1. Create a 64-bit Wine prefix:
   ```bash
   export WINEPREFIX="$HOME/.wine-panzer"
   export WINEARCH=win64
   wineboot --init
   ```

2. Copy the builder package into the prefix:
   ```bash
   mkdir -p "$WINEPREFIX/drive_c/panzer_builder"
   cp -r /path/to/pdt-recompile-builder-2.0.0-win64/* "$WINEPREFIX/drive_c/panzer_builder/"
   ```

3. Download native Zig 0.15.2:
   ```bash
   mkdir -p "$WINEPREFIX/zig-native"
   curl -L https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz | \
     tar -xJ -C "$WINEPREFIX/zig-native" --strip-components=1
   ```

4. Fix the Zig path in `builder.exe`:
   ```bash
   python3 -c "
   with open('$WINEPREFIX/drive_c/panzer_builder/builder.exe', 'rb') as f:
       data = f.read()
   data = data.replace(b'/zig/zig.exe', b'\\\\zig\\\\zig.exe')
   with open('$WINEPREFIX/drive_c/panzer_builder/builder.exe', 'wb') as f:
       f.write(data)
   "
   ```

5. Build the Windows proxy and install the launcher scripts:
   ```bash
   "$WINEPREFIX/zig-native/zig" cc -O2 -target x86_64-windows \
     zig_proxy.c -lws2_32 \
     -o "$WINEPREFIX/drive_c/panzer_builder/zig/zig.exe"
   cp zig_daemon.py "$WINEPREFIX/zig_daemon.py"
   cp panzer_build.sh "$WINEPREFIX/panzer_build.sh"
   chmod +x "$WINEPREFIX/zig_daemon.py" "$WINEPREFIX/panzer_build.sh"
   ```

## Build a game

Run the launcher and follow the builder prompts to choose your disc image (`.cue`, `.chd`, or `.iso`) and output folder:

```bash
~/.wine-panzer/panzer_build.sh
```

You can also pass the builder options directly, for example:

```bash
~/.wine-panzer/panzer_build.sh \
  --disc "/path/to/Panzer Dragoon Saga (Disc 1).cue" \
  --out "$HOME/Documents/Panzer Dragoon Games"
```

For a multi-disc game, pass each disc with its own `--disc` option (e.g. *Panzer Dragoon Saga* Discs 1–4):

```bash
~/.wine-panzer/panzer_build.sh \
  --disc "/path/to/Disc 1.cue" \
  --disc "/path/to/Disc 2.cue" \
  --disc "/path/to/Disc 3.cue" \
  --disc "/path/to/Disc 4.cue" \
  --out "$HOME/Documents/Panzer Dragoon Games"
```

## Play the game

You must change directory (`cd`) into the output folder so the game executable can locate its `SDL2.dll` and `.ini` settings:

```bash
cd "$HOME/Documents/Panzer Dragoon Games"
WINEPREFIX=~/.wine-panzer wine pds-recompile.exe
```

In-game, **Esc** or gamepad **Back** opens settings; **F11** toggles fullscreen.

## Steam Deck / SteamOS Notes

1. **Persistent Cache (No `/tmp` / RAM-disk errors)**:
   SteamOS mounts `/tmp` as a `tmpfs` (in RAM). To prevent `error: CacheCheckFailed` or memory exhaustion during compilation, the scripts explicitly isolate all Zig compilation caches to persistent storage on disk inside `$WINEPREFIX/zig-cache/`.
2. **Custom Wine / Proton**:
   If running on SteamOS without system Wine installed, you can specify your Proton or Wine binary using the `WINE` environment variable:
   ```bash
   WINE="/path/to/proton" ~/.wine-panzer/panzer_build.sh
   ```

## Disclaimer

This repository contains scripts and compatibility shims, not game code, assets, or disc images. You need your own game discs to build the games.


## License

This project is licensed under the [MIT Non-Commercial License](LICENSE) (MIT-NC). You are free to copy, modify, and distribute this software for non-commercial purposes only. It may not be sold or monetized.
