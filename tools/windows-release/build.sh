#!/bin/bash
# Cross-compile the Windows release on Linux, in a container with MinGW-w64.
#
#   tools/windows-release/build.sh [dol-generated-dir rel-generated-dir]
#
# Output: build/windows-release/twin-snakes/ (twin-snakes.exe, SDL3.dll,
# README.txt). With the two generated directories - DolRecomp output made
# with DOLRECOMP_LLVM_TARGET=x86_64-w64-windows-gnu - the native game module
# is linked as a DLL into module/: FOR THE OWNER'S OWN MACHINES ONLY, it is
# compiled from the game's code and never distributed (project rule 8).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/windows-release"
IMAGE=mgs-windows-build:bookworm
DOLGEN="${1:-}"; RELGEN="${2:-}"
docker build -q -t "$IMAGE" "$ROOT/tools/windows-release" >/dev/null
SHADERS="$ROOT/build/runtime/runtime/shaders"
[ -f "$SHADERS/gx.vert.inc" ] || { echo "build the runtime here first (shaders)"; exit 1; }
mkdir -p "$OUT"
TC="$ROOT/tools/windows-release/mingw64.cmake"
docker run --rm -u "$(id -u):$(id -g)" -v "$ROOT:$ROOT" -w "$ROOT" "$IMAGE" bash -euc '
  B='"$OUT"'/build
  cmake -S . -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_TOOLCHAIN_FILE='"$TC"' \
      -DSDL3_DIR=/opt/sdl3/x86_64-w64-mingw32/lib/cmake/SDL3 \
      -DMGS_PREBUILT_SHADERS='"$SHADERS"' > "$B.configure.log"
  cmake --build "$B" --target twin-snakes
  P='"$OUT"'/twin-snakes
  mkdir -p "$P"
  cp "$B/host/twin-snakes.exe" "$P/"
  cp /opt/sdl3/x86_64-w64-mingw32/bin/SDL3.dll "$P/"
  if [ -n "'"$DOLGEN"'" ]; then
    M="$B-module"
    cmake -S game/module -B "$M" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE='"$TC"' \
        -DDOL_GENERATED='"$DOLGEN"' -DREL_GENERATED='"$RELGEN"' \
        -DDOL_FILE='"$ROOT"'/discs/GGSPA4/disc1/sys/main.dol \
        -DREL_FILE='"$ROOT"'/discs/GGSPA4/disc1/files/shared/mgso_pal.rel \
        -DGXRUNTIME_DIR='"$ROOT"'/extern/ModernGekko-Template/lib/ModernGekko/vendor/dolphin/GXRuntime \
        -DREL_BASE=0x7F008000 > "$M.configure.log"
    cmake --build "$M"
    mkdir -p "$P/module"
    cp "$M/gGGSPA4_recomp.dll" "$P/module/"
  fi
'
cat > "$OUT/twin-snakes/README.txt" <<'README'
Metal Gear Solid: The Twin Snakes - native PC port (Windows build)

START
  Double-click twin-snakes.exe. The launcher asks for disc 1 and disc 2 the
  first time - .iso, .gcm or NKit images of the European release, with any
  file names - checks them, and remembers them.

CONTROLS
  Any controller (Xbox, PlayStation, Switch Pro), or keyboard:
  X=A  Z=B  S=X  A=Y  arrows=D-pad  Enter=Start  Q/W/E=L/R/Z  Esc=quit.

WHERE THINGS ARE KEPT
  Settings (volume, fullscreen, "start straight away") and the chosen discs:
    %APPDATA%\twin-snakes\settings.ini
  Plain text, safe to edit. Delete it and the launcher starts from defaults.
  disc1.path and disc2.path beside it hold the remembered disc images.

  Memory card: saves\slot_a.raw in this folder. If a save goes wrong and the
  game stops after the logos, use "Reset memory card" in the launcher - the
  old card is kept beside it as slot_a.raw.backup-<date>-<time>.raw.

REQUIREMENTS
  64-bit Windows 10 or 11, a GPU with Vulkan, and a CPU with AVX2 and FMA
  (Intel Haswell / AMD Zen or newer).
README
if [ -n "$DOLGEN" ]; then
    printf '%s\n' "" "PERSONAL BUILD: module\\ holds the native game compiled from YOUR disc." \
        "Do not share this folder with anyone." "" | cat - "$OUT/twin-snakes/README.txt" \
        > "$OUT/twin-snakes/README.tmp" && mv "$OUT/twin-snakes/README.tmp" "$OUT/twin-snakes/README.txt"
fi
sed -i 's/$/\r/' "$OUT/twin-snakes/README.txt"
echo "built: $OUT/twin-snakes"
