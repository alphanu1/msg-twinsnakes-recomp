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

## Status, 2026-09-18

**Phase 0, in progress — 1,067 symbols, 18,485 function boundaries.**

Note on terminology, since the numbers here are easy to misread: **instructions
translated (99.87%) is not the same as decompiled.** Translation is a
mechanical rewrite of machine code into C; decompilation in the matching-source
sense is out of scope by design and sits at ~0%. Phase 0 progress is 56.5%. The toolchain is
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
- [ ] Name the last 6 GX functions and the 172 remaining SDK entry points the
      engine calls. The biggest untapped source is the REL: 17,000 functions,
      with its own file-name strings.
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
- [x] **The route to the screen** — an EFB that GX clears, `GXCopyDisp`
      executed for real from the pixel engine's copy registers, BT.601
      conversion to YUV 4:2:2 at the address the game programmed, and
      scan-out from the video interface's own register (`tests/test_efb.c`).
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
- [ ] **Indirect textures, lighting, fog and blending** — configured by
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
