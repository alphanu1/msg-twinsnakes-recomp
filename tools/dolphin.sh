#!/usr/bin/env bash
# Dolphin, via flatpak (org.DolphinEmu.dolphin-emu).
#
# NOT the pacman package: this system is ~800 packages behind, and installing
# dolphin-emu from the current repo is a partial upgrade that fails on an
# mbedtls/mbedtls3 file conflict. See F6 in HANDOFF.md. Flatpak sidesteps it.
#
#   tools/dolphin.sh tool extract <disc.iso> <out-dir>   headless extraction
#   tools/dolphin.sh tool header  <disc.iso>             disc ID, region, hash
#   tools/dolphin.sh tool verify  <disc.iso>             check against redump
#   tools/dolphin.sh nogui <args...>                     headless run (SDK log)
#   tools/dolphin.sh gui   <args...>                     the full emulator
#
# dolphin-tool is what phase 0 wants: extraction and hashing without a GUI.
set -euo pipefail

mode="${1:-gui}"; shift || true
case "$mode" in
  tool)  cmd=/app/bin/dolphin-tool ;;
  nogui) cmd=/app/bin/dolphin-emu-nogui ;;
  gui)   cmd=/app/bin/dolphin-emu ;;
  *) echo "usage: $0 {tool|nogui|gui} [args...]" >&2; exit 2 ;;
esac

exec flatpak run --command="$cmd" --filesystem=home org.DolphinEmu.dolphin-emu "$@"
