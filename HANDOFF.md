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

## STATE, 2026-09-19: THE GAME RUNS ITS OWN MAIN LOOP ON OUR RUNTIME

Under `host/twin-snakes`, with no emulator code linked (`ldd` shows SDL3, libc
and libm), the boot now reaches:

```
[OSReport] Retail 2
[OSReport] Memory 24 MB
[OSReport] Arena : 0x80290700 - 0x81700000
[OSReport] << Dolphin SDK - OS   release build: Jul 23 2003 (0x2301) >>
[OSReport] << Dolphin SDK - DVD  release build: Jul 23 2003 (0x2301) >>
[OSReport] << Dolphin SDK - VI   release build: Apr 17 2003 (0x2301) >>
[OSReport] << Dolphin SDK - GX   release build: Jul 23 2003 (0x2301) >>
[mgs] OSLink: overlay .text at 0x7F0080EC
```

("Dolphin" there is Nintendo's codename for the GameCube and the name of its
SDK. It is the *game* reporting what it was built against, years before the
emulator took the same name. Nothing emulator-derived is linked.)

What that means concretely: the OS is up, the scheduler is switching threads on
real interrupts delivered through the guest's own dispatcher, the game reads its
5.5 MB overlay off the disc through our DVD layer, and `OSLink` relocates it.
Between OS init and there the game renders — thousands of GX commands, its own
`GXDrawDone` answered by a PE-finish interrupt the host raises from the command
stream.

**What is NOT done:** no pixels. The host counts GX FIFO bytes and discards
them; turning them into an image is phase 3, and the Konami logo seen under
ModernGekko was drawn by Dolphin's GPU emulation, not by anything of ours.

Nine findings from this session are recorded below as **F61-F69**. The one
worth reading first is **F68**: a translation layer that fixes control flow
passes every test that only checks control flow, and hides a data bug
indefinitely.

---

## NEXT, IN ORDER

1. **Get further into the boot.** The panic is fixed (F84, F85) and file
   loading works (F87, F88): the engine opens and reads `stage.dat`,
   `dummy.tpl` and the rest. The next thing is simply to see where it stops.
2. **Name the remaining unnamed SDK entry points the engine calls** — 176 of
   336. Ordered alignment is exhausted (F72); the live routes are the call
   graph and the `__FILE__`/`__LINE__` pairs, and the biggest untapped one is
   the REL, which has 17,000 functions and its own file-name strings.
3. **Renderer gaps:** indirect textures, lighting, fog, blending, near-plane
   clipping. All configured by registers the parser already reads.
4. **Wire the REL into the module** (F30) — the four items above. This is what
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

**F51 — the game's own code now runs under our runtime, with no ModernGekko
and no Dolphin in the process.**

```
guest memory: 24 MB MEM1, 16 MB ARAM
disc 1: [GGSPA4, disc 1]   disc 2: [GGSPA4, disc 2]
worker pool: 31 threads
self-check: read 32 bytes of the overlay; first word 0x00000001
module: GGSPA4, entry 0x80005240, 303 chunks, 1 rel module
patch table: installed
stopped: pc = 0x8001C174   (__OSPSInit)
```

It ran `__start` → `__init_registers` → `__init_data` → `__init_hardware` and
got **into `OSInit`** before stopping at `__OSPSInit`, which writes HID2 to
enable paired singles. A hardware special-register write is exactly the
boundary a host without hardware should stop at, so this is the expected stop
rather than a fault.

*How the host and the module meet:* the module exports a descriptor with the
entry point and a dispatch routine, and expects a `CPUState` whose `ram` points
at guest memory. The host fills that structure **at offsets taken with
`offsetof` rather than guessed**, and guards them: the module reports its own
`cpu_state_size`, and if it does not match the 3536 bytes those offsets were
derived from, the host refuses to load rather than writing `ram` to a stale
offset and failing somewhere unrelated.

*Two link-visibility traps, both silent:*

- **The patch hook was hidden by upstream's version script.** `module.exports`
  lists exactly two global symbols; everything else is `local`. So
  `dlsym("mgs_dispatch_set_patch_hook")` returned NULL, the host fell back to
  translated SDK code, and the symptom was "the hook is never called" rather
  than "the hook was not found". We now ship our own exports file.
- The module had to be **rebuilt** after the hook was added — an older module
  loads and runs perfectly well without it, which is what phase 1 did.

*Why 0 SDK calls were served natively:* the boot stops at `__OSPSInit` before
reaching any patched function. `OSReport`, the DVD shims and the rest come
later. The next step is the `PPC*` special-register shims — `PPCMthid2`,
`PPCMtmsr` and their neighbours — which are the functions standing between here
and `OSInit` completing.

**This is the phase 1 borrowed runtime being paid back.** Nothing in this
process is Dolphin-derived: our guest memory, our disc layer, our worker pool,
our patch table, our CPU seam.

**F52 — there is a window, and it shows real state from the first frame.**
`runtime/platform/sdl_video.c` plus a boot overlay in the host. On screen now:
MEM1/ARAM sizes, disc 1 with its 1,653 FST entries, disc 2 mounted, 31 worker
threads, the DVD self-check, the module's id and entry point, 303 chunks with
1 REL module, the patch table installed, and where execution stopped.

*Opened early on purpose, and the reasoning is borrowed from the Model 2
rules:* **the screen is the only output channel that survives into a shipped
build.** A terminal is available now and will not be later, and a boot that
fails in front of a black window tells you nothing. So the window presents a
framebuffer from frame one and shows real state until there is game output to
replace it.

*Three decisions in the video layer:*

- **The framebuffer is the GameCube's own XFB geometry, 640×480**, and the
  window scales it. That keeps the eventual GX path honest — whatever the
  renderer produces lands in exactly this buffer, at exactly this size, rather
  than in something shaped for convenience now.
- **Nearest-neighbour scaling, not linear.** This is a 640×480 image on a 4K
  display and the pixels are the point; smoothing them is a decision for a
  later upscaling pass, not a default that quietly hides what was drawn.
- **Hex digits and short labels, not blocks.** Also from the Model 2 rules,
  and immediately worth it: `STOPPED AT PC 0x8001C174` is actionable where a
  coloured square is not.

**Nothing Dolphin-derived is in this process.** Our guest memory, disc layer,
worker pool, patch table, CPU seam, window and font.

**F53 — the boot went from 2 steps to 12,553, and every step of that was a
distinct bug in the host rather than in the translation.**
Worth recording individually, because each one presented as something other
than what it was.

**1. The host called `dispatch` once.** Translated code returns to the host at
chunk boundaries, when its cycle budget expires, and at addresses its tables do
not cover — so the host must re-enter at the current `pc`, repeatedly. One call
executes a few instructions and returns, which looks exactly like *"the game
stopped here"*. It reported a stall at `__OSPSInit`'s entry that was really a
missing loop.

**2. `mfhid0` is not translated.** DolRecomp emits
`ppc_fallback_instruction` for SPR access and leaves it to the host — which is
the right division, since an SPR means whatever the machine underneath says it
means. Without a handler the default is an illegal-instruction exception, two
instructions into `ICFlashInvalidate`. `cpu->instruction_fallback` is the
provided seam; `host/spr.c` fills it.

*And it catches what the patch table structurally cannot:* a patched SDK
function is only intercepted when reached **through dispatch**. A call inside
the same generated chunk compiles to a plain `goto` and never consults the
hook — which is why patching `ICFlashInvalidate` alone changed nothing.
`__OSPSInit` calls it that way.

**3. A native replacement must return.** It stands in for a function ending in
`blr`, so the host has to set `pc = lr` after it runs. Without that the pc
never moves and the host re-dispatches forever — indistinguishable from a
spinning guest, and it appeared *after 1,002 otherwise correct native calls*.

**4. A handled instruction must advance the pc.** Same shape one level down:
performing the effect is not completing the instruction. The translated code
returns with `pc` still on it.

**5. Nobody had loaded the DOL.** The host allocated 24 MB of zeros and jumped
to the entry point, so the right code ran against the wrong memory. **The
failure was silent**, because an unloaded `.sdata` reads as zeros and zero is a
plausible value for almost anything: `VIGetTvFormat` read the TV mode through
r13, got zero, and branched through a null pointer. `runtime/dvd/dol.c` now
does what the apploader does — 8 sections, 1,987,872 bytes, `.bss` cleared at
`0x801E7DC0+615068`, entry `0x80005240`, all matching what `dtk` reports.

**6. A repeated pc is not a spin.** A loop re-enters at the same head every
time its cycle budget expires, so `__fill_mem` clearing 615 KB of `.bss`
tripped a hang detector thousands of times while making perfect progress.

*Where it is now:* **12,553 steps, 143 SDK calls served natively, 169 host
instructions handled, none unhandled — and `OSReport` firing, so the game is
printing through our implementation.** It stops in `__OSThreadInit +0x80`,
inside `OSInit`, branching through a null where the first thread context
should be. That is the next piece: the scheduler has a selection policy
(F39) but no context switch yet.

**F54 — the quietest bug yet: clearing `.bss` wiped `.sdata`.**

A DOL header's bss entry is one address and length covering everything the
linker left out of the file — and on this game **it spans `.sdata`**. bss runs
`0x801E7DC0 + 615068`, ending at `0x8027DFC4`; `.sdata` sits at `0x8027D980`,
in the middle of it. Our loader copied the sections and *then* cleared bss,
erasing initialised data it had just written.

**The symptom was as quiet as it gets.** Every small-data-area read returned
zero, and zero is a plausible value everywhere. It surfaced as `__OSThreadInit`
branching through a null `SwitchThreadCallback` — a function pointer whose
correct value, `0x80022E7C` (`DefaultSwitchThreadCallback`), was sitting in the
DOL the entire time.

*What made it findable:* dumping r13 at the stop and checking it against the
SDA convention. r13 was **correct** — `0x80285980`, which is `.sdata + 0x8000`,
exactly where PowerPC puts the small-data base so signed 16-bit offsets reach
the whole area. A correct base reading zeros means the memory is wrong, not the
register, and that points at the loader rather than the translation.

**Clear bss first, then load sections.** The order is the fix.

**The result, and it closes a loop:**

```
[OSReport] << Dolphin SDK - EXI  release build: Apr 17 2003 12:33:17 (0x2301) >>
[OSReport] << Dolphin SDK - SI   release build: Apr 17 2003 12:33:19 (0x2301) >>
```

**The game prints its own SDK version banners through our native `OSReport`.**
Phase 0 identified SDK `0x2301` by reading strings out of the binary; the
running game now reports the same build itself, through our implementation of
the function it uses to say so.

*Where it stops:* **14,940 steps**, at `pc = 0x00000C00` — the PowerPC **system
call vector**. The guest executed `sc`. Exception handlers are copied into low
memory by the OS at runtime, so they are in no generated chunk, which is why
dispatch has no code for the address. That is the next piece, and it is a
different kind of problem from the six before it: not a host bug, but code the
game creates while running.

**F55 — exception vectors are serviced by the host, and the boot stopped
faulting.**

`DCFlushRange` ends in `sc`: the SDK uses a system call as a completion
barrier. The guest took it, landed on the system-call vector at `0xC00`, and
dispatch had no code there — correctly, because **exception handlers are copied
into low memory by the OS at runtime and exist in no generated chunk.**

*Why patching `DCFlushRange` did not help, and would not have:* it is called
from `__OSInitAudioSystem` **inside the same generated chunk**, which compiles
to a plain `goto`. The patch table is only consulted through dispatch. This is
the second time that structural limit has bitten (F53 item 2), and it is worth
stating as a rule: **the patch table cannot intercept intra-chunk calls.**
Anything that must work regardless of how it was reached belongs at the
instruction or exception level, not in the table.

*What the host does now:* a pc on a vector is serviced rather than treated as a
gap. A **system call resumes at `srr0`** — which is what the SDK's own handler
does, and honest here because this host has no supervisor mode, so the state
transition an exception performs on hardware has no counterpart. **Program,
DSI, alignment and FP-unavailable still stop**, because those are faults rather
than barriers and resuming would hide the cause and fail somewhere unrelated.

**Result: 2,000,000 steps with no fault** — it now runs until the host's own
step limit rather than crashing, with 143 SDK calls served natively, 173 host
instructions, 1 system call serviced, and nothing unhandled.

*Where it is:* `__OSInitAudioSystem +0xD0`, polling **DSP registers at
`0xCC005000`**. That is memory-mapped I/O, and the host implements none: the
`CPUState` has `external_read`/`external_write` hooks that nothing has filled,
so every hardware register reads as zero. The game is no longer crashing — it
is waiting for hardware that does not exist yet.

**That is the next layer, and it is a different kind of work:** VI, PI, DSP and
the CP registers are what the remaining OS init, the frame loop and eventually
GX all talk to.

**F56 — a memory-mapped I/O layer, and the pattern that runs through it.**
`runtime/platform/mmio.c` services everything at `0xCC000000` through the
`external_read`/`external_write` hooks DolRecomp routes non-RAM access to.
Nothing had filled them, so every hardware register read as zero.

*Unmodelled registers read back their last written value*, which is a better
default than zero: a great deal of SDK code writes a register and reads it back
to confirm, and honouring that removes a whole class of false stalls for free.

**The pattern that keeps recurring: set-a-bit-and-wait.** Software sets a start
bit, hardware clears it — or sets a completion flag — when the operation
finishes. A register that merely remembers what was written never completes
anything, and the SDK spins. Three of these were found by walking into them one
at a time:

| register | bit | what spun on it |
|---|---|---|
| `EXIxCR` | TSTART | `EXISync` |
| `DSPCR` | RES | `__OSInitAudioSystem`'s reset wait |
| `AR_DMA_CNT` → `DSPCR` ARINT | completion | the ARAM DMA wait |

Completing these instantly is honest rather than a shortcut: there is no
physical device on the other end and no bus latency to reproduce. ARAM in
particular is a real 16 MB store in this runtime, so a DMA has genuinely
already happened by the time the guest looks.

*One register must MOVE rather than merely be plausible:* the video
interface's half-line counter, which the SDK polls to wait for retrace. It is
advanced by the host's own cadence rather than by reads, so a guest polling it
sees time pass at the rate the host runs — not as fast as it can spin.

**F57 — where the boot is actually blocked now, and it is not MMIO.**

With MMIO in, the boot runs **40,000,000 steps without faulting**, serving
**13,328,496 SDK calls natively**, with 173 host instructions and nothing
unhandled. It sits in `__OSInitAudioSystem +0xE8`.

*The counter that identified the real problem:* **32 MMIO reads** across those
40 million steps. A loop polling a hardware register would have produced
millions. So the loop is **not** reading hardware — it is polling a location in
RAM, waiting for a value that only an **interrupt handler** would write.

**This host delivers no interrupts.** That is the gap: the SDK's boot sets
hardware in motion and then waits for the completion interrupt to run a handler
that updates memory. Servicing the register is not enough; something has to
deliver the interrupt.

*What that needs, and it is well defined:* the host's frame loop raises VI
retrace, DSP and DVD interrupts on a cadence; `__OSInterruptInit`'s registered
handlers are invoked through the same dispatch path everything else uses; and
`OSDisableInterrupts`/`OSRestoreInterrupts` — already native — gate delivery so
the guest's critical sections still mean something.

**That is the next piece, and it is the last structural one before the frame
loop.** Everything after it — VI presenting an XFB, the game reaching its main
loop — depends on interrupts existing.

**F58 — interrupt delivery, built so the guest keeps deciding.**
`host/interrupt.c`. The host raises an interrupt by setting a cause bit in the
processor interface and calling **`__OSDispatchInterrupt`** — the guest's own
dispatcher. It does not reimplement the SDK's handling, reach into the handler
table, or know which handler is registered. The guest decides all of that,
exactly as it would on hardware.

*That needed a new primitive:* `mgs_module_call_guest` — enter the guest, run a
function to completion, return with every other register and the interrupted pc
untouched. The return is detected with a **sentinel link register**: a real
`blr` jumps to whatever `lr` held, so setting it to an address outside every
code range makes the function's own return land somewhere recognisable, and the
run loop can stop without understanding the callee at all. Registers are saved
and restored because the interrupted code is entitled to find them as it left
them; not doing so corrupts whatever was running, intermittently and far from
the cause.

*The mask is honoured, and the first run proved it:* **20,000 retrace
interrupts raised, 20,000 refused** — because the guest had not unmasked VI
yet. That is correct, not a failure. Delivering anyway would break precisely
the atomicity the cooperative scheduler exists to preserve (F39).

**F59 — guest time never advanced, and 13 million `OSGetTick` calls said so.**

`OSGetTime`/`OSGetTick` read `rt->ticks`, and **nothing incremented it.** The
design note said "a tick count the frame loop advances"; the frame loop did
not. Every timed wait in the SDK therefore spun forever.

*What made it findable was a counter reading wrong rather than a crash:*
**13,328,496 SDK calls served natively** in 40 million steps — roughly one call
every three instructions. That is not a program making progress; that is a
timed wait polling a clock. Combined with `r31 = 0xCC00500A` showing the
hardware register was the right one, the remaining suspect was time itself.

The run loop now advances the timebase, 32 ticks per step against the Gekko's
40.5 MHz — the 162 MHz bus divided by four. Driven from the loop rather than
the host clock, because a replayed run has to be reproducible.

*Effect, immediately:* the wait completes, the boot moves on to
`__OSInitAudioSystem +0x154`, and MMIO reads go from 32 to **2,001,047** — it
is now genuinely talking to hardware rather than waiting on a clock that never
ticked.

**F60 — the intra-chunk limit cannot be flagged away, and audio init is a
genuine tunnel.**

*Two routes tried and closed, recorded so they are not re-attempted:*

- **`--partition-instructions` does not help.** It sizes the LLVM backend's
  object partitions, not the C backend's chunk splitting — the DOL still
  emits 25 chunks at any value. So making SDK calls cross-chunk, and therefore
  visible to the patch table, is not available as a build flag.
- **Patching the loaded image does not help either.** For a *static*
  recompilation the instructions are already compiled into the module;
  rewriting guest memory changes what the game reads, not what executes. That
  technique belongs to emulators and does not transfer.

*Where the boot sits:* `__OSInitAudioSystem +0x134`, waiting on DSPCR bit
`0x400`. The guest **clears that bit itself and then waits for hardware to
raise it asynchronously**, so no amount of care on the write side completes it:
there is no DSP here to raise it. Making reads report the completion flags as
set was tried and is retained as a documented stand-in, but it is not
sufficient on its own.

**The judgement call, stated plainly:** audio is **phase 4**, and nothing
between here and a picture on screen needs the DSP to behave like a
coprocessor. Continuing to model it now is work against the wrong phase. The
options worth weighing next time this is picked up:

1. **Model the DSP mailbox properly** — honest, and phase 4 needs it anyway,
   but it is phase 4's work being done early.
2. **Get the patch table to see intra-chunk calls** — the general fix, and it
   would unblock every future SDK function, not just this one. Likely needs a
   DolRecomp change: emitting a dispatch call rather than a `goto` for
   addresses named in the `--map`.
3. **Compare against Dolphin** — it boots this game, so its DSP register
   behaviour at exactly this point is observable rather than guessable. The
   oracle argument (F45) applies to hardware behaviour just as much as to
   rendering.

**Option 2 is the one with leverage.** The intra-chunk limit has now blocked
three separate things — `ICFlashInvalidate`, `DCFlushRange`, and
`__OSInitAudioSystem` — and every future SDK shim inherits it. Fixing it once
is worth more than working around it a fourth time.

**F61 — the FP-unavailable exception was not a fault. It was the SDK working.**
The boot stopped at `pc = 0x800` after 708,106 steps, which reads like a
floating-point fault and is not one. The Dolphin SDK does not save 32 FPRs on
every thread switch: it restores a context with `MSR[FP]` **clear** and installs
`OSSwitchFPUContext` on the FP-unavailable vector, so the first float a thread
executes traps there, the handler swaps FPU ownership between OSContexts and
resumes with FP enabled. A thread that never touches floating point never pays.

The vector's code is *copied into low memory at runtime* by `__OSExceptionInit`,
so no translated chunk contains it and dispatch legitimately has nothing at
0x800. The host now performs `OSSwitchFPUContext` against its own register
file: same ownership word at `0x800000D8`, same `OS_CONTEXT_STATE_FPSAVED` gate
on the load, same OSContext offsets. The guest cannot tell by inspecting memory.

**What was wrong for three attempts:** the step count stayed at exactly 708,106
across an MSR-init change, an `mtmsr`/`mfmsr`/`rfi` change and an srr1-restore
change. Identical step counts across three different fixes is a message: none
of them were on the path. The actual evidence was one line away — printing
`cpu->exception` and `srr1` showed `msr = 0x00000032`, FP clear, which is what
the SDK *intends*.

**F62 — the SPR shadow table was invisible to the generated code.**
`host/spr.c` kept SPR writes in its own array. That is fine for registers only
the guest reads back, and wrong for the ones the *translated code* consults:

- `HID2[PSE]` gates every paired-single instruction. Without it the first
  `psq_st` in `PSMTXIdentity` raised an illegal-instruction program exception —
  exactly what real hardware does.
- `GQR0-7` carry the scale and type a `psq_l`/`psq_st` quantises with. A stale
  GQR does not fault; it silently returns wrongly-scaled numbers, which is worse.
- `SRR0`/`SRR1` are the pair `rfi` consumes. `OSLoadContext` ends with
  `mtsrr0`/`mtsrr1`/`rfi`, so a shadow copy would have every context switch
  resume wherever the host last wrote, not where the scheduler chose.

Those four are now mirrored into the CPU state in both directions.

**F63 — `__OSDispatchInterrupt` does not return, so calling it could not work.**
It ends in `OSLoadContext`, an `rfi` — the thread it resumes may not be the one
that was interrupted. The host had been invoking it with a return sentinel and
waiting. That is not a shortcut that mostly works; it cannot work, and it
presented as 1000 interrupts "failing" after burning 200,000 steps each.

Replaced with the state transition the hardware performs: spill the register
file into the current thread's OSContext, set `OS_CONTEXT_STATE_EXC`, put the
exception number and context in r3/r4, clear `MSR[EE]` and `MSR[FP]`, and set
the pc. Then **return to the main loop**, which keeps stepping inside the
handler like any other guest code. Resuming is the guest's business, via its
own `rfi`.

**F64 — the run loop read `pc` before raising the interrupt.**
One line, and it cost the entire boot. `mgs_module_run` captured `pc` at the top
of the iteration, then raised the retrace interrupt (moving the pc to the
dispatcher), then dispatched the **stale** value — re-entering the interrupted
code with the exception's MSR still in force. `MSR[EE]` therefore stayed clear
for ever and exactly one interrupt was delivered in a 40-million-step run.

Anything that can move the pc now runs *before* pc is read. With that fixed the
game went from idle to alive in one step: the GX banner printed, 19,645 retraces
were delivered, and the game issued its first real graphics commands.

**F65 — "interrupts disabled" was two variables that nothing kept in step.**
`mgs_OSDisableInterrupts` maintained `rt->interrupts_enabled`; the host's
"may I deliver an interrupt?" gate read `MSR[EE]`. The guest could enable
interrupts and the host would never notice — 1,355 retraces refused while the
scheduler idled. The shims now act on `MSR[EE]` itself, which is what the SDK's
own versions do (`mfmsr`, clear or set the bit, `mtmsr`). One bit, one meaning.

**F66 — PI's interrupt status is a mirror of device lines, not a latch.**
The host set PI's VI bit when it raised a retrace and never cleared it. A device
drops its line when the guest acknowledges *at the device*: the VI handler
clears the display-interrupt INT bits, the PE finish handler writes bit 3 of the
pixel engine's control register. With VI permanently asserted, and VI outranking
the pixel engine in the SDK's priority table, the PE finish interrupt was never
the highest-priority pending source — so `GXDrawDone` slept for ever while the
retrace handler kept running perfectly. That looked like a healthy idle loop and
was not.

Both linkages are now modelled. `GXDrawDone` also needed the draw-done token
recognised in the FIFO byte stream (BP opcode `0x61`, register `0x45`, bit 1) so
the host can answer it: a graphics processor that completes instantly is still a
graphics processor that completes.

**F67 — a wrong sign in one constant reset the whole game.**
`DVD_STATE_BUSY` was defined as `-1`. The SDK's value is `1`; `-1` is
`DVD_STATE_FATAL_ERROR`. `DVDReadPrio` treats 0 as done, `-1` as fatal and 10 as
cancelled, and sleeps on anything else — so the very first synchronous read
reported a fatal error *before the drive had been asked*, returned `-1` without
waiting, and the game copied an empty buffer over its module. `OSLink` then read
a header of zeros, the link failed, and the game reset itself: `GXAbortFrame`,
arena reset, jump to address zero.

Five separate wrong answers, one wrong sign. The full state table is now copied
verbatim from the SDK header with a comment saying why guessing a sign here is
not a small error.

**Also wrong, found on the way:** the host→guest return sentinel was
`0x0DEADBEE`, which is not 4-byte aligned. `blr` ignores the low two bits, so a
callback ran correctly, returned correctly, and the host then waited for an
address that cannot occur. It reported "gave up at 0x0DEADBEC" — two less than
the sentinel, which *is* the masking, stated plainly in the output.

**F68 — the game's module lives in the second addressable window, and its
recompiled twin has to live at the same address.**
Twin Snakes' loader copies its 5.5 MB overlay to a hard-coded `0x7F008000` and
links it there. Two consequences, and the second is the deeper one:

1. **The window has to exist.** `0x7E000000-0x7FFFFFFF` is not MEM1 and not a
   mirror of it; Dolphin calls it "fake VMEM" and provides 32 MB there for every
   GameCube title, which is why phase 1 — on a Dolphin-derived runtime — never
   noticed it was needed. With nothing mapped, the copy went nowhere.
2. **The recompiled overlay's base is not ours to pick.** `--rel-base` fixes
   every absolute address in the generated code. We had used `0x80500000`, an
   arbitrary choice, and taught dispatch to translate call targets between the
   two. That makes *branches* work and leaves every **load and store** reading
   from an address 0x014F8000 away from where the data is. Nothing translates a
   load. The overlay is now recompiled at `0x7F008000`, so guest and recompiled
   addresses are the same address.

**The lesson worth keeping:** a translation layer that fixes control flow will
pass every test that only checks control flow, and hide a data bug indefinitely.

**F69 — the boot ROM's legacy is not optional.**
The host never wrote the low-memory globals the IPL and apploader leave behind,
and zero is a *valid-looking* answer to every question the SDK asks of them.
`memorySize` 0 printed "Memory 0 MB"; `consoleType` 0 made `OSGetConsoleType`
return its unknown-board value `0x10000002`, which reports as "Development
HW-1"; the TV mode defaulted to NTSC on a PAL disc. The game believed it was on
a debug board. `runtime/os/boot_info.c` now writes the disc ID, the boot magic,
24 MB, retail hardware, the 162/486 MHz clocks and the TV standard implied by
the game ID's region letter.

**F70 — the renderer's first half exists, and it is tested without the game.**
`runtime/gx/` now holds a FIFO parser, a vertex decoder, transform/projection/
viewport, and a scanline rasteriser with a depth buffer; `runtime/gx/efb.c`
holds the embedded framebuffer and the copy out to the external one.
`tests/test_gx.c` drives raw command bytes through all of it and checks a known
pixel; `tests/test_efb.c` checks the copy and the colour conversion both ways.

**Why test it without the game.** The failures that matter here are invisible
in a running game. A vertex size computed one byte short does not draw a
slightly wrong triangle - it desynchronises the byte stream, and every command
after it is nonsense. That presents as "the game is not drawing", which is
indistinguishable from fifty other causes. So the sizes are asserted directly,
for direct and indexed attributes, for fixed-point formats where the shift
changes the value and must not change the size, and for the matrix index that
comes first and is easy to place last.

**The design decision worth keeping:** every vertex format the hardware allows
is collapsed to ONE host vertex before anything downstream sees it. The
rasteriser has a single layout to be correct about, and every format bug is in
one file.

**What is missing:** textures, the texture environment stages, lighting, and
near-plane clipping (a triangle straddling the camera is dropped whole rather
than split). Untextured geometry in the right place proves every stage before
it, which is why it came first.

**F71 — the renderer works, and four bugs stood between "no pixels" and the
Konami logo. All four were quiet.**

Each drew *something*, or drew nothing in a way that looked like a different
problem entirely. That is the character of renderer bugs and the reason the
tests assert intermediate values rather than only the final image.

1. **The copy stride register was 0x4E. It is 0x4D.** 0x4E is the copy's
   vertical scale. The wrong one gave a line pitch of 6944 bytes instead of
   1024, so every framebuffer copy wrote 3 MB instead of 458 KB, over the
   game's own memory. It presented as the guest crashing at an unrelated
   address ~2 million steps later, and I wrongly cleared the display path of
   causing it because the control run had hit its timeout rather than its
   step limit - the absence of a "stopped" line read as "no crash".

2. **The projection is six floats and a type word, not seven floats.** Read
   as seven, the type word became a denormal in the last coefficient and the
   perspective/orthographic flag was never set. Every vertex came out behind
   the eye: 108 triangles submitted, 108 clipped, 0 drawn.

3. **Quad expansion overwrote vertex 0 with vertex 3.** A quad is two
   triangles, 0-1-2 and 0-2-3, and the second needs vertex 0 again. A
   three-vertex buffer loses it, and the second triangle comes out
   degenerate - which the rasteriser discards as zero-area. Half of every
   quad silently disappeared and the surviving half looked perfectly correct.

4. **A vertex without a matrix index still has one.** It comes from a
   command-processor register rather than from the vertex. Defaulting to zero
   draws every such object at whatever matrix zero happens to hold.

**And one in our own diagnostics, which is worth recording separately.** The
host's guest-memory accessors in `host/module.c` folded an address and indexed
without a bounds check. The guest panicked, the thread dump walked the thread
list, one link was garbage, and the HOST segfaulted - inside the code that was
reporting the guest's problem. A diagnostic that crashes is worse than no
diagnostic, because the crash it produces is in the wrong place. Those
accessors read guest POINTERS, and are called precisely when the guest has
already gone wrong, so garbage is their expected input, not an exception.

**Evidence, from one headless run at 4 million steps:**

| | |
|---|---|
| GX commands parsed | 7,203 |
| Parser desyncs | **0** |
| Primitives / vertices / triangles | 54 / 216 / 108 |
| Triangles drawn (clipped) | 108 (0) |
| Pixels shaded | 12,494,848 |
| Textured triangles | 108 |
| Textures decoded / cache hits / refused | 1 / 107 / 0 |
| Frames copied to the external framebuffer | 55 |

**The image is not committed.** A rendered frame is the game's own artwork, and
rule 8 admits no exception for it being ours that drew it. The counts above are
analysis evidence and are committed; the pixels are not.

**F72 — ordered alignment is exhausted; two new routes are not.**
Three reference decomps (MKDD, TTYD, Pikmin 2) run together agree on 445 names
and add **zero** beyond the existing map. Alignment cannot name a function the
reference games never linked, and that is where the remaining 185 SDK entry
points live. Two replacements, both now tools:

- **`tools/match-callgraph.py`** — a function's callees are a fingerprint,
  weighted by rarity. Cross-checked against the SDK module its neighbours
  belong to, which withdrew 5 of 27. Iterates: each confirmed name is
  evidence next round. 24 names.
- **`tools/attribute-by-strings.py` + `tools/match-source-order.py`** — this
  binary hands `__FILE__` **and `__LINE__`** to a tracking allocator, so a
  function's exact source location is compiled into it. 29 names, each from a
  line number that lands in exactly one function.

One name, `vorbis_book_init_decode`, was produced by both routes
independently. 52 added in total; phase 0 average 56.6% -> 57.6%.

**F73 — this game embeds Tremor, and the binary says so.**
`main.dol` contains `res012.c`, `floor0.c`, `sharedbook.c`, `framing.c`.
`res012.c` is decisive: stock libvorbis renamed that file years before this
game shipped, and **Tremor** — Xiph's fixed-point Vorbis decoder, the build
intended for consoles — kept it. Both are BSD-3-Clause and now recorded in
`THIRD_PARTY.md`. Stock libvorbis was fetched first, contributed nothing once
Tremor was in, and was removed rather than left lying in `extern/`.

This also explains the audio architecture: the game decodes Ogg Vorbis in
software on the CPU, so phase 4 needs a working Tremor path rather than only
a DSP voice mixer.

**F74 — the compiler did not emit functions in source order, and assuming it
did would have cost two correct names.**
`tools/match-source-order.py` was written to require address order to follow
source order. `floor0.c` refused it: the *highest* attributed address holds
the allocation at line 302, the file's *third* function. Metrowerks reordered
them. The constraint was wrong and was removed; uniqueness of the containing
function, plus injectivity, is what the evidence actually supports.

Worth keeping because the failure mode was benign only by luck: an ordering
assumption that is *nearly* true produces an alignment that is off by one for
everything after the first discrepancy, and every name after that point is
confidently wrong.

**F75 — `dolsdk2004` is a decompilation, not leaked source, and it is now
recorded.**
It had been in `extern/` and load-bearing all session — every register layout
and SDK semantic in F61-F69 came from reading it — without a row in
`THIRD_PARTY.md`, which rule 11 requires. Its README states it decompiles the
SDK's built library archives and "does not provide a complete copy" of the
SDK, which is the community's own work and what rule 9 admits. It carries no
licence file, so `THIRD_PARTY.md` now says that plainly and states the two
practices that follow: nothing is copied from it, and names taken from it
carry an origin so they can be withdrawn per symbol if that ever changes.

**F76 — the boot blocker is an allocation failure, and it is now located
exactly.**
The game reaches `"memory.c" on line 1197` and suspends its main thread. Two
independent routes agree on which function that is, which is what makes the
identification solid rather than probable:

- **At runtime**, the panic's own stack dump named `0x7F0FCF00` as a return
  address, so the caller starts at `0x7F0FCEB8`.
- **Statically**, `tools/attribute-by-strings.py` found one REL function
  holding a pointer to the string `memory.c` together with the immediate
  `0x4AD` - 1197. It is at REL offset `0x0F4DCC`, which loads at
  **`0x7F0FCEB8`**.

The function itself is four instructions of substance:

```
    li   r3, 2          ; heap 2
    li   r4, 0          ; flags
    mr   r5, <size>
    li   r6, 0x20       ; 32-byte alignment
    bl   fn_1_F4988     ; the allocator
    mr.  r31, r3
    bne  done           ; non-NULL: return it
    ... OSPanic("memory.c", 1197, ...)
```

So: **an allocation from the engine's heap 2, 32-byte aligned, returned NULL.**
That is the game running out of one of its own heaps, not a crash and not a
fault in translated code. The arena it was given is 0x80290700-0x81700000,
about 20 MB, so the question is what heap 2 was sized from and what has
already been taken out of it - most likely something our shims allocate
differently, or a free that never happens because the DVD or audio path that
would trigger it is stubbed.

**Where to start:** `fn_1_F4988` is the allocator. Its heap table, and what
sized heap 2, is the thing to read next.

**F77 — the REL can be attributed to source files too, and it names its own
error sites.**
`tools/attribute-by-strings.py` now handles RELs. That needed one thing the
DOL did not: a REL is relocatable and **every section is based at zero**, so
an address alone is not a location - `.text+0x1000` and `.data+0x1000` are
different places. dtk's labels say which (`lbl_1_data_D5F8`), and the section
names are taken from the disassembler's own output matched by size rather
than by ordinal, because this REL has two four-byte sections before `.rodata`
and any ordinal rule puts every later section one or two names out.

Six file names, ten functions: `memory.c`, `libgv_cnf.c`, `gcn_dgd.c`,
`gcn_spheremap.c`, `GCN_prim2.c`, `mpegGCN.c`. Recorded in
`config/symbols/mgso_pal.rel.files.txt` as attributions, **not names** - the
engine is Konami's own code and no public decompilation exists, so the file
is recoverable and the name is not. It is still the difference between
`fn_1_F4DCC` and "the allocator wrapper in memory.c".

`mpegGCN.c` also closes an old question (F10): the MPEG video decoder is in
the REL, at offset 0x149128, and is the game's own code rather than an SDK
component.

**F78 — the engine's memory architecture, read out of the binary.**
Following F76's panic upward gives the whole shape, and it is worth having
written down because every future out-of-memory question needs it.

```
libgv_cnf.c:94   fn_1_F7F94   if (heapSize == 0) heapSize = 0xDCF800   (~13.8 MB)
                              total = heapSize + 0x400000              (~17.8 MB)
                              block = alloc(total, MUST_SUCCEED, "libgv_cnf.c", 94)
                              carve heap bases out of `block`

                 fn_1_F43E0   HeapCreate(2, 0, base2, size2)   <- from bss+0x27738/0x27740
                              HeapCreate(4, 0, base4, size4)

                 fn_1_F48B8   heap->freeList = start; heap->size = heap->free = size
                 fn_1_F4988   alloc(heap, flags, size, align) - walks the free list
memory.c:1197    fn_1_F4DCC   p = alloc(2, 0, size, 32); if (!p) OSPanic(...)
```

The heap table is at REL `.bss+0x24AD8`, **44 bytes per heap**, with `freeList`
at +0x08, `size` at +0x24 and `free` at +0x28. The DOL-side allocator is
`fn_8004E7BC(size, mustSucceed, file, line)`, which calls a function pointer at
`0x8020E158` and panics itself if that returns NULL and `mustSucceed` is set.

**What this rules out.** The 17.8 MB block was requested with `mustSucceed`,
and the panic we see is `memory.c:1197`, not `libgv_cnf.c:94` - so **the big
allocation succeeded**. The arena is 0x80290700-0x81700000, about 20.4 MB, and
17.8 MB fits. It is not a shim returning a wrong arena size, and it is not the
framebuffer copies: after the stride fix those write 458 KB at 0x80066480 and
0x8015A480, both below the arena.

So heap 2 is genuinely being exhausted, or its free list is being corrupted
after it is built.

**What to do next, in order:**

1. **Log the engine's own allocator.** `fn_1_F4988` is reached cross-module
   rarely, but `fn_8004E7BC` is called from the REL and therefore passes
   through `dolrecomp_dispatch_replacement` - logging `(size, file, line)`
   there gives the allocation sequence without touching the game's code.
2. **Dump heap 2's descriptor at the panic.** The REL's `.bss` address is
   whatever `OSLink` was handed, which the host already sees; the table is at
   `+0x24AD8` and heap 2 at `+0x58` into it.
3. **Note that only ONE DVD read has happened** by this point - the overlay
   itself. If the game expects to read a configuration file that sets
   `heapSize` (the `bss+0x27740` global), it has not, and the 0xDCF800 default
   applies. Whether that default is larger or smaller than the configured
   value is the question that decides whether this is our bug at all.

**F79 — callers are evidence too, and leaves have nothing else.**
The call-graph matcher used only callees, which says nothing about a leaf
function - and a great many SDK functions are leaves. Who CALLS a function is
as distinctive: `OSInit` calling something at a particular point identifies it
as well as that something calling `OSDisableInterrupts`. Our side is exact,
because the disassembly says which named function contains each `bl`.

Shared callers are weighted like shared callees, by rarity: a caller with three
callees is decisive, one with forty is nearly free. With two kinds of evidence
to meet it, the distinctiveness threshold drops to two and the module
cross-check still filters - the one pass that proposed a name whose module
disagreed with its neighbours had it withdrawn.

22 more names over four passes before the fixpoint stops finding anything.

`tools/merge-symbols.py` is now the single place candidates enter the map, so
the invariants live in one place rather than in whatever script produced them.
Refused: an address already named, a name already used, a size disagreeing
with dtk's boundary, an address that is not a boundary. Two candidates
claiming one address with different names withdraw both. Two stages proposing
the same name is the opposite case and is recorded in the origin.

**Session total: 868 -> 942 names, phase 0 average 56.6% -> 57.9%.**

**F80 — the assembly matcher was seeing a quarter of the SDK's assembly, and
the three most-called unnamed functions in the game were in the rest.**

`tools/match-sdk-asm.py` handled `void f() { asm { ... } }` and found 33
signatures. The SDK decomp writes nearly all of its assembly as
`asm void PSVECAdd(...) { ... }` - the whole function - and there are **146**.
Handling both forms gives 113 usable signatures.

Two changes made them comparable:

- **Offsets cannot be compared across the two sides.** The decomp writes them
  symbolically (`psq_l f2, Vec.x(a), 0, 0`) because the Metrowerks assembler
  resolves them from the struct; our disassembly has the resolved numbers.
  Including them made every signature fail to match rather than making it
  stricter, which is why the first attempt produced **zero** matches.
- **The quantised W bit replaces them, and carries the distinction they were
  there for.** `psq_l` with W=0 moves a pair, W=1 moves one. A three-component
  vector loads 2+1; a four-component quaternion loads 2+2. Their mnemonic
  sequences are otherwise identical - `psq_l, psq_l, ps_add, psq_st` twice -
  so without W the matcher refused both as non-unique, and with mnemonics
  alone it would have named one of them wrong.

12 exact matches. **Nine were already named**, by run alignment, which is an
independent confirmation of those nine rather than a wasted result. The three
new ones are `PSQUATAdd`, `PSQUATSubtract` and `PSQUATScale` - at 913, 697 and
687 call sites, **the three most-called unnamed functions in the binary**.

| measure | before | after |
|---|---|---|
| SDK call sites covered | 2,764 / 7,078 (39.1%) | **5,061 / 7,078 (71.5%)** |
| SDK entry points named | 164 / 336 | **167 / 336** |
| phase 0 average | 57.9% | **64.6%** |

**The lesson worth keeping:** three names moved call-site coverage by 32
points. Counting *functions* named treats a leaf called 900 times the same as
one called once, and the naming effort had been steered by that count. The
call-site measure is the one that says what the engine actually depends on.

**F81 — the remaining unnamed SDK entry points are mostly unreachable by the
call graph, and it is worth knowing which.**
Of 172 unnamed at the time of measuring: 43 have a named callee, 14 a named
caller, 52 either - and **50 have no calls and no callers at all**. The call
graph has a hard ceiling here and it has been reached. What names a leaf is
its instruction sequence (F80) or a string it references (F77), and those are
the two routes with room left.

**F82 — a function that prints its own name in its own message names itself,
and this build kept 15 such messages.**
`tools/name-by-messages.py` (stage 5g). The SDK's error paths say who they
are - `"VIConfigure(): Tried to change mode..."`,
`"OSCheckHeap: Failed 0 <= heap && heap < NumHeaps in %d"`,
`"__DSP_boot_task()  : IRAM MMEM ADDR: 0x%08X"` - and a function holding a
pointer to a string that opens with its own name is that function.

Two checks, because a message is not proof: the name must exist in the SDK
decomp, which is what distinguishes a real symbol from prose that happens to
parse as an identifier; and exactly one function may reference the string,
because a message referenced from two places is evidence about neither.

**Yield: one.** `OSCheckHeap` at `0x8001CC74`. Most of the fifteen messages
are already referenced by named functions. It is worth keeping anyway for two
reasons - it costs nothing to re-run as the map grows, and the one it found is
in the path that is currently blocking the boot: `OSCheckHeap` is what
`fn_8004E7BC` calls when an allocation fails with MUST_SUCCEED (F78), so the
memory analysis now has that function named rather than numbered.

**Nothing on the REL side.** Its strings are asset names - textures, areas -
not diagnostics, and it references no function-naming message at all.

**F83 — the engine cannot be named by any automated route available, and that
is worth stating rather than rediscovering.**
`main.dol` embeds Tremor and Ogg (F73), which are public. The REL embeds
nothing: a scan for copyright notices, version strings and the markers of
zlib, libpng, FreeType, Lua and Bink finds none. All 16,667 of its functions
are Konami's own code with no public decompilation.

So "Functions named — 946 / 18,485 — 5.1%" will not move much by automation,
and the average that includes it understates the project's position. The
measures that mean something for a recompilation are the two about what the
engine actually calls: **SDK entry points named, 167/336**, and **SDK call
sites covered, 5,061/7,078 (71.5%)**. `tools/progress.py` already says the
average is "a headline, not a statistic"; F80 is the demonstration - three
names moved call-site coverage 32 points and function coverage by 0.02.

**F84 — the boot blocker was an arena 646 KB too small, and the chain to it
was four layers deep.**

Each layer looked like a different bug, and three of them were dead ends until
the layer above was understood:

1. The engine panics at `memory.c:1197`. Heap 2's free-list head is
   `0x00380000` - **not an address, but NULL plus an offset**, which is what
   `libgv_cnf.c` computes when the block it carves the heaps from is NULL.
2. So the ~17.8 MB allocation failed, and failed **silently**: the game's
   allocator is a function pointer, and its `MUST_SUCCEED` path calls
   `OSCheckHeap` and then returns NULL anyway rather than halting.
3. That allocation came from a heap of 19,546,080 bytes -
   `OSCreateHeap(0x8045C020, 0x81700000)`. The heap starts **1,882,912 bytes
   above the arena base**, because the game takes five arena allocations
   first (4 KB, 16 KB, 106 KB, 32 KB, 16 KB and one of 1,703,936).
4. And the arena's ceiling was `0x81700000`, because `BootInfo->arenaHi` was
   left at zero and the SDK then falls back on the DOL's own `__ArenaHi`
   linker symbol - a megabyte below the top of memory.

**On real hardware the apploader sets that field.** It loads the disc's
filesystem table at the top of MEM1 and gives the arena everything below it.
`runtime/os/boot_info.c` now does the same: the FST is copied to
`0x817F8EE0`, `FSTLocation` and `FSTMaxLength` are set, and the arena ceiling
is the FST's address. Arena `0x8028E700-0x817F8EE0`. The panic is gone, heap 2
is created and fully consumed as intended, and the game proceeds into the
overlay and starts issuing disc reads.

**Worth keeping about the shape of this:** the visible symptom was four layers
from the cause, and every layer in between was a *correct* mechanism reporting
a *correct* result about bad input. Nothing on the path was broken except the
last thing checked.

**F85 — the recompiled overlay's `.bss` sat on top of its own relocation
tables, so every engine global read relocation data instead of zero.**

The game loads a REL *file*. Its loaded sections end at `0x491B7C` and the
remaining 925 KB is relocation data. `.bss` is not in the file: the game
allocates it separately and hands the pointer to `OSLink`, which relocates
every reference in ITS copy to point there.

DolRecomp compiles the overlay at a fixed base and resolves those references
itself, placing `.bss` immediately after `.data` - where a **statically
linked** module's bss belongs, and which here is exactly the relocation
tables. So the recompiled engine's globals landed at `0x7F499BA0` and up,
reading relocation data.

That is why the heap table was empty although `fn_1_F43E0` had run: it wrote
descriptors into memory that was never zero, and the allocator then read a
free list that was never a list.

The host now zeroes that region once linking is done - which is what `.bss`
means, and the relocation tables are dead by then. The range is read from the
module's own section table rather than assumed, with one catch worth
recording: **after linking, the header's offsets are addresses.** `OSLink`
rewrites `sectionInfoOffset` and every section offset in place, so adding the
module base a second time overflows and every read returns zero from nowhere.
And the `.bss` section entry then points at the memory the GAME allocated,
somewhere else entirely, so taking the maximum over all sections picks that
instead of the image's end.

**F86 — host-side observation of the guest, without replacing anything.**
Three facilities added to `host/module.c`, all read-only:

- `mgs_module_watch(addr)` - record a call's arguments the first time the
  guest reaches it. Used for `OSLink`, whose `r4` is the only report of where
  the overlay's `.bss` was allocated.
- `mgs_module_trace_calls(addr, fn)` - every call, with its arguments. Used
  for the game's tracking allocator, which turned "the arena ran out" into a
  list of five allocations with their source file and line.
- `mgs_module_on_linked(fn)` - the window between `OSLink` returning and the
  overlay's first instruction, which is the only moment the `.bss` fix can
  happen.

The allocator itself is a leaf reached inside a chunk, so the host never sees
it; **the stack does**. PowerPC frames are a linked list - first word the
previous frame, second word the return address - so walking it named the
subsystem that took each arena allocation.

**F87 — the game opens every file with a leading `./`, and our FST lookup
refused all of them.**

After the overlay links, the engine loads its data: `"./stage.dat"`,
`"./shared/codec.dat"`, `"./shared/face.dat"`, `"./shared/movie.dat"`,
`"./shared/vox.dat"`, `"./demo.dat"`. `mgs_fst_find` did not handle `.` or
`..`, so every one returned entry 0, the `DVDFileInfo` was left empty, and
each read was refused.

**The engine does not treat a refused read as fatal. It retries.** For ever.
So this presented as a game that had booted perfectly, was scheduling threads,
servicing retrace, running its own main loop - and loading nothing at all.
`DVDReadAsyncPrio` was called **16,907,347 times** in one run with **one**
read completed, and `fn_1_450` and `DVDReadAsyncPrio` were the two hottest
functions in the profile by an order of magnitude.

The SDK's own `DVDConvertPathToEntrynum` accepts relative components, so
`mgs_fst_find` now does: `.` stays in the current directory, `..` takes the
parent from the directory entry's first word, and `..` at the root stays at
the root rather than walking off the front of the table. `tests/test_fst.c`
checks all of those against the real 1,653-entry disc FST - the check that
would have caught this in a second rather than a morning.

**F88 — `DVDReadAsyncPrio` should never have been looking up a path.**
The shim searched the FST for a file whose start address matched the file
info's, and used that entry's NAME - which is not a path, so anything in a
subdirectory failed to resolve even once the `./` problem was fixed.

The SDK does not do this and does not need to: `DVDReadAsyncPrio` adds the
file info's start address to the caller's offset and hands the result to
`DVDReadAbsAsyncPrio`. The file info already *is* the answer. The shim now
does the same, and also refuses a read that runs past the file's length, as
the SDK does, rather than silently returning a short one.

**The pattern in both of these:** a lookup that answers "not found" is
indistinguishable, from the outside, from a game that has nothing to load.

*Record further findings here as they are established — including the ones that
turned out wrong. They are worth more than a clean narrative.*
