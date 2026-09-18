#!/usr/bin/env bash
# Assemble a game tree ModernGekko will accept, and point the template at it.
#
# Stage 8 of docs/decompilation-process.md. Two things need bridging:
#
#  1. ModernGekko looks for the overlay at files/_Main.rel, hardcoded - that is
#     Luigi's Mansion's name. Twin Snakes calls it files/shared/mgso_pal.rel.
#  2. The extracted disc under discs/ is our provenance record and is left
#     untouched; this builds a tree of symlinks beside it instead.
#
# Symlinks rather than copies: the disc is 1.2 GB and nothing here modifies it.
#
#   tools/phase1-setup.sh [disc-dir] [slug]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
disc="${1:-$root/discs/GGSPA4/disc1}"
slug="${2:-TwinSnakes-GGSPA4}"
game="$root/build/phase1/game"
tmpl="$root/extern/ModernGekko-Template"

[ -f "$disc/sys/main.dol" ] || { echo "no main.dol under $disc" >&2; exit 1; }
rel="$disc/files/shared/mgso_pal.rel"
[ -f "$rel" ] || { echo "no REL at $rel" >&2; exit 1; }

rm -rf "$game"
mkdir -p "$game/files"
ln -sfn "$disc/sys" "$game/sys"
for f in "$disc"/files/*; do
  ln -sfn "$f" "$game/files/$(basename "$f")"
done
ln -sfn "$rel" "$game/files/_Main.rel"

mkdir -p "$tmpl/extracted"
ln -sfn "$game" "$tmpl/extracted/$slug"

echo "game tree:  $game"
echo "disc id:    $(head -c 6 "$game/sys/boot.bin")"
echo "overlay:    _Main.rel -> $(basename "$rel")  $(sha1sum "$rel" | cut -c1-16)"
echo
echo "then:  make -C extern/ModernGekko-Template run GAME=$slug"
