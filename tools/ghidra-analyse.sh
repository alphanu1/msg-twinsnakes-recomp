#!/usr/bin/env bash
# Analyse a game binary in Ghidra headless and export an auditable inventory.
#
# WHAT THIS COMMITS AND WHAT IT DOES NOT.
#
# It exports addresses, sizes, names and call counts — FACTS about the binary,
# the same class of information as a symbol map, and what every decompilation
# project publishes.
#
# It deliberately does NOT export decompiled pseudo-C. That is a derivative of
# the game's copyrighted code, project rule 8 keeps it out of the repository,
# and README.md states publicly that no game code is here. The Ghidra project
# and any decompiler output stay under build/, which is git-ignored.
#
# Evidence without distribution: the run log and the inventory are committed,
# along with a SHA-256 of each, so anyone can re-run this and compare.
#
#   tools/ghidra-analyse.sh <binary> <name>
#
# -loader-autoloadMaps false is required, not cosmetic: with map autoloading on,
# the GameCube loader pops a GUI "load a symbol map?" dialog during import,
# which throws in headless mode and fails the run outright.
set -euo pipefail

bin="${1:?usage: $0 <binary> <name>}"
name="${2:?usage: $0 <binary> <name>}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
work="$root/build/ghidra"
eviden="$root/docs/evidence"
mkdir -p "$work" "$eviden"

flatpak run --command=bash --filesystem=home org.ghidra_sre.Ghidra -c "
  /app/lib/ghidra/support/analyzeHeadless '$work' '$name' \
    -import '$root/$bin' \
    -loader GameCubeLoader \
    -loader-autoloadMaps false \
    -processor 'PowerPC:BE:32:Gekko_Broadway' \
    -scriptPath '$root/tools/ghidra-scripts' \
    -postScript ExportEvidence.java '$eviden/$name.functions.txt' \
    -deleteProject
" 2>&1 | tee "$eviden/$name.analysis.log"

{ echo "# SHA-256 of this analysis run's outputs."
  echo "# Re-run tools/ghidra-analyse.sh and compare to verify."
  sha256sum "$root/$bin" | sed "s|$root/||"
  sha256sum "$eviden/$name.functions.txt" | sed "s|$eviden/|docs/evidence/|"
} > "$eviden/$name.sha256"
echo "wrote $eviden/$name.{functions.txt,analysis.log,sha256}"
