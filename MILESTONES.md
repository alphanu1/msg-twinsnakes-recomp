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

**Phase 0, in progress — 501 symbols recovered, structure understood.** The toolchain is
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
| 1 | Boot in ModernGekko | Title screen renders through recompiled CPU code, no interpreter fallback on the boot path | 1–2 weeks | blocked on 0 |
| 2 | Native OS + DVD + PAD, headless | Main loop runs headless, reads assets, responds to input, `OSReport` matches Dolphin | 3–4 weeks | blocked on 1 |
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
- [ ] Name the remaining GX surface — ~164 functions. **The signature source is
      found and verified**: `doldecomp/mkdd` links the same SDK `0x2301` build,
      92% of shared symbols are byte-identical in size, and it names 177 GX
      functions. Needs complete `.text` function boundaries, then ordered run
      alignment (F11). This blocks phase 3.
- [ ] Recover engine function boundaries in `mgso_pal.rel` — 4.3 MB, and `dtk`
      found only `_prolog`, `_epilog` and `_unresolved` there, as expected for
      game-specific code with no signatures.
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

- [ ] Run DolRecomp over both `main.dol` files.
- [ ] Build under the ModernGekko/RecompCore template, Dolphin providing GX,
      audio and HLE.
- [ ] Log every interpreter-fallback hit. The boot path must reach zero; the
      list of everything else is the phase-2 and phase-3 backlog.
- [ ] Stand up the differential harness: `OSReport` diff and guest-memory
      checksums at fixed frames, Dolphin vs. port. When it reports a
      divergence, decompile the *caller* and read what it does with the value —
      usually faster than instrumenting the shim.

---

## Phase 2 — Native OS + DVD + PAD, headless

*Exit: the game runs its main loop headless, reads assets, responds to input,
and its `OSReport` output matches Dolphin's.*

Order within the phase is set by what blocks boot: OS, then DVD, then PAD.

- [ ] **OS** — fiber/ucontext scheduler running exactly one guest thread at a
      time, arena allocator over guest memory, monotonic clock scaled to the
      40.5 MHz timebase, alarms driven from the frame loop, REL loader with
      relocation. *The high-risk item: threading semantics must be exact.*
- [ ] **DVD** — FST lookup, `DVDReadAsync` on a worker thread with callbacks
      fired on the guest thread. `.iso`/`.gcm` direct, `.rvz` decoded,
      extracted folder for development. `.nkit` refused.
- [ ] **Virtual two-disc mount** — mount both images at startup; when the game
      polls for disc 2, report cover opened, disc 2 inserted, cover closed, on
      the SDK's expected timing.
- [ ] **VI** — stubs, plus the frame loop.
- [ ] **PAD** — SDL3 gamepad, GC layout, analog triggers and C-stick semantics.
- [ ] GX calls log and discard.

---

## Phase 3 — GX renderer

*Exit: the title screen, the Dock and the Heliport render correctly at native
resolution, compared frame-by-frame against Dolphin screenshots.*

This is the project. ~200 functions and the widest error bars in the plan.

- [ ] **Vertex converter** — one converter turning any GX vertex stream
      (direct/8-bit/16-bit indexed, s8/s16/f32 with fractional shift) into a
      single fixed host layout, so the host renderer sees exactly one format.
- [ ] **FIFO command parser** — games write raw FIFO commands, not just API
      calls.
- [ ] **TEV shader generator** — up to 16 stages, per-stage input selection,
      bias, scale, clamp, indirect texturing, alpha compare. One fragment
      shader per unique configuration, cached by hash; expect a few hundred to
      a few thousand. **Now that the port is GPL-3, lift Dolphin's
      `PixelShaderGen.cpp` rather than reimplementing it** — record the source
      commit in `THIRD_PARTY.md`.
- [ ] **Texture decoder** — I4, I8, IA4, IA8, RGB565, RGB5A3, RGBA8, CMPR.
      Also a Dolphin lift under GPL-3.
- [ ] **EFB copy emulation** and display lists via `GXCallDisplayList`.
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
