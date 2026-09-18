#!/usr/bin/env bash
# Run Ghidra's headless analyser against the flatpak install.
#
# Ghidra is org.ghidra_sre.Ghidra (12.1.3), NOT a pacman package. It bundles its
# own JDK 21, so no system java is needed. The sandbox holds filesystems=home,
# so any path under ~ — this repository included — is visible to it.
#
#   tools/ghidra.sh <project-dir> <project-name> [analyzeHeadless args...]
#
# The Gekko/Broadway processor spec comes bundled inside the GameCubeLoader
# extension — do NOT also install ghidra-gekko-broadway-lang standalone, it
# duplicates the language definition (F3 in HANDOFF.md).
#
#   Extensions/GameCubeLoader   loader + PowerPC:BE:32:Gekko_Broadway
#
# Use -processor "PowerPC:BE:32:Gekko_Broadway" — stock Ghidra's
# PowerPC:BE:32:default mis-decodes the paired-single instructions.
#
# To rebuild the loader after a Ghidra upgrade, build it INSIDE the sandbox
# (GHIDRA_INSTALL_DIR=/app/lib/ghidra is not visible from outside):
#   flatpak run --command=bash --filesystem=home --share=network \
#     org.ghidra_sre.Ghidra -c 'cd extern/Ghidra-GameCube-Loader && \
#     ./gradlew --no-daemon -PGHIDRA_INSTALL_DIR=/app/lib/ghidra buildExtension'
set -euo pipefail

exec flatpak run --command=/app/lib/ghidra/support/analyzeHeadless \
  org.ghidra_sre.Ghidra "$@"
