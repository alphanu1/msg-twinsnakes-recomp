#!/bin/bash
# Build the Linux release - Steam Deck included - in a Debian 11 container.
#
#   tools/linux-release/build.sh [module.so]
#
# Output: build/linux-release/twin-snakes/  (the game, lib/libSDL3.so.0, a
# start script). With a module argument the native game module is linked
# again in the container, from this tree's generated objects, and put in
# module/ - FOR THE OWNER'S OWN MACHINES ONLY: it is compiled from the
# game's code and is never distributed (project rule 8). A player's
# launcher builds their own.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/linux-release"
IMAGE=mgs-linux-build:sniper
MODULE_DIR="${1:-}"

# Valve's Steam Runtime 3 "sniper" SDK, as a flat image (see Dockerfile).
SYSROOT_URL="https://repo.steampowered.com/steamrt-images-sniper/snapshots/latest-container-runtime-public-beta/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-sniper-sysroot.tar.gz"
if ! docker image inspect steamrt-sniper-sdk:sysroot >/dev/null 2>&1; then
    mkdir -p "$OUT"
    [ -f "$OUT/sniper-sdk-sysroot.tar.gz" ] || curl -L -o "$OUT/sniper-sdk-sysroot.tar.gz" "$SYSROOT_URL"
    docker import "$OUT/sniper-sdk-sysroot.tar.gz" steamrt-sniper-sdk:sysroot
fi
docker build -q -t "$IMAGE" "$ROOT/tools/linux-release" >/dev/null
mkdir -p "$OUT"

# The shaders are SPIR-V: platform-independent, compiled by glslc on the
# development machine, which Debian 11 does not package.
SHADERS="$ROOT/build/runtime/runtime/shaders"
[ -f "$SHADERS/gx.vert.inc" ] || { echo "build the runtime here first (shaders)"; exit 1; }

# The tree is mounted at its own path so absolute paths recorded in build
# files (generated objects, the module's inputs) resolve inside as outside.
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:$ROOT" -w "$ROOT" \
    -e MODULE_DIR="$MODULE_DIR" "$IMAGE" bash -euc '
  B='"$OUT"'/build
  cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DSDL3_DIR=/opt/sdl3/lib/cmake/SDL3 \
      -DMGS_PREBUILT_SHADERS='"$SHADERS"' \
      -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
      -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON -DCMAKE_INSTALL_RPATH="\$ORIGIN/lib" \
      > "$B.configure.log"
  cmake --build "$B" --target twin-snakes
  P='"$OUT"'/twin-snakes
  mkdir -p "$P/lib"
  cp "$B/host/twin-snakes" "$P/"
  cp -L /opt/sdl3/lib/libSDL3.so.0 "$P/lib/"
  if [ -n "$MODULE_DIR" ]; then
    M="$B-module"
    grep -E "^(DOL_GENERATED|REL_GENERATED|DOL_FILE|REL_FILE|GXRUNTIME_DIR|REL_BASE)" \
        "$MODULE_DIR/CMakeCache.txt" | sed "s/:[A-Z]*=/=/; s/^/-D/" > "$B.module.args"
    cmake -S game/module -B "$M" -G Ninja -DCMAKE_BUILD_TYPE=Release $(cat "$B.module.args") > "$M.configure.log"
    cmake --build "$M"
    mkdir -p "$P/module"
    cp "$M/gGGSPA4_recomp.so" "$P/module/"
  fi
'
cat > "$OUT/twin-snakes/README.txt" <<'README'
Metal Gear Solid: The Twin Snakes - native PC port (Linux / Steam Deck build)

START
  Run play.sh. The launcher asks for disc 1 and disc 2 the first time -
  .iso, .gcm or NKit images of the European release, with any file names -
  checks them, and remembers them.
  Steam Deck: in desktop mode, Steam > Add a Game > Add a Non-Steam Game >
  play.sh, then start it from Game Mode with the Deck's controls.

CONTROLS
  Any controller (Xbox, PlayStation, Switch Pro, Steam Deck), or keyboard:
  X=A  Z=B  S=X  A=Y  arrows=D-pad  Enter=Start  Q/W/E=L/R/Z  Esc=quit.

WHERE THINGS ARE KEPT
  Settings (volume, fullscreen, "start straight away") and the chosen discs:
    Linux / Steam Deck  ~/.config/twin-snakes/settings.ini
                        (Deck: /home/deck/.config/twin-snakes/)
    Windows             %APPDATA%\twin-snakes\settings.ini
    Mac                 ~/Library/Application Support/twin-snakes/settings.ini
  Plain text, safe to edit. Delete it and the launcher starts from defaults.
  disc1.path and disc2.path beside it hold the remembered disc images.

  Memory card: saves/slot_a.raw in this folder. If a save goes wrong and the
  game stops after the logos, use "Reset memory card" in the launcher - the
  old card is kept beside it as slot_a.raw.backup-<date>-<time>.raw.

  Start without the launcher: ./play.sh --play   Bring it back: --launcher
README
if [ -n "$MODULE_DIR" ]; then
    printf '%s\n' "" "PERSONAL BUILD: module/ holds the native game compiled from YOUR disc." \
        "Do not share this folder with anyone." "" | cat - "$OUT/twin-snakes/README.txt" \
        > "$OUT/twin-snakes/README.tmp" && mv "$OUT/twin-snakes/README.tmp" "$OUT/twin-snakes/README.txt"
fi
cat > "$OUT/twin-snakes/play.sh" <<'PLAY'
#!/bin/bash
# Start the game from this folder. On a Steam Deck: add this script to Steam
# as a non-Steam game, or run it from the desktop. The launcher asks for the
# disc images the first time.
cd "$(dirname "$0")" || exit 1
exec ./twin-snakes "$@"
PLAY
chmod +x "$OUT/twin-snakes/play.sh"
echo "built: $OUT/twin-snakes"
