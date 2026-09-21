# Milestones

Ordered to answer one question as early and as cheaply as possible:

> **Is the recompiled CPU code correct?**

Everything is sequenced so that question is answered before a single line of our
own SDK shim can be blamed for anything. `twin-snakes-native-port-design.md` is
the analysis, `docs/decompilation-process.md` is the mechanics stage by stage,
and this is the order of work. The study sets the *content*, this sets
the *order*. Both are checked.

**Updated on every commit.** A phase moves when its exit criterion is met, not
when the work feels done.

---

## Status, 2026-09-19

**Phase 0, in progress — 1,258 symbols, 18,485 function boundaries.**

Note on terminology, since the numbers here are easy to misread: **instructions
translated (99.87%) is not the same as decompiled.** Translation is a
mechanical rewrite of machine code into C; decompilation in the matching-source
sense is out of scope by design and sits at ~0%. Phase 0 progress is 70.7%. The toolchain is
built and verified. Both PAL discs are extracted, the SDK build is known, and
`config/GGSPA4.toml` holds the executable hashes. What remains in phase 0 is the
symbol recovery itself.

**Target retargeted to PAL (GGSPA4)** on 2026-09-18 — that is the dump that
exists. The design document is updated to match.

Four of the design document's phase-0 unknowns are now answered — SDK build,
code layout, audio codec and the two-disc question. See below.

**The licence question is settled: GPL-3.0.** Dolphin's texture decoder and TEV
shader generator are lifted rather than reimplemented, which takes months off
phase 3.

| Phase | Goal | Exit criterion | Estimate | State |
|---|---|---|---|---|
| 0 | Ground truth and symbols | Symbol map covering every SDK entry point the game calls, plus engine function boundaries | 2–4 weeks | **in progress** — discs extracted, SDK identified |
| 1 | Boot in ModernGekko | Title screen renders through recompiled CPU code, no interpreter fallback on the boot path | 1–2 weeks | **boots — Konami logo at ~43 fps**; one interpreter fallback remains |
| 2b | *(within 2)* Our own renderer | — | — | **the Konami logo is drawn by `runtime/gx/`**, 0 parser desyncs; the 86% of the boot that was `memcpy`/`__fill_mem` is now native (F92) — measured before the change, effect not yet re-measured |
| 2 | Native OS + DVD + PAD, headless | Main loop runs headless, reads assets, responds to input, `OSReport` matches Dolphin | 3–4 weeks | **runs 2M steps without faulting**; waiting on MMIO we have not built |
| 3 | GX renderer | Title screen, the Dock and the Heliport render correctly at native resolution, frame-compared against Dolphin | 2–4 months | blocked on 2 |
| 4 | Audio | Music, codec calls and SFX match Dolphin within tolerance | 3–6 weeks | blocked on 3 |
| 5 | Saves and completeness | Game completable start to finish on both platforms | 1–2 months | blocked on 4 |
| 6 | Port features | Public release | ongoing | blocked on 5 |

---

## Why this order

**Phase 1 is deliberately throwaway.** Running under the Dolphin-derived
ModernGekko/RecompCore runtime first proves the recompiled CPU code is correct
*before* our own shims exist to be blamed. Skipping it means every phase-2 bug
has two possible causes instead of one.

The differential harness built for phase 1 — diff `OSReport` logs and
guest-memory snapshots at fixed frame counts, Dolphin against the port — stays
useful for the rest of the project and is the main debugging tool in phases 3
and 4. Build it properly.

**Audio is last of the core work** because the game runs silently. **GX is the
project**, and it is the row with the widest error bars.

---

## Phase 0 — Ground truth and symbols

*Exit: a symbol map covering every SDK entry point the game calls, plus function
boundaries for the engine code.*

The absence of a public decomp is the main cost of the whole approach, and it is
paid here. The recompiler needs function boundaries and SDK symbols — nothing
more.

- [x] **Fetch the toolchain.** `tools/bootstrap.sh --all` — ten upstreams into
      `extern/`, pinned in `deps.lock`, licences recorded in `THIRD_PARTY.md`.
- [x] **Ghidra** — 12.1.3 flatpak, already installed, bundles its own JDK.
      Driven through `tools/ghidra.sh`.
- [x] **GameCubeLoader extension built and installed**, against Ghidra 12.1.3
      exactly. It supplies both the DOL/REL/ISO loader *and*
      `PowerPC:BE:32:Gekko_Broadway` — stock Ghidra has no Gekko variant and
      mis-decodes paired-singles. Do not also install the standalone language
      (F3).
- [x] **Dolphin installed** (flatpak) with `dolphin-tool` for headless
      extraction and hashing. `tools/dolphin.sh`.
- [x] **DolRecomp built** with the LLVM 20 backend, 33/33 tests passing (F5).
- [x] **`dtk` 1.8.4 built.**


- [x] **Discs obtained and extracted** — PAL, NKit containers (knowingly; see
      F8). Hashes recorded in `config/GGSPA4.toml`.
- [x] **Both discs' executables are byte-identical** — `main.dol`, the REL and
      the apploader share a SHA-1 across discs. **The recompiler runs once, not
      twice.**
- [x] **Extracted** `sys/main.dol`, `files/shared/mgso_pal.rel` and the FST
      from both discs via `tools/dolphin.sh tool extract`.
- [x] **SDK identified: Dolphin SDK `0x2301`**, newest component 2003-08-06,
      Metrowerks CodeWarrior. All thirteen component build strings are recorded
      in `config/GGSPA4.toml`. This is the key for signature matching.
- [x] **501 symbols recovered** by `dtk` signature matching — OS (87), the
      Metrowerks TRK debugger (79), CodeWarrior runtime (78), DVD (21), PPC
      (20), GX (17), EXI (13), SI (7). Saved to
      `config/symbols/main.dol.symbols.txt` with addresses and sizes.
- [x] **The DOL/REL split is the translated/native boundary.** `main.dol` is
      7.9% of the code and is almost entirely SDK + runtime + debugger; the REL
      is 92.1% and is the whole engine, with no SDK copy. See F9.
- [x] **Complete function boundaries recovered** — `dtk dol split` over the DOL
      and REL together found **18,485 functions** (1,818 in the DOL, 16,667 in
      the REL).
- [x] **Ordered run alignment against `doldecomp/mkdd` run** — **+350 symbols**,
      taking the map from 501 to **851** and GX from 13 to **93**. Verified
      three ways: two independent passes agreeing, exact size matching by
      construction, and 82% of the GX names cross-checked against
      `dolsdk2004`. Method, commands and numbers in
      `docs/decompilation-process.md` stage 5.
- [x] **Gap-resync alignment added** — GX 93 → **148**, map 851 → **915**, SDK
      entry points the engine calls 70 → **96**. Precision rose with it: the
      independent `dolsdk2004` cross-check went 82% → 85%.
- [x] **Ordered alignment exhausted, and two routes built to replace it**
      (F72). Three reference decomps run together — MKDD, TTYD, Pikmin 2 —
      agree on 445 names and add **zero** beyond the map, because alignment
      cannot name a function the reference games never linked.
      - `tools/match-callgraph.py` — a function's callees are a fingerprint,
        weighted by rarity, cross-checked against the SDK module its
        neighbours belong to. Iterates to a fixpoint. **24 names.**
      - `tools/attribute-by-strings.py` + `tools/match-source-order.py` — the
        binary hands `__FILE__` *and* `__LINE__` to a tracking allocator, so
        a function's exact source location is compiled into it. **29 names.**
      - One name agreed by both routes independently. **52 added**; phase 0
        average 56.6% → **57.6%**, GX surface 168 → **171 of 177**.
- [x] **The game embeds Tremor** (F73) — Xiph's fixed-point Vorbis decoder,
      established from `res012.c` in the binary's own strings. Phase 4 needs
      a software Vorbis path, not only a DSP voice mixer.
- [x] **The REL attributes to source files as well** (F77) — six file names,
      ten functions, in `config/symbols/mgso_pal.rel.files.txt`. Attributions
      rather than names: the engine is Konami's own code and no public
      decompilation exists, so the file is recoverable and the name is not.
      It located the boot blocker exactly (F76) and placed the MPEG decoder,
      closing a phase-0 question (F10).
- [x] **Callers used as evidence as well as callees** (F79), so leaf functions
      become identifiable at all. +22 names over four passes to a fixpoint.
      `tools/merge-symbols.py` is now the single gate every candidate passes,
      enforcing the map's invariants in one place.
      **Session total: 868 → 942 names; phase 0 average 56.6% → 57.9%.**
- [x] **The assembly matcher fixed, and with it call-site coverage** (F80) —
      it had been reading 33 of the SDK's 146 assembly functions, and the
      three most-called unnamed functions in the whole binary were in the
      rest. `PSQUATAdd`, `PSQUATSubtract` and `PSQUATScale`, at 913/697/687
      call sites. **SDK call sites covered 39.1% → 71.5%; phase 0 average
      57.9% → 64.6%.** Nine further matches confirmed names run alignment had
      already produced, independently.
- [x] **Naming from diagnostic messages** (F82) — `tools/name-by-messages.py`.
      One name, `OSCheckHeap`, and it is in the path currently blocking the
      boot. Nothing on the REL side: its strings are asset names.
- [x] **Established that the engine cannot be named automatically** (F83) —
      the REL embeds no public library, so its 16,667 functions have no public
      source to draw names from. The measures that mean something here are
      SDK entry points named and call sites covered, not the function count.
- [x] **Inline-assembly matching was not exhausted — it was broken** (F90).
      Two defects, each silently subtracting from the count: `nofralloc` is a
      directive that emits no code, and the trailing `blr` was stripped from
      our side only. 12 matches → **34**, 5 new, at 164/104/39/17 engine call
      sites. Call sites covered **71.5% → 76.1%**.
- [x] **A wrong name found and corrected** (F97) — `0x8001D124` was
      `DCZeroRange`; its body is a `dcbst` loop, so it is
      `DCStoreRangeNoSync`. Confirmed twice over: the SDK's `OSCache.c` order,
      and the absence of any `dcbz` in `main.dol` outside `__LCEnable`.
- [x] **`main.dol` attributed to its source files** (F96) —
      `config/symbols/main.dol.files.txt`, 64 attributions. It is not only SDK:
      it carries Konami's sound layer and a complete **Tremor**, 198 functions
      in `0x8004E700`-`0x80062000`, all unnamed and heavily called. Line
      numbers run strictly downwards within every Tremor unit and upwards
      within every `sd_*.c` one, which is the check that they are read right.
- [x] **The GX surface this game uses is fully named** (F108) — 81 of 81,
      which is the phase 3 specification. The row that measured it was also
      wrong: it compared counts against another game's symbol table and read
      101.1%. It now measures against `config/gx-surface-used.txt`, and the
      headline average went down as a result.
- [x] **Naming from a register shadow, a struct offset or a constant pool**
      (stage 5h, F118-F120) — 21 symbols covering 220 call sites, including
      the memory-card module and three more of the matrix library. Starting
      from the *data* rather than the function names several at once and
      makes each checkable field by field: `__GXData+0x1DC` is PE_CONTROL
      because `GXInit` writes `0x43` into its top byte, and `.sdata2
      0x8027E530` holds pi/180, which only a matrix library keeps.
- [x] **The remaining work is sorted by whether a reference exists for it**
      (F116) — `tools/unnamed-by-region.py`. Three quarters of the unnamed
      call sites are in Konami's own code, where there is no reference binary
      and no upstream source. The raw count oversells what is available.
- [x] **`tools/progress.py --check` fails on a stale progress table** —
      `HANDOFF.md` had drifted to 961 against a real 964 because one commit
      added symbols without regenerating. Rule 14 is now enforced rather than
      remembered, and the check was verified by being shown a stale number.
- [ ] Name the remaining 143 SDK entry points
      the engine calls — 193 of 336 are named, covering 86.7% of call sites.
      50 of the rest are leaves the call graph cannot reach (F81), so
      instruction-sequence matching, string references and the stage-5h data
      route are what is left. The 198 Tremor/`sd_*` functions are attributed
      but not named: Konami edited Tremor, so upstream line numbers do not
      align and ordinal alignment would give names no valid origin (F96).
- [x] **Engine function boundaries recovered** — **16,667 functions** in the
      REL.
- [x] **221 engine functions classified by SDK usage**, 177 of them renderer
      code. Signature matching and debug strings both proved dead ends there
      (only ~25 names in strings); cross-module SDK calls are what scales.
- [x] **The GX surface the game actually uses is known: 81 functions** of the
      SDK's ~200 — `config/gx-surface-used.txt`. This is the phase 3 scope.
- [x] **Display lists confirmed used** (`GXCallDisplayList`), so the FIFO
      command parser is required rather than merely prudent.
- [x] **The symbol map feeds DolRecomp's patch table** via `tools/make-map.py`
      and `--map`; 68 of 69 used GX functions are exposed as hook points.
- [ ] Name the 266 unnamed DOL functions the REL calls directly. Each is an SDK
      entry point the engine demonstrably uses; these are the highest-value
      naming targets left.
- [ ] Name engine functions in the REL itself. Ghidra work, the long tail.
- [ ] Recover engine function boundaries with `dtk` and Ghidra + the Gekko spec.
      Engine functions are mapped by address only; that is sufficient.
- [ ] **Decompile, in Ghidra, whatever signature matching misses.** An SDK
      function only matches if the same build appears in a decomp we have; for
      the rest, read the pseudo-C and recognise it by behaviour. The same tool
      resolves jump tables and tells code from data. Targeted decompilation is
      cheap and in scope — it is a *matching* decomp of the whole binary that
      is not. See the design doc, "Where Ghidra's decompiler earns its place".
- [ ] Run the game in Dolphin and log every SDK call for the first 60 seconds.
      This log is the specification for what phase 2 must implement, and it is
      now also the fastest way to name the unmatched GX functions: a logged call
      with a known caller identifies the callee.

**Open questions from the design doc that phase 0 answers:**

- [x] **DOL plus one large REL.** `main.dol` 1.9 MB; `mgso_pal.rel` **5.7 MB**
  — most of the game's code is in the overlay, and both must be recompiled.
  The engine has its own `rel_loader.c`. *Remaining: function count, and whether
  `rel_loader.c` wraps `OSLink` or replaces it.*
- [x] **Directly.** The SDK lives entirely in `main.dol`; the REL holds no
  copy and calls in by relocation.
- Which disc-change path the game uses (`DVDGetCurrentDiskID`, cover polling) —
  this determines the virtual two-disc mount.
- [x] **Stock AX, no custom microcode** — phase 4 takes the cheaper branch.
- [x] **The stream codec is Ogg Vorbis (Tremor), not DSP-ADPCM.** The game
  decodes it in software, so the translated code may simply run as-is rather
  than needing a native decoder.
- [x] **The opening video is never STARTED** (F148). `movie.dat` is opened at
  init and never read — zero of the boot's 271 disc reads touch it — so
  playback is not being requested rather than failing. And the decoder is the
  game's own code: `mpegGCN.c` is in the REL, already recompiled to native.
  The runtime needs a **presentation path**, not a decoder, plus whatever
  gates the start.
- [x] **Neither — it is MPEG.** 95 MB in `files/shared/movie.dat`, decoded by
  the game's own `mpegGCN.c`. The runtime needs an MPEG decoder and a
  presentation path; that is new work the plan did not carry.
- The CARD enumeration surface, since Psycho Mantis reads *other games'* saves.
- Whether game logic is frame-locked at 30 fps — decides whether phase 6's 60
  fps is possible at all.

---

## Phase 1 — Boot in ModernGekko

*Exit: title screen renders through recompiled CPU code with no interpreter
fallback hits on the boot path.*

- [x] **DolRecomp run over `main.dol` and `mgso_pal.rel`** — **1,232,487 of
      1,234,136 instructions decoded (99.87%), 0 unknown**. The REL decodes at
      100%. 295 MB of C, 11.5M lines, 303 chunks. The remaining 1,649 are
      embedded data in `.init`, not failures.
- [x] **Self-modifying-code warnings checked and benign** — all 124 are cache
      maintenance, exception-vector installation or DMA writes in SDK
      routines. No self-modifying game code.
- [ ] Build under the ModernGekko/RecompCore template, Dolphin providing GX,
      audio and HLE. Submodules are ~1 GB; ModernGekko is a Dolphin-derived
      C++ build.
- [x] **REL dispatch route confirmed**: the game loads its overlay via `OSLink`
      (one call site, from `rel_loader_LoadRel` at `0x800066F8`), so the
      runtime's existing REL machinery can hook it. No custom loader to
      reimplement first (F27).
- [ ] Wire the REL: ModernGekko hardcodes `files/_Main.rel`; ours is
      `files/shared/mgso_pal.rel`.
- [x] **It boots.** Module loads at `entry=0x80005240` (`__start`), Konami logo
      renders at ~43 fps.
- [ ] **One interpreter fallback to clear**: chunk `[0x800455E0,0x800495E0)`,
      caused by SDK stub patching in the `Hu_IsStub`/`AMC_IsStub` region, which
      invalidates 16 KB and drags a dozen GX functions into the interpreter
      with it (F29). Exit criterion needs this at zero.
- [ ] Stand up the differential harness: `OSReport` diff and guest-memory
      checksums at fixed frames, Dolphin vs. port. When it reports a
      divergence, decompile the *caller* and read what it does with the value —
      usually faster than instrumenting the shim.

---

## Phase 2 — Native OS + DVD + PAD, headless

*Exit: the game runs its main loop headless, reads assets, responds to input,
and its `OSReport` output matches Dolphin's.*

Order within the phase is set by what blocks boot: OS, then DVD, then PAD.

- [x] **Runtime skeleton and build** — `runtime/` with the design document's
      layout, root CMake tree, `ctest` wired.
- [x] **Guest memory** with explicit byte-swap accessors and the cached/uncached
      alias folded, plus tests. Nothing casts a guest structure.
- [x] **Patch table generated from `config/symbols/`** — the mechanism the
      design document calls the single most important decision, using the same
      `DOLRECOMP_ENABLE_REPLACEMENTS` hook phase 1 proved.
- [x] **First OS shims**: `OSReport`, `OSGetTime`, `OSGetTick`,
      `OSDisableInterrupts`, `OSEnableInterrupts`, `OSRestoreInterrupts`.
- [x] **Scheduler selection policy** — priority-ordered, cooperative, one
      thread at a time, `OSThread` fields read from guest memory at the SDK's
      offsets. Tested: `RUNNING` is runnable, ties keep the incumbent, suspend
      blocks, duplicates refused (F39).
- [ ] **OS** — the context switch itself (fibers or `ucontext`), arena
      allocator over guest memory, monotonic clock scaled to the 40.5 MHz
      timebase, alarms driven from the frame loop, REL loader with relocation.
      *The high-risk item: threading semantics must be exact.*
- [x] **Host worker pool** (`runtime/platform/jobs.c`) — sized from the core
      count, leaving one for the guest thread. This is where multi-core is
      available: guest threads cannot be parallelised (F39/F40), the runtime's
      own work can.
- [x] **FST parsing and path lookup** — validated against the real 1,653-entry
      disc FST, which independently recovers the REL's recorded size (F41).
- [x] **Disc layer** — image and extracted-folder backends behind one
      interface, kind detected not declared, short reads honest (F42).
- [x] **Asynchronous reads** — on the worker pool, data copied into guest
      memory on the guest thread's drain, `DVDCommandBlock` status moving
      BUSY → END, tested with 32 reads in flight (F46).
- [x] **DVD shims wired into the patch table** — `DVDConvertPathToEntrynum`,
      `DVDOpen`, `DVDFastOpen`, `DVDClose`, `DVDReadAsyncPrio`,
      `DVDGetCommandBlockStatus`. 12 SDK functions patched (F47).
- [x] **Virtual two-disc mount** — and it needed no drive-state machine. The
      engine uses `DVDGetCurrentDiskID` + `DVDCompareDiskID`, not cover
      polling, so the swap is answering differently (F48). Phase 0 open
      question closed.
- [ ] Disc path resolution: explicit argument, then config, then conventional
      locations. The image is never required to sit beside the executable. `.iso`/`.gcm` direct, `.rvz` decoded,
      extracted folder for development. `.nkit` refused.
- [x] **The boot ROM's arena ceiling** (F84) — the apploader places the disc's
      filesystem table at the top of MEM1 and gives the arena everything
      below. Left at zero, the SDK falls back on the DOL's `__ArenaHi` symbol
      a megabyte lower, the engine's 17.8 MB heap block fails, and the game
      panics in `memory.c:1197`. **That panic is now gone and the game runs
      in its own overlay.**
- [x] **The overlay's `.bss` zeroed after linking** (F85) — the recompiled
      module places it on top of the REL's relocation tables, so every engine
      global read relocation data instead of zero.
- [x] **Read-only host observation of the guest** (F86) — call watching, call
      tracing with arguments, and a hook for the window between `OSLink`
      returning and the overlay's first instruction.
- [x] **The engine loads its data** (F87, F88) — relative path components in
      the FST lookup, which the game uses for every file it opens, and
      `DVDReadAsyncPrio` reading by disc offset as the SDK does rather than
      searching for a name. Before this, `DVDReadAsyncPrio` was called
      16,907,347 times in one run with **one** read completed, because the
      engine retries a refused read for ever rather than failing.
- [x] **Watching a run that takes minutes** (F89) — `MGS_HEARTBEAT` reporting
      the game's own progress rather than host steps, Ctrl-C ending a run with
      its report intact, and allocator tracing with file and line.
- [ ] **Virtual two-disc mount** — mount both images at startup; when the game
      polls for disc 2, report cover opened, disc 2 inserted, cover closed, on
      the SDK's expected timing.
- [x] **A window** — SDL3, 640×480 XFB geometry, nearest scaling, with a boot
      overlay showing real state from the first frame (F52).
- [x] **MMIO layer** — the `0xCC000000` register block, with set-and-wait bits
      completing as hardware would (F56).
- [x] **Interrupts** — raised from the run loop and dispatched through the
      guest's own `__OSDispatchInterrupt`, with the guest's mask honoured
      (F58). Needed a host-to-guest call primitive with a sentinel return.
- [x] **Guest time advances** — the timebase at 40.5 MHz, driven by the run
      loop. Without it every timed wait in the SDK spun forever (F59).
- [x] **Patch table sees intra-chunk calls** — solved without touching
      DolRecomp: `tools/inject-patch-guards.py` post-processes the generated
      chunks, inserting a dispatch check after each patched function's label.
      Idempotent, 36 guards across 5 files. Native SDK calls went 168 → 210 →
      237 immediately, and every future shim inherits the fix (F60).
- [x] **Lazy floating-point context switching** — the SDK traps to 0x800 by
      design; the host now performs `OSSwitchFPUContext` against its own
      register file (F61).
- [x] **Paired singles and the quantisation registers** — `HID2[PSE]`,
      `GQR0-7`, `SRR0`/`SRR1` mirrored into the CPU state the generated code
      reads, not a shadow table it cannot see (F62).
- [x] **Interrupts delivered as a state transition**, not a call:
      `__OSDispatchInterrupt` never returns, it ends in `OSLoadContext`'s
      `rfi` (F63). With the run loop's stale-pc bug fixed (F64) the game began
      scheduling threads and rendering.
- [x] **`MSR[EE]` is the one interrupt flag** — the shims act on the processor
      bit the host gates on, not a private copy (F65).
- [x] **Device interrupt lines modelled into PI** — VI's display interrupts and
      the pixel engine's acknowledge, so PE-finish is reachable and
      `GXDrawDone` completes (F66).
- [x] **The boot ROM's low-memory globals** — disc ID, 24 MB, retail hardware,
      bus/core clocks, PAL TV mode. Without them the game believed it was on a
      development board (F69).
- [x] **Synchronous DVD reads** — `DVDReadAbsAsyncPrio`, the entry point
      `DVDReadPrio` actually uses, plus absolute-offset disc reads mapped back
      through the FST for an extracted disc. The DVD state table was wrong by
      one sign and that alone reset the game (F67).
- [x] **The second addressable window** (0x7E000000, 32 MB) and the overlay
      recompiled at its real load address, so guest and recompiled addresses
      agree for loads as well as branches (F68).
- [x] **VI** — retrace interrupts delivered through the guest's own handler,
      display interrupts asserted and acknowledged. *Presenting the XFB is
      phase 3's: nothing writes it until there is a renderer.*
- [x] **PAD mapping** — GC layout, analog triggers with a late digital click,
      C-stick, inverted Y, SDK-clamped stick ranges. Pure layer, tested with
      no hardware present (F49).
- [x] **SDL3 gamepad backend** — the first SDL3 in the tree. Optional in the
      build: absent, everything still compiles and tests.
- [ ] Wire `PADInit`/`PADRead`/`PADClamp` into the patch table.
- [ ] GX calls log and discard.

---

## Phase 3 — GX renderer

*Exit: the title screen, the Dock and the Heliport render correctly at native
resolution, compared frame-by-frame against Dolphin screenshots.*

**This phase does not have to wait for phase 2.** Dolphin's FIFO recorder
produces a `.dff` containing the complete renderer input — the FIFO command
stream, texture and vertex memory updates, and the full BP/CP/XF register
state. The renderer can therefore be built and tested against recorded frames
with the game not running, and compared pixel-for-pixel against Dolphin's
output of the same file (F45). Record the tooling; never commit a recording,
which contains the game's own graphics data.

This is the project. ~200 functions and the widest error bars in the plan.

- [x] **Vertex converter** — `runtime/gx/vertex.c`. Direct and 8/16-bit
      indexed, u8/s8/u16/s16/f32 with the attribute table's fractional shift,
      every colour format including the 24-bit 6666, arrays read through the
      CP base/stride registers. Everything becomes one `MgsGxVertex`, so the
      rasteriser has one layout to be correct about.
- [x] **FIFO command parser** — `runtime/gx/fifo.c`. The stream is bytes, not
      calls: a draw command's LENGTH depends on registers set arbitrarily far
      back, so the parser carries CP/XF state forward and refuses to guess a
      size it cannot compute. One wrong length desynchronises everything
      after it, so a refusal is counted and recovered from rather than
      papered over. Nested display lists execute inline with their own parser
      state; strips, fans and quads expand to triangles with the winding the
      hardware uses.
- [x] **Transform, projection and viewport** — position matrices selected per
      vertex from XF memory, the packed 6-float projection with its separate
      orthographic flag, and the viewport's 342-pixel bias.
- [x] **Scanline rasteriser** — `runtime/gx/raster.c`. Edge-function fill,
      depth buffer with the full comparison set, back-face culling by area
      sign, perspective-correct Gouraud interpolation. Tested end to end from
      raw command bytes to a known pixel (`tests/test_gx.c`).
- [x] **The rasteriser runs on every core** (F160, F161). A sampling profiler
      (`MGS_PROFILE=1`, built because `perf` is not on this host) put 36% of
      the whole program in `combine` — nine lines of integer arithmetic that
      were an out-of-line call made four times per pixel. Inlining it took a
      boot from 42.3s to 39.3s; splitting large triangles into bands of
      scanlines across the host worker pool took it to **10.6s**, 13.5 to
      51.4 Mpx/s. Bands own disjoint rows, so nothing is shared, no
      arithmetic changes, and the framebuffer stays bit-identical
      (MEM1 `0x8C8E2DB54E773E25`) — which is what makes the number
      believable. `MGS_RASTER_THREADS=1` is the serial control. This is a
      stopgap for phases 1–2, not a substitute for the design document's
      Vulkan backend.
- [x] **The route to the screen** — an EFB that GX clears, `GXCopyDisp`
      executed for real from the pixel engine's copy registers, BT.601
      conversion to YUV 4:2:2 at the address the game programmed, and
      scan-out from the video interface's own register (`tests/test_efb.c`).
- [x] **Dynamically updated textures** (F156). The cache keyed on address and
      never looked at the contents, and nothing invalidated it, so a texture
      rewritten in place was served stale for the life of the run — which
      froze the intro movie on its first decoded frame. Now matched on an
      FNV-1a hash of the encoded bytes, sampled on a stride above 4 KB, with
      a changed texture reusing its own slot rather than adding a second
      entry.
- [x] **Textures** — `runtime/gx/texture.c`. All eleven formats with their
      tiling, including RGBA8's two-halves-per-tile layout and CMPR's
      big-endian DXT1 with reversed selectors; palettes read from where the
      game keeps them rather than from an emulated texture memory; an
      address-keyed cache with least-recently-used replacement; clamp, repeat
      and mirror wrapping with bilinear filtering and the half-texel offset.
- [x] **Texture environment** — `runtime/gx/tev.c`. The general combiner, not
      the SDK's named modes: sixteen stages of
      `d + lerp(a, b, c)` with bias, scale and clamp, on colour and alpha
      independently, chaining through the four colour registers. Plus the
      alpha test, which is what makes cut-out art work at all.
- [x] **THE KONAMI LOGO RENDERS.** 2026-09-19. 108 textured triangles, one
      texture decoded and 107 cache hits, 0 parser desyncs, 12.5 million
      pixels, 55 frames copied to the external framebuffer and presented.
      Drawn entirely by `runtime/gx/`; `ldd` on the host lists SDL3, libc and
      libm and nothing else.
- [x] **The boot is measured, not guessed at** (F91, F92). `MGS_PROFILE`
      samples the guest pc at a prime interval **coprime to every period in
      the run loop** — the old heartbeat at 2,000,003 aliased with the
      2,000-step retrace tick and put every sample in
      `__OSDispatchInterrupt`, which reads as a hang. `MGS_PROFILE_CALLERS`
      attributes arrivals at an address to the link register.
      `tools/resolve-addrs.py` names the addresses afterwards, so an old dump
      can be re-resolved as naming improves.
- [x] **86% of the boot was two functions with no logic in them** (F92, F93).
      `memcpy` 62.8%, `__fill_mem` 23.5%. Nineteen memcpy calls in the whole
      boot; **one moves 5.7 MB** — the overlay, copied byte-at-a-time into the
      second address window. `runtime/os/mem_shims.c` does all three natively,
      as `memmove` because the guest's `memcpy` chooses its direction.
      **The game was never stalled after the logo — it was copying.**
- [x] **The renderer is reached through a function pointer** (F94), so the
      call graph cannot find it. `fn_1_F394C` is a 12-level task scheduler;
      `mgs_dump_tasks` in `host/heaps.c` dumps the table, the per-level gate
      and the per-node flag bits.
- [x] **The scissor box** (F100). It was parsed into `bp.h` and never read,
      so every triangle was clipped only to the framebuffer. The hardware
      will not write outside the box, so ignoring it draws pixels the game
      did not ask for - and a render-to-texture pass, which puts a small box
      in a corner of the embedded framebuffer, scrawls over everything else
      in there. Both the box and its origin register are honoured, and
      `bp.written[]` now distinguishes an offset SET to zero from one never
      set. Checked by disabling it and confirming the test fails at exactly
      the first pixel past the box's edge.
- [x] **The engine draws a sphere** (F98) — `gcn_emit_sphere_strips`, 32
      strips of 66 vertices with position and normal, the normal's z from
      `sqrtf(1 - x*x - y*y)`. Around 2,048 triangles, almost certainly a
      sphere map built by render-to-texture. This is the first engine
      geometry the port has reached, as opposed to the boot logo.
- [x] **Display-list RECORDING is no longer executed as drawing** (F102).
      `GXBeginDisplayList` points the CPU-side FIFO at a buffer and leaves
      the graphics processor's alone; the game keeps storing to the
      write-gather pipe but the data is being written down, not drawn. The
      host executed it, under whatever vertex descriptor was live rather
      than the one the list will be called under. Comparing the two FIFO
      descriptions says which is happening. **Desyncs 198 to 0.**
- [x] **The DSP mailbox handshake** (F105). `__DSP_boot_task` waits for the
      DSP to post 0x8071FEED and then sends a dozen messages, spinning after
      each until the DSP takes it. A sent message is consumed as the send
      completes, reading the mailbox's low half empties it, and unhalting the
      DSP posts the boot message. **The handshake, not a DSP** - no microcode
      runs. 90,000,000 steps of spinning became 60,000,000 of running.
- [x] **The divergence is a failed disc read, and a race of mine** (F205).
      Diffing two diverging logs shows one extra line - `READ FAILED: 32768
      bytes at 0x2B642960` - with everything after following from the retry.
      The offset is valid (inside `stage.dat`), and on that path the per-file
      tally I added today raced: `read_job` runs on the worker pool and the
      tally did an unguarded search-then-insert from several threads. Locked,
      the requester documented as approximate, and `READ FAILED` now says
      why. Whether the race caused the failure is not yet shown.
- [x] **Determinism returns with the race fixed** (F206). Five 120M runs
      byte-identical where one in three diverged before; `MGS_JOBS=1` gives
      three identical runs; and the race was reachable only from worker
      threads. **Fifteen consecutive byte-identical runs**, P=0.0023 against
      the old one-in-three rate.
      **A diagnostic that writes shared state is part of the program.**
- [x] **Two corrections at the root** (F215). The two type-5 starts are one
      pass over two stream objects, not two one-shot sites - `fn_80053988`
      traces at exactly 1 call - and I had mapped a return address to the
      wrong `bl`, a half-instruction error that inverts the conclusion. The
      refill site at `0x80053AF8` is still never reached. Also:
      `MGS_TRACE_FN` **cannot see functions entered by fall-through** - a
      zero means "never entered at this address", not "never executed".
- [ ] **Two faults between us and AX** (F219). We post DSP task mail
      `0xDCD10003` (**done**, which *unlinks the task*) where a persistent AX
      task needs `0xDCD10001` (**resume**) - and resume is the only thing
      that sets `__AXOutDspReady`, without which no mixing frame runs and no
      PCM is pulled from Vorbis. `MGS_DSP_RESUME=1` selects it; not default,
      because AX then reaches unmodelled paths. And un-stubbing
      `__OSInitAudioSystem` (now possible per-address with `MGS_UNPATCH`)
      segfaults in **`EXIGetID+0x2D0`**, not in audio.
- [ ] **Determinism is load-dependent** (F220). Two HEAD runs, both
      completing 120M steps, gave 5 and 9 files at **load average 70**.
      F206's fifteen identical runs were on an idle machine. Something
      besides the fixed data race is sensitive to host scheduling.
- [x] **The movie is paced by audio consumption** (F218). `fn_80055114`
      re-arms a slot when a **playback position** reaches its end marker
      (`0x8000`); that position is advanced by `fn_80056688`, which traced at
      **13 calls of 0x400 bytes - 13 KB of the 32 KB needed** - and which is
      a **Vorbis read callback** (`framing.c` surrounds its caller). So the
      chain runs: audio consumer -> Vorbis decode -> read callback ->
      position -> slot re-arm -> refill -> disc read. Every link of the
      earlier chain was read correctly and **in the wrong direction**.
      **This reframes F188:** the audio DMA running for 93 s measured the
      hardware, not whether the game's mixer consumes decoded PCM.
- [x] **`MGS_TRACE_FN` counts are lower bounds** (F217). `fn_80053B60` is
      called by exactly one `bl` and demonstrably ran - a watch caught it
      writing memory - while the tracer reported **0 calls**. Translated code
      calls translated code directly in C, so a function is counted only when
      it is where a dispatch starts. **Watches and host counters are exact
      and unaffected; trace counts are floors.** Use the tracer for *who* and
      *with what*, not for *how many*.
- [x] **The oracle works, and corrected the root** (F216).
      `tools/dolphin-watch.py` reads guest RAM out of a running Dolphin via
      its shared-memory mapping - no GDB stub (this build has none) and no
      UI. First measurement: `0x8021A078` reads **3 in a working run**, the
      same as ours, so **F214's root cause was wrong** - 3 is the configured
      mode, not a stall. The real divergence is that the slot bytes
      **recycle** in Dolphin (~10 times in 76 s) and are frozen in ours.
      Caveat: the two runs are not yet aligned to the same game moment.
- [x] **Use Dolphin as the oracle for the stream state** (F215). It is
      installed and both discs are on disk; watching `0x8021A078` in a
      working run answers what advances it, which sixteen links of backward
      derivation have not.
- [x] **The root: one state word, written once** (F214). `0x8021A078`, the
      stream object's state, changes **exactly once all run** - `0 -> 3` -
      and `fn_80053988` dispatches on it: state 1 asks for a header read,
      **state 2 asks for the refill**, state 3 asks to start. Stuck at 3 it
      can only ever start. The loop that would move it is cut in a circle:
      state 2 needs `obj->0xD6 & 0x4` (watched: `0x00` all run), that bit is
      set only by `fn_80053200` (traced: **0 calls**), which is the
      completion callback of a 96-byte `DVDReadAsyncPrio` issued only by the
      type-1 case, which needs state 1. **Every part behaves as written.**
      Open: what advances the state off 3.
- [x] **The refill message type is never sent** (F213). The dispatch maps
      precisely: **types 3 and 5 alone** return a slot to service, type 5 is
      a one-shot start (2 calls, one per stream object, from two distinct
      sites), and **type 3 - "prepare a slot, reply with type 4" - is never
      sent by anyone.** Every message reaching the queue is accounted for:
      a routed type 5 x2, a wake-up sentinel x2, and 26 of types the table
      sends elsewhere. The refill case is compiled, reachable and correct,
      and nothing asks for it. **Open: what should send type 3.**
- [x] **The stream thread's dispatch is mapped** (F212). A 17-entry jump
      table on `msg->0x08`; **exactly two types, 3 and 5, make a slot ready**
      and both go through `fn_800534D4`, which runs **twice in a whole run**.
      Of the four senders to its queue, one emits type `0x0F` twice, one type
      `0x0C` thirteen times, and the remaining pair are unread - including
      `0x80052FF8`, the slot-DONE site, which sent exactly two, matching the
      two refills. **Next: read that message's type field properly.**
- [x] **A fourth stale copy, in `docs/decompilation-process.md`** (F211a).
      Its summary table read 1,006 / 5.4% and 198 SDK entry points, with
      `mgso_pal.rel` at 9 where the maps hold 27. Corrected, prose included,
      and `--check` now verifies it. Of four places carrying these figures,
      the two that were checked stayed right and the two that were not both
      drifted.
- [x] **README's progress block is generated and checked** (F211). It had
      gone stale by 46 functions - 1,006 against 1,052 - because `--check`
      covered HANDOFF and MILESTONES but not README, whose block only stayed
      current if someone remembered to run `--readme` and paste it.
      `--write` now regenerates it in place and `--check` verifies it.
- [x] **The far end is `sd_stream2.c`'s own thread** (F210). `fn_80055264`
      (size `0x4C4`) is a live stream thread, blocked in `OSReceiveMessage` -
      and it is **not** starved: its queue takes 30 messages from four sites.
      Its two slots are made ready **once** (watched: five state changes all
      run), used, and marked done, and nothing returns them. Two 32 KB slots
      is 64 KB, exactly where the destination pointer stopped. **Open: which
      of those 30 messages should ask for a refill.**
- [x] **The whole chain, measured end to end** (F209). Thirteen links from
      the frozen picture back to its cause, every one a counter from a
      reproducible run: `fn_80054D14` in main.dol posts to the drain's queue
      **twice in 60 calls and never again**, so the drain succeeds twice, so
      the destination buffer never wraps a third time, so its space check
      fails, so kind-1 records are peeked and replaced 1,085 times, so the
      ring head is pinned, so the refill fires twice, so **movie.dat is read
      8 times - 0.28% of it** - and everything downstream follows.
      **Open: which condition inside `fn_80054D14` gates the send.**
- [x] **The pinning record is identified** (F208). The head advances 456
      times and stops at `0x817789F0`; that record's tag oscillates
      `1 -> 0x81 -> 1` **2,171 times** between `gcn_pool_acquire` marking it
      taken and `gcn_pool_clear_entry_flag` putting it back - **found and
      returned 1,085 times, never consumed.** `fn_1_8FE8` is a peek looking
      for an *empty* kind-1 record and correctly leaves one that has data.
      **Open: who consumes a kind-1 record that has a payload.**
- [x] **The refill fires twice in a whole run** (F207). The pump is healthy -
      2,204 calls, all normal exits, `pool->0x38` zero throughout - and the
      trigger it guards, `free > capacity/3`, is true only **twice**. The
      ring reclaims space from the **front only**, stopping at the first
      occupied record, so one unconsumed record at the head pins it for ever
      and the refill never fires again. **That is why nothing asks for the
      next byte of `movie.dat`.** Next: which record sits at the head.
- [x] **The record producer is found** (F206). `fn_1_132368` wraps
      `fn_1_1321A8`, the pool's pump, which bails on `pool->0x34` - exactly
      the flag that makes `gcn_pool_acquire` return NULL - and feeds from
      `fn_1_9C8` in the file-service cluster.
 `MGS_JOBS=<n>` now
      sizes the worker pool so concurrency can be isolated as a cause. Three 120M runs,
      same build and arguments: two byte-identical, the third differing by
      one refused DVD read. The DVD model is designed against exactly this -
      completion is decided by the guest clock, with a busy-wait so host
      thread scheduling cannot be observed - so something escapes that, and
      it is not yet found. Replay against Dolphin is the design's oracle for
      divergence, so this blocks that. **A single run is not evidence.**
- [x] **The tracer sees host-invoked calls** (F204). `MGS_TRACE_FN` matched
      `pc` only in the run loop; `mgs_module_call_guest` has its own. Now one
      `fntrace_step` called from both.
- [x] **The tracer is blind to host-invoked callbacks** (F203).
      `gcn_stream_read_done` traces as 0 calls while the host counts 406
      callbacks run: `MGS_TRACE_FN` matches `pc` in the run loop, and a
      callback the host invokes never passes it. Worth knowing before
      trusting a zero. The stream itself is clear - the movie asked for
      256 KB and got 256 KB - and at the frame where the picture stops the
      task mask still reads **zero**, so the F196 deadlock is a consequence
      arriving 600 frames later, not the cause.
- [x] **Correction: the pool is a record ring and nothing leaks** (F202).
      `gcn_pool_acquire` compares the entry tag to its `kind` argument - it
      **finds** a record, it does not allocate one - so acquires and frees
      were never required to balance and the "1,101 leaked" accounting was
      an interpretation fitted to a name I had chosen. Read correctly, the
      movie consumed 719 records spanning ~178 KB of the 256 KB it was given
      and then found none waiting: an empty stream buffer, not a pool fault.
- [x] **A guest-function tracer, and the pool that starves the movie**
      (F201). `MGS_TRACE_FN` reports a guest function's arguments, caller and
      result (`MGS_TRACE_FN_MAX=0` counts silently); validated on
      `DVDReadAsyncPrio` and shown not to perturb. It gives the accounting:
      one shared pool, 8,839 acquires, **1,848 successes against 747 frees**,
      and the 1,101 difference is exactly one caller's successes - `fn_1_8FE8`,
      which disposes of entries through `gcn_pool_clear_entry_flag` (clears a
      header bit) rather than `gcn_pool_free` (zeroes it). **1,049 to 1,052.**
      Not yet established that nothing else frees them.
- [x] **The park traced to one NULL pointer** (F200). The mask's whole
      history is four game-logic writes, each with its writer named; the one
      that sticks is `gcn_task_mask_set(8)`, and it is a deliberate `if`
      whose condition is `*(ctx + 0x25E8) == 0`. That pointer has one real
      setter, `ctx->0x25E8 = fn_1_1323C4(...)`, and that function returns
      NULL either on a busy flag or an exhausted slot array. **1,047 to
      1,049.** Next needs a runtime trace of one guest function's arguments,
      which the host cannot yet do generally.
- [x] **A duplicate `static` broke the boot, and is now checked** (F199).
      `module.c` already held `s_watch_addr` - OSLink's address, from which
      the overlay's `.bss` base is read - and the new memory watch declared
      the same name. Two file-scope statics of one name are ONE object in C,
      silently, so every run overwrote OSLink's address and the boot stopped
      after loading the module. `tools/check-duplicate-statics.py` now runs
      in ctest (**17 tests**) and found a second instance on its first run.
- [x] **Four names out of the deadlock** (F198): `mpeg_movie_task`,
      `mpeg_poll_stream_events`, `gcn_event_poll` and `gcn_task_table_init`,
      **1,043 to 1,047**. The watch confirms the static reading exactly - the
      only two changes to the mask in a boot are the loader's `memcpy`
      bringing the image in and the initialiser zeroing it. So the `0x2450`
      seen earlier was never a cutscene parking the game; it was the module
      still being loaded.
- [x] **A per-step guest watch, and a duplicate removed** (F197). The engine
      task dump already existed in `host/heaps.c`; I wrote a second one and
      both printed before I noticed. Removed, with the new part
      (`MGS_TASK_DUMP`, dumping a task's own node) folded into the real one.
      The mask turns out to have **exactly one `stw` in the whole overlay** -
      the initialiser, writing zero - so `MGS_WATCH=<address>` now names the
      writer of any guest word, checked every step.
- [x] **The movie deadlock, traced end to end** (F196). The decoder task sits
      in state 1, which advances only on event code 1; the event poll
      `fn_1_F52A8` returns **zero events whenever the global task mask is
      non-zero**, and the mask ends at `0x8`. So the bit a cutscene sets to
      park the game also suppresses the event that would end the cutscene -
      a closed loop. **Caveat kept explicit:** the picture stops at frame 960
      and the mask only reaches `0x8` after 1,560, so this explains why
      nothing recovers, not what stops it.
- [x] **The read path is proved innocent, and an over-claim corrected**
      (F195). Reads now record three frames of guest call chain; the movie's
      ended in our own host->guest sentinel, naming `gcn_stream_read_done`
      (the DVD callback) and `gcn_stream_issue_read`, **1,041 to 1,043**.
      406 reads, 406 callbacks, 0 refused: the movie was handed its data and
      told so. And the engine task table shows the `mpegGCN.c` task
      **registered, ungated and not skipped** - so "the decoder never runs",
      asserted twice, was read off a top-30 profile of 2,765 addresses whose
      last row was 0.5%. Depth is now `MGS_PROFILE_TOP`.
- [x] **Blocked queues are decoded, and all are empty** (F194). "Blocked on
      queue 0x..." cannot tell a scheduling fault from a thread waiting for
      something never sent; the dump now decodes the `OSMessageQueue` behind
      the thread-queue and reports occupancy. All five blocked threads sit on
      empty queues. The overlay thread is a healthy worker idling on its
      mailbox - which named `gcn_worker_loop`, `gcn_worker_take_request` and
      `gcn_worker_set_status`, **1,038 to 1,041**. No producer runs, and the
      game reports no error at all.
- [x] **The movie fault is measured, not inferred** (F193). `movie.dat` is
      94,935,040 bytes; the game reads 262,144 - eight 32 KB reads reaching
      exactly `0x40000` - and stops. **0.28% of the file**, and a round
      number: a 256 KB buffer that fills and never drains. The machine is
      not frozen (1,920 frames, 8.8 MB of stage loaded, 93 s of audio); the
      *picture* is, from the frame after the last movie read. Not the task
      mask - that reads 0x0, everything running, right through the frozen
      stretch and only gates afterwards. `mpegGCN.c` never appears in the
      profile and an overlay thread sits blocked on queue `0x7F4A595C`.
      **Next: what posts to that queue.**
- [x] **zlib named by its struct offsets** (F192). `inflate_fast`,
      `inflate_trees_bits`, `inflate_trees_fixed` - the first from seven
      parameter displacements matching zlib's published `inflate_blocks_state`
      and `z_stream` field for field, the other two from a closed region with
      zero-byte gaps. **1,035 to 1,038 functions named.** Stage 5p; new origin
      `struct-abi`. The route generalises to every public library the engine
      embeds, and to none of Konami's own code.
- [x] **The provenance ledger is checked, not just written** (F192).
      Seven origins were in use and undefined, making README's per-symbol
      provenance claim uncheckable for those symbols. `tools/progress.py
      --check` now verifies every origin against the ledger.
- [x] **Batch runs exit again, with their whole report** (F191). `main` holds
      the last frame until the window is closed; under `SDL_VIDEODRIVER=dummy`
      there is no window, so it waited for ever with the report sitting
      unflushed in a stdio buffer - which is how two investigations came to
      read truncated reports without noticing. A windowless driver now counts
      as headless, and stdout is flushed before the hold. 2M steps: hangs
      indefinitely, to exits in 1.2 s.
- [x] **The movie player is `mpegGCN.c`** (F190), REL `0x149128`, runtime
      `0x7F151214` - the intro is MPEG decoded by translated engine code.
      `tools/resolve-addrs.py` now falls back to the file attribution and
      prints `[mpegGCN.c+0xEC]` where no name exists, which is what makes a
      profile of the engine readable at all: its symbol map is 87 lines.
- [x] **A read tally per file** (F189). The sampled disc trace showed one
      read of the movie and seven had happened; `mgs_disc_report` now counts
      reads, bytes and the furthest offset per file at exit. A rate-limited
      log answers what is happening, never how much.
- [x] **Audio is not the movie blocker** (F188). With the engine in, the game
      programmes it and streams 93.43 s of sound in 94.81 s of guest time,
      in buffers that measure **5.000 ms** - the AX callback period, which
      our engine was never told and which falls out of the game's own
      programming against our block rate. Disc reads stay at exactly 115,
      stopping at the same offset. Audio had been the last hypothesis
      standing since F184, by elimination; it is now tested and cleared.
- [x] **The audio DMA engine** (F187). `0xCC005030` was plain storage and
      AID - the completion that asks for the next buffer of sound - was
      raised by nothing. Modelled from Dolphin, which corrects the obvious
      guess: the interrupt fires when the FIFO **starts** a transfer, not
      when it ends, and the engine relatches from the same registers on
      completion. Paced on the guest clock at 4,000 32-byte blocks a second
      rather than completed instantly, because a movie takes its timing from
      these. Eight cases in `tests/test_ai_dma.c`, confirmed to bite by
      three mutations. **Still not modelled:** AIS, the stream-trigger
      interrupt at `0xCC006C0C`.
- [x] **Audio start-up is driven by a test, not by a boot** (F186).
      `tests/test_dsp_init.c` walks `__OSInitAudioSystem`'s register sequence
      against `MgsMmio` directly - reset, mailbox drain, two ARAM DMAs, the
      0x400 wait, un-halt, boot mail, final reset. **Every wait passes**, so
      the handshake is not what stalls the movie. It exists because F184
      showed withdrawing the audio shim crashes for unrelated reasons, which
      makes booting a useless way to test this. A duplicate boot-mail path
      posting 0x80544348 was removed: it contradicted F105 above, and the
      SDK's comparison of that value has an empty body.
- [x] **`rand` and `PSMTX44Identity`** (F106) — 472 call sites between them.
      `rand` is identifiable from its constants alone: 1103515245 and 12345,
      the ISO C standard's own example generator. **Call sites covered 76.1%
      to 82.8%** on two names.
- [x] **The audio stack initialises** (F104). AR, ARQ, AI, AX and DSP all
      report in. Two walls, each one register: the ARAM ready bit at
      `0xCC005016` bit 0, which `__ARChecksize` spins on, and the audio
      interface's sample counter at `0xCC006C08`, which `__AI_SRC_INIT`
      times against `OSGetTime` to work out which clock it is on. Audio RAM
      and its DMA engine are in `runtime/dsp/aram.c` - 16 MB, which is what
      phase 4 needs anyway. **No sound is produced yet**: this is the
      hardware coming up, not a mixer.
- [x] **The boot's stopping point is a livelock, and it is one event**
      (F122-F124). `MGS_STEPS=200000000` produces byte-identical output to
      40,000,000 - same 20,429 commands, same 24,716 triangles, same 62
      completions - so the boot stops progressing rather than running out of
      time. The thread burning every step is the engine's own idle
      graphics-service thread (`poll; OSYieldThread; goto`, an
      `OSCreateThread` entry), so the profile shows an idle system, not a
      fault. The fault is a single missing signal: **62 completions
      delivered, 62 acknowledged by the guest, 61 with the frame ring's flag
      set, 60 signalled**, and the main loop's 62nd wait sleeps on a
      semaphore nothing tops up. Three explanations were ruled out by
      measurement rather than argument - see F124.
- [x] **The first livelock is broken: the external interrupt is
      level-triggered** (F126). PI asserts its line while any armed cause bit
      is set, and the SDK's `__OSDispatchInterrupt` services exactly one
      source per entry — so anything still pending must be re-taken.
      `host/interrupt.c` raised only on new events, so every completion
      arriving behind a higher-priority source was lost. `PI_VI` outranks
      `PI_PE`, and the 62nd frame completion was the first to land with a
      retrace pending. At the same 40,000,000 steps: **20,429 GX commands to
      177,805, 24,716 triangles to 514,828, 75 EFB copies to 474**, and the
      boot now stops inside the engine's own code rather than the SDK's idle
      spin. Three runs byte-identical, so F110's determinism survives it.
- [x] **The stuck DSP line: an interrupt raised for no device** (F128). PI's
      DSP bit serves three sources and the SDK's dispatcher reads the DSP's
      own status register to tell them apart; ARAM completions were asserting
      the line and setting no status bit, so no handler could be chosen and
      nothing could clear it. The line now mirrors the device, as VI's always
      has. **`PI cause 0x40` stuck to `0x00000000`, 871,157 re-offers to
      1,122, 55 disc reads to 64**, determinism intact.
- [x] **The third wall identified: the boot is stuck DECOMPRESSING** (F132).
      The hot routine at REL `.text 0xF0F2C` is **zlib's `huft_build`**,
      called from `inflate_trees_dynamic` — proved by five verbatim zlib error
      strings in this binary plus independent structural agreement. It holds
      **81.6% of the boot's samples**. The stuck-ness is now certain:
      **800,000,000 steps produce byte-identical output to 40,000,000** —
      same GX commands, same disc reads, same heap free lists, same task
      table. And inflate is **not failing**: none of its five error strings
      is pointed at by any word in guest RAM.
- [x] **The decompression wall: the host was clobbering CTR** (F134).
      `mgs_module_call_guest` saved `gpr`, `pc` and `lr` and nothing else, so
      a disc-read callback left CTR holding an overlay function pointer —
      `0x7EFFC638` — and the `bdnz` loop in zlib's `while (a--)` had 2.13
      billion iterations to run instead of seven. The host enters guest code
      at an arbitrary instruction boundary, so it is an interruption and the
      whole volatile state must be preserved. **GX commands 177,806 to
      2,294,248, triangles 514,828 to 3,862,060, disc reads 64 to 271**, at
      the same 40,000,000 steps. Three runs byte-identical.
- [x] **The boot reaches a live idle loop** (F135, F136). Frames now scale
      with budget — 3,740 EFB copies at 40M steps, **28,311 at 200M** — so the
      engine is alive rather than wedged, and the load has finished (disc
      reads stop at 271 in both). The renderer is behaving: forcing the depth
      test off gives ten times the lit pixels and does **not** change the best
      frame, so the later frames are genuinely sparse rather than hidden.
- [x] **The command stream is clean: 6,317 desyncs to ZERO** (F137). Every
      one was the same refusal — `GXCallDisplayList(0x8097CAE0, 83)`, once a
      frame, rejected because the SIZE was not a 32-byte multiple. The SDK
      asserts that, but asserts are compiled out of a release build and the
      hardware simply fetches; the ADDRESS is what carries the signal and it
      was always valid. **Pixels written 12,156,928 to 348,188,074, lit
      1,167,715 to 19,526,875, textured triangles 2,716 to 14,356, and the
      best frame 22,034 lit pixels to 26,570 — in 4,258 colours against 278.**
- [x] **THE PORT DRAWS RECOGNISABLE FRAMES** (F138). The boot renders the
      **Konami** logo and then the **Silicon Knights** logo — the latter with
      its sword, its textured green circuit-board cube and gold-edged
      lettering, all correct. First time the port has produced something a
      person would recognise rather than a pixel count. Frames are written
      outside the repository and are never committed (rule 8: the artwork is
      the game's, whoever's code drew it).
- [x] **The boot reaches an interactive menu** (F139). The game's "Warning —
      No Memory Card" screen, with Retry and Continue without saving. It waits
      there because that is what the game does with no card and no input.
- [x] **Textures in the second window now resolve: 2,964 refusals to ZERO**
      (F142). The engine overlay lives at `0x7E000000`, which is not an
      address a GameCube has, so the SDK's cached-to-physical arithmetic
      (mask to 26 bits, shift down 5) turns a pointer into its own static data
      into a physical address that means nothing. The information is
      ambiguous rather than lost: an address that cannot be in MEM1 came from
      the window above it, and `0x7C000000 | phys` inverts the mask exactly.
      **Textured triangles 14,356 to 17,320, cache misses 2,977 to 14.**
      A general hazard of the second-window design, worth looking for
      elsewhere.
- [ ] **EFB-to-texture copies** (F145). `mgs_efb_copy` writes nothing unless
      the copy targets the external framebuffer, so all **1,883**
      render-to-texture copies in a boot discard their output — and
      **3,860,150 of 3,873,706 triangles** are drawn into that path. This is
      what truncates the memory-card screen's text: the quads are full width
      and the texture they sample was never written. Needs an encoder for the
      copy formats, **GB8 (0xC)** first, writing tiled at
      `copy_dest`/`copy_stride`. The truncation was chased through the
      scissor, depth buffer, display lists, textures, viewport and geometry
      first — all excluded by measurement (F139-F144). **It is not the cause
      of the truncated text** (F146): nothing samples what those copies write,
      and the text's own texture is fully populated. Still worth implementing
      on its own merits.
- [x] **The boot's end is a prompt, not a wall** (F149). It reaches the game's
      "No Memory Card" warning and cannot leave it: **3** serial-interface
      reads in a whole boot, so PAD found no controller and stopped polling,
      and nothing can press Retry or Continue. The port is doing exactly what
      a console with no card and no controller would do.
- [x] **A CONTROLLER IS REPORTED AND THE BOOT GETS PAST THE WARNING SCREEN**
      (F153). 2026-09-19. The serial interface was a stub that cleared the
      transfer-start bit and never finished a transfer, so the SDK asked for a
      device id once and never read the reply. Transfers now complete, empty
      ports report NOREP, enabled ports are polled every field, and RDST is
      cleared when the input buffer is read — that last part is what makes it
      work rather than stall. Keyboard maps to the button word in
      `sdl_video.c`; `MGS_PAD_SCRIPT` drives a run through a menu unattended.
      **The intro video plays.**
- [x] **A frame rate cap** (F163). Splitting the rasteriser across cores
      removed the thing that had been pacing the game by accident, and the
      intro logos ran far too fast. Capped on the XFB copy — one finished
      game frame — rather than on the retrace tick, which fires seventeen
      times more often. `MGS_FPS_CAP`; headless stays uncapped so it stays
      reproducible. The windowed default of 60 is a guess and the correct
      figure for this PAL build is not yet established.
- [ ] **The intermittent freeze in the pad poll** (F162). A boot wedges in
      `gp_poll_once + 0x74`, reached from `gp_poll_thread`, at exactly 13,060
      GX commands — the number `mgs_mmio.c` already records as the signature
      of a serial-interface stall fixed once before. Intermittent because
      runs are not deterministic: DVD reads finish on host worker threads, so
      the same binary completes 271 or 287 of them. `si_poll_frame` runs off
      the step-counted frame tick, so the race is elsewhere in the SI path.
- [x] **A memory card** (F172). Mounts, and the notice screen it was causing
      is gone — a headless boot now reaches the intro movie with no pad
      script. Emulated as an EXI device with a pre-formatted image persisted
      to `saves/slot_a.raw`, validated on load, and the machine's SRAM and
      clock alongside it. The last fault was one byte: the flash id's
      checksum in SRAM, which the mount verifies and which must be the
      complement of the id's sum. Saving and loading real files is still to
      come; the device and the mount are done.
- [ ] **`verify-hash`, a native binary.** The hashes are recorded in
      `config/GGSPA4.toml` and **nothing checks them**: project rule 8 and
      the design document both describe this tool as though it exists, and it
      does not, so the build currently accepts any `main.dol` it is pointed
      at. Closing that makes a claim the project already makes about itself
      true. It hashes the extracted executables rather than the disc image,
      so container format does not matter and only a different revision is
      refused — which is correct, since every address in `config/symbols/` is
      keyed to GGSPA4.
- [ ] **`extract-disc`, a native binary.** Pulls `main.dol` and
      `mgso_pal.rel` out of an image so `verify-hash` and the recompiler have
      something to work on.
- [ ] **A first-run launcher** (design document, phase 6). Asks for the two
      images, hash-checks them, runs the recompile, and leaves the module in
      `module/` beside the executable — where the app already finds it. Not
      first-run generation inside the game process: recompiling emits 50-150
      MB of C and then compiles it, which needs a toolchain and minutes, and
      does not belong behind a game window.
      **These three are native binaries, not Python**, because they ship to
      people with a game and a computer rather than a development
      environment. The scripts under `tools/` stay scripts — different
      audience, one that already has the toolchain.
- [x] **Launch without a wrapper script.** The module is found rather than
      demanded: `MGS_MODULE`, then a `module` folder beside the executable,
      then the executable's own folder, then this repository's build tree —
      any file ending `_recomp.so`, since its name carries the game id and a
      player has one. Discs were already located the same way, including
      beside the executable for a portable folder — though that had
      never actually worked, because `main.c` passed NULL where the locator
      takes the executable's directory, leaving every search relative to the
      binary as dead code. Both now take it and walk up from it.
      **Verified with no arguments from the repository root, from `build/`,
      and from an unrelated directory**: the disc and the module are found in
      all three.
      **`module/` and `*_recomp.so` are git-ignored** — the module is
      generated from the player's own disc, so it is game-derived code and
      rule 8 forbids committing it. That is precisely what lets the
      executable be distributed.
- [ ] **A memory card, superseded detail** (F166, F167). Emulated as an EXI *device*, not as
      SDK shims: the SDK identifies it correctly — 16 Mbit, sectors of 8192,
      status READY and UNLOCKED, EXT set — off a pre-formatted 2 MB image
      persisted to `saves/slot_a.raw`. It does not yet mount; next is
      the RTC/SRAM device on channel 0 device 1, which `DoMount` reads while
      mounting: four of nine EXI transfers in a boot go to devices that are
      not implemented, and the mount fails with IOERROR because of it.
- [ ] **A memory card, mounting** (F166). Not low difficulty after all, and not
      stubbable at the SDK's API: the SDK reads the card's header, directory
      and FAT directly out of the work area, so answering READY to
      `CARDMount` and `CARDCheckEx` leaves it finding nothing and
      unmounting. Emulating the device on EXI — a 2 MB image backed by a
      host file — makes the SDK's own code work unmodified and gives real
      save/load. Phase 5 by the plan; bringing it forward would unblock the
      reproducible headless path to the intro movie.
- [ ] **Play back what is recorded.** The sphere-map geometry now lands in
      its buffer correctly; `GXCallDisplayList` has not yet been reached
      within the step budgets run so far, so the playback path is written
      but not exercised against it.
- [x] **Something renders correctly** (F129). The fullest frame of a boot
      holds **22,034 lit pixels, 9.6% of 512x448, in 278 distinct colours** —
      a structured banner across the middle of the screen. The whole route
      works end to end: FIFO, vertex decode, transform, viewport, scissor,
      depth, combiner, EFB and copy-out.
- [x] **The renderer is doing what it is told** (F130). The untextured
      majority uses exactly two combiner configurations — one that says "take
      the vertex colour" (white, 348,160 small triangles, and these are the
      lit pixels) and one that says "output zero" (165,888 large ones). Both
      compute correctly by hand against the implementation. **Corrected by
      F152:** the first configuration is `0x08FACF` = `a=ZERO b=RASC c=ONE
      d=ZERO`, which computes `RASC` — the vertex colour, and that colour is
      `0xFFFFFFFF`. Most untextured draws emit *white*, not black; only the
      165,888 at `0x08FFFF` are black. A black screen
      with a logo banner is a boot screen, not a broken renderer.
- [x] **Blending and the colour/alpha update masks** (F152). `BP_BLEND_MODE`
      (0x41, CMODE0) was defined and read nowhere, like `BP_ZMODE` before it.
      Now honoured per draw at the pixel write, with the blend factors taking
      which operand they are computing (ids 2 and 3 name the *other* side, so
      `GX_BL_SRCCLR` and `GX_BL_DSTCLR` are one number read two ways).
      562,689,130 of 574,258,282 pixels take the path. It does **not** fix the
      truncated text: the best frame is byte-identical, because blending is
      disabled on 99.6% of draws and the enabled ones are ONE/ZERO or plain
      src-alpha.
- [ ] **Indirect textures, lighting and fog** — configured by
      registers this reads but does not yet act on.
- [ ] **Near-plane clipping** — a triangle straddling the camera is currently
      dropped whole rather than split.
- [ ] **Indirect texturing is CONFIRMED USED**, not hypothetical — the engine
      calls `GXSetTevIndirect`, `GXSetIndTexMtx`, `GXSetIndTexOrder`,
      `GXSetIndTexCoordScale` and `GXSetNumIndStages`. The design document
      lists this among the edge cases that overrun phase 3. Plan for it rather
      than discovering it.
- [ ] **TEV shader generator** — up to 16 stages, per-stage input selection,
      bias, scale, clamp, indirect texturing, alpha compare. One fragment
      shader per unique configuration, cached by hash; expect a few hundred to
      a few thousand. **Now that the port is GPL-3, lift Dolphin's
      `PixelShaderGen.cpp` rather than reimplementing it** — record the source
      commit in `THIRD_PARTY.md`.
- [ ] **Texture decoder** — I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, CMPR.
      Also a Dolphin lift under GPL-3.
- [ ] **EFB copy emulation** and display lists via `GXCallDisplayList`.
      **Confirmed used**: `GXCopyTex`, `GXSetTexCopySrc`/`Dst` mean
      render-to-texture is in play, not just display copies.
- [ ] **Hardware lighting — confirmed used.** `GXInitLightAttn`,
      `GXInitLightColor`, `GXInitLightPos`, `GXLoadLightObjImm`,
      `GXSetChanCtrl`, `GXSetNumChans`, `GXSetChanAmbColor`/`MatColor`. This is
      a GX stage distinct from TEV and needs its own model.
- [ ] **Palettised textures — confirmed used.** `GXInitTexObjCI` with
      `GXLoadTlut`/`GXInitTlutObj` means TLUT handling is required, not
      optional.
- [ ] **Vulkan backend** through the platform layer.
- [ ] Maintain a list of unsupported GX features, gated by game screen. Phase 3
      targets the first playable area only — indirect texturing, EFB
      peek/poke, Z-textures and bump mapping are where this phase overruns.

---

## Phase 4 — Audio

*Exit: music, codec calls and SFX match Dolphin's output within tolerance.*

- [ ] AX voice mixer, `AXRegisterCallback` at 5 ms, mix at 32 kHz into SDL3.
- [ ] DSP-ADPCM / AFC and PCM decoders, per-voice sample-rate conversion.
- [ ] ARAM as a second 16 MB host buffer; DMA is a memcpy plus completion.
- [ ] Disc streaming for voice-over and music.

Cost here depends entirely on the phase-0 answer about custom microcode.

---

## Phase 5 — Saves and completeness

*Exit: the game is completable start to finish on both platforms.*

- [ ] CARD emulation over a per-user save directory, keeping the 8 KB block
      format so saves stay Dolphin-compatible.
- [ ] The Psycho Mantis save-file scan — the runtime must expose a *directory*
      of save files, not just the game's own.
- [ ] Disc-2 swap exercised in the real story flow.
- [ ] Every remaining SDK stub replaced with a real implementation.
- [ ] Memory-leak and thread audit.
- [ ] Replay-based regression suite: recorded Dolphin inputs, guest-memory
      checksums at fixed frames.

---

## Phase 6 — Port features

*Exit: public release.*

- [ ] Widescreen — needs game-side patches in `patches/` for culling frustum
      and UI, the first legitimate use of a hand-written function replacement.
      This work starts in Ghidra's decompiler view: the replacement cannot be
      written without understanding the function it replaces. The pseudo-C
      stays out of the repository; the replacement written from it is our own
      code and is committed.
- [ ] 60 fps, *if* phase 0 found the logic is not frame-locked.
- [ ] Resolution scaling, keyboard and mouse.
- [ ] Launcher with ISO picker and hash check.
- [ ] Packaging: portable zip on Windows; AppImage or Flatpak on Linux, with
      Steam Deck as a first-class target.

---

## Decisions still open

These are from the design doc and are not milestones — but each one changes the
shape of a phase, so none should be discovered late:

- [x] **Settled 2026-09-18: GPL-3.0**, reusing Dolphin's texture decoder and
      shader generator. Months off phase 3.
- [ ] Vulkan-only, or SDL3 GPU for one code path at the cost of GX expressiveness?
- [ ] Is Steam Deck a launch target? Changes phase 6 packaging and controller work.
- [ ] Contribute the runtime back as a shared GameCube runtime, or keep it Twin
      Snakes-specific? Shared is more work up front and more valuable.
- [ ] How public is the project, given Konami's ownership of the game code and
      Nintendo's of the SDK and platform?
