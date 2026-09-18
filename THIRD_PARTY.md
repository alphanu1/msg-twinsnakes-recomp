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
| `libogc` | **BSD-style** (Wiedenbauer/Murphy) | reference | Public GX/OS API *shapes*. The permissive alternative to a leaked Nintendo SDK header — this is why it is here. |
| `dolphin` | **GPL-2.0+ / GPLv3-compatible in aggregate** | **liftable** | The oracle for hardware semantics *and* now a source of code: `PixelShaderGen.cpp` (TEV) and the texture decoder are the two phase-3 lifts that matter. **Sparse checkout** — VideoCommon, HW, PowerPC, DiscIO, AudioCommon, Common. 17 MB instead of ~1 GB. |
| `ww` (Wind Waker recomp) | **MIT** | reference | The shape of a true native port: own recompiler, GX→D3D11, TEV→HLSL. MIT, so lifting from it is actually permitted — the one reference here without a licence cost. |
| `RecompCore` | **GPL-2.0+/GPLv3-compatible** | liftable | Dolphin fork with static-recomp core and interpreter fallback. The fallback design can now be taken, not just read. 102 MB, the largest entry. |

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
pacman route is a full `pacman -Syu` of ~800 packages. **That is the user's
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
