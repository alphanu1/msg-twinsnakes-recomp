# Handoff

Current state, what is complete, and what is next. **Updated on every commit** —
including findings that turned out wrong, and why. A finding that lives only in
a commit message is lost.

---

## STATE, 2026-09-18: 501 SYMBOLS RECOVERED, STRUCTURE UNDERSTOOD

The repository holds the design document, the project configuration, and a
pinned dependency set. `extern/` has all ten upstreams (146 MB, git-ignored).
Nothing has been built, no disc dumped, no symbol recovered.

**The toolchain is complete and tested.** Ghidra 12.1.3 (flatpak) with the
GameCubeLoader extension built against that exact version, which also supplies
`PowerPC:BE:32:Gekko_Broadway`. Dolphin (flatpak) with `dolphin-tool` for
headless extraction. `dtk` 1.8.4 built. DolRecomp built with its LLVM 20
backend, **33/33 tests passing including `paired_single`**.

**The port is GPL-3.0** — decided 2026-09-18, and it is the biggest thing to
happen to the plan so far. See "Decisions" below.

## PHASE 0 PROGRESS — 56.5%

Regenerate with `tools/progress.py`; do not hand-maintain these numbers.

| Measure | | |
|---|---|---|
| Functions named | 866 / 18,485 | 4.7% |
| Function boundaries recovered | 18,485 / 18,485 | 100.0% |
| SDK entry points the engine calls, named | 151 / 336 | 44.9% |
| SDK call sites covered | 2,725 / 7,078 | 38.5% |
| GX surface named | 167 / 177 | **94.4%** |
| **Average of the five** | | **56.5%** |

The average is an unweighted mean of five dissimilar measures — a headline, not
a statistic. Read the rows. In particular the 3.9% and the 100% are both true
and neither is the answer: the engine is translated mechanically, so naming it
buys debugging rather than correctness, while boundaries are what the
recompiler actually consumes. **The row that governs the remaining work is the
SDK boundary at 29.5%.**

---

**The target is now PAL, GGSPA4** — retargeted 2026-09-18 because that is the
dump that exists. The design document is updated to match (rule 12).

Both discs are extracted, the SDK build is identified, and
`config/GGSPA4.toml` holds the hashes. **Nothing is blocking phase 0 any
more** — what remains is the symbol recovery itself.

### What is in the tree

| File | What it is |
|---|---|
| `twin-snakes-native-port-design.md` | The design document. Binding — see project rule 12. |
| `MILESTONES.md` | Phase order, exit criteria, per-phase checklists. |
| `HANDOFF.md` | This file. |
| `.gitignore` | Vendored repos, build output, and everything disc-derived. |
| `.vscode/settings.json` | `git.ignoredRepositories` for the `extern/` clones. Tracked. |
| `deps.lock` | The ten upstream pins. Committed; `extern/` itself is not. |
| `tools/bootstrap.sh` | Fetches them. `--all` for the reference group, `--update` to re-pin. |
| `README.md` | What this is, and the provenance statement: no leaked source, ever. |
| `docs/decompilation-process.md` | All nine stages, disc to binary, with per-stage provenance. |
| `docs/legal-position.md` | Provisions relied on, their conditions, and honest exposure. |
| `THIRD_PARTY.md` | Licence and group per dependency, and the system packages. |
| `config/GGSPA4.toml` | The target build: SDK, executable hashes, disc paths. |
| `discs/GGSPA4/disc{1,2}/` | Extracted discs, 2.5 GB. **Git-ignored**, never committed. |
| `tools/ghidra.sh` | Headless Ghidra against the flatpak install. |
| `tools/dolphin.sh` | Dolphin via flatpak: `tool` (extract/verify/header), `nogui`, `gui`. |

Nothing else exists yet: no `CMakeLists.txt`, no `runtime/`, no `config/`, no
`extern/`. The layout in the design document's "Build system and repository
layout" section is the target, not the current state.

### Decisions made so far

- **Static recompilation, not emulation and not a matching decomp.** DolRecomp
  for the CPU side, our own native runtime. Rationale in the design doc's
  Overview.
- **The translated/native boundary is the SDK's public API** — ~400 documented
  functions. This is the load-bearing decision of the architecture.
- **Not a Dolphin fork.** A native runtime is both the better port and the safer
  legal shape.
- **US release GGSEA4 is the bring-up target.** PAL and JP come later.
- **Analysis evidence is committed; decompiled pseudo-C is not.** Decided
  2026-09-18 after weighing it explicitly. `docs/evidence/` carries Ghidra's
  function inventories, the run logs and hashes — facts, and enough to
  corroborate the symbol map independently. Ghidra's reconstructed source stays
  under `build/`, git-ignored. The reason is not caution: `Sega v. Accolade`
  and `Sony v. Connectix` both turned on intermediate copies **not being
  distributed**, so the case law protects producing that output, not publishing
  it. Committing it would also add no evidential weight — anyone can re-derive
  it from their own disc — while falsifying the README's notice.
- **The README states the statutory basis in full**, not just conclusions:
  CDPA s.50B / s.50BA / s.296A, EU Directive 2009/24/EC Art. 5(3) and 6,
  17 U.S.C. §102(b) and §107, Feist, Accolade, Connectix, Google v. Oracle, and
  why §1201 is not engaged. Each is stated with how this project meets its
  conditions, because a bare assertion is worth nothing to whoever has to
  evaluate it quickly. A rights-holder contact line is included.
- **The port is GPL-3.0** (2026-09-18). Dolphin's texture decoder and
  `PixelShaderGen.cpp` are lifted rather than reimplemented — months off phase
  3, the longest phase in the plan. RecompCore's interpreter fallback and
  ModernGekko's runtime become liftable too. It costs nothing that was wanted:
  DolRecomp was already GPL-3 and permissive redistribution was never a goal.
  **This does not relax rule 8** — no game code or assets, ever. That rule is
  about copyright in the game, not about our licence.
- `.vscode/` is tracked so `git.ignoredRepositories` reaches every clone;
  `extern/` is git-ignored so vendored repos never enter this history.

---

## NEXT, IN ORDER

1. **Name the 266 DOL functions the REL calls but we have not named.** Of 336
   SDK entry points the engine calls directly, 70 are named. These 266 are the
   highest-value targets in the project — each is demonstrably used. The
   Dolphin SDK-call log is the best route.
2. **Log 60 seconds of SDK calls from Dolphin.** Still the specification for
   phase 2, and the better route to the last ~84 GX names than more alignment.
3. **Name the last ~84 GX functions.** 93 of MKDD's 177 are named; the runs
   broke on size disagreement. Phase 3 wants the full surface.
4. **Stand up the repository skeleton** — CMake tree, `tools/verify-hash`
   against `config/GGSPA4.toml`, `runtime/` and `patches/` directories.
5. **Decide where MPEG video lives.** `mpegGCN.c` and 95 MB of `movie.dat` are
   real work that no phase owns (F10).

---

## WHAT NOT TO RE-PROPOSE

- **Skipping phase 1.** Running under the Dolphin-derived runtime first is
  deliberately throwaway work, and it is what makes every phase-2 bug have one
  possible cause instead of two.
- **Hand-editing generated C.** It is regenerated from the user's disc on every
  build. A hand-written replacement goes in `patches/`, for one named function,
  only when a port feature needs it.
- **A matching decompilation.** No public decomp exists for Twin Snakes; it
  would start from zero symbols and take years. Static recompilation needs only
  function boundaries and SDK symbols.
- **Committing anything disc-derived** — DOL, RELs, generated C, extracted
  assets. This is the rule that keeps the project distributable.

---

## Findings

**F1 — "matching decomp is out of scope" reads more broadly than it means.**
That line in the design document is about the *whole binary*: recovering C that
recompiles byte-exactly, for every function. It is not a reason to avoid the
decompiler. Targeted decompilation of individual functions in Ghidra is cheap,
in scope, and should be used freely — for the SDK functions signature matching
misses, for diagnosing a divergence by reading the caller, for writing a
`patches/` replacement, and for custom DSP microcode if phase 0 finds any. The
design document now has a section on it, "Where Ghidra's decompiler earns its
place", with the anchors that make pseudo-C readable and the limits
(paired-singles decompile poorly; no struct layouts come for free).

The discipline it does *not* relax: the decompiler's output is derived from the
game's code, so it is never compiled in and never committed — notes that quote
it included. A replacement written from that understanding is our own code and
is committed. Record what a function *does*, not what Ghidra printed.

**F2 — GPL is not forced on us by DolRecomp.** *(Superseded in effect by the
GPL-3 decision of 2026-09-18 — kept because the mechanism still matters if the
licence is ever revisited, and because the header override is worth doing on its
own merits.)*
DolRecomp is **GPL-3.0**, which looked at first like it settled the design
document's biggest open question by default. It does not, for two reasons.
First, it is a *translator* that runs at build time; a GPL tool does not impose
its licence on its output, the same way GCC does not. Second — the part that
actually needed checking — the C it emits begins:

```c
#ifndef DOLRECOMP_CPU_HEADER
#define DOLRECOMP_CPU_HEADER "cpu/cpu.h"
#endif
#include DOLRECOMP_CPU_HEADER
```

The default is DolRecomp's own GPL header, which *would* be linked into the
shipped binary. But it is overridable, and upstream's own test suite overrides
it (`tests/cmake/codegen_compile.cmake` passes `DOLRECOMP_CPU_HEADER=
"host_cpu.h"`). **So: define that macro to our own `PPCContext` header and the
generated code carries no GPL.** Writing that header is a phase-1 task and it
should be done deliberately, not left on the default.

What *would* force GPL is unchanged: linking Dolphin, ModernGekko or RecompCore
code into the shipped binary. Reading them is free. ModernGekko is GPL and is
the phase-1 host — that is fine precisely because phase 1 output is never
shipped.

**F3 — the Gekko language ships inside the GameCube loader. I installed it
twice and had to undo it.** *(Recorded because the wasted step is the useful
part.)*

`extern/ghidra-gekko-broadway-lang` was installed standalone first. That took
two attempts: its README says to copy into
`Ghidra/Processors/PowerPC/data/languages`, which is **read-only under flatpak**
(`/app`), so it went to the user `Extensions/` directory instead — where its
`.slaspec` then failed to compile, because it `@include`s five stock PowerPC
`.sinc` files that were no longer beside it. The error names none of them:

```
SleighException: Errors compiling .../ppc_gekko_broadway.slaspec
```

Copying the stock `.sinc` files across fixed it, and all of that turned out to
be unnecessary. **`Ghidra-GameCube-Loader` bundles the same Gekko/Broadway
language *and* those five `.sinc` files**, correctly packaged. Installing both
gave two definitions of `PowerPC:BE:32:Gekko_Broadway` — the same duplicate-
language fault the i960 extension has. The standalone copy was removed.

**So: install `GameCubeLoader` only.** It provides both the loader and the
processor spec. `extern/ghidra-gekko-broadway-lang` stays pinned as the upstream
of that language, but is not installed.

Build the loader **inside the sandbox** — `GHIDRA_INSTALL_DIR=/app/lib/ghidra`
does not exist outside it:

```
flatpak run --command=bash --filesystem=home --share=network \
  org.ghidra_sre.Ghidra -c 'cd extern/Ghidra-GameCube-Loader && \
  ./gradlew --no-daemon -PGHIDRA_INSTALL_DIR=/app/lib/ghidra buildExtension'
```

Verified: `Using Language/Compiler: PowerPC:BE:32:Gekko_Broadway:default`,
import succeeded, no duplicate-language errors from our extensions.

**Pre-existing, not ours:** the i960 extension (Model 2's) is installed twice —
`i960/` and `ghidra_i960-master/` — which is what produces `ERROR Language
i960:LE:32:default previously defined` on every Ghidra start. Its
`Module.manifest` also has an invalid `name=i960` line; a language module's
manifest should be empty. Deleting one of the two directories fixes it. Left
alone because it belongs to another project's setup.

**F4 — Ghidra does not remove the need for Dolphin.** Worth stating because the
Gekko processor spec makes it look as though it might. Ghidra is static: it says
what the code *is*. Dolphin is dynamic: it says what the code *does*. The plan
depends on the second in four places — the 60-second SDK-call log that specifies
phase 2, the phase-1 host, the `OSReport`/memory-checksum differential harness,
and phase 3's frame-by-frame exit criterion. No amount of static analysis
produces any of them.

**F5 — DolRecomp's LLVM backend rejects the system LLVM.** It requires LLVM
**19 or 20** and hard-fails otherwise:

```cmake
if(LLVM_PACKAGE_VERSION VERSION_LESS 19 OR LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL 21)
    message(FATAL_ERROR ...)
```

This system has LLVM 22. Arch packages `llvm20` side-by-side with the default,
so it installs without disturbing anything:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDOLRECOMP_ENABLE_LLVM=ON -DLLVM_DIR=/usr/lib/llvm20/lib/cmake/llvm
```

Built clean, **33/33 tests pass** — including `paired_single`, `llvm_codegen`
and `llvm_generated_compile`. The LLVM backend is the one the design document
prefers for the multi-MB generated files, so it is worth keeping working; pin
the `LLVM_DIR` in the CMake tree rather than relying on whatever `llvm-config`
resolves to.

**F6 — this system is ~800 packages behind, so do not install Dolphin from
pacman.** `sudo pacman -S dolphin-emu` fails:

```
mbedtls3: /usr/lib/libmbedcrypto.so.16 exists in filesystem (owned by mbedtls)
```

Upstream split `mbedtls` (now 4.2.0) from a `mbedtls3` compat package, and the
installed `mbedtls` 3.6.5 owns those sonames. Installing anyway would be a
partial upgrade, which Arch does not support; the only clean pacman fix is a
full `pacman -Syu` of ~800 packages, **which is the user's call and was not
taken** — this machine also carries Quartus and FPGA toolchains that a big
upgrade could disturb.

**Dolphin is installed as a flatpak instead** (`org.DolphinEmu.dolphin-emu`,
2606a), consistent with Ghidra already being one. `dolphin-tool` inside it does
`extract`, `verify` and `header` headlessly, which is exactly what phase 0
wants. Wrapper: `tools/dolphin.sh`.

The additive packages installed fine and are not a partial upgrade — `volk`,
`llvm20`, `llvm20-libs`, `jdk21-openjdk` (the last only to build the Ghidra
extension; Ghidra itself bundles its own JDK).

**F7 — the disc contradicted the design document twice, both in our favour.**

*Audio is Ogg Vorbis, not DSP-ADPCM.* The document predicted "DSP-ADPCM is most
likely". The DOL carries the full Tremor source-file list — `floor0.c`,
`floor1.c`, `codebook.c`, `mapping0.c`, `res012.c`, `sharedbook.c`, `framing.c`
— beside the engine's own `sd_ogg.c`, `sd_stream2.c`, `sd_sound.c`. The game
decodes Vorbis **in software**, which means the translated code may simply run
as-is and phase 4 may not need a native stream decoder at all. Combined with
stock AX (no custom microcode), phase 4 is the cheap branch of the estimate on
both counts.

*Most of the code is in the overlay, not the executable.* `main.dol` is 1.9 MB;
`files/shared/mgso_pal.rel` is **5.7 MB**. Analysing only `main.dol` would miss
the majority of the game. Both must go through Ghidra, `dtk` and DolRecomp. The
engine also ships its own `rel_loader.c`, so it may not be using `OSLink`
directly — worth reading before writing the REL loader in phase 2.

*And one simplification:* the two discs carry **byte-identical** `main.dol`,
`mgso_pal.rel` and `apploader.img` (SHA-1 verified). The recompiler runs once,
not once per disc as the plan assumed; the disc swap is purely a DVD/asset
concern. The disc-number field at `boot.bin` offset 0x06 is 0x00 and 0x01, as
the document predicted.

**F8 — the target is PAL, and the dumps are NKit. Both accepted knowingly.**

*Region.* The dumps are `GGSPA4` (PAL/Europe), not the `GGSEA4` (US) build the
document specified. Retargeted to PAL on 2026-09-18 rather than waiting for a US
dump. Every address and symbol recovered from here is PAL-specific and would not
transfer to a US build; US and JP remain later additions, now from the other
direction. **PAL carries one cost the US build would not**: 50 Hz and 576i are
its native modes, which tangles with the "is the logic frame-locked at 30" and
widescreen questions. Check for a 60 Hz boot option before assuming 50 Hz timing
is what the logic runs on.

*Container.* The images are NKit-scrubbed, which the document refuses as "not
guaranteed byte-exact". Accepted because NKit rewrites junk and padding
*between* files rather than the files themselves, and because the hash check
that matters is on the **extracted executables**, not the image — which also
makes it container-independent. **This is unverified**, and it is the one open
risk in the current setup: it stays unverified until someone with a
redump-clean dump confirms `main.dol` SHA-1
`124bca49886033df5053d3be1f8748a218ddf60f`. If it ever fails to match, every
address recovered before that point is suspect.

**F9 — the DOL/REL split *is* the translated/native boundary, and that is the
most useful thing phase 0 has produced.**

| | `.text` | Share | Contents |
|---|---|---|---|
| `main.dol` | 380 KB | **7.9%** | Nintendo SDK, CodeWarrior runtime, Metrowerks TRK debugger |
| `mgso_pal.rel` | 4.3 MB | **92.1%** | The entire game engine |

The REL contains **no copy of the SDK** — no `Dolphin SDK` build strings, no
`GX*` symbol strings — so it calls into the DOL by relocation. The DOL is
therefore very nearly *the thing being replaced natively* and the REL very
nearly *the thing being translated*. DolRecomp's real work is the REL; the patch
table maps DOL addresses.

`dtk` recovered **501 symbols** from the DOL by signature matching against the
SDK `0x2301` build — OS 87, TRK 79, CodeWarrior runtime 78, DVD 21, PPC 20, GX
17, EXI 13, SI 7. Saved with addresses and sizes to
`config/symbols/main.dol.symbols.txt`.

**The gap that matters: only 17 GX functions were named, and the SDK's GX is
~200.** The rest are in the DOL and unnamed. That is the largest hole in the
symbol map and it blocks phase 3. The phase-0 Dolphin SDK-call log is probably
the fastest way to close it — a logged call with a known caller identifies its
callee.

**F10 — the engine is Konami's, not Silicon Knights'.** The design document said
"Silicon Knights' own (shared lineage with Eternal Darkness)". The REL says
otherwise: `MGS2MAIN`, `libgv_cnf.c`, `.kms` model lookups, `data.cnf`,
`gcn_dgd.c`, `GCN_prim2.c`, `gcn_spheremap.c`. Silicon Knights built the game on
**Konami's MGS2 engine**. This is better for us than the original assumption —
`libgv` and the `.kms` format have prior art in the MGS modding community,
whereas the Eternal Darkness engine has none. The SDK-boundary argument is
unaffected.

**And 95 MB of MPEG video.** `files/shared/movie.dat` carries MPEG pack,
sequence and GOP start codes, decoded by the game's own `mpegGCN.c`. The
document asked "any Bink or THP?" and the answer is neither. **This is new work
the plan did not carry**: the runtime needs an MPEG decode and presentation
path. Cheap in absolute terms — the game decodes in software, as with Vorbis —
but it is not in any phase's checklist yet.

Disc layout for reference: `demo.dat` 426 MB, `stage.dat` 199 MB,
`shared/vox.dat` 97 MB, `shared/movie.dat` 95 MB, `shared/codec.dat` 12 MB,
`shared/face.dat` 7 MB.

**F11 — Mario Kart Double Dash's decomp is our signature source, and it is an
exact SDK-build match.** This answers the GX gap from F9.

`doldecomp/mkdd` links **the same Dolphin SDK `0x2301`, with the same component
build dates** (AI/AR/ARQ/CARD/DSP all 2003-04-17, as ours). Its
`config/MarioClub_us/symbols.txt` carries **183 GX symbols with addresses and
sizes**, 177 of them functions, against our 13 named.

Verified rather than assumed: of **287 symbols named in both**, **264 have
byte-identical sizes (92%)**. Every mismatch is a C-runtime function — `exit`,
`fwrite`, `memchr`, `vprintf` — which varies with CodeWarrior options, not an
SDK function. `GXInit` is 0x798 in both; `GXInitFifoBase` 0x6C in both.

**The deltas are piecewise constant, not global.** `GXInit` through
`GXSetGPFifo` — five consecutive functions — all sit at exactly `-0x82098`, then
the offset shifts to `-0x81f74`, `-0x822a8`, `-0x82200` and so on. That is the
expected shape: each game links only the SDK functions it references, so runs
present in both stay contiguous and identically ordered while dropped functions
shift the offset. **A single constant offset would be wrong; ordered run
alignment is right.**

The recipe that follows:

1. Recover complete function boundaries for our `.text` (dtk full analysis or
   Ghidra) — we currently have boundaries only for the 501 named symbols.
2. Walk both symbol lists in address order and align runs, anchored on the
   symbols already named in both.
3. Within an aligned run, transfer names by position; confirm each with the
   size match before accepting it.

`extern/dolsdk2004` is pinned alongside as a cross-check: it carries **267 GX
function names** in source form, which is the authoritative list of what the GX
surface contains. Its `baserom/` is empty by design — the SDK `.a` files are not
distributed, so it cannot be used for direct byte-signature generation, only for
names and prototypes.

**F12 — what exists publicly for this game, and one thing to stay away from.**

*No Twin Snakes decompilation exists.* The design document's premise holds:
searching turns up no symbol map, no decomp, no reverse-engineering project for
`mgso_pal.rel`. The engine side is ours to do.

*The `.kms` model format has partial prior art* — `Jayveer/MGS-KMS-EVM-Noesis`,
a Noesis plugin reading MGS2 KMS/EVM models and MAR animations. Caveats: it
targets **PS2 MGS2**, not GameCube, its own README calls the format handling
incomplete, and it carries **no licence file**. Useful as documentation of the
format's shape; not a component to depend on, and not to be copied given the
unclear licence.

*`FoxdieTeam/mgs_reversing`* is an active MGS1 **PSX** reimplementation. Wrong
platform and wrong engine for us, but Twin Snakes is MGS1's *content* on MGS2's
*engine*, so its naming of stages and game logic may map onto ours. Worth a look
when the REL's engine functions need names, not before.

**Stay away from the leaked MGS2 source — now a hard rule and a public
statement.** Project rule 9 was rewritten to make this absolute, and
`README.md` states it publicly: the project does not use, reference or
incorporate leaked source, and everything here comes from full decompilation
and recompilation of a legally owned disc.

**The repository is currently private**, which matters for how to read all of
this. Nothing has been distributed, so the legal questions the design document's
legal section raises are not yet live — they become live at publication. That is
the moment to re-read that section and confirm the README's claims are still
true of every file. The value of keeping the provenance rule absolute *now* is
precisely that it cannot be retrofitted later: a contaminated symbol recovered
today would still be contaminated on the day the repository goes public.

The original note, for the record: MGS2's source code leaked publicly in
2026 and **Konami is actively litigating to identify the leaker**. That code is
the engine in this very game. Project rule 9 already forbids verbatim leaked SDK
source; this is the same category with far more legal heat, and touching it
would put the project in exactly the position the design document's legal
section is written to avoid. Everything we need is obtainable from clean-room
decomps and our own analysis.

**F13 — stage 5 ran; the alignment works and is self-checking.**
`dtk dol split` recovered **18,485 function boundaries** (1,818 DOL, 16,667
REL). Ordered run alignment against MKDD then assigned **350 names**: the map
went 501 → **851**, GX went 13 → **93**.

Three checks agree, which is what makes the result trustworthy:

- The two passes in `tools/align-symbols.py` — between-anchor segment matching
  and outward run extension — **produced the same names with no
  contradictions**. Segment matching found 59 segments and added nothing run
  extension had missed. Two different methods over the same data agreeing is
  the strongest evidence available without MKDD's own binary.
- Sizes match exactly by construction: a name is never written onto a function
  of a different size, and a function claimed twice with different names is
  dropped rather than guessed.
- **82% of the GX names (62 of 76) independently appear in
  `doldecomp/dolsdk2004`**, a decompilation of a different SDK release by a
  different project. The 14 that do not are all internal `__GX*` statics, where
  decomp projects legitimately differ on naming — no public API function is
  unaccounted for.

*Why the remaining 84 GX functions did not come across:* the runs break on size
disagreement, wherever the two games link different neighbouring functions.
That is the conservative behaviour working as intended — extending through a
mismatch would produce a plausible and wrong map.

**F14 — this file's NEXT section had gone stale, and that is a process
failure worth recording.** It still listed "dump both discs" and
`config/GGSEA4.toml` several commits after both were done, because successive
edits appended to it instead of rewriting it. Rule 14 says this file is updated
every commit; the lesson is that *updating* means re-reading the whole section
against reality, not adding a line to the top. A stale NEXT is worse than no
NEXT — it sends the next session to work that is already finished.

**F15 — the engine is mapped, and phase 3 is scoped to 44 GX functions.**

Boundaries: **16,667 functions** in the REL (`dtk dol split`).

*Two naming routes tried and rejected, recorded so they are not re-attempted:*
signature matching is structurally impossible — the engine appears in no other
binary, and `dtk` finds three symbols in 4.3 MB. Debug strings yield only
**~25** function names and 8 source filenames; worth harvesting once, not a
strategy.

*The route that works:* the REL calls the DOL's SDK by cross-module relocation,
and the DOL is 851 named symbols. `tools/classify-rel.py` classified **221
engine functions by the SDK they call — 177 of them renderer code**. It assigns
no names; it says where to look, which is the expensive part of reading 16,667
functions.

**The most valuable output is the GX surface.** The game calls **44 GX
functions** of the SDK's ~200 (`config/gx-surface-used.txt`). That is phase 3's
scope, measured rather than estimated.

**And it confirms the design document's biggest phase-3 risk is real.** The
document lists indirect texturing among the edge cases that make phase 3
overrun. The engine calls `GXSetTevIndirect`, `GXSetIndTexMtx`,
`GXSetIndTexOrder`, `GXSetIndTexCoordScale` and `GXSetNumIndStages`. **It is
used.** Fourteen `GXSetTev*` calls, including `GXSetTevSwapModeTable`,
`GXSetTevKColor` and `GXSetTevColorS10`, say the TEV space is used broadly.
Phase 3 should plan for indirect texturing rather than discover it.

*Known incompleteness:* direct calls only. `GXCallDisplayList` and raw FIFO
writes bypass the API, so the FIFO parser is still required and the real surface
is a superset of these 44. The Dolphin SDK-call log will settle it.

**F16 — Ghidra independently corroborates the whole symbol map.** Run via
`tools/ghidra-analyse.sh`, output in `docs/evidence/`.

| | Ghidra | `dtk` | agreement |
|---|---|---|---|
| `main.dol` functions | 1,687 | 1,818 | 92.8% |
| `mgso_pal.rel` functions | 16,323 | 16,667 | **97.9%** |

Of our 773 named symbols, Ghidra confirms **606 at the same addresses**. The
167 it does not are all *data* symbols — `__GXData`, `__PADSpec`,
`__DVDVersion` — which it correctly declines to call functions. So the two
agree wherever they are describing the same kind of thing.

The REL number is the one that matters: 4.3 MB with no symbols, no signatures
and no prior art, and two analysers sharing no code land within 2% of each
other.

*Practical trap:* `-loader-autoloadMaps false` is required. With map autoloading
on, the GameCube loader opens a GUI "load a symbol map?" dialog mid-import,
which throws headless and fails the run with a stack trace that does not say
why.

**F17 — what actually protects this work, and what a disclaimer does not do.**
Written up in `docs/legal-position.md`; the short version, because it changes
what we may commit.

*The provisions that do something:* **17 U.S.C. §102(b)** and **Feist** (facts
and methods of operation are not copyrightable — this is what makes the symbol
map and `docs/evidence/` defensible). **Sega v. Accolade** and **Sony v.
Connectix** (intermediate copying while reverse engineering is fair use).
**Google v. Oracle** (reimplementing an API is transformative — directly on
point for the SDK runtime). In the UK, **CDPA s.50B and s.50BA** give a
*statutory right* to decompile for interoperability, and **s.296A makes it
unwaivable** — stronger than US fair use, which is a defence rather than a
right.

*The limit that decides our practice:* Accolade and Connectix both turned on
the intermediate copies **not being distributed**. They protect the *act* of
decompiling, not *publishing the output*. That is the whole basis for
committing addresses and names but not reconstructed source — it is not
caution, it is the line the case law actually draws.

*s.50B's fourth condition is the one to watch:* the information must not be
used to create a "substantially similar program". A reimplemented *SDK* is not
similar to the *game*. Always argue interoperability, never substitution.

*Where we are exposed regardless, and this stays in the document:* trademark is
separate from copyright (name the game, never use logos or imply endorsement);
these provisions protect our analysis, not our users' copies; **Konami is
actively litigating** over the leaked source of this game's engine; and a DMCA
takedown does not require the sender to be right — careful projects have been
taken down anyway.

*On disclaimers:* a disclaimer creates no protection and changes no liability.
It is worth having for three narrower reasons — it records purpose
contemporaneously, and both fair use and s.50B turn on purpose; it gives
whoever reads a takedown notice the decisive facts quickly; and **it binds our
own behaviour**, since it is only worth anything while it stays true. Project
rule 16 makes that binding explicit: if a change would falsify the notice, the
change is wrong.

**F18 — gap resync: relaxing the alignment made it *more* accurate.**
The aligner stopped dead at the first size mismatch, which is why 84 GX
functions were still unnamed. It now looks ahead a bounded distance for a point
where **3 consecutive sizes agree** and resumes there. The window guard is the
whole design: a single size coincidence is common — plenty of functions are
0x20 bytes — but three in a row agreeing by chance is not.

| | before | after |
|---|---|---|
| symbols in the map | 851 | **915** |
| GX functions named | 93 | **148** |
| SDK entry points the engine calls, named | 70 | **96** |
| engine functions classified | 221 | **249** |
| engine functions identified as renderer | 177 | **203** |
| GX surface known to be used | 44 | **69** |

**The counter-intuitive part, and the reason to trust it:** a looser rule should
normally cost precision. It did the opposite. The independent `dolsdk2004`
cross-check went **82% → 85%**, and resync changed **no** previously-assigned
name — 0 changed, 1 lost, 64 added. A relaxation that *increases* agreement with
a source it has never seen is doing real work rather than guessing.

Two addresses where the alignment disagreed with an existing name were left
alone: `__OSDBINTEND` vs `__OSDBJump`, `__OSSystemCallVectorStart` vs
`SystemCallVector`. Both are label-versus-function disagreements where each name
can be legitimately true at one address. Signature matches outrank alignment, so
the existing names stand.

**F19 — the wider GX surface changes what phase 3 has to build.** Three
features are now confirmed in use rather than assumed:

- **Render-to-texture.** `GXCopyTex`, `GXSetTexCopySrc`/`Dst` — the renderer
  needs EFB-to-texture copies, not just display copies to the XFB.
- **Hardware lighting.** `GXInitLightAttn`/`Color`/`Pos`, `GXLoadLightObjImm`,
  `GXSetChanCtrl`, `GXSetNumChans`, `GXSetChanAmbColor`/`MatColor`. This is a
  GX stage distinct from TEV, with its own state model, and the design document
  does not currently budget for it.
- **Palettised textures.** `GXInitTexObjCI` alongside `GXLoadTlut` and
  `GXInitTlutObj` — TLUT handling is required, not optional.

With indirect texturing already confirmed (F15), phase 3's feature set is now
measured rather than estimated, and it is larger than the plan assumed.

**F20 — inline-assembly matching, and the four names worth not guessing.**
Parts of the SDK are written as inline asm, so the compiler emits them verbatim
and the machine code is identical across SDK revisions, link orders and games.
`tools/match-sdk-asm.py` matches full opcode sequences against `dolsdk2004`,
needing no anchors at all — a completely independent technique from stage 5.

It produced **3** names, and **refused 4**. The refusals matter more. Four of
our functions matched, in pairs: two matched `PSQUATAdd`, two matched
`PSQUATSubtract`. A 4-float add is a 4-float add, so a name matching two
addresses proves the sequence does not identify the function.

Those four are the **hottest unnamed functions in the binary** — `0x80025800`
alone has **913 call sites**, `0x80025824` has 687. Naming them on a coin-flip
would have been the most damaging single error available, and it would have
looked like progress. They stay unnamed until something disambiguates them.

Accepted, each verified instruction-for-instruction against SDK source:
`PSVECSquareMag`, `PSQUATDotProduct`, `PSVECSquareDistance`.

**F21 — how complete phase 0 is, stated four ways.** "Percent decompiled" has
no single honest answer here, because this is a recompilation and we are not
producing matching source.

| Measure | | |
|---|---|---|
| Functions named | 717 / 18,485 | **3.9%** |
| Function boundaries recovered | 18,485 / 18,485 | **100%** |
| SDK entry points the engine calls, named | 99 / 336 | **29.5%** |
| — weighted by call sites | 1,882 / 7,078 | 26.6% |

**3.9% is the least useful of these.** The engine is translated mechanically;
DolRecomp does not care what a function is called. Names in the REL buy
debugging and hand-written patches, not correctness — which is why 0% named
there is not a blocker and should not be treated as one.

**100% boundaries is what unblocks the build**, since boundaries are what the
recompiler consumes.

**29.5% of the SDK boundary is the real progress number**, and the honest read
is that phase 0 is roughly a third done by the measure that matters.

**F22 — library-locality attribution tops out at 17%. Do not retry it.**
SDK libraries link contiguously, so an unnamed function bracketed by named
functions of one library should be attributable to it.
`tools/attribute-library.py` does that, and reaches **40 of 237** unnamed SDK
entry points.

*The useful part:* **29 unnamed GX functions carrying 199 call sites** —
renderer entry points the engine demonstrably uses, which phase 3 must
implement whatever they are called.

*Why it stops there, and why tuning will not help:*

- **Libraries genuinely interleave.** `SI` sits entirely inside `OS`'s span.
  Only `TRK`, `EXI` and `SI` have ranges overlapping nothing, and they are the
  libraries that matter least.
- **Naming density is too low.** At 40% of the DOL named, consecutive named
  functions often belong to different libraries, so the bracket test refuses —
  197 of 237 landed on an apparent boundary.

*Two fixes that were necessary but not sufficient, recorded so they are not
re-derived:* `DBG` and `EXI2_` had to be split from `DB` and `EXI` (they are
the debugger mailbox library, 190 KB away), and ranges had to be trimmed so one
misfiled symbol could not stretch a library across the binary — `__DBVECTOR`
alone was swallowing DVD, VI and GX.

**The remaining 237 need dynamic information**, not more static inference. A
Dolphin SDK-call log names an entry point from the call itself rather than from
its neighbours, and phase 2 needs it regardless. That is the next move.

**F23 — three reference decomps by consensus, and an error it caught in my
own work.** MKDD is not the only 2004 GameCube game whose decomp links SDK
`0x2301`. `doldecomp/ttyd` and `doldecomp/pikmin2` do too, and each is a
*better* reference than MKDD on its own:

| reference | anchors | names |
|---|---|---|
| mkdd | 264 | 415 |
| ttyd | 273 | 496 |
| pikmin2 | 280 | 436 |

Run together, a name is accepted only by **consensus**: **445 names where two
or more independent references concur**, 122 from a single reference, and **7
conflicts discarded outright**. Where references disagree, every claim at that
address is dropped — a contradiction means at least one is wrong and nothing
available can say which.

| | before | after |
|---|---|---|
| symbols in the map | 918 | **1,067** |
| GX surface named | 148 / 177 | **167 / 177 (94.4%)** |
| SDK entry points the engine calls | 99 / 336 | **151 / 336 (44.9%)** |
| SDK call sites covered | 26.6% | **38.5%** |

**The consensus caught a wrong name I had committed.** Stage 5b named
`0x80025728` as `PSQUATDotProduct` from inline-assembly matching; consensus
said `PSVECDotProduct`. Reading the function settled it — **consensus was
right**. Ours loads at offsets `0x4` and `0x0`, overlapping, which is the
three-component dot-product trick; `PSQUATDotProduct` loads at `0` and `8` for
four components.

*The bug was mine:* `tools/match-sdk-asm.py` compared **opcode mnemonics and
ignored operands**, and those two functions have identical mnemonic sequences —
`psq_l, psq_l, ps_mul, psq_l, psq_l, ps_madd, ps_sum0`. The tool now includes
memory offsets in the signature, and with that fix it no longer claims the
match. The name is corrected in the map.

*Worth keeping in view:* the guard I wrote for this class of error (refuse when
one name matches two addresses) did not catch it, because the failure was the
other shape — one address matching two names that my signature could not
distinguish. **Two independent methods disagreeing is what caught it**, which
is the argument for keeping more than one.

*Also restored:* 8 addresses carry legitimate alias names (`__save_fpr` and
`_savefpr_14`, `_dtors` and `__destroy_global_chain_reference`). An
address-keyed rebuild silently dropped one of each; the map is keyed to keep
both.

**F24 — the whole game translates. This is the feasibility question answered.**
DolRecomp was run over both modules with `--gamecube --cpu gekko`:

| | instructions | decoded | unknown |
|---|---|---|---|
| `main.dol` | 97,240 | 95,591 (98.30%) | **0** |
| `mgso_pal.rel` | 1,136,896 | 1,136,896 (100.00%) | **0** |
| **combined** | **1,234,136** | **1,232,487 (99.87%)** | **0** |

The 1,649 not "known" in `main.dol` are **embedded data** in `.init` — constant
pools inside the text section, which the decoder correctly refuses to treat as
code. Not failures.

**Read that as translation, not decompilation.** Every instruction was
recognised and mechanically rewritten as C against a CPU-state struct — no
types, no names, no control flow, assembly wearing C syntax. Decompiled in the
matching-source sense this project is at roughly **0%, by design**: the design
document rules matching decompilation out in its second paragraph. And 99.87%
translated does not mean 99.87% finished — translated code needs a runtime
under it, and that runtime is phases 2–5, where nearly all the work remains.

**The REL decoding at 100% is the result that matters.** It is 92% of the game,
it is Konami's engine, and no prior art for it exists anywhere. It decoded
without a single unknown instruction, paired-singles included.

Output: 295 MB of C, 11.5M lines, 303 chunks. The design document budgeted
50–150 MB for a 3 MB DOL; at 4.9 MB of code this is proportionate.

*The self-modifying-code warning is benign.* DolRecomp flagged 124 addresses as
possibly patching executable memory. Cross-referenced against the symbol map:
81 are `SPEC0/1/2_MakeStatus`, the rest cache maintenance (`__flush_cache`,
`ICInvalidateRange`), exception-vector installation (`OSExceptionInit`) and DMA
register writes. Every one is an SDK routine with a legitimate reason to touch
executable memory. **No self-modifying game code**, as the design document
predicted. The symbol map turned an alarming warning into an explained list in
a single pass — the clearest payoff it has produced so far.

*Practical note for phase 1:* `ModernGekko-Template` recompiles `main.dol`
only. For this game that would leave out 92% of the code, so the REL must be
built alongside it. DolRecomp supports RELs directly (`--rel-base`, and a REL
or REL folder as input), so this is a wiring problem rather than a missing
capability.

*Record further findings here as they are established — including the ones that
turned out wrong. They are worth more than a clean narrative.*
