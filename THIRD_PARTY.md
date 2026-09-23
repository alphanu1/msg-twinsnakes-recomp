# Third-party dependencies

Every upstream component, its pin, its licence, and what we do with it.
`deps.lock` holds the exact commits; `tools/bootstrap.sh` fetches them into
`extern/`, which is git-ignored. **Nothing here is vendored into this
repository's history.**

Licence compatibility is checked **before** lifting code, not after
(project rule 10).

---

## The licence position: settled

**The port is GPL-3.0** (decided 2026-09-18; design document, open questions).
That resolves what was the project's biggest open question and it changes how
this table is read: the GPL references below are no longer "read but do not
touch" — **Dolphin's texture decoder and `PixelShaderGen.cpp` are lifted
directly**, which is months off phase 3.

It costs nothing that was wanted. DolRecomp is GPL-3 already, and permissive
redistribution was never a goal.

Two things still apply:

- **Per-dependency compatibility is still checked before lifting.** GPL-3 does
  not make everything compatible with everything — a permissively-licensed
  upstream stays under its own terms, and anything GPL-2-**only** would be a
  genuine conflict. Nothing here is.
- **Anything lifted gets a row recording the source commit and what changed**
  (project rule 11).

| Group | Meaning |
|---|---|
| **tool** | Runs at build time. Its licence does not reach the shipped binary, the way GCC's does not. |
| **linked** | Code that ends up in the shipped binary. |
| **liftable** | GPL reference we may now copy from, source commit recorded. |

## Fetched into `extern/`

| Name | Licence | Group | Role |
|---|---|---|---|
| `DolRecomp` | **GPL-3.0** | tool | PowerPC→C/LLVM recompiler. **Built with the LLVM 20 backend, 33/33 tests pass.** Requires LLVM 19–20 and rejects the system's LLVM 22 — configure with `-DLLVM_DIR=/usr/lib/llvm20/lib/cmake/llvm` (F5). Same licence as the port, so the `DOLRECOMP_CPU_HEADER` override (F2) is now a design choice, not a requirement. |
| `ModernGekko-Template` | **GPL-3.0-or-later** | liftable | Phase 1 host: Dolphin-derived runtime providing GX/audio/HLE. |
| `decomp-toolkit` (dtk) | **MIT OR Apache-2.0** | tool | Function-boundary analysis, DOL/REL handling. Phase 0. **Built: `dtk` 1.8.4** at `extern/decomp-toolkit/target/release/dtk`. |
| `ghidra-gekko-broadway-lang` | **Apache-2.0** | tool | Gekko/Broadway processor spec. **Pinned but NOT installed** — `Ghidra-GameCube-Loader` bundles the same language, and installing both duplicates it. Kept as the upstream of record. See F3. |
| `Ghidra-GameCube-Loader` | **Apache-2.0** | tool | DOL/REL/ISO loader **and** the Gekko/Broadway language. Built against Ghidra 12.1.3 and installed. Rebuild it inside the flatpak sandbox after any Ghidra upgrade — see `tools/ghidra.sh`. |
| `VulkanMemoryAllocator` | **MIT** | linked | GPU allocator. Not in Arch repos, so it is fetched rather than packaged. MIT, so it constrains nothing. |
| `dolsdk2004` | **no licence file** — see the note below | reference | `doldecomp/dolsdk2004` @ `2328b416`. A **decompilation** of the 2004-04-20 Dolphin SDK's library archives, by the doldecomp community. It is the reference for SDK *semantics* — what `__OSDispatchInterrupt` does after calling a handler, where `OSBootInfo` lives, which bit of `DVDCommandBlock::state` means busy. Every one of those was a boot-blocking bug this session (HANDOFF F61-F69). **Read for behaviour and for names; no code is copied from it**, and none is compiled into this project. |
| `tremor` | **BSD-3-Clause** (Xiph.Org) | reference | `xiph/tremor` @ `820fb323`. **This game embeds Tremor**, Xiph's fixed-point Vorbis decoder - the console build of libvorbis. Established from the binary itself: `main.dol` contains the strings `res012.c`, `floor0.c`, `sharedbook.c` and `framing.c`, and `res012.c` is decisive because stock libvorbis renamed that file years earlier. Used to NAME those functions, by the exact `__FILE__`/`__LINE__` pair each one compiles in (stage 5f). No code copied. |
| `ogg` | **BSD-3-Clause** (Xiph.Org) | reference | `xiph/ogg` @ `1b75110b`. The container half of the same decoder - `framing.c`, which the binary also names. Same use, same rule: names only. |
| `libogc` | **BSD-style** (Wiedenbauer/Murphy) | reference | Public GX/OS API *shapes*. The permissive alternative to a leaked Nintendo SDK header — this is why it is here. |
| `dolphin` | **GPL-2.0+ / GPLv3-compatible in aggregate** | **liftable** | The oracle for hardware semantics *and* now a source of code: `PixelShaderGen.cpp` (TEV) and the texture decoder are the two phase-3 lifts that matter. **Sparse checkout** — VideoCommon, HW, PowerPC, DiscIO, AudioCommon, Common. 17 MB instead of ~1 GB. |
| `ww` (Wind Waker recomp) | **MIT** | reference | The shape of a true native port: own recompiler, GX→D3D11, TEV→HLSL. MIT, so lifting from it is actually permitted — the one reference here without a licence cost. |
| `RecompCore` | **GPL-2.0+/GPLv3-compatible** | liftable | Dolphin fork with static-recomp core and interpreter fallback. The fallback design can now be taken, not just read. 102 MB, the largest entry. |

### Behaviour taken from Dolphin, with the commit

Rule 11 wants a commit hash, not "copied from Dolphin". These are behaviours
read out of `extern/dolphin` @ `ee018d0` and reimplemented here, rather than
files copied — but the *knowledge* is Dolphin's and is recorded as such.

| What | Where here | Dolphin source |
|---|---|---|
| The TLUT load address is 25 bits; the GameCube ignores the rest | `runtime/gx/fifo.c`, `BP_LOAD_TLUT0` | `Source/Core/VideoCommon/BPStructs.cpp`, `BPMEM_LOADTLUT1`: `addr = addr & 0x01FFFFFF` with the comment "The GameCube ignores the upper bits of this address. Some games (WW, MKDD) set them." Twin Snakes is another such game — it set them, and every CI-format texture in the movie was refused until this matched the hardware (HANDOFF F241). |
| Only the LOW half of the ARAM DMA length starts a transfer | `runtime/platform/mmio.c`, the `AR_DMA_CNT+2` block | `Source/Core/Core/HW/DSP.cpp`: `AR_DMA_CNT_H` is registered as a plain `MMIO::Utils::HighPart` write with the comment "AR_DMA_CNT_L triggers DMA", and only the `AR_DMA_CNT_L` handler calls `Do_ARAM_DMA()`. Ours ran the copy on the low half already but raised the COMPLETION interrupt on the high half, so every completion was announced before its copy and a high-half write with no transfer announced one that never happened: 147 transfers against 294 delivered interrupts (HANDOFF F261). |

### The memory card's wire protocol

`runtime/platform/exi_card.c` implements the card as an EXI device. The
protocol it answers — the command opcodes, the status bits, and the way a
read or program address is packed across four bytes — was established from
Dolphin's implementation of the same device
(`Source/Core/Core/HW/EXI/EXI_DeviceMemoryCard.cpp` and
`Source/Core/Core/HW/GCMemcard/`) at the pinned commit
`ee018d00e60b9eb727489908a8daec5c537f44a8`. Dolphin is GPL-2.0-or-later and
this project is GPL-3.0, so lifting is permitted under project rule 10;
in the event the code here is our own, written to the same hardware
behaviour, and the card's on-disc format is generated rather than copied.

The SDK-side expectations it has to satisfy — that `CARDProbeEx` reads the
size out of `id & 0xFC` and the sector size from a table indexed by
`(id & 0x3800) >> 11`, and that `__EXIProbe` debounces presence for about
300 ms — were read from `dolsdk2004`, for behaviour only, per the note
below. See HANDOFF F166 and F167.

### `mkdd` as a second naming reference

`mkdd` was pinned for its symbol maps. Stage 5l also reads its **SDK sources**
— `extern/mkdd/libs/dolphin` and `libs/PowerPC_EABI_Support` — as a second
reference for function names, source order and call structure, at
`doldecomp/mkdd` commit `ffc513c5`.

It earns its place by disagreeing usefully. Where `dolsdk2004` left a gap
ambiguous between two candidates, mkdd's ordering resolved it; and it covers
translation units `dolsdk2004` does not carry at all, including the
CodeWarrior standard library that a good deal of `main.dol` consists of. Seven
of the names in stage 5l came from it.

**Read for names, order and call structure. No code is copied from it**, none
is compiled into this project, and the same rule 9 reasoning recorded below
for `dolsdk2004` applies unchanged: it is a community decompilation produced
from shipped binaries, not a leak.

### Why `dolsdk2004` is not a rule 9 problem, and where it is still exposed

Rule 9 forbids leaked source, **including a leaked copy of Nintendo's Dolphin
SDK**, absolutely. `dolsdk2004` is not one: its own README states it is a
*decompilation* of the SDK's built library archives — "This repository does
not provide a complete copy of that version of the SDK" — produced the way
every doldecomp project is, from the shipped binaries. That is the community's
own work, which rule 9 explicitly admits, and it is the same category as the
MKDD and TTYD maps that `align-symbols.py` already consumes.

**The honest part.** It carries no licence file, so its terms are unstated, and
a decompilation of copyrighted libraries is not a settled thing in law however
it was produced. Two consequences, and they are already the practice here:

- **Nothing is copied from it.** Not a function body, not a struct definition,
  not a constant table. What is taken is *understanding* — that a register is
  at 0x4D rather than 0x4E — and the constants this project uses are written
  from the hardware's behaviour, checked against it, and then verified against
  the game's own code. Where a value here matches one there, it matches
  because both describe the same silicon.
- **Names taken from it are recorded as such.** A symbol whose only evidence
  is a name in a decomp gets that origin in `config/symbols/`, so
  `README.md`'s provenance claim stays checkable per symbol (rule 15).

If it were ever shown to contain leaked material rather than decompiled
material, the correct response is to stop using it and to re-derive anything
that rests on it — which the per-symbol origins make possible rather than
hypothetical. That is why the origins exist.

## System packages, not vendored

Already installed or available from Arch's repositories. Using the distro
package instead of a submodule keeps `extern/` to what genuinely has no package.

| Package | State | Role |
|---|---|---|
| **Ghidra 12.1.3** | **installed — flatpak** `org.ghidra_sre.Ghidra` | Phase 0. Bundles its own JDK 21. Sandbox holds `filesystems=home`, so this repository is visible to it. Drive with `tools/ghidra.sh`. |
| **Dolphin 2606a** | **installed — flatpak** `org.DolphinEmu.dolphin-emu` | Disc extraction (`dolphin-tool extract/verify/header`), SDK-call logging, the phase-1 host and the frame-by-frame oracle. Drive with `tools/dolphin.sh`. **Flatpak, not pacman** — see F6. |
| `volk` 1.4.357 | **installed** | Vulkan meta-loader |
| `llvm20` / `llvm20-libs` 20.1.8 | **installed** | DolRecomp's LLVM backend. Side-by-side with the system LLVM 22, which DolRecomp rejects (F5). |
| `jdk21-openjdk` | **installed** | Only to build the Ghidra extension with gradle. Ghidra itself bundles its own JDK. |
| `sdl3` 3.4.14, `spdlog` 1.17, `vulkan-headers` 1.4.357, `vulkan-icd-loader`, `shaderc`, `glslang`, `spirv-tools` | **installed** | Platform layer, renderer, shader compilation |
| `clang` 22 / `gcc` 16 / `cmake` 4.4 / `ninja` 1.13 / `cargo` 1.97 | **installed** | Build. Design doc prefers Clang for the multi-MB generated files. |

**Nothing further to install.** The only phase-0 blocker is a disc dump.

### Why two flatpaks instead of pacman packages

This machine is **~800 packages behind**, and `pacman -S dolphin-emu` fails on
an `mbedtls`/`mbedtls3` file conflict — the distribution split the package and
the installed 3.6.5 owns the sonames the compat package now ships. Installing
through it would be a partial upgrade, which Arch does not support; the clean
pacman route is a full `pacman -Syu` of ~800 packages. **That is your
call and was not taken**, not least because this machine carries Quartus and
FPGA toolchains that a large upgrade could disturb.

Flatpak sidesteps it entirely, and Ghidra was already installed that way. The
four pacman packages that *were* installed are purely additive — they upgrade
nothing — so they are not a partial upgrade.

---

## Rules for this file

- A dependency is added here **in the same commit** that adds it to
  `deps.lock`, with its licence and group filled in — not later.
- Anything **lifted** from a reference gets its own row recording the source
  commit and what was changed. "Copied from Dolphin" is not a record; a commit
  hash is.
- Moving a pin is its own commit, saying what moved and why.
