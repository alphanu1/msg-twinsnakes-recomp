#!/usr/bin/env bash
# Fetch the upstream dependencies listed in deps.lock into extern/.
#
# extern/ is git-ignored. Nothing fetched here ever enters this repository's
# history — the pins in deps.lock are the record, and THIRD_PARTY.md carries
# the licence position for each entry.
#
#   tools/bootstrap.sh              fetch the core group
#   tools/bootstrap.sh --all        core + reference (Dolphin, libogc, ww, ...)
#   tools/bootstrap.sh --update     re-fetch to the pinned commit, discarding
#                                   any local state in extern/
#
# After adding a dependency here, add it to .vscode/settings.json
# git.ignoredRepositories in the SAME change — project rule 6.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
extern="$root/extern"
lock="$root/deps.lock"
groups="core"
update=0

for arg in "$@"; do
  case "$arg" in
    --all)    groups="core reference" ;;
    --update) update=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

mkdir -p "$extern"

# Dolphin is fetched sparsely: we read it for GX and paired-single semantics,
# not to build it. A full clone is ~1 GB of history we never look at.
sparse_paths_dolphin=(
  Source/Core/VideoCommon
  Source/Core/Core/HW
  Source/Core/Core/PowerPC
  Source/Core/DiscIO
  Source/Core/AudioCommon
  Source/Core/Common
  docs
  COPYING
  license.txt
)

fetch() {
  local name="$1" commit="$2" url="$3" mode="$4"
  local dir="$extern/$name"

  if [ -d "$dir/.git" ]; then
    if [ "$update" -eq 0 ]; then
      printf '  %-28s present, skipping (use --update to re-pin)\n' "$name"
      return
    fi
    printf '  %-28s updating to %s\n' "$name" "${commit:0:8}"
  else
    printf '  %-28s cloning %s\n' "$name" "$url"
    git init -q "$dir"
    git -C "$dir" remote add origin "$url"
  fi

  if [ "$mode" = "sparse" ]; then
    git -C "$dir" config core.sparseCheckout true
    git -C "$dir" sparse-checkout init --cone 2>/dev/null || true
    if [ "$name" = "dolphin" ]; then
      git -C "$dir" sparse-checkout set "${sparse_paths_dolphin[@]}"
    fi
    git -C "$dir" fetch -q --depth 1 --filter=blob:none origin "$commit"
  else
    git -C "$dir" fetch -q --depth 1 origin "$commit"
  fi

  git -C "$dir" checkout -q --detach FETCH_HEAD
  # Read-only mirrors: never commit into them, never push from them.
  git -C "$dir" config remote.origin.pushurl "DO-NOT-PUSH"
}

echo "Fetching into $extern (groups: $groups)"
while read -r name commit url mode group; do
  case "$name" in ''|'#'*) continue ;; esac
  for g in $groups; do
    [ "$group" = "$g" ] && fetch "$name" "$commit" "$url" "$mode"
  done
done < <(grep -vE '^\s*(#|$)' "$lock")

echo
echo "Done. extern/ is git-ignored; pins are in deps.lock."
