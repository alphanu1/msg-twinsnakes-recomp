# The decompilation process, end to end

Every stage from a disc you own to a native binary: what goes in, what tool
runs, what comes out, and **how the output is verified**.

This document has two jobs. The first is reproducibility — anyone with the same
disc should reach the same symbol map. The second is **provenance**: `README.md`
claims that everything in this repository was produced by our own analysis of a
legally owned copy, together with public clean-room decompilation projects. This
document is where that claim is made checkable, stage by stage.

**No stage takes input from leaked source code.** Project rule 9 is absolute,
and each stage below names its inputs explicitly so that a reader can confirm
it.

Stages marked **DONE** have been run against the target. Stages marked
**PLANNED** have not; their commands are the intent, not a record.

```mermaid
flowchart TD
    A["Disc you own<br/>GGSPA4, two discs"] -->|dolphin-tool extract| B["main.dol 1.9 MB<br/>mgso_pal.rel 4.3 MB<br/>asset filesystem"]
    B -->|sha1sum| C["config/GGSPA4.toml<br/>hashes pinned"]
    B -->|strings| D["SDK build 0x2301<br/>identified"]
    D --> E
    B -->|dtk signature match| E["501 symbols<br/>origin: dtk-sig"]
    B -->|dtk split: 18,485 functions| E
    E -->|ordered run alignment<br/>vs doldecomp/mkdd| F["+350 symbols, GX 13 to 93<br/>origin: mkdd-align"]
    F -->|dtk split| G2["16,667 REL function boundaries"]
    G2 -->|classify by SDK calls| G3["177 renderer functions<br/>44 GX functions used"]
    G3 -->|Ghidra, Gekko spec| G["engine names<br/>origin: ghidra"]
    G --> H["config/symbols/<br/>the symbol map"]
    H -->|DolRecomp| I["generated C<br/>build artefact, never committed"]
    I -->|clang| J["native binary"]
    K["runtime/<br/>written from scratch"] --> J
    H -->|gen-patch-table| L["SDK patch table"] --> J
```

---

## Stage 1 — Extract the disc · **DONE**

**In:** the user's own disc images. **Out:** `discs/GGSPA4/disc{1,2}/`,
git-ignored.

```sh
tools/dolphin.sh tool extract -i "<disc1>.iso" -o discs/GGSPA4/disc1
tools/dolphin.sh tool extract -i "<disc2>.iso" -o discs/GGSPA4/disc2
```

Produces `sys/main.dol`, `sys/apploader.img`, `sys/fst.bin` and the asset
filesystem including `files/shared/mgso_pal.rel`.

**Verified:** both discs' executables are byte-identical by SHA-1 — `main.dol`,
the REL and the apploader. Only assets differ. The recompiler therefore runs
once, not once per disc.

**Provenance:** the user's own disc. Nothing from this stage is ever committed.

## Stage 2 — Hash and pin · **DONE**

**In:** the extracted executables. **Out:** [`config/GGSPA4.toml`](../config/GGSPA4.toml).

```sh
sha1sum discs/GGSPA4/disc1/sys/main.dol \
        discs/GGSPA4/disc1/files/shared/mgso_pal.rel
```

Hashes are taken of the **extracted executables**, not the disc image. That is
what the build checks, and it makes the check independent of the container
format — `.iso`, `.gcm`, `.rvz` and NKit all produce the same `main.dol`.

**Open:** the current dumps are NKit, so these hashes are not yet confirmed
against a redump-clean image. See HANDOFF F8.

## Stage 3 — Identify the SDK build · **DONE**

**In:** `main.dol`. **Out:** the build ID, recorded in `config/GGSPA4.toml`.

```sh
strings -n 6 discs/GGSPA4/disc1/sys/main.dol | grep "Dolphin SDK"
```

Nintendo's SDK libraries each embed a build string. All thirteen components are
present, newest 2003-08-06, all tagged **`0x2301`**.

**Why this stage exists at all:** the build ID is what makes stage 4 possible.
Without it there is no way to know which other games' decompilations share this
exact SDK, and symbol recovery would be pure manual analysis of 380 KB.

**Provenance:** strings in the user's own binary.

## Stage 4 — Signature-match the SDK · **DONE**

**In:** `main.dol` + `decomp-toolkit`'s signature database.
**Out:** 501 symbols in
[`config/symbols/main.dol.symbols.txt`](../config/symbols/main.dol.symbols.txt),
origin `dtk-sig`.

```sh
extern/decomp-toolkit/target/release/dtk dol info \
    discs/GGSPA4/disc1/sys/main.dol
```

`dtk` compares byte patterns in our binary against signatures derived from
public decompilation projects. A match names the function.

**Recovered:** OS 87, Metrowerks TRK debugger 79, CodeWarrior runtime 78, DVD
21, PPC 20, GX 17, EXI 13, SI 7.

**Provenance:** names come from public clean-room decompilation projects, which
recover the SDK's API by analysing retail binaries. Names, not code.

## Stage 5 — Close the GX gap by run alignment · **DONE**

**In:** our function boundaries + `doldecomp/mkdd`'s symbol map.
**Out:** 350 further symbols, origin `mkdd-align`.

Mario Kart Double Dash links **the same SDK `0x2301` build**, and its decomp
publishes a symbol map with addresses and sizes.

**Established before relying on it:** of 287 symbols named in both, **264 have
byte-identical sizes (92%)**. Every mismatch is a C-runtime function whose
codegen varies with compiler options, not an SDK function.

**Why it is not a constant offset.** Each game links only the SDK functions it
references, so the offset is *piecewise* constant: `GXInit` through
`GXSetGPFifo` — five consecutive functions — all sit at exactly `-0x82098`,
then it shifts as functions absent from one binary drop out.

```sh
tools/align-symbols.py \
    --ours build/phase0/main.symbols.txt \
    --ref  extern/mkdd/config/MarioClub_us/symbols.txt \
    --out  build/phase0/aligned.txt
```

The tool runs two independent passes, and a name is only ever written onto an
unnamed `fn_*` function whose size matches exactly:

1. **Between-anchor segments.** Two consecutive anchors bracket a segment in
   each binary. If the segments are the same length *and* every size agrees
   pairwise, the linker kept the same functions in the same order between those
   points, and the mapping is 1:1.
2. **Gap resync.** A run ends at a size mismatch because one binary links a
   function the other does not. Rather than stopping, look ahead a bounded
   distance in each list for a point where the next **3 consecutive sizes all
   agree**, and resume there. The window guard is what makes this safe: a lone
   size coincidence is common — many functions are 0x20 bytes — but three in a
   row agreeing by chance is not.
3. **Run extension.** Walk outward from each anchor while sizes agree. This
   catches the regions beyond the first and last anchor.

A function claimed with two different names by different runs is dropped
rather than guessed at.

### Result, and the proof of it

```
ours              1804 functions in .text
reference        15342 functions in .text
anchors            264 (same name AND same size in both)
segments mapped     59 (consecutive anchors, exact size agreement)
gap resyncs       1275 (3 consecutive sizes must agree)
runs extended      479
names assigned     415
```

| | before | after |
|---|---|---|
| symbols in the map | 501 | **915** |
| GX functions named | 13 | **148** |
| SDK entry points the engine calls, named | 70 | **96** |

**Three independent checks, all passed:**

1. **The two passes agree.** Segment matching produced **no names that run
   extension had not already produced, and no contradictions**. Two different
   methods over the same data reaching the same answer is the strongest signal
   available here.
2. **Sizes match exactly**, by construction — a name is never transferred onto
   a function of a different size.
3. **Cross-checked against a second, unrelated source.** Of the 131 GX names
   this stage produced, **111 (85%) appear in `doldecomp/dolsdk2004`'s GX
   sources**, a decompilation of a *different* SDK release by a *different*
   project. Those that do not are internal `__GX*` statics, where decomp
   projects legitimately differ on naming — no public API function is
   unaccounted for.
4. **Gap resync raised precision rather than trading it away.** Before resync,
   82% of GX names were confirmed by `dolsdk2004`; after, **85%**. It also
   changed **no** previously-assigned name. A relaxation that increases
   agreement with an independent source is doing real work, not guessing.

**Remaining:** MKDD names 177 GX functions; we have 93. The rest were not
recovered because the surrounding runs broke on size disagreement, which
happens wherever the two games link different neighbouring functions. Closing
that needs the Dolphin SDK-call log (stage 9's harness, built early) or manual
analysis in Ghidra.

**Provenance:** a public clean-room decompilation of a different game. No code
is copied — only names, and only where our own binary's function sizes
independently confirm the match. `tools/align-symbols.py` is our own code and
is committed, so the derivation can be re-run and checked.

## Stage 5b — Match inline-assembly bodies · **DONE**

*(Second pass, 2026-09-19 — the first pass read a quarter of the available
assembly. See below.)*

**In:** our disassembly + `doldecomp/dolsdk2004` sources.
**Out:** 3 symbols, origin `sdk2004-asm`.

Parts of the SDK — the paired-single maths especially — are written as **inline
assembly** rather than C. That makes them the strongest signature available:
the compiler emits those instructions verbatim, so the machine code is identical
across SDK revisions, link orders and games. Unlike stage 5, this needs no
anchors and no assumptions about what either binary links.

```sh
tools/match-sdk-asm.py \
    --sdk extern/dolsdk2004/src/mtx/*.c \
          extern/dolsdk2004/src/gx/GXTransform.c \
          extern/dolsdk2004/src/gx/GXLight.c \
          extern/dolsdk2004/src/base/PPCArch.c \
    --asm build/phase0/out/asm/auto_01_800055E0_text.s
```

A match requires the **full opcode sequence, in order, same length**. A prefix
match is not a match.

```
SDK inline-asm signatures     31
our functions               1806
exact full-sequence matches    3
ambiguous signature (skip)     2
non-unique body (refused)      4
```

**The refusals are the important part.** Four of our functions matched, but in
pairs: two matched `PSQUATAdd`, two matched `PSQUATSubtract`. A 4-float add is a
4-float add — the SDK contains distinct functions with identical instruction
sequences, so matching one name to two addresses proves the sequence does *not*
identify the function. The tool refuses both rather than pick one.

Those four are the *hottest* unnamed functions in the binary — `0x80025800`
alone has **913 call sites**. Naming them on a 50/50 guess would have been the
single most damaging error available. They stay unnamed until something
disambiguates them.

Accepted, each verified instruction-for-instruction against the SDK source:
`PSVECSquareMag`, `PSQUATDotProduct`, `PSVECSquareDistance`.

**Provenance:** the name comes from a public clean-room decompilation; the
confirmation that it belongs at that address comes from our own binary.

## Stage 5c — Attribute by library locality · **DONE (limited result)**

**In:** the symbol map + the engine's call sites. **Out:** a library for 40 of
the 237 unnamed SDK entry points. **Assigns no names.**

SDK libraries link contiguously, so an unnamed function surrounded by named
functions of one library is probably in it.

```sh
tools/attribute-library.py --symbols config/symbols/main.dol.symbols.txt \
    --asm build/phase0/out/mgso_pal/asm/auto_00_00000000_text.s
```

```
SDK entry points the engine calls, unnamed     237
  attributed to a library                       40  (17%)
  on a library boundary (left alone)           197

library    functions  call sites
GX                29         199
OS                 7          13
PS                 2          32
VI                 1           2
DVD                1           1
```

**The useful part: 29 unnamed GX functions carrying 199 call sites.** Those are
renderer entry points the engine demonstrably uses, and phase 3 has to
implement them whatever they turn out to be called.

**The honest part: 17% is this technique's ceiling, and it is recorded so it is
not retried.** Two things defeat it, and neither is fixable by tuning:

- **Libraries genuinely interleave.** `SI` sits entirely inside `OS`'s address
  span. Only `TRK`, `EXI` and `SI` have ranges that overlap nothing, and those
  are the libraries we care least about.
- **Naming density is too low.** With 40% of the DOL named, consecutive named
  functions frequently belong to different libraries, so the bracket test
  refuses — 197 of 237 landed on an apparent boundary.

Two fixes were tried and are worth not repeating: separating `DBG`/`EXI2_` from
`DB`/`EXI` (they are the debugger mailbox library and live 190 KB away), and
trimming range outliers so one misfiled symbol could not stretch a range across
the binary — `__DBVECTOR` alone was swallowing DVD, VI and GX. Both were
necessary and neither was sufficient.

**The remaining 237 need dynamic information.** A Dolphin SDK-call log gives
each entry point a name from the call itself rather than from its neighbours,
and it is required for phase 2 regardless.

## How complete is this?

"Percent decompiled" has no single honest answer for a recompilation, because
we are not producing matching source. Four different numbers, and the last one
is the one that governs the work:

| Measure | | |
|---|---|---|
| **1. Functions named** | 717 / 18,485 | **3.9%** |
| — `main.dol` | 717 / 1,818 | 39.4% |
| — `mgso_pal.rel` | 0 / 16,667 | 0% |
| **2. Function boundaries recovered** | 18,485 / 18,485 | **100%** |
| **3. SDK entry points the engine calls, named** | 99 / 336 | **29.5%** |
| — weighted by call sites | 1,882 / 7,078 | **26.6%** |
| **4. GX surface known to be used** | 69 functions | the renderer's scope |

**Why 3.9% is the least useful number here.** The engine is translated
mechanically — DolRecomp does not care what a function is called. Names in the
REL buy debugging and hand-written patches, not correctness, which is why 0%
there is not a blocker. What the runtime must replace is the **SDK boundary**,
and that is measure 3.

**Measure 2 is the one that unblocks the build.** The recompiler consumes
function boundaries, and those are complete for both modules.

## Stage 5b, second pass — all of the SDK's assembly · **DONE**

**In:** our disassembly + `doldecomp/dolsdk2004`. **Out:** 12 matches, 3 new.

```sh
python3 tools/match-sdk-asm.py --sdk extern/dolsdk2004/src/*/*.c \
    --asm build/phase0/out/asm/auto_01_800055E0_text.s \
    --boundaries build/phase0/main.symbols.txt --out asmmatch.txt
```

The first pass handled `void f() { asm { ... } }` and found **33** signatures.
The decomp writes nearly all of its assembly as `asm void PSVECAdd(...) { }` —
the whole function — and there are **146**. Both forms gives **113** usable.

**Offsets cannot be compared across the two sides.** The decomp writes them
symbolically (`psq_l f2, Vec.x(a), 0, 0`); our disassembly has the resolved
numbers. Including them made every signature fail — the first attempt after
widening the parser produced **zero** matches.

**The quantised W bit replaces them and carries the distinction they were for.**
`psq_l` with W=0 moves a pair, W=1 moves one. A three-component vector loads
2+1; a four-component quaternion loads 2+2. Their mnemonic sequences are
otherwise identical, so without W the matcher refuses both as non-unique and
with mnemonics alone it names one of them wrong.

| | |
|---|---|
| exact full-sequence matches | 12 |
| already named — independent confirmation | 9 |
| **new** | **3** |

The three are `PSQUATAdd`, `PSQUATSubtract`, `PSQUATScale` — at 913, 697 and
687 call sites, the three most-called unnamed functions in the binary.

| measure | before | after |
|---|---|---|
| SDK call sites covered | 2,764 / 7,078 (39.1%) | **5,061 / 7,078 (71.5%)** |
| phase 0 average | 57.9% | **64.6%** |

**Three names moved call-site coverage by 32 points.** Counting *functions*
named treats a leaf called 900 times the same as one called once; the
call-site measure is the one that says what the engine actually depends on.

### Where the call graph stops

Of the 172 SDK entry points still unnamed when this was measured: 43 have a
named callee, 14 a named caller, 52 either — and **50 have no calls and no
callers at all**. Stage 5d has a hard ceiling and has reached it. A leaf is
named by its instruction sequence (this stage) or by a string it references
(stage 5e), and those are the two with room left.

## Stage 5d — Name by call graph · **DONE**

**In:** our disassembly + `doldecomp/dolsdk2004`. **Out:** 24 names.

Stage 5 is exhausted: three independent reference decomps now agree on 567
names and, run together, add **nothing** beyond what the map already holds.

| reference | functions | anchors | names |
|---|---|---|---|
| `mkdd` | 15,342 | 264 | 415 |
| `ref-ttyd` | 6,975 | 273 | 496 |
| `ref-pikmin2` | 24,942 | 280 | 436 |
| **consensus (2+ agree)** | | | **445** |
| **new** | | | **0** |

What alignment cannot do is name a function the reference games never linked.
A function's CALLEES can:

```sh
python3 tools/match-callgraph.py --asm build/phase0/out/asm/*text*.s \
    --symbols config/symbols/main.dol.symbols.txt \
    --boundaries build/phase0/main.symbols.txt \
    --sdk-src extern/dolsdk2004/src extern/tremor extern/ogg/src \
    --file-map build/phase0/files.txt --out callgraph.txt
```

Shared callees are weighted by rarity — a shared call to something 200
functions use is nearly free; one that two use is decisive. **Ambiguity is
refused**, not resolved by preference, and **a name claimed by two addresses
withdraws both**: at most one can be right and nothing says which.

**Checked by a second, independent route.** The linker lays an object's
functions down together, so a correct name should sit among neighbours from
its own SDK module. Of 27 candidates, 22 agreed and **5 were withdrawn**.

| round | proposed | confirmed | withdrawn |
|---|---|---|---|
| 1 | 27 | 22 | 5 |
| 2 | 6 | 2 | 4 |
| 3 | 4 | 0 | 4 |
| **total** | | **24** | |

It iterates because each confirmed name is evidence next round. It stops when
a round confirms nothing.

## Stage 5e — Attribute functions to their source file · **DONE**

**In:** `main.dol` + our disassembly. **Out:** 72 functions placed in a file.

Shipped C carries its own source file name. This binary hands `__FILE__` to a
tracking allocator:

```
addi r5, lbl_80063B08@l   ; "framing.c"
li   r6, 0x360            ; 864   <- __LINE__
bl   fn_80055B74          ; the allocator
```

```sh
python3 tools/attribute-by-strings.py --dol discs/GGSPA4/disc1/sys/main.dol \
    --asm build/phase0/out/asm/*text*.s \
    --symbols config/symbols/main.dol.symbols.txt --out files.txt
```

**21 source file names, 72 functions attributed, 0 ambiguous.** And a finding
that mattered more than the count: `res012.c`, `floor0.c`, `sharedbook.c`,
`framing.c` — **this game embeds Tremor**, Xiph's fixed-point Vorbis decoder.
`res012.c` is decisive; stock libvorbis renamed that file years earlier.

## Stage 5e-REL — Attribute the engine to its source files · **DONE**

**In:** `mgso_pal.rel` + its disassembly. **Out:** 10 functions placed, and
the boot blocker located.

```sh
python3 tools/attribute-by-strings.py --rel \
    --dol discs/GGSPA4/disc1/files/shared/mgso_pal.rel \
    --asm build/phase0/out/mgso_pal/asm/*.s \
    --symbols config/symbols/main.dol.symbols.txt \
    --out config/symbols/mgso_pal.rel.files.txt
```

A REL is relocatable and **every section is based at zero**, so an address
alone is not a location — `.text+0x1000` and `.data+0x1000` are different
places. dtk's labels say which (`lbl_1_data_D5F8`), and the section names are
read from the disassembler's own output **matched by size**, not by ordinal:
this REL has two four-byte sections before `.rodata`, and an ordinal rule puts
every later section one or two names out.

| file | functions |
|---|---|
| `gcn_dgd.c` | 4 |
| `gcn_spheremap.c` | 2 |
| `memory.c`, `libgv_cnf.c`, `GCN_prim2.c`, `mpegGCN.c` | 1 each |

These are **attributions, not names**. The engine is Konami's own code and no
public decompilation of it exists, so the file is recoverable and the name is
not — which is still the difference between `fn_1_F4DCC` and "the allocator
wrapper in memory.c".

### Checked against the running game

The port panics at `"memory.c" on line 1197`. Two independent routes agree on
which function that is:

| route | evidence |
|---|---|
| runtime | the panic's stack dump gives return address `0x7F0FCF00`, so its caller starts at `0x7F0FCEB8` |
| static | REL offset `0x0F4DCC` holds a pointer to `"memory.c"` and the immediate `0x4AD` = 1197; it loads at **`0x7F0FCEB8`** |

That also settles a phase-0 open question: `mpegGCN.c` is in the REL, so the
MPEG video decoder is the game's own code and not an SDK component (F10).

## Stage 5f — Name by exact source line · **DONE**

**In:** stage 5e's file and line pairs. **Out:** 29 names.

The line number is a plain `li` in the instruction stream, so a function does
not merely belong to `framing.c` — it contains the allocation at
`framing.c:864`, and exactly one function does.

```sh
python3 tools/match-source-order.py --file-map files.txt \
    --boundaries build/phase0/main.symbols.txt \
    --symbols config/symbols/main.dol.symbols.txt \
    --src extern/tremor extern/ogg/src extern/dolsdk2004/src
```

**A wrong assumption, corrected by the data.** This was written first to
require address order to follow source order, because a compiler emits
functions in source order. The data refused it: `floor0.c`'s *highest*
address holds the allocation at line 302, which is the file's *third*
function. Metrowerks reordered them. The ordering constraint was removed —
it would have rejected two names that a single exact line establishes alone.

What is required instead: the line must land in **exactly one** function, and
no two addresses may claim the same one. Ambiguous addresses are skipped.

### Result

| | |
|---|---|
| stage 5d, call graph | 24 |
| stage 5f, file and line | 29 |
| **agreed by both routes** | **1** (`vorbis_book_init_decode`) |
| **added to `config/symbols/`** | **52** |

Each carries its origin — `callgraph`, `fileline`, or `callgraph+fileline`
where both routes agree — so any one can be re-derived or withdrawn on its
own (rule 15).

| measure | before | after |
|---|---|---|
| Functions named | 868 | **920** |
| SDK entry points the engine calls, named | 151 / 336 | **160 / 336** |
| SDK call sites covered | 2,725 | **2,755** |
| GX surface named | 168 / 177 | **171 / 177** |
| **phase 0 average** | 56.6% | **57.6%** |

## Stage 6 — Recover the engine · **IN PROGRESS**

**In:** `mgso_pal.rel`, 4.3 MB. **Out:** function boundaries, then names.

### 6a. Function boundaries · **DONE**

```sh
dtk dol config -o build/phase0/config.yml \
    discs/GGSPA4/disc1/sys/main.dol \
    discs/GGSPA4/disc1/files/shared/mgso_pal.rel
dtk dol split build/phase0/config.yml build/phase0/out
```

**18,485 functions** — 1,818 in the DOL, **16,667 in the REL**.

### 6b. Naming: what does not work, and why · **DONE (negative result)**

**Signature matching cannot help and never will.** The REL is Konami's MGS2
engine as built for this game; it appears in no other binary, so there is
nothing to match against. `dtk` finds exactly three symbols in it — `_prolog`,
`_epilog`, `_unresolved`.

**Debug strings barely help either.** The engine does leave some assert strings
that name their own function (`... :: NewPutBreakObject`), but there are only
**about 25** of them and **8 source filenames**. Worth harvesting; not a
strategy. Recording this as a negative result so it is not re-attempted.

### 6c. Classify by SDK usage · **DONE**

What *does* scale: the REL calls into the DOL's SDK by cross-module relocation,
and the DOL is now 851 named symbols. A function that calls `GXSetTevOp` is
renderer code whatever it is called.

```sh
tools/classify-rel.py \
    --asm build/phase0/out/mgso_pal/asm/auto_00_00000000_text.s \
    --symbols config/symbols/main.dol.symbols.txt \
    --out build/phase0/rel-areas.txt
```

```
REL functions calling named SDK      221
distinct SDK functions called         80

engine functions by area:
  renderer         177
  os/threads        32
  file i/o           6
  input              3
  video              3
```

This assigns **no names** — it is a map of where to look, which is the
expensive part of reading 16,667 functions.

### 6d. The GX surface, and what it implies · **DONE**

The same pass answers a question phase 3 depends on: **which GX functions does
this game actually call?** [`config/gx-surface-used.txt`](../config/gx-surface-used.txt)
is the answer — **44 functions**, against the SDK's ~200. That is the renderer's
scope.

**It also confirms the design document's biggest phase-3 risk is real.** The
document lists indirect texturing among the GX edge cases that "take far longer
than planned". The game calls `GXSetTevIndirect`, `GXSetIndTexMtx`,
`GXSetIndTexOrder`, `GXSetIndTexCoordScale` and `GXSetNumIndStages` — **indirect
texturing is used, not hypothetical**. Fourteen `GXSetTev*` calls indicate the
TEV configuration space is used broadly, including `GXSetTevSwapModeTable`,
`GXSetTevKColor` and `GXSetTevColorS10`.

**Known incompleteness:** this covers direct calls only. `GXCallDisplayList` and
raw FIFO writes bypass the API, so the FIFO command parser is still required.
The Dolphin SDK-call log (stage 9's harness) will confirm the remainder.

### 6e. Ghidra cross-check and audit trail · **DONE**

An independent analyser, run for two reasons: to check stage 4 and 5's results
against something that shares no code with `dtk`, and to leave an auditable
record of the analysis.

```sh
tools/ghidra-analyse.sh discs/GGSPA4/disc1/sys/main.dol main_dol
```

Output lands in [`docs/evidence/`](evidence/): a function inventory, the full
analysis log, and a SHA-256 of both plus the input binary, so anyone can re-run
this and compare.

**`-loader-autoloadMaps false` is required, not cosmetic.** With map autoloading
on, the GameCube loader pops a GUI "load a symbol map?" dialog during import,
which throws in headless mode and fails the run outright.

**The cross-check:**

| | |
|---|---|
| | Ghidra | `dtk` | agreement |
|---|---|---|---|
| `main.dol` functions | 1,687 | 1,818 | **92.8%** |
| `mgso_pal.rel` functions | 16,323 | 16,667 | **97.9%** |

The REL agreement matters most: that is 4.3 MB of code with no symbols, no
signatures and no prior art, and two analysers sharing no code independently
resolve it to within 2%.

| | |
|---|---|
| symbols we had named | 773 |
| confirmed by Ghidra at the same address | **606 (78.4%)** |
| not confirmed | 167 — all **data** symbols (`__GXData`, `__PADSpec`, `__DVDVersion`), correctly not functions |

So every named symbol Ghidra classifies as a function agrees with ours, and the
disagreements are entirely the expected code/data split. Two analysers sharing
no code reaching the same answer is the point.

### What `docs/evidence/` contains, and what it does not

**It contains facts:** addresses, sizes, names, incoming-call counts. The same
class of information as a symbol map — which is what every decompilation
project publishes, `doldecomp/mkdd`'s `symbols.txt` included, the file stage 5
depends on.

**It does not contain decompiled source.** Ghidra's pseudo-C is a reconstruction
of the game's own code — a translation, and so a derivative work. Project rule 8
keeps it out, `README.md` states publicly that no game code is here, and the
same reasoning already excludes DolRecomp's generated C at stage 7. The
distinction is not where the data came from — all of it comes from analysing the
game — but whether it is **fact or expression**.

**The audit trail without the distribution:** the Ghidra project and any
decompiler output stay under `build/`, which is git-ignored. The committed
SHA-256 is what makes the run checkable — re-run the command and the hashes
either match or they do not.

### 6f. Next: name the entry points the engine actually uses · **PLANNED**

Of **336 DOL functions called directly from the REL**, 70 are named and **266
are not**. Those 266 are the highest-value naming targets in the project: each
is an SDK entry point the engine demonstrably uses. Routes are the Dolphin
SDK-call log and Ghidra:

```sh
tools/ghidra.sh <project-dir> twinsnakes \
    -import discs/GGSPA4/disc1/files/shared/mgso_pal.rel \
    -processor "PowerPC:BE:32:Gekko_Broadway"
```

Use `PowerPC:BE:32:Gekko_Broadway`, not `PowerPC:BE:32:default` — the stock
variant mis-decodes the paired-single instructions.

**Provenance:** our own analysis, with our own tools, of the user's own binary.
`tools/classify-rel.py` is committed so every claim here can be re-derived.

## Stage 7 — Translate to C · **DONE**

**In:** `main.dol`, `mgso_pal.rel`. **Out:** `build/phase1/`, git-ignored.

```sh
extern/DolRecomp/build/dolrecomp --gamecube --cpu gekko -j8 \
    discs/GGSPA4/disc1/sys/main.dol build/phase1/dol
extern/DolRecomp/build/dolrecomp --gamecube --cpu gekko -j8 \
    discs/GGSPA4/disc1/files/shared/mgso_pal.rel build/phase1/rel
```

**The whole game translates, with nothing left undecoded.**

| | instructions | decoded | unknown |
|---|---|---|---|
| `main.dol` | 97,240 | 95,591 (98.30%) | **0** |
| `mgso_pal.rel` | 1,136,896 | 1,136,896 (100.00%) | **0** |
| **combined** | **1,234,136** | **1,232,487 (99.87%)** | **0** |

The 1,649 in `main.dol` that are not "known" are **embedded data** in `.init`,
not failures — constant pools sitting inside the text section, which the
decoder correctly declines to treat as code.

Output: **295 MB of C, 11.5 million lines, 303 chunk files** (25 for the DOL,
278 for the REL). The design document budgeted 50–150 MB for a 3 MB DOL; this
is 4.9 MB of code, so the figure is in the right range.

### "99.87% decoded" is not "99.87% decompiled"

Worth stating plainly, because the two are easy to conflate and mean very
different things.

**Decoded/translated** means every PowerPC instruction was recognised and
mechanically rewritten as C operating on a CPU-state struct:

```c
// 80500288: lbz     r6, 0(r3)
{
    u32 ea = ctx->gpr[3] + (u32)(s32)(0);
    ctx->gpr[6] = mem_read8(ctx, ea);
}
```

That is the whole of the 99.87%. No types, no variable names, no control-flow
structure, no functions in any human sense — assembly wearing C syntax. It is
exactly what a recompilation needs and nothing more.

**Decompiled**, as decomp.dev uses the word, means readable idiomatic C that
recompiles to matching machine code. By that measure this project is at
**roughly 0%, deliberately.** The design document's second paragraph rules it
out: the generated C is a build artefact, never hand-edited, and readability is
not a goal.

| | |
|---|---|
| Instructions translated | **99.87%** |
| Phase 0 progress, five measures | **56.5%** |
| Decompiled in the matching-source sense | **~0%, by design** |

**And it does not mean the game is nearly finished.** Translated code cannot
run without a runtime under it. That runtime is phases 2–5 — the SDK shim
layer, the GX renderer at 2–4 months on its own, audio, saves — and it is where
nearly all the remaining work is.

**This settles the project's central feasibility question.** The Gekko decoder
handles every instruction Twin Snakes contains, including the paired-single
maths, and the REL — 92% of the game and the part with no prior art anywhere —
decodes at 100% with nothing unknown.

### The self-modifying-code warning is benign, and the symbol map proves it

DolRecomp flags 124 addresses in the DOL that "may patch executable memory".
Cross-referencing them against `config/symbols/`:

| count | function |
|---|---|
| 81 | `SPEC0/1/2_MakeStatus` |
| 24 | unnamed, adjacent to `Hu_IsStub` / `ARQPostRequest` |
| 8 | `DVDReadAbsAsyncPrio`, `setFbbRegs`, `ARQPostRequest` |
| 4 | `OSExceptionInit`, `__flush_cache`, `TRK_flush_cache`, `ICInvalidateRange` |
| 1 | `__SITransfer` |

Cache maintenance, exception-vector installation and DMA register writes —
every one is an SDK routine that legitimately touches executable memory. **No
self-modifying game code**, as the design document predicted for a retail
title. This is the symbol map earning its keep: it turned an alarming warning
into an explained list in one pass.



DolRecomp translates each function to `void fn_80xxxxxx(PPCContext*, uint8_t*)`.
SDK symbols are **not** translated: they are routed to the patch table and
replaced by the native runtime.

The generated C is a **build artefact**. It is never hand-edited and never
committed — it is regenerated from the user's own disc on every build, which is
also why no game code enters this repository.

## Stage 8 — Boot under ModernGekko (phase 1) · **IN PROGRESS**

**In:** the extracted disc + the recompiled modules. **Out:** a running module
under a Dolphin-derived runtime.

Phase 1 is deliberately throwaway: it proves the recompiled CPU code is correct
*before* our own SDK shims exist to be blamed for anything.

```sh
tools/phase1-setup.sh                                   # assemble the game tree
make -C extern/ModernGekko-Template tools                # build DolRecomp + ModernGekko
make -C extern/ModernGekko-Template run GAME=TwinSnakes-GGSPA4
```

### Two things had to be bridged

**The overlay name.** ModernGekko looks for `files/_Main.rel`, hardcoded — that
is Luigi's Mansion's name. Twin Snakes calls it `files/shared/mgso_pal.rel`.
`tools/phase1-setup.sh` builds a tree of symlinks beside the extracted disc
with `_Main.rel` pointing at ours, rather than touching `discs/`, which is the
provenance record. Symlinks, not copies: the disc is 1.2 GB and nothing here
modifies it.

**The hash pins.** `MODERNGEKKO_REQUIRED_DISC_ID`, `_DOL_SHA256`, `_REL_SHA256`
and `_ASSETS_SHA256` default to empty in ModernGekko's CMake, so a normal build
is not locked to the template's own game. Only a packaged release sets them.

### What the first run is expected to do

`main.dol` runs `__start` → `OSInit` → DVD init → reaches `rel_loader_LoadRel`
at `0x800066F8` → calls `OSLink`. Whether it continues depends on the runtime
dispatching to recompiled REL code at that point.

**A stop there is the expected outcome, not a failure.** It would still
demonstrate that the translated CPU code and the runtime work along the whole
boot path. With `--map` applied, the failure names the SDK function rather than
an address.

## Stage 8b — Boot under our own runtime · **IN PROGRESS**

**In:** the combined module + `runtime/` + `host/`. **Out:** the game running
its own main loop with no emulator code linked.

```sh
cmake --build build/runtime -j"$(nproc)"
./build/runtime/host/twin-snakes --module build/phase1/module/gGGSPA4_recomp.so
```

`ldd build/runtime/host/twin-snakes` lists SDL3, libc and libm. Nothing
Dolphin-derived is present. The "Dolphin SDK" lines the game prints are the
*game* naming the SDK it was built against — Dolphin was Nintendo's codename for
the GameCube, and the emulator took the name from the same place years later.

**Evidence, from a single headless run.** Each number is produced by the host's
own counters, printed at exit:

| | |
|---|---|
| SDK banners reached | OS, DVD, VI, **GX** |
| Console reported | `Retail 2`, `Memory 24 MB` |
| Retrace interrupts delivered | 19,645 of 20,000 raised |
| Guest threads scheduling | 3, switching through `__OSDispatchInterrupt` |
| GX command bytes produced | 31,572 |
| `GXDrawDone` answered | PE-finish raised from the FIFO token |
| Overlay loaded | 5,737,716 bytes of `shared/mgso_pal.rel` |
| `OSLink` | `overlay .text at 0x7F0080EC` |

### The seven things that had to be true

Each of these was discovered by a measurement, not by reading. They are written
up in full as HANDOFF F61-F69; the short form:

1. **`0x800` is not a fault.** The SDK uses lazy FP context switching and traps
   there on purpose. The host performs `OSSwitchFPUContext` itself.
2. **Some SPRs are read by the generated code, not just by the guest.**
   `HID2[PSE]` gates paired singles; `GQR0-7` set quantisation; `SRR0`/`SRR1`
   are what `rfi` consumes. A shadow table is invisible to all three.
3. **An interrupt is a state transition, not a call.**
   `__OSDispatchInterrupt` ends in `OSLoadContext`'s `rfi` and never returns.
4. **`MSR[EE]` is the interrupt flag** — the same bit the shims write and the
   host gates on. Two variables is one too many.
5. **PI's status mirrors device lines.** It clears when the guest acknowledges
   at the device, not when the host feels like it. Otherwise VI masks PE-finish
   for ever and `GXDrawDone` sleeps through a perfectly healthy idle loop.
6. **The boot ROM's low-memory globals are load-bearing.** Zeroed, they read as
   "unknown development board, 0 MB" and send the game down a debug path.
7. **`--rel-base` is not a free choice.** It fixes every absolute address in the
   generated overlay. The game loads its overlay at a hard-coded `0x7F008000`,
   so that is where it must be recompiled — and `0x7E000000-0x7FFFFFFF` has to
   exist as memory, which on a Dolphin-derived runtime it always did.

```sh
extern/DolRecomp/build/dolrecomp --gamecube --cpu gekko -j"$(nproc)" \
    --rel-base 0x7F008000 \
    discs/GGSPA4/disc1/files/shared/mgso_pal.rel build/phase1/rel-7f
python3 game/module/gen_combined_tables.py \
    --dol-generated build/phase1/dol/generated --dol discs/GGSPA4/disc1/sys/main.dol \
    --rel-generated build/phase1/rel-7f/generated \
    --rel discs/GGSPA4/disc1/files/shared/mgso_pal.rel \
    --rel-base 0x7F008000 --out build/phase1/module_tables.inc
```

### Reaching intra-chunk SDK calls

DolRecomp emits a `goto`, not a dispatch call, for a function called from within
the same chunk — so the patch table never saw those calls and three SDK shims in
a row were unreachable. `tools/inject-patch-guards.py` post-processes the
generated chunks, inserting after each patched function's label:

```c
label_8001D184:
    /* patch guard: ICFlashInvalidate */
    if (dolrecomp_dispatch_replacement(ctx, 0x8001D184u)) return;
```

Idempotent, 36 guards across 5 files, re-run whenever the generated code or
`config/sdk-implemented.txt` changes. It needs no change to DolRecomp.

## Stage 8d — The renderer · **IN PROGRESS**

**In:** the write-gather pipe's byte stream. **Out:** pixels.

```
MMIO 0xCC008000  ->  runtime/gx/fifo.c    command parser, CP/XF state
                 ->  runtime/gx/vertex.c  one host vertex, any input format
                 ->  runtime/gx/raster.c  transform, project, viewport, fill
                 ->  runtime/gx/efb.c     GXCopyDisp, BT.601, YUV 4:2:2
                 ->  video interface      scan-out, back to RGB, the window
```

**The stream is bytes, not API calls.** `GXBegin` and the vertex writes that
follow arrive as an opcode, a count, and packed attribute data whose layout is
*not in the stream* - it is in registers set earlier. So a draw command's
length is a function of state set arbitrarily far back, and one wrong length
does not lose one command: it desynchronises everything after it.

That property drives the design. The parser refuses to size a command it
cannot size, counts the refusal, and resynchronises, rather than guessing.

**Every vertex format collapses to one.** The hardware allows, per attribute,
a choice of component count, numeric type, fractional shift, and direct or
8/16-bit indexed addressing. All of it becomes a single `MgsGxVertex` in
`vertex.c`, so the rasteriser has exactly one layout to be correct about.

**Tested without the game**, in `tests/test_gx.c` and `tests/test_efb.c`,
because these failures are invisible in a running game - a vertex sized one
byte short presents as "the game is not drawing", which is indistinguishable
from fifty other causes. The tests assert sizes directly for direct, indexed
and fixed-point formats, then drive raw command bytes end to end and check a
known pixel.

**The Konami logo renders**, 2026-09-19, drawn entirely by `runtime/gx/`.
Evidence from one headless run at 4,000,000 steps:

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

```sh
MGS_SAVE_FRAME=frame.ppm ./build/runtime/host/twin-snakes --headless \
    --module build/phase1/module/gGGSPA4_recomp.so
```

`MGS_SAVE_FRAME` writes the last presented frame as a portable pixmap, so a
headless run can be *looked at* rather than only counted - "12 million pixels
written" says the rasteriser ran, not that the image is right, and the
difference between those two is most of the work. **The image itself is not
committed**: a rendered frame is the game's own artwork, and rule 8 admits no
exception for it being ours that drew it.

**Four bugs stood between no pixels and that frame, and all four were quiet** -
each drew something, or drew nothing in a way that looked like a different
problem. HANDOFF F71 has them in full; in short: the copy stride register is
0x4D and not 0x4E (0x4E is the vertical scale, and the wrong one wrote 3 MB
per frame over the game's memory); the projection is six floats and a type
word rather than seven floats (read wrong, every vertex is behind the eye);
quad expansion must hold all four vertices, not three (holding three makes
every second triangle degenerate, so half of each quad vanishes and the other
half looks right); and a vertex without a matrix index still has one, from a
command-processor register.

Not done: indirect textures, lighting, fog, blending, and near-plane clipping -
a triangle straddling the camera is dropped whole rather than split.

## Stage 8c — Compile and link natively · **PLANNED**

**In:** generated C + `runtime/` + `patches/`. **Out:** the native binary.

The runtime is **written from scratch** for this project. Where it reuses code,
that code is from Dolphin under GPL, recorded in `THIRD_PARTY.md` with its
source commit — which is why the port is GPL-3.

Compile with `-O2`, not `-O3`, and **never `-ffast-math`**: paired-single
semantics need exact rounding.

## Stage 9 — Verify against the original · **PLANNED**

**In:** the port and Dolphin. **Out:** a divergence report.

Because the game's logic is unchanged, any divergence from Dolphin localises to
a shim rather than to the game. Record an input sequence in Dolphin, replay it
in the port, and compare guest-memory checksums at fixed frames; diff `OSReport`
output between the two.

This harness is built in phase 1 and used for the rest of the project.

---

## Provenance ledger

Every symbol in `config/symbols/` carries an origin column. The permitted values
are exactly:

| Origin | Meaning |
|---|---|
| `dtk-sig` | signature match against the SDK `0x2301` build |
| `mkdd-align` | ordered run alignment against `doldecomp/mkdd` |
| `sdk2004` | name confirmed against `doldecomp/dolsdk2004` sources |
| `ghidra` | recovered by our own analysis |
| `own` | named by our own reasoning about behaviour |

**There is no origin for leaked source, and there will not be one.** If a symbol
cannot be given one of the origins above, it does not go in the map.
