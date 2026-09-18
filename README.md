# Twin Snakes Native Port

A **static recompilation** of Metal Gear Solid: The Twin Snakes (GameCube, 2004)
into native Windows and Linux builds.

The game's PowerPC code is translated ahead of time into C, compiled with a
normal host compiler, and linked against a runtime that reimplements the
GameCube SDK on SDL3 and Vulkan. At runtime there is no PowerPC, no interpreter
and no JIT: the game's own logic runs as native machine code, so behaviour,
saves and physics match the original disc exactly.

**Status: phase 0 of 6** — symbol recovery. Not playable, not close. See
[MILESTONES.md](MILESTONES.md) for the plan and [HANDOFF.md](HANDOFF.md) for
where the work actually stands.

---

## Provenance

**This project does not use, reference, or incorporate any leaked source code.**

Metal Gear Solid 2's source code — the engine this game is built on — was leaked
publicly in 2026, and Konami is pursuing legal action over it. **That material
is not used here, in any form, at any point.** It has not been read, consulted,
or referred to, and it never will be. This is a standing prohibition in the
project's rules, not a preference.

The same applies to any leaked copy of Nintendo's Dolphin SDK.

Everything in this repository is derived by our own analysis of a legally owned
copy of the game, by **full decompilation and recompilation**:

1. The user's own disc is extracted, and its executables are hashed.
2. Those executables are disassembled and analysed — function boundaries,
   control flow, data-versus-code — with open tools: Ghidra, `decomp-toolkit`,
   and our own scripts.
3. Nintendo SDK functions are identified by **byte-signature matching against
   public clean-room decompilation projects**, which recover the SDK's public
   API by analysis of retail binaries. Those projects' symbol *names* are the
   community's own naming, not anyone's source code.
4. The recovered machine code is translated to C by
   [DolRecomp](https://github.com/ExpansionPak/DolRecomp), compiled, and linked
   against an SDK runtime **written from scratch** for this project.

Every symbol name, address and structure layout here was produced by that
process. Where a name comes from a public decompilation project, it is recorded
in [THIRD_PARTY.md](THIRD_PARTY.md) with its upstream and commit.

**[docs/decompilation-process.md](docs/decompilation-process.md) documents every
stage end to end** — inputs, tools, commands, outputs and how each stage's
output is verified. Every symbol in `config/symbols/` additionally carries an
origin column recording which stage produced it, so the claim above is
checkable per symbol rather than in the aggregate.

## What is not in this repository

**No game code. No game assets. Ever.**

- No `main.dol`, no `.rel`, no disc image, no extracted textures, audio or video
- No generated C — it is produced at build time from the user's own disc and is
  never committed
- No memory-card saves, no ripped data of any kind

The repository contains our own code, our own configuration, and symbol names.
You supply your own disc; the build hashes what you give it and refuses anything
that does not match.

## What you need

Two disc images of a copy of the game **you own**. GameCube discs cannot be read
by a PC drive; they are dumped on a Wii with CleanRip.

Supported: `.iso`, `.gcm`, `.rvz`, or an extracted folder.

The bring-up target is the PAL release, disc ID `GGSPA4`. US (`GGSEA4`) and
Japanese (`GGSJA4`) support comes later — every address in the project is tied
to a specific build.

## Legal notice

**Not affiliated with, endorsed by, or associated with Konami, Nintendo, or
Silicon Knights.** "Metal Gear Solid", "The Twin Snakes" and "Konami" are
trademarks of Konami Digital Entertainment; "Nintendo" and "GameCube" are
trademarks of Nintendo. They are used here only to identify the game this
software interoperates with.

**No game code or data is distributed here.** This repository contains no
executable, no assets, no decompiled source and no generated code from the
game. You must supply your own legally obtained copy; the build hashes it and
refuses anything else. Nothing here will run without it.

**This is an interoperability project.** Its purpose is to allow a game you own
to run on hardware you own, by reimplementing the console's SDK in code written
from scratch. It does not circumvent any technical protection measure, and it
does not substitute for the game.

**Nothing here derives from leaked source code.** See Provenance above. Every
symbol carries an origin recording which analysis produced it.

The reasoning behind this position, the provisions it relies on, the conditions
attached to them, and the places this project is exposed anyway, are set out in
[docs/legal-position.md](docs/legal-position.md). None of it is legal advice.

## Licence

**GPL-3.0.** The runtime reuses code from Dolphin, which is GPL, and the
recompiler is GPL-3 already. See [THIRD_PARTY.md](THIRD_PARTY.md) for the
licence position of every dependency.

## Building

```sh
tools/bootstrap.sh          # fetch pinned dependencies into extern/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Dependencies are fetched, never vendored: `extern/` is git-ignored and every
upstream is pinned by commit in [deps.lock](deps.lock).

## Documents

| | |
|---|---|
| [twin-snakes-native-port-design.md](twin-snakes-native-port-design.md) | The design. Architecture, the translated/native boundary, the phase plan. |
| [MILESTONES.md](MILESTONES.md) | Phase order, exit criteria, per-phase checklists. |
| [HANDOFF.md](HANDOFF.md) | Current state, what is next, and every finding — including the wrong ones. |
| [docs/decompilation-process.md](docs/decompilation-process.md) | Every stage from disc to binary: inputs, commands, verification, provenance. |
| [THIRD_PARTY.md](THIRD_PARTY.md) | Licence, pin and role for each dependency. |
| [docs/legal-position.md](docs/legal-position.md) | What protects this work, the conditions attached, and where it is exposed. |
