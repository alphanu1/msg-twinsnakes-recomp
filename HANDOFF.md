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

1. **Wire the REL into the module** (F30) — the four items above. This is what
   stands between the Konami logo and the title screen, and it is phase 1's
   remaining work.
2. **Clear the interpreter fallback** on chunk `[0x800455E0,0x800495E0)`,
   caused by SDK stub patching in the `Hu_IsStub` region (F29).
3. **Name the 266 DOL functions the REL calls but we have not named.** Of 336
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

**F25 — the symbol map feeds DolRecomp's patch table directly, and that is
the mechanism the design document describes.** `tools/make-map.py` emits a
linker MAP from `config/symbols/`; `dolrecomp --map` consumes it and emits
`generated_symbols.h`, a name-to-address table the runtime hooks to route SDK
calls to native implementations instead of translated ones.

**68 of the 69 GX functions the game uses are exposed in that table.** So the
path from symbol recovery to native SDK replacement is wired end to end, and
phase 2 has its hook points already.

`--map` is DOL input only, which suits us exactly: the REL has no names to
give it, and the SDK boundary we want to hook is entirely in the DOL (F9).

A practical benefit for phase 1: generated code and backtraces name the SDK
function rather than `fn_8003F070`, so a boot failure says where it failed.

**F26 — a bigger symbol map doubled what the classifier can see.** Re-running
`tools/classify-rel.py` against the 1,067-symbol map:

| | before | after |
|---|---|---|
| engine functions calling named SDK | 249 | **528** |
| distinct SDK functions called | 106 | **151** |
| GX surface known to be used | 69 | **81** |

**`GXCallDisplayList` is now visible**, which closes a gap that was previously
only a caveat: display lists were known to bypass the API and were therefore
unmeasurable. They are used, so the FIFO command parser is confirmed necessary
rather than merely prudent. Also newly visible: `GXLoadPosMtxImm`,
`GXSetProjection`, `GXProject`, `GXInitLightSpot`, `GXInitLightDistAttn` — the
transform and lighting pipeline.

**And a whole category appeared: 265 engine functions doing matrix maths**,
which no previous pass could see. The heaviest callers are `PSVECDotProduct`
(127 sites), `PSVECScale` (80), `PSVECSubtract` (46), `PSVECAdd` (44),
`PSMTXConcat` (16). The design document mentions optionally replacing the
paired-single matrix library with SSE/NEON; this says that is a performance
lever with real weight behind it, not a footnote.

**F27 — the game loads its REL through `OSLink`, which makes phase 1 a
wiring job rather than an unknown.** This was the open risk in the boot
estimate: Twin Snakes ships its own `rel_loader.c`, so it might have bypassed
the SDK entirely and loaded the overlay by hand, leaving nothing for a runtime
to hook.

It does not. `rel_loader.c`'s strings are in **`main.dol`**, not the REL, and
`OSLink` at `0x80020AD8` has **exactly one call site** — `0x800067C4`, inside
`fn_800066F8`, which sits beside `main` at `0x80006958` in the game's own boot
code. Named `rel_loader_LoadRel`, origin `own`: our own analysis, from the call
graph plus the `rel_loader.c` string.

**Why that matters:** `OSLink` is a standard SDK function, so ModernGekko's
existing REL machinery can intercept it and dispatch to recompiled REL code.
No custom loader to reverse and reimplement before anything boots.

*Still to wire, and these are known rather than discovered:*

- ModernGekko looks for **`files/_Main.rel`**, hardcoded — Luigi's Mansion's
  name. Ours is `files/shared/mgso_pal.rel`. A symlink, or a patch.
- The REL is optional in `game.cpp` (`if is_regular_file`), so without it the
  runtime boots `main.dol` alone and the game stops when it wants the overlay.
- `MODERNGEKKO_REQUIRED_*_SHA256` pins default to empty in CMake, so a normal
  build is not locked to Luigi's Mansion.

*Expectation for the first boot, so it is not mistaken for failure:* `main.dol`
runs `__start` → `OSInit` → DVD init → reaches `rel_loader_LoadRel` → stops for
want of the overlay. **That is the expected outcome**, and it still proves the
translation and the runtime work along the boot path. With `--map` in place the
failure will name the SDK function rather than an address.

**F28 — phase 1 boots. The game renders.** `tools/phase1-setup.sh` then
`tools/phase1-run.sh`: ModernGekko loads `gGGSPA4_recomp.so` at
`entry=0x80005240` — which is `__start` in our symbol map, so the entry point
agrees — and the game reaches the **Konami logo at roughly 43 fps**.

That is recompiled PowerPC executing as native code and driving a real
renderer. The CPU-side feasibility question is now answered in practice, not
just on paper.

**This is not our runtime.** ModernGekko is Dolphin-derived: the log shows
`radv`, Dolphin's Vulkan backend. **No SDL3 is involved yet** — SDL3 and our own
SDK shims are phase 2. Phase 1 is deliberately throwaway and exists only to
prove the translated CPU code before our own code can be blamed for anything.

**F29 — one chunk falls back to the interpreter, and it is worth fixing.**

```
[staticrecomp] SMC: chunk [0x800455E0,0x800495E0) hash mismatch;
               interpreter until next invalidation
```

Phase 1's exit criterion is *no interpreter fallback on the boot path*, so this
is exactly what phase 1 is for. The symbol map identifies the cause precisely:

- The chunk holds **46 named functions**, opening with `GXSetFogRangeAdj`,
  `GXSetBlendMode`, `GXSetColorUpdate`, `GXSetZMode` and closing with the
  `EXI2_*` debugger-mailbox functions, `AMC_IsStub` and `Hu_IsStub`.
- **All 10 of DolRecomp's SMC-flagged addresses inside it sit in the
  `Hu_IsStub` region** (`0x80048ED4`–`0x80048FE0`).

So the runtime patching is **stub replacement in the AMC/Hu area** — the SDK
overwriting its own stubs, which is legitimate and expected. The cost is that
it invalidates the whole 16 KB chunk, dragging **a dozen GX functions into the
interpreter with it** purely because they share a chunk with the patched stubs.

*Two routes, and neither needs the game changed:* finer chunk granularity so a
stub patch invalidates only its own neighbourhood, or treating the AMC/Hu stub
region as data rather than code so its hash is not covered. The second looks
cleaner and matches what the stubs actually are.

**F30 — why it stops after the Konami logo: the REL is never recompiled into
the module.** The logo is drawn by `main.dol` code. The game then calls
`rel_loader_LoadRel` → `OSLink`, and there is no recompiled REL code to reach,
so it goes no further.

The cause is upstream tooling, not our setup. The port command in the log is:

```
dolrecomp --backend=c --cpu gekko --gamecube .../sys/main.dol .../dolrecomp-output
```

**`main.dol` only.** `moderngekko-port` has no REL handling anywhere — neither
`gen_module_tables.py` nor `module_export.c` mentions RELs. The ABI defines
`rel_modules` and `num_rel_modules` in `ModernGekkoModuleDesc`, but **nothing in
the toolchain ever populates them.** The `_Main.rel` symlink got the runtime to
*hash* the overlay; it was never going to get it *compiled*.

For most GameCube games that is a minor gap. For this one it is 92% of the code.

**What integration actually requires — four things, none of them a symlink:**

1. **A symbol clash to resolve.** Both generated sets define `dolrecomp_call`,
   so the two cannot simply be linked into one module. One side needs
   namespacing, or DolRecomp needs to emit prefixed symbols.
2. **A top-level dispatch router.** Entry points differ — DOL `0x80005240`,
   REL `0x805000EC` — so dispatch must route by address: below `0x80500000`
   to the DOL table, at or above it to the REL table.
3. **REL section metadata.** `ModernGekkoRelModule` wants `module_id`,
   `version`, `section_count`, `section_info_offset`, `file_size` and a section
   array. DolRecomp's REL output emits none of it, so we parse it from the REL
   ourselves — `dtk rel info` already reads exactly these fields.
4. **An `OSLink` hook**, so linking the overlay maps recompiled code rather
   than leaving the runtime to interpret it.

**Where this work belongs: `game/` in this repository, not `extern/`.** Project
rule 4 keeps upstream unmodified, and the design document already reserves
`game/` for game-specific glue. Writing our own module glue also avoids
carrying a patch against two upstreams for the life of the project.

*Revised phase 1 estimate:* the design document allowed 1–2 weeks for phase 1.
Booting took hours; this is the rest of it, and days is realistic. Nothing here
is unknown — every piece is specified, and the REL already translates at 100%.

**F31 — the cross-module hook already exists: `DOLRECOMP_ENABLE_REPLACEMENTS`.**
Joining the two modules looked like it would need namespacing or upstream
patches. It needs neither.

Almost every shared symbol in the generated headers is `static inline`, so two
modules in one binary do not collide. The one exception is deliberate:

```c
#if defined(DOLRECOMP_ENABLE_REPLACEMENTS)
int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address);
#else
static inline int dolrecomp_dispatch_replacement(...) { return 0; }
#endif
```

and `dolrecomp_call` consults it **before** its own table. So DolRecomp already
ships the extension point, and defining that one function is the whole
integration. **Phase 2 will use the same hook** to route SDK calls to native
implementations, so this is the patch-table mechanism arriving early.

*Built in `game/module/`, not `extern/`* (rule 4):

| file | role |
|---|---|
| `dispatch.c` | `dolrecomp_dispatch_replacement`, routing by address |
| `dol_bridge.c` / `rel_bridge.c` | expose each module's table as `mgs_dol_call` / `mgs_rel_call` |
| `module_glue.h` | shared declarations, deliberately including neither `generated.h` |
| `CMakeLists.txt` | one static library per module, then link together |

*Three details that are not arbitrary:*

- **Separate translation units per module.** Both headers define
  `dolrecomp_find_original` and `dolrecomp_call_original` as `static inline`
  over *their own* chunk tables, so one file seeing both would get one table
  silently shadowing the other.
- **The bridges call `dolrecomp_call_original`, not `dolrecomp_call`** — the
  table lookup rather than the full path. Calling the latter would re-enter
  the router and recurse.
- **The REL range is bounded** to `0x805000EC + 0x456400`, its actual `.text`
  extent. An unbounded "high addresses are the REL" test would claim addresses
  the REL does not own, and a false claim returns 1, which tells the caller the
  call was handled when it was not.

*Verified before committing to the full build:* the router compiles and exports
`dolrecomp_dispatch_replacement` while importing `mgs_dol_call`/`mgs_rel_call`;
the REL bridge compiles against the REL's header, exports `mgs_rel_call`, and
resolves to the REL's `func_*` chunk symbols. The interfaces fit.

**F32 — the module descriptor now covers both modules.**
`game/module/gen_combined_tables.py` produces `module_tables.inc` over
`main.dol` *and* `mgso_pal.rel`, where upstream's `gen_module_tables.py` reads
one DOL.

```
3 code ranges (2 DOL + 1 REL), 161 smc ranges, 303 chunk ranges (hashed)
```

The REL's coverage is `0x805000EC .. 0x809564EC`, and chunk counts line up
exactly with what was compiled: **25 DOL + 278 REL = 303**, with 303 hashes.

**Validated against upstream rather than assumed:** our DOL tables are
identical to the ones `gen_module_tables.py` emits for the same input — 144
entries either way. So the generator is a faithful extension, not a
reimplementation that happens to run.

*Two things I got wrong first, both worth recording:*

- **Chunk ranges are not code ranges.** They are one per generated
  `func_XXXXXXXX` translation unit, each running to the next. My first version
  reused the code ranges and produced **3** chunks instead of 303. That is not
  cosmetic: **a chunk is the SMC demotion granule**, so a single patched
  instruction retires its whole chunk to the interpreter. Three chunks would
  have meant one patch anywhere demoting a third of the game — and it would
  still have run, just slowly, which is the kind of bug that hides.
- **`generated_smc.txt` already contains ranges**, `0xSTART-0xEND` with an
  inclusive end. I was scraping individual addresses and re-coalescing them,
  which gave 168 ranges where the correct parse gives 161.

*Why hashes cannot simply be concatenated from two runs:* each chunk's hash is
FNV-1a-64 over the **original bytes** at that address, so every range has to be
read from the file it came from. The REL maps by simple subtraction from
`0x80500000` — DolRecomp preserves file offsets, which the entry point
confirms: `.text` at file offset `0xEC`, entry reported at base + `0xEC`.

*The module target* links both static libraries under
`--whole-archive` — chunk functions are reached only through the dispatch
tables, never by direct reference, so a normal static link would discard every
one of them — plus the router, behind upstream's unmodified `module_export.c`
and version script.

**F33 — the combined module loads. Two link gaps found by running it.**
`build/phase1/module/gGGSPA4_recomp.so`, 150 MB, **25 DOL + 278 REL chunk
symbols**, router and both bridges linked, `staticrecomp_get_module` exported.

Descriptor, against the template's DOL-only module:

| | ours | template |
|---|---|---|
| abi / cpu_abi / state_size | 3 / 4 / 3536 | 3 / 4 / 3536 |
| game_id / entry | GGSPA4 / 0x80005240 | GGSPA4 / 0x80005240 |
| code ranges | **3** | 2 |
| smc ranges | **161** | 117 |
| chunk ranges | **303** | 25 |

Every ABI field matches; the coverage fields are larger because they include
the REL.

*Both failures were link-time omissions that a clean link did not catch*, and
both are recorded because the symptom pointed nowhere useful:

- **`undefined symbol: g_mem_write_journal`.** The **GXRuntime CPU sources
  belong to the module, not the host** — `cpu.c`, `cpu_exception.c`,
  `cpu_interpreter{,_table,_float,_integer}.c`. They supply the write journal,
  the interpreter used for SMC fallback, and the exception path. Nothing in the
  module references them directly, so omitting them links cleanly and fails at
  `dlopen`.
- **`undefined symbol: fmod`.** The generated float paths call libm, which is
  not implicit for a shared object here.

*The useful technique:* `moderngekko-run` reports only "native module was
rejected", which names neither symbol. A twenty-line `dlopen` harness that
calls `staticrecomp_get_module` and prints the descriptor gave the real error
immediately, and then let the descriptor be diffed field-by-field against the
template's. Worth reaching for first next time rather than reading validation
code.

**F34 — measured where it is stuck, and it is not what I assumed.**
`game/module/dispatch.c` now keeps an address histogram when
`MGS_DISPATCH_TRACE` is set; `tools/trace-report.py` resolves it against the
symbol map. One traced run answered what several rounds of reasoning had not:

```
dispatched=187,948,404   rel=0   dol=187,948,404   unclaimed=0

0x8001B640  155,459,478  82.7%  PPCHalt +0x4
0x8001FCCC    3,102,225   1.7%  OSDisableInterrupts
0x8001FCF4    3,102,218   1.7%  OSRestoreInterrupts
0x8002342C    3,004,915   1.6%  SelectThread +0x104
...                             OSYieldThread, OSSaveContext, OSLoadContext
```

**The REL is never executed — `rel=0` across 188 million dispatches** — and
`unclaimed=0` means the game never jumps outside the DOL at all. It is sitting
in the idle loop: halt, yield, reschedule, halt. A thread is waiting for work
that never completes.

So this was never a dispatch problem. The router is correct and simply never
consulted for a REL address.

**F35 — ModernGekko never loads a REL. It only hashes one.**
Every use of `metadata.main_rel` is a SHA-256 for identity and netplay
compatibility. Nothing maps it, relocates it or dispatches into it, and nothing
in the runtime reads `rel_modules` beyond validating it. **The `_Main.rel`
symlink was a red herring**, and so was populating `rel_modules`: both are
accepted and then ignored.

*The architectural problem underneath, now established rather than suspected:*
the game loads its own REL from disc through `rel_loader_LoadRel` → `OSLink`,
into guest RAM at **whatever address its allocator returns**. DolRecomp emitted
our recompiled REL at `REL_AUTO_BASE` `0x805000EC`, which is **synthetic** — a
convenience base, not where the overlay actually lives at runtime. Those two
addresses will never coincide by luck.

**What phase 1 actually needs, and it is more than wiring:**

1. Hook `OSLink` (or `rel_loader_LoadRel` at `0x800066F8`, which we named) to
   learn the **real** base address the game linked the overlay at.
2. Translate in the router: `guest_addr` → `0x805000EC + (guest_addr - real_base)`.
3. Only then does REL dispatch resolve.

*What was nonetheless gained, and it is not nothing:* the module builds, loads
and validates with both modules in it; the descriptor is correct;
`native=147M, fallback=0` says the translated DOL code runs natively with no
interpreter cost; and the dispatch trace is now a permanent instrument for
answering "where is it" in one run instead of by argument.

*The lesson worth keeping:* I reasoned about the REL path for several rounds
before measuring it. The histogram took twenty minutes to build and settled it
immediately. **Instrument first.**

**F36 — it is not blocked on the REL at all. It is blocked on the GPU FIFO.**
The `OSLink` hook was added and **never fired**: the game never reaches the
point of loading its overlay. So the REL work, while necessary, was not what
was holding it.

The trace names the real loop. My first report mislabelled the hottest entry as
`GXGetGPStatus +0x50`, because the reporter falls back to the nearest preceding
*named* symbol — `GXGetGPStatus` is `0x50` bytes and ends exactly at
`0x80040748`, so the hot address is the **next, unnamed** function.

Disassembling it settled what three reference decompilations could not (they
disagree on what follows `GXGetGPStatus`, and none has a `0x98`-byte function
there). It reads `__piReg` and `__cpReg` to recover the FIFO read and write
pointers and returns them through `r4`/`r5` — **`GXGetFifoPtrs`**, confirmed
structurally against `dolsdk2004`'s `GXFifo.c` line for line, including
`OSPhysicalToCached` compiled as `addis r0, r6, 0x8000` and the `wrPtr`/`rdPtr`
offsets at `0x18`/`0x14`. Its two compare targets are therefore the `CPUFifo`
and `GPFifo` globals. Three names added, origin `own`.

**So the loop is:** call `GXGetFifoPtrs` → check whether the graphics FIFO has
drained → `OSYieldThread` → repeat, ~1.5 million times. **The game is waiting
for the GP to consume the command FIFO, and it never does.** Everything else in
the profile — `SelectThread`, `OSSaveContext`/`OSLoadContext`,
`OSDisableInterrupts`, `PPCHalt` — is the scheduler servicing that wait.

*What this means for sequencing:* the REL is downstream of a working graphics
FIFO, not the other way round. The engine is loaded *after* the game gets its
renderer going, which is why `OSLink` is never reached. **Phase 1's remaining
problem is a GX/FIFO one**, and it sits in ModernGekko's Dolphin-derived
runtime rather than in our translated code — `fallback=0` says our code runs
natively and correctly right up to the wait.

*Next, in order:* find why the CP read pointer never advances. Either the
write-gather-pipe writes from recompiled code are not reaching Dolphin's CP
emulation, or the CP registers the game polls are not being updated. The
`hook_fb=16132` counter says SDK calls *are* reaching the host, so the HLE path
is live — it is specifically the FIFO plumbing that is not.

**F37 — the MMIO path is wired, so the FIFO stall is not a missing hook.**
`StaticRecompCore.cpp` installs `external_read`/`external_write` on the guest
CPU state, and `mem_write32` routes any non-RAM address to them. So writes to
the write-gather pipe at `0xCC008000` and reads of the CP registers at
`__cpReg` do reach Dolphin's MMIO emulation. Whatever stops the FIFO draining
is inside that emulation or in how the FIFO was configured, not a hole in the
plumbing.

**The strategic question this raises, which is worth answering deliberately.**

Phase 1 exists for one purpose, in the design document's words: to prove the
recompiled CPU code is correct *before* our own SDK shims exist to be blamed.
Measured against that, it has largely delivered:

| | |
|---|---|
| instructions translated | 1,232,487 of 1,234,136, **0 unknown** |
| executed natively in one run | **147,000,000** |
| interpreter fallback | **0** |
| decode or compile errors | **0** across 305 objects |
| self-modifying code | 124 flags, all explained as SDK cache and vector work |

The remaining blocker is **ModernGekko's GX FIFO emulation** — someone else's
Dolphin-derived runtime, which phases 2–5 replace wholesale with our own SDK on
SDL3 and Vulkan. Time spent fixing it buys a prettier phase 1 and nothing that
survives into the port.

*The counter-argument, and it is real:* a title screen under ModernGekko would
exercise the REL dispatch path end to end, which our own runtime will need too,
and the `OSLink` hook and address translation are already written and untested.
Reaching it would validate that work against a runtime we did not write —
exactly the independent check phase 1 is for.

*Recorded as a decision to take, not taken.*

**F38 — phase 2 started: the runtime is ours from here.**
Decision taken deliberately (F37): phase 1 proved what it exists to prove —
1,232,487 instructions translated with none unknown, 147M executed natively,
zero interpreter fallback — and its remaining blocker is inside ModernGekko's
FIFO emulation, which phases 2–5 delete. Everything built from now survives
into the port.

*Standing:* `runtime/` in the design document's layout, root CMake tree, tests
under `ctest`, first tests passing.

**Guest memory** (`runtime/memory/`) is the foundation everything else sits on,
so it was built carefully rather than quickly. Every accessor byte-swaps
explicitly; the cached `0x8...` and uncached `0xC...` aliases fold to the same
storage, because the game uses both for one buffer and an accessor that knew
only the cached form would fail on display lists and DMA specifically; and out
of range access returns zero rather than walking the block, since a guest
pointer is not trusted. **Nothing casts a guest structure to a host one** — a
host struct has host endianness and host padding, and the guest's does not.

*Tested first and properly, for a specific reason:* a wrong-endian access does
not crash, it returns a plausible number. That class of bug surfaces hours
later as a divergence from Dolphin rather than as a failure at the point of
the mistake.

**The patch table** (`tools/gen-patch-table.py`) is generated straight from
`config/symbols/` and is the mechanism the design document calls the single
most important decision in the architecture. It reuses the exact hook phase 1
proved: `dolrecomp_dispatch_replacement`, consulted before the translated
code's own table, so returning a native implementation means the original
never runs. Sorted and binary-searched rather than switched, because it sits in
the hot dispatch path.

Adding a name to `config/sdk-implemented.txt` without writing `mgs_<name>` is a
link error by design: the table must never claim a function the runtime cannot
service.

**First six OS shims.** `OSReport` came first, and not for convenience — the
design document's whole test strategy is diffing `OSReport` streams against
Dolphin at fixed frames, so until it is native and captured there is nothing to
compare. It walks the PowerPC EABI varargs by hand (r3 the format, r4–r10, then
the guest stack) because the arguments are in guest memory in guest byte order
and cannot be handed to the host's `vprintf`.

*A gap left visible rather than papered over:* float arguments travel in f1..
under the EABI, not the GPRs, and are not wired yet. They print as `<f>` so a
message stays readable and the gap is obvious, instead of silently printing a
wrong number.

*Time is a tick count the frame loop advances, never a host clock reading.*
That is a correctness requirement: replaying a recorded input sequence and
comparing guest memory at fixed frames only works if guest time is a function
of frames rather than of how fast the host ran.

**F39 — the cooperative scheduler, and why "exactly one at a time" is a
correctness rule rather than a simplification.**

GameCube OS threads are cooperative on a single core: a thread runs until it
blocks or yields, and nothing preempts it. The game is written against that,
so **every sequence between two yield points is atomic from its point of
view**. Run two guest threads genuinely in parallel on host threads and that
atomicity disappears — silently, as occasional corruption rather than as a
crash. So exactly one guest thread executes at any instant, and host threads
live only in the platform layer, never touching guest memory except through
queued events.

*The `OSThread` structure lives in guest memory* — the game allocates it and
reads its fields — so the runtime owns only the execution context. Every
visible field is read and written through the byte-swapping accessors at the
SDK's own offsets (`state` `0x2C8`, `priority` `0x2D0`, `suspend` `0x2CC`,
`stackBase` `0x304`, …), taken from `dolsdk2004`'s `OSThread.h`. **No host
struct is overlaid on it**: a host struct carries host endianness and host
padding, and this one was laid out by a 2003 PowerPC compiler.

*Three selection rules that are easy to get subtly wrong, so they are pinned
by tests rather than trusted to inspection:*

- **`RUNNING` counts as runnable.** The current thread is a candidate to
  continue — that is precisely what makes this cooperative rather than
  round-robin.
- **Ties leave the incumbent in place.** Priority comparison is strictly
  less-than, so two equal-priority threads do not swap on every reschedule.
  With `<=` they would, and the game's assumption that it keeps the CPU until
  it yields would break without any visible error.
- **A positive `suspend` count blocks even a `RUNNING` thread**, and threads
  are created suspended, because `OSCreateThread` does not start one.

`tests/test_os_thread.c` covers each, plus that a duplicate `OSThread*` is
refused — two slots for one guest thread would mean two host contexts for it —
and that priority round-trips through guest memory big-endian.

*Not yet built, and deliberately separated:* the context switch itself. The
selection policy is testable without it and is where the subtle bugs live;
fibers or `ucontext` come next, and the policy being already pinned means a
switching bug cannot be mistaken for a scheduling one.

**F40 — multi-core, aimed at the half of the work that can take it.**
There are two kinds of concurrency here and only one is available.

**Guest threads cannot go parallel.** Not a performance trade: the game
assumes every sequence between two yield points is atomic, because that is
what a cooperative single-core scheduler guarantees. Parallelising them breaks
an assumption already baked into the game's code, and no locking in the
runtime can restore it. The failure mode is rare corruption, not a crash
(F39).

**The runtime's own work can, and there is a lot of it.** The design document
already names audio callback, file prefetch and GPU submission. Add the two
that dominate phase 3 — **texture decoding and shader compilation** — both
pure functions over independent inputs.

`runtime/platform/jobs.c` is the pool. On this machine: **32 hardware threads,
31 workers, 31-way measured peak concurrency.**

*Three decisions worth keeping:*

- **One core is left to the guest thread**, deliberately. It is the critical
  path, it cannot be parallelised, and starving it to run background work is a
  net loss however many cores are spare.
- **A refused submission returns 0 rather than failing**, so a caller always
  has the option of running the job inline. That keeps the single-core path
  and the many-core path the same code, instead of two paths where only one
  gets exercised.
- **`wait()` means finished, not dequeued.** Workers signal completion only
  when the queue is empty *and* nothing is in flight, so a waiter cannot wake
  early and read results that are still being written. Tested.

**The rule that makes it safe: a job never touches guest memory.** It works on
host buffers, and results reach the guest through a queue the guest thread
drains at a point it chose. That is the same discipline the DVD shim needs
regardless — the SDK's contract is that a `DVDReadAsync` callback runs on the
guest thread, not on whichever host thread finished the read.

*Where this pays off, in order:* phase 2's file prefetch, phase 3's texture
decode and shader compilation (a few hundred to a few thousand shaders, by the
design document's estimate), phase 4's audio mixing.

**F41 — FST parsing, and a bug that only nesting exposed.**
`runtime/dvd/` parses the disc filesystem table and resolves paths. The format
has two traps, both taken:

- **The name offset is 24-bit**, packed into the same word as the type byte.
  Reading that word as a `be32` folds the type in and puts the name megabytes
  away.
- **Directories are ranges, not links.** A directory at index *i* owns entries
  *i+1 .. next-1*, so a lookup scans the current directory's range and skips a
  nested directory by jumping to its own `next` rather than descending. That is
  also what stops a file inside a directory being visible at the top level.

*The bug worth recording:* `name_eq` compared the entry name against the path
segment and then checked that **the path** ended there, rather than that **the
entry name** did. For a top-level file the path really does end at the segment,
so every top-level lookup passed — `demo.dat`, `stage.dat` — and every nested
one failed. A fixture with only flat files would have shown nothing.

**Validated against the real disc, not just a fixture.** The test loads
`discs/GGSPA4/disc1/sys/fst.bin` when a disc is extracted: **1,653 entries**,
and `shared/mgso_pal.rel` resolves to offset `0x3DCCE870`, length **`0x578CF4`**
— *exactly* the 5,737,716 bytes `config/GGSPA4.toml` records for the REL,
recovered here by a completely independent route. 1,653 entries of a 2004 disc
break parsers that a three-entry fixture will not.

*Design decision taken without asking, and easily revisited:* reads will come
from an **extracted folder first**, disc images after. The design document
lists both, an extracted folder is what we have and what it calls the
development path, and the FST gives the path-to-file mapping either way.

**F42 — the disc layer: one seam, two backends.**
`runtime/dvd/disc.c` mounts either an image (`.iso`/`.gcm`) or an extracted
folder, and **detects which rather than being told**, so the caller passes
whatever the user gave it. Both resolve paths through the same FST, so
`DVDOpen` never learns the difference — which is what stops the development
path and the shipped path drifting apart.

*The image backend refuses anything without the GameCube magic* (`0xC2339F3D`
at `0x1C`) rather than guessing. NKit and compressed formats fail there by
design: the design document refuses NKit as not byte-exact.

*One behaviour that is deliberate and would be easy to get wrong:* **a read
running off the end of a file is a short read, not an error.** The SDK's
`DVDRead` returns a length and the game checks it, so failing would diverge
from the hardware the game was written against. Tested at the exact boundary —
16 bytes returned from a 64-byte read at `len-16`, and 0 from a read starting
at `len`.

Verified against the real disc: mounts as `GGSPA4`, disc number 0, 1,653 FST
entries, and the REL reads back at its recorded 5,737,716 bytes with the
module id `1` in its first word.

**F43 — where the user's disc images live, and why not the program folder.**
`runtime/dvd/disc_locate.c`. The program folder is **supported but last**: the
images are the user's property and should not be tied to an install that
updates or is reinstalled under them, and the design document specifies a
launcher that asks for them and hash-checks them.

Resolution order, most explicit first: command-line argument, `$MGS_DISC1`/
`$MGS_DISC2`, a remembered path from a previous run, `discs/<id>/discN` (this
repository's layout), then beside the executable.

*Three deliberate choices:*

- **An explicit path is returned even if it does not exist.** The useful error
  is "that path did not mount", naming what the user asked for — not silently
  falling through to something they did not choose.
- **A remembered path is honoured only if it still exists**, so an image the
  user has moved does not resolve to a stale location.
- **Nothing ever searches the filesystem for a disc image.** A port that goes
  hunting for game data it was not pointed at is doing something the user did
  not ask for.

The remembered path goes in the user's config directory (XDG), never into the
install. We write the *path*; the image itself is the user's and is never
copied or moved.

*Bug found by the tests:* `mkdir` created only the last directory level, so
remembering a path failed whenever `$XDG_CONFIG_HOME` pointed somewhere whose
parent did not already exist — precisely the case the feature is for. Now
creates every missing parent.

**F44 — the phase 3 estimate should probably come down, and the reason is new
information rather than optimism.**

The design document's 2–4 months for the GX renderer was written under an
assumption that no longer holds: **a from-scratch, permissively-licensed
renderer.** Two things have changed since.

- **GPL-3 was accepted**, so Dolphin's `PixelShaderGen.cpp` and texture decoder
  are *liftable rather than reference-only*. Those are the two hardest parts of
  phase 3 — TEV semantics and the eight texture formats — and they are the
  parts the estimate was mostly made of.
- **The GX surface is measured, not guessed**: 81 functions actually called,
  against the SDK's ~200. Scope roughly halved.

*What pushes the other way, and should not be forgotten:* the feature set is
**larger** than the plan assumed — indirect texturing, hardware lighting,
render-to-texture and palettised textures are all confirmed in use, and display
lists mean the FIFO parser is required. Lifting Dolphin's code still means
adapting it to our GX state model and our Vulkan backend rather than dropping
it in.

*Not revised in the design document yet, deliberately:* an estimate should move
when there is evidence, and the honest evidence is "two of the largest cost
drivers were removed and one was added". That argues for less than 2–4 months,
but the number should come from starting the work rather than from reasoning
about it.

**F45 — the oracle is stronger than a reference: Dolphin can hand us the
renderer's input directly, and that decouples phase 3 from phase 2.**

Dolphin ships `FifoRecorder`, `FifoPlayer` and `FifoDataFile`. A recorded
`.dff` is not a video — it is **the complete input to a GX renderer**:

| | |
|---|---|
| `fifoData` | the raw FIFO command stream, per frame |
| `memoryUpdates` | textures and vertex arrays, sorted by FIFO position |
| `BPMem` | blitting processor: TEV stages, blending, alpha compare |
| `CPMem` | command processor: vertex descriptors and formats |
| `XFMem` + `XFRegs` | transform: matrices, lighting, projection |

**So the GX renderer can be built and tested with the game not running at
all.** Record a `.dff` in Dolphin at the three places phase 3's exit criterion
names — title screen, the Dock, the Heliport — then replay those streams into
our renderer and compare against Dolphin's own output of the same file.

*Why that changes the plan's shape, not just its speed:*

- **Phase 3 stops waiting on phase 2.** The design document sequences the
  renderer after the native OS/DVD/PAD work because the game has to reach the
  renderer to exercise it. With recorded FIFO, it does not.
- **The test input is frame-exact and reproducible**, so a rendering
  regression is a diff rather than an argument, and a bug reproduces on demand
  instead of "somewhere in the Heliport".
- **Comparison is pixel-level against a known-good renderer** of the same
  input — which is a far stronger check than comparing screenshots of two
  programs that each had to get to the same place first.

*Combined with the two facts in F44* — Dolphin's `PixelShaderGen.cpp` and
texture decoder being liftable under GPL-3, and the GX surface measured at 81
functions rather than ~200 — **the case for phase 3 being much shorter than
2–4 months is now three independent things, not optimism.** The design
document's estimate assumed a from-scratch renderer, developed against a game
that first had to boot, with no reproducible input.

*Concrete next step whenever phase 3 starts:* record `.dff` files from Dolphin
for the three exit-criterion scenes and commit **the tooling** to replay them —
never the recordings themselves, which contain the game's own graphics data
(rule 8).

**F46 — asynchronous DVD reads, and the rule that decides the design.**
`runtime/dvd/dvd.c`. The SDK's contract is that a `DVDReadAsync` callback runs
on the **guest** thread, not on whatever finished the read — because callbacks
touch game state, and game state has exactly one legal writer (F39/F40).

So a read is three steps across two places:

| where | step |
|---|---|
| guest thread | **submit**: allocate a host buffer, queue the job |
| worker | **read**: fill the *host* buffer. Touches no guest memory, runs no callback |
| guest thread | **drain**: copy into guest RAM, then run the callback |

**The copy into guest memory happens on the drain, not in the worker.** That
is the whole point: the middle step is where the cores go, and the other two
are serialised with everything else the guest does.

*Details that are load-bearing rather than tidy:*

- **`done` is published with a release store and read with an acquire load.**
  Without that ordering a drain could observe `done == 1` while `result` is
  still the old value — on a weakly-ordered host, or after the optimiser gets
  involved.
- **`DVD_STATE_BUSY` is written before the job is queued**, so the game can
  never poll a request that is neither busy nor finished.
- **A refused submission runs the read inline** rather than failing.
  Correctness never depended on the read being elsewhere; only throughput did.

*The test that would catch the mistake worth making:* 32 reads in flight at
once, each to its own guest destination, then every destination checked against
an independent synchronous read of the same slice. A worker writing guest
memory directly, or a shared scratch buffer, shows up there as crossed data —
and nowhere else, because a single read would look perfect either way.

**F47 — the guest-facing DVD shims, and two things reading the real header
caught.**
`runtime/dvd/dvd_shims.c` implements what translated code calls:
`DVDConvertPathToEntrynum`, `DVDOpen`, `DVDFastOpen`, `DVDClose`,
`DVDReadAsyncPrio`, `DVDGetCommandBlockStatus`. **12 SDK functions are now in
the patch table**, so their translated bodies never run.

*A bug in code I had already written and tested:* I placed the command
block's `state` at offset `0x08`. The SDK's own header puts it at **`0x0C`** —
`0x08` is `command`. Writing status there would have corrupted the command
word *and* left the game polling a state that never changed, and the tests
passed because they read back the same wrong offset they wrote. **Reading the
header, not the tests, found it.** Offsets are now named constants with that
trap called out in the comment.

*An error the generator caught:* I listed `DVDReadAsync` as implemented. It is
a **macro** in the SDK header expanding to `DVDReadAsyncPrio`, so no such
function exists to patch, and it correctly was not in the symbol map. The
generator refused to claim an address it could not find and named it. That is
the "adding a name without an implementation is a link error" property working
in the other direction — the map is right and the list was wrong.

`DVDGetFileInfoStatus` is the opposite case: a real function our alignment has
not reached. Implemented and ready, commented out of the list rather than
quietly dropped, so it goes in the moment the symbol appears.

*One design point worth recording:* `DVDReadAsync` receives a `DVDFileInfo`,
which carries a **disc offset and a length — not a name**. So the shim
resolves the offset back to an FST entry to learn which file to read. That
works because a file's offset is unique on the disc, and it avoids keeping
host-side state keyed on a guest pointer the game is free to move or reuse.

**F48 — the two-disc swap is far simpler than planned, because the engine
was measured rather than assumed.**

Phase 0 left this open: *"which SDK disc-change path it uses
(`DVDGetCurrentDiskID`, cover-status polling)"*, and the design document
budgeted for the hard answer — reporting cover-open, disc-inserted and
cover-closed on the SDK's own timing.

The call graph settles it. Of every DVD function in the binary, **the engine
calls five**, and the disc-change path is two of them:

| | call sites |
|---|---|
| `DVDGetCurrentDiskID` | 2 |
| `DVDCompareDiskID` | 2 |

**No cover polling. No `DVDGetDriveStatus`, no `DVDLowWaitCoverClose`.** The
game asks which disc is present and compares it against the one it wants. So
the swap is: *answer differently*. There is no drive-state machine to model and
no timing to match — and both would have been guesswork, since the SDK's real
timing is not documented anywhere we have.

**This closes a phase-0 open question and deletes a planned piece of work.**

*Implementation:* `DVDDiskID` is 32 bytes at the start of MEM1, where the
apploader leaves the boot header; `diskNumber` is one byte at offset 6.
`DVDCompareDiskID` compares the six identity bytes only — **`gameVersion` and
the streaming fields are deliberately excluded**, because the SDK ignores them
and a game asking for "disc 2" would otherwise be refused by a version byte it
never set. When the ids differ *only* in disc number and the other disc is
mounted, that is the swap request: satisfy it, republish the identity, answer
yes.

*Tested both ways*, since not every checkout will have disc 2 extracted:
mounted, the swap succeeds and the published identity follows it; unmounted,
it is refused and the active disc does not change. A different game is never a
match whatever the disc number.

**14 SDK functions now patched.**

**F49 — controllers, and the first SDL3 in the tree.**
`runtime/pad/pad.c` is a **pure mapping layer** — normalised axes in, guest
`PADStatus` out — and `runtime/platform/sdl_input.c` is a thin SDL3 backend
that feeds it. Split that way for one reason: **a test that needs a controller
plugged in is a test that never runs.** All the behaviour worth checking lives
in the pure layer and is tested with no hardware present.

*Three things a naive mapping gets wrong, each of which feels like a control
bug rather than a code bug:*

- **Analog triggers carry two signals.** The GameCube's L and R report an
  8-bit pressure value *and* a separate `PAD_TRIGGER_L`/`R` bit that latches
  only near full travel. Twin Snakes reads both — the analog value aims, the
  click fires. Deriving the click from "axis > 0" fires the instant the player
  begins to aim; deriving pressure from the button removes aiming entirely.
  The click point is set at 230 of 255.
- **Y is inverted between the two worlds.** SDL reports down as positive, the
  GameCube reports up as positive. Getting it wrong inverts aiming.
- **Sticks do not use the full signed range.** The SDK clamps to roughly ±72
  before a game sees anything, and games are calibrated against the clamped
  range. Feeding a full −128..127 is oversensitive in a way that reads as a
  deadzone bug rather than a scaling one. The C-stick has its own smaller
  range (±60).

Also: out-of-range input is **clamped, not wrapped** — a wrap would send a full
right deflection hard left — and axes divide by 32767 rather than 32768, so
full deflection reaches exactly 1.0 instead of stopping one unit short of the
SDK's range.

*Verified against real SDL3:* initialises, reports connected gamepads, and an
empty port returns `PAD_ERR_NO_CONTROLLER` rather than a neutral reading — the
game can tell "no controller" from "controller at rest".

**SDL3 is optional in the build.** `find_package(SDL3 QUIET)`: present, the
input backend compiles; absent, the runtime still builds and every test still
runs. The mapping layer has no SDL dependency at all.

**F50 — live controller probe, and a wrong diagnosis corrected.**
`tools/probes/pad_probe.c`, built alongside the tests but **not a test** — it
needs hardware, and a test that needs hardware does not run in CI. `--sample N`
records the extremes reached over N seconds rather than printing a live line,
because the live display overwrites itself with a carriage return and is
unreadable once piped.

*The correction, recorded because it was written down wrong first:* I
concluded from a raw `dd` read that the Razer Wolverine V2 Pro "sent zero
events at the kernel level" and that the hardware was not transmitting. **That
was wrong, and the test was at fault.** `dd bs=8 count=40` blocks until forty
events arrive and is then killed by the timeout with its buffer discarded, so
it reports nothing whether the device is silent or not.

A proper non-blocking read of `/dev/input/js1` shows the opposite: **22
synthetic init events and all 22 controls enumerated**, with axes 3 and 4 at
−32767, which is simply the two triggers at rest. The device works. Nothing
had been moved during the sampling window.

*Two things worth keeping from it:* zero is centre for a thumbstick, so an
all-zero reading at rest is correct rather than suspicious; and a resting
trigger reads full-negative on the joystick interface, which `trigger_norm`
already clamps to 0 — so the resting state maps to no pressure, as it should.

**Windows input needs no second implementation.** SDL3 is the platform layer
precisely so that `sdl_input.c` gets XInput, DirectInput and RawInput on
Windows unchanged. All the GameCube-specific behaviour — trigger click point,
inverted Y, SDK stick ranges — lives in `pad/pad.c`, which has no OS
dependency and is the only part that could be got wrong per platform.

*Build note:* `find_package(SDL3)` moved to the top-level `CMakeLists.txt`.
In a subdirectory it sets `SDL3_FOUND` only in that scope, so `tests/` could
not see it and the probe target was silently never created — no error, just a
missing binary.

*Record further findings here as they are established — including the ones that
turned out wrong. They are worth more than a clean narrative.*
