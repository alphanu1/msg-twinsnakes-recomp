#!/usr/bin/env bash
# Run the phase 1 build under ModernGekko.
#
# Phase 1 runs on ModernGekko, which is DOLPHIN-DERIVED - Dolphin's Vulkan
# backend, Dolphin's audio, Dolphin's HLE. It is NOT our SDL3 runtime, and
# it is deliberately throwaway: it proves the recompiled CPU code is correct
# before our own SDK shims exist to be blamed for anything. SDL3 and our own
# runtime arrive in phase 2.
#
#   tools/phase1-run.sh [extra moderngekko-run args...]
#
# Useful extras:
#   --headless            no window, for log capture
#   --allow-interpreter   permit interpreter fallback rather than failing
#   --graphics <backend>  override the video backend
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
slug="${GAME:-TwinSnakes-GGSPA4}"
title="${TITLE:-MGS: Twin Snakes}"
log="$root/build/phase1/boot.log"

mkdir -p "$(dirname "$log")"
exec make -C "$root/extern/ModernGekko-Template" run \
    GAME="$slug" RUN_ARGS="--title '$title' $*"
