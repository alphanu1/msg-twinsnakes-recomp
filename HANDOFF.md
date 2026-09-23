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

## PHASE 0 PROGRESS — 70.8%

Regenerate with `tools/progress.py`; do not hand-maintain these numbers.

| Measure | | |
|---|---|---|
| Functions named | 1,078 / 18,485 | 5.8% |
| Function boundaries recovered | 18,485 / 18,485 | 100.0% |
| SDK entry points the engine calls, named | 206 / 336 | 61.3% |
| SDK call sites covered | 6,155 / 7,078 | 87.0% |
| GX surface the game uses, named | 81 / 81 | **100.0%** |
| **Average of the five** | | **70.8%** |

The average is an unweighted mean of five dissimilar measures — a headline, not
a statistic. Read the rows. In particular the 3.9% and the 100% are both true
and neither is the answer: the engine is translated mechanically, so naming it
buys debugging rather than correctness, while boundaries are what the
recompiler actually consumes. **The row that governs the remaining work is the
SDK boundary** — now 53.6% of entry points and 83.5% of call sites.

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

**The renderer is ours and it draws.** `runtime/gx/` parses the command stream,
decodes every vertex format, transforms, rasterises, samples textures and runs
the TEV combiner; `host/display.c` executes the game's framebuffer copies from
the parser's own register state and presents what the video interface points
at. The Konami logo is drawn by that code and nothing else — 7,203 commands,
0 parser desyncs, 108 triangles, 12,494,848 pixels.

**Where the boot stops, and what that turned out to mean.** After the logo the
game keeps running a frame loop but issues no further GX commands. That looked
like a stall for three sessions. It is not one: a sampling profile puts
**62.8% of the run inside `memcpy` and 23.5% inside `__fill_mem`**, and caller
attribution shows **nineteen memcpy calls in the whole boot, one of which
moves 5.7 MB** — the engine overlay, copied a byte at a time into the second
address window where every store takes the slow external-write path. The game
was never stalled; it was copying. `runtime/os/mem_shims.c` does those three
natively now.

Findings from this session are **F90-F151**. The two worth reading first are
**F91** — the heartbeat that aliased with the retrace tick and made every
sample land in `__OSDispatchInterrupt`, which reads exactly like a hang in the
interrupt handler — and **F94**, the engine's per-frame work being reached
through a function pointer, so no amount of following `bl` targets finds the
renderer.

---

## NEXT, IN ORDER

1. **Report a controller, then a memory card (F149).** The boot ends at the
   "No Memory Card" warning and **cannot leave it**: only **3**
   serial-interface reads happen in a whole boot, so PAD found no controller
   and stopped polling, and nothing can press Retry or Continue. Everything
   downstream — the opening video included — is simply never reached, which
   supersedes F136's reading of that screen as an idle wall. Both are Low
   difficulty in the design document's SDK table (PAD: SDL3 gamepad; CARD:
   files in a save directory; EXI/SI: stubs that return success). The
   controller is the smaller job and unblocks every menu, not just this one.
2. **Watch `0x7F50068C` — the font's `GXTexObj` (F141).** A static global in
   the overlay's `.bss` holding `0x941A8036`. The address it carries (55.6 MB)
   is beyond anything a GameCube has, which is the shape of an unrelocated
   file offset rather than a pointer. A watch on that word names whoever
   fills it, and says whether an asset load failed or a relocation step was
   skipped. All 2,964 texture refusals are this one texture, and F139/F140
   already rule out TMEM preload and our own BP decode.
3. **Why is 165,888 triangles' worth of geometry BLACK? (F151).** They are
   configured `a=b=c=d=ZERO` in the combiner, which computes exactly that, and
   on a menu screen they should not be. Either a TEV stage that should supply
   colour is not reaching the combiner, or the configuration is read from
   stale BP state. Depth ordering is now excluded: honouring ZMODE changed the
   run substantially and left this screen byte-identical.
4. ~~Clear the depth buffer every frame~~ — **superseded (F151).** ZMODE is
   now honoured per draw, which is the principled version of that idea, and
   the screen did not change.
4. **Count text quads that bind NO texture (F147).** The 2D pass draws ~13.7
   triangles a frame (~7 quads) but only three text-strip textures are ever
   sampled successfully. A quad that binds nothing draws untextured and
   disappears, which looks exactly like a truncated line. Instrument
   `bind_texture` returning NULL per frame against the quads that are
   visible. Excluded already: scissor, depth, display lists, texture
   refusals, viewport, geometry extent, RTT output, texture content, texture
   coordinates, SU size (F139-F147). Superseded: (F146).
5. **Implement EFB-to-texture copies (F145, still worth doing).**
   `mgs_efb_copy` writes nothing unless the copy targets the external
   framebuffer, so 1,883 copies a boot discard their output and 3,860,150 of
   3,873,706 triangles draw into that path. Needs an encoder for the copy
   formats, GB8 (0xC) first. Nothing samples those destinations today, so it
   is not the text bug — but a renderer that throws away its
   render-to-texture output will not survive contact with the rest of the
   game.
6. **Re-measure the texture-enable claim (F129, suspect after F137).** "Only
   778 of 514,826 triangles want a texture" was measured while a display list
   was being dropped every frame and the stream desynced 6,317 times. It is
   now 14,356 with zero desyncs. The conclusion that the game deliberately
   draws untextured needs redoing on a clean stream.
2. ~~Why 99.8% of the geometry shades black~~ — **answered, and it is not a
   fault (F130).** The combiner does exactly what the game configures. The
   screen is black with a logo because the boot is on a logo screen.
8. **The 77 GX desyncs (F126).** Was 2 in 20,429 commands, now 77 in
   177,805 — the rate rose, so it is not simply more traffic. With 20x the
   geometry flowing, the parser is meeting command shapes it never reached
   before. `MGS_TRACE_GXDESYNC` names them.
9. **Find who is eating the DSP mailbox (F109).** The interrupt routes, both
   task messages are posted and read, and neither callback runs - so a reader
   other than `__DSPHandler` is consuming them, or `__DSP_curr_task` is not
   the task being watched. `fn_800376E4` is `DSPReadMailFromDSP` and
   `fn_80037F28` loops on it; that is the first place to look. The boot waits
   on **`init_cb`** (task+0x28), not `done_cb`.
10. **Dump the engine's task table** (`mgs_dump_tasks`, `host/heaps.c`). The
   per-frame work is reached through a function pointer at `+0x04` of a node
   in a 12-level table at REL `.bss+0x23708`, gated by a per-level mask at
   `+0x40` and per-node flag bits 12..15. That table says directly which tasks
   exist and which are gated off; the call graph cannot.
11. **Name the remaining 143 SDK entry points the engine calls.** 193 of 336
   are named and they cover 86.7% of call sites. Ordered alignment is
   exhausted (F72); the live routes are the call graph, the `__FILE__`/
   `__LINE__` pairs, inline-assembly matching (F90), and — the one that paid
   best — **starting from a register shadow or a struct offset rather than
   from the function** (F118, F119). `__GXData+0x1DC` being PE_CONTROL, or
   `CARDStat+0x28` being `gameName`, identifies several functions at once and
   the identification is checkable field by field.

   Aim it using the region split in **F116**, not the raw count: only about a
   quarter of the remaining call sites are in code with any public reference.
12. **The 198 functions in `0x8004E700`-`0x80062000`.** Now attributed
   (`config/symbols/main.dol.files.txt`): Konami's sound layer and a complete
   Tremor. Heavily called by the engine and entirely unnamed. Tremor's upstream
   source is in `extern/tremor`, but Konami edited it and the line numbers do
   not match, so ordinal alignment would produce names with no valid origin.
13. **Renderer gaps:** indirect textures, lighting, fog, blending, near-plane
   clipping. All configured by registers the parser already reads.
14. **The second window is the performance floor.** The whole engine runs at
   `0x7E000000`, so every load and store goes through `external_read`/
   `external_write` rather than the generated code's fast path. The `memcpy`
   shim removed the largest single consumer; the rest of the engine still pays
   it on every access.
15. **Find why the opening video is never requested (F148).** `movie.dat` is
    opened and never read — zero of 271 disc reads touch it — so playback is
    not starting rather than failing. `mpegGCN.c` is in the REL and already
    runs natively, so this is a presentation path plus whatever gates the
    start, not a decoder. Superseded framing:
15. **Decide where MPEG video lives.** `mpegGCN.c` and 95 MB of `movie.dat` are
   real work that no phase owns (F10).
16. **Phase 4 needs a software Tremor path**, not only a DSP voice mixer — the
   decoder is in `main.dol` and runs on the CPU.

---

## WHAT NOT TO RE-PROPOSE

- **Skipping phase 1.** Running under the Dolphin-derived runtime first is
  deliberately throwaway work, and it is what makes every phase-2 bug have one
  possible cause instead of two.
- **Hand-editing generated C.** It is regenerated from my disc on every
  build. A hand-written replacement goes in `patches/`, for one named function,
  only when a port feature needs it.
- **A matching decompilation.** No public decomp exists for Twin Snakes; it
  would start from zero symbols and take years. Static recompilation needs only
  function boundaries and SDK symbols.
- **Committing anything disc-derived** — DOL, RELs, generated C, extracted
  assets. This is the rule that keeps the project distributable.
- **Reading "N call sites unnamed" as "N names available" (F116).** Three
  quarters of what is left is in Konami's own sound, Tremor and CR_System
  code, where no reference binary and no upstream source exist. Sort the
  remainder by region before aiming at it.
- **Treating a host-initiated guest call as an ABI call (F134).** The host
  enters guest code at an arbitrary instruction boundary, so it is an
  asynchronous interruption: CTR, CR, XER and the FP file must all be saved,
  not just the registers a caller would care about. Saving only gpr/pc/lr
  cost this boot 13x its graphics, via one `bdnz` loop count.
- **Reading a conclusion out of an instrument's silence (F131).** pc hooks
  only see pc at dispatch boundaries and miss calls inside one chunk. Zero
  arrivals is not evidence of "never called"; it is evidence of nothing.
- **Judging source by a filtered range (F130).** Two "found the bug" calls in
  one session came from `sed`/`grep` ranges that had truncated a function.
  Read to the closing brace before concluding anything about a switch table.
- **"The texture path is broken" (F129).** 778 textured out of 514,826 looks
  damning and is not: every triangle runs one TEV stage, none wants a texture
  on a later stage, and all 778 binds succeed. The game draws the rest
  untextured on purpose. The fault is in the combiner's untextured path.
- **"The black geometry must be a blend / a masked pass / alpha-tested"
  (F152).** All three are excluded by measurement. Blending is *off* for
  99.6% of draws; `write_masked` is 0, so colour writes are never disabled;
  and the game sets `ALPHA_COMPARE = 0x3F0000` (op0 = op1 = ALWAYS), so it
  disables the alpha test on purpose. Those draws are opaque black by
  intent and would be on console too.
- **Answering "put it behind the UI" with the depth buffer (F152).** The
  game runs these draws with depth off: **0 pixels of 574,258,282** are
  rejected by the depth test in a whole boot. Honouring ZMODE was correct
  and changed the frame by nothing. Submission order alone decides what is
  in front here.
- **Concluding from a run whose build you did not verify (F162).** Two runs
  with different code came back byte-identical across 45 million commands.
  The build had not finished. Use a RUNTIME switch to A/B a feature, not a
  rebuild.
- **"The command processor executes from the byte address it is handed"
  (F162).** Plausible, and wrong for display lists: masking to the 32-byte
  fetch boundary gives 0 desyncs where the exact address gives 26,323,104.
- **Fixing the first desync you find (F161).** All 388,030 desyncs in a video
  run share ONE reason. The first one found was a different, rarer fault, and
  hours went into it. Count failures by reason BEFORE examining any instance.
- **Changing a size without changing the reader (F161).** `mgs_gx_vertex_size`
  and `mgs_gx_decode_vertex` both encode the vertex layout. Fixing one alone
  just moves which consistency check fires.
- **"A ragged display-list size proves corruption" (F160).** It does not. The
  game passes non-multiples routinely - 14,717 of 18,000 in a boot - and F137
  had already measured that rounding them up costs 231 desyncs. The refutation
  was in the comment above the code being read.
- **"Display lists ending mid-command is the bug" (F160).** 7,164 of 18,000
  lists in a boot end mid-command, and that boot has 0 desyncs. The parser
  discards the partial command and restores the caller's state, which is what
  the hardware does. Pre-existing, and not the video fault.
- **A matching function count as proof on its own (F159).** Four unnamed OS
  functions sat in a bracket, and `OSMessage.c` has exactly four functions -
  but `OSMessage.c` was already named elsewhere in the binary. Before claiming
  a file fills a gap, grep the map for that file's functions first.
- **A diagnostic that derives its context from the corrupt value (F158).**
  The desync report printed the vertex format as `op & 7` where `op` was the
  garbage byte it was complaining about. It described the corruption, not the
  cause, and sent two hypotheses in the wrong direction.
- **Treating "unknown" as "zero-length" (F158).** The parser accepted any
  opcode it did not recognise below 0x80 as a valid empty command, which meant
  it walked silently through garbage and reported the desync in the wrong
  place entirely. An unknown opcode is a desync wherever it appears.
- **Measuring where the damage shows rather than where it enters (F163).**
  Three times in one session the reported fault was downstream of the cause:
  the desync reason, the misaligned display list, and the texture copy. Each
  time, fixing what fired moved the failure to the next check. Ask what the
  input to the broken stage looked like before changing the stage.
- **Filtering on one attribute when two textures share it (F163).** A dump
  keyed on 512x448 caught the CMPR texture, not the RGBA8 one, and produced a
  confident "the video decodes correctly" that was simply the wrong texture.
- **Caching anything on an address alone (F156).** An address is not an
  identity. The texture cache matched on address/format/size and never read
  the bytes, so every dynamically updated texture - a video frame above all -
  was served stale forever. And when fixing it: a changed entry must be
  REPLACED, never duplicated, or the cache fills with stale versions of one
  texture and thrashes everything else out.
- **Blaming the recompiler's floating point before checking (F156).** The
  generated module is built with -O3 against the project's own rule, which
  looks like a paired-single rounding bug waiting to happen. It is not one
  here: there are zero FMA and zero AVX instructions in the module, so no
  contraction is possible and the arithmetic is IEEE-exact. Fix the rule
  breach because it is a breach, not as a cure for a symptom.
- **Half a device (F128, F153).** Twice now, implementing part of a
  peripheral has been worse than leaving it stubbed: the DSP line mirror cost
  12x the boot, and completing serial transfers without also clearing RDST
  cost 11x the graphics. Both times the fix was to make the other half
  honest, not to revert.
- **Reading `ps -o pcpu` as the current CPU rate (F154).** It is the average
  since the process started. It said 98.8% while the live figure was ~10%,
  which inverts the diagnosis: 10% with bad throughput means *waiting*, and
  the wait was 20,000 vsynced presents per boot.
- **"A successful read in a loop means the guest is waiting for input"
  (F153).** It is equally the signature of a flag that never clears. Holding
  a button produced byte-identical counters; `MGS_PAD_SCRIPT` and
  `MGS_PAD_BUTTONS` exist to tell those two apart.
- **"Fewer GX commands means less progress" (F153).** The stalled run emitted
  *more* initialisation output than the baseline while drawing 190x less. The
  baseline spends most of its budget re-rendering one screen.
- **A "diagnostic" that changes pixels (F150).** Suppressing black writes to
  see if the text reappeared took the boot from 2,476,033 GX commands to
  7,235. Altering the EFB alters what copies write into guest memory, and the
  guest reads guest memory. Observe with counters; never steer the thing you
  are measuring.
- **Using `MGS_NO_DEPTH` to test depth ordering (F150).** It forces every
  pixel through, which makes black overdraw worse. The question "is stale
  depth letting the wrong thing win" needs depth CLEARED more often, not
  disabled.
- **Judging a frame by the buffer at exit (F127, F129).** The EFB is black at
  exit while the run's best frame is 9.6% lit. Use `MGS_SAVE_BEST`.
- **Raising a shared interrupt line without setting the device's status bit
  (F128).** PI's DSP bit serves three sources and the SDK's dispatcher reads
  `__DSPRegs[5]` to tell them apart. A line asserted with no status bit is an
  interrupt no handler can be chosen for, and therefore one nothing can ever
  clear.
- **Fixing one half of an inconsistent pair (F128).** Making PI's DSP bit
  mirror the device — strictly more accurate — cost 12x the boot on its own,
  because the other half was still poking PI directly. Accuracy applied
  halfway is worse than leaving both wrong.
- **Treating the external interrupt as edge-triggered (F126).** PI's line is
  level-driven and the SDK's dispatcher services ONE source per entry, so
  anything still pending must be re-taken. Raising only on new events loses
  every event that arrives behind a higher-priority source. This cost the
  boot 20x its geometry.
- **"The boot is just slow" (F122).** 200 million steps produce byte-identical
  output to 40 million. It is a livelock. A change that only moves the step
  count has changed nothing.
- **Making the poll/yield loop cheaper (F123).** `fn_8004C948` is a thread
  entry point whose body is `poll; OSYieldThread; goto` — the engine's idle
  graphics-service thread. It eats every step because nothing else is
  runnable, not because it is wrong.
- **Modelling GPU completion latency (F115, and again in F124).** The one
  completion that declines is the first, delivered before anything was
  submitted; the ring self-corrects. Delay is not the fix and made the boot
  strictly worse.
- **Matching a function by its opening instructions (F118).** `__CARDIsReadable`
  has `__CARDIsWritable` inlined into it, so its head signature names the
  wrong function. Read to the `blr`, and treat a callee that disagrees with
  the match as evidence rather than noise.
- **Judging a rendering change by one headless run (F162).** Runs are not
  deterministic — the same binary completes 271 or 287 DVD reads, because
  reads finish on host worker threads — and it intermittently wedges in
  `gp_poll_once` at exactly 13,060 GX commands. A wedged run looks exactly
  like a catastrophic regression: 109 triangles instead of 3.7 million.
  **This has already produced one confidently wrong conclusion.** Two wedged
  runs were read as proof that the rasteriser is chaotically sensitive to
  floating-point rounding — that replacing `edge/area` with a multiply by
  the reciprocal, a one-ULP change, broke the boot. It does not, and it did
  not; a valid optimisation was rejected on that basis. Before believing any
  rendering result, check the triangle count against the ~3.7M baseline and
  re-run at least three times.
- **Optimising a hot loop by reading it.** Three rounds chosen that way
  bought about 6% between them and missed that `combine` was 36% of the whole
  program (F160). Building the sampling profiler took less time than the
  round that failed. `MGS_PROFILE=1`.
- **Comparing wall-clock times between builds without normalising.** The
  workload is not fixed: the guest runs a step budget, and how much it draws
  within it varies run to run. Compare Mpx/s over the pixels actually drawn,
  and confirm the MEM1 hash is unchanged.

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
full `pacman -Syu` of ~800 packages, **which was my call and was not
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
these provisions protect the analysis, not anyone's copy of the game; **Konami is
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
whatever it was given. Both resolve paths through the same FST, so
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

**F43 — where my disc images live, and why not the program folder.**
`runtime/dvd/disc_locate.c`. The program folder is **supported but last**: the
images are mine and should not be tied to an install that
updates or is reinstalled under them, and the design document specifies a
launcher that asks for them and hash-checks them.

Resolution order, most explicit first: command-line argument, `$MGS_DISC1`/
`$MGS_DISC2`, a remembered path from a previous run, `discs/<id>/discN` (this
repository's layout), then beside the executable.

*Three deliberate choices:*

- **An explicit path is returned even if it does not exist.** The useful error
  is "that path did not mount", naming what was asked for — not silently
  falling through to something they did not choose.
- **A remembered path is honoured only if it still exists**, so an image the
  I have moved does not resolve to a stale location.
- **Nothing ever searches the filesystem for a disc image.** A port that goes
  hunting for game data it was not pointed at is doing something I did
  not ask for.

The remembered path goes in my config directory (XDG), never into the
install. Only the *path* is written; the image itself is mine and is never
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

**F89 — how to watch a run that takes minutes, and two mistakes from not
being able to.**
The port now runs at roughly real-time - about 120,000 host steps a second,
and a step is a VI tick every 2,000 - so a run long enough to be interesting
takes minutes, and its report only prints at the end. Two wrong conclusions
came straight out of that:

- **"It is stuck after `dummy.tpl`."** It was not. It was running, slowly, and
  a log that had not grown in four minutes looked identical to a hang.
- **"120 deferred DVD completions is a bug."** They are transient: the guest
  has interrupts off, the pump declines, and the read is delivered on a later
  pass. The read it was waiting on completed.

There was also an hour of measurements taken against a machine running five
stale copies of the port at once, which made everything look far slower than
it was. **Kill the previous run before starting another**; `pkill -x` did not
reliably do it, killing by PID did.

Three things added so that this does not recur:

- **`MGS_HEARTBEAT=N`** prints progress every N steps - and prints what the
  GAME has achieved (framebuffer copies, files read), not just the host's
  step count, which only ever says the host is alive.
- **Ctrl-C ends a run cleanly** rather than killing it, so the report still
  prints. `SIGINT`/`SIGTERM` set a `volatile sig_atomic_t` the loop checks —
  **and a SECOND signal now `_exit`s**, because the flag is only read between
  dispatch calls and translated code that loops without exhausting its cycle
  budget never returns to be asked. See F95.
- **`MGS_TRACE_ALLOC`** logs the game's tracking allocator and its matching
  free with the source file and line each was called from.

**Choose the heartbeat interval so it does not alias with the frame tick.**
At 1,000,000 every beat landed on the VI interrupt, because the interrupt
fires every 2,000 steps and 1,000,000 is a multiple of it - so every sample
reported the same pc and looked like a hang in `__OSDispatchInterrupt`.
**The fix chosen at the time, 2,000,003, has the same defect** — see F91.

---

**F90 — The inline-assembly matcher had two defects, each subtracting from the
count silently, and together they cost 22 matches.** `PSMTX44Concat` is
byte-for-byte identical to our `fn_800250CC` — all 65 instructions in order —
and did not match.

- `nofralloc` is a Metrowerks **directive**, not an instruction. It emits no
  code, so it was in the SDK side of every signature that used it and could
  never be in ours.
- The trailing `blr` was stripped from **our** side only. Some SDK asm bodies
  write the return explicitly; ours always has it.

Each made the affected functions differ by exactly one token, and since a
match needs the full sequence, each one failed. Neither produced a diagnostic:
the tool reported "12 matches" and looked finished.

Fixed, it reports **34 matches, 5 new** — `OSSwitchFiber`, `PSMTXMultVec`,
`PSMTX44Copy`, `PSMTX44Concat`, `PSMTX44Transpose` — at 164, 104, 39 and 17
engine call sites. Call-site coverage 71.5% → **76.1%**.

**The lesson is about the shape of the failure, not the bug.** A matcher that
refuses everything is obviously broken. One that quietly matches a third of
what it should looks exactly like a method that has been exhausted, and that
is what it was believed to be for two sessions.

---

**F91 — `MGS_HEARTBEAT=2000003` aliases with the retrace tick just as badly as
1,000,000 did.** 2,000,003 mod 2,000 is **3**, so consecutive samples land
three steps further into the same interrupt handler. Every beat read
`__OSDispatchInterrupt`, which is indistinguishable from a hang in the
interrupt handler — and was read as one.

Avoiding a multiple is not enough; the interval must be **coprime** to every
period in the run loop: retrace 2,000, host pump 512, display 256, PE-finish
64. `MGS_PROFILE` samples at **1,009**, prime and sharing no factor with any
of them.

---

**F92 — 86% of the boot is `memcpy` and `__fill_mem`, and one call is 5.7 MB.**
The sampling profile: `memcpy+0x18` **62.8%**, `__fill_mem` **23.5%**,
everything else 13.7%, over 8,920 samples and 152 distinct addresses.

Caller attribution (`MGS_PROFILE_CALLERS`, counting *every* arrival and reading
the link register) found **nineteen memcpy calls in the entire boot**, one of
which moves **5,737,728 bytes** from `rel_loader_LoadRel+0x94` — the engine
overlay. Translated, that is `lbzu`/`stbu` 5.7 million times, every store
landing in the second address window and taking the slow external-write path:
about one host step per byte, matching the 5,654,184 second-window writes the
run reports.

**The game was never stalled after the logo. It was copying.** Three sessions
of "why has it stopped rendering" were asking the wrong question, and a
five-minute profile answered it.

---

**F93 — the guest's `memcpy` is `memmove`, and the shim must be too.** It
compares `src` against `dest` and copies backwards when `src < dest`. The game
is entitled to rely on overlap working; the host's `memcpy` is undefined
exactly there. `runtime/os/mem_shims.c` calls `memmove`, and its
window-straddling fallback copies in the same direction the guest would so
both paths behave identically.

`__fill_mem` takes its value as the **low byte** of r4 (`clrlwi r4, r4, 24`)
and does not preserve r3 — which is why the SDK's `memset` saves the
destination in r31 and restores it.

**`tools/gen-patch-table.py` matched only `.text`.** The CodeWarrior block
moves are linked into `.init`, so the names resolved, the addresses did not,
and they were reported as unimplemented rather than as a section filter.

---

**F94 — the engine's per-frame work is reached through a function pointer, so
the call graph cannot find the renderer.** The main loop is
`bl fn_80007180 ; bl fn_1_F394C`. The first clears one word. The second
contains exactly one `bl`; its real work is a `bctrl`.

`fn_1_F394C` is a **scheduler**: a 12-level table at REL `.bss+0x23708`,
0x44 bytes per level, each level a linked list of nodes. `+0x40` of the level
is a gate AND'd with a global mask (set = skip the level); `+0x08` of a node
carries flag bits 12..15 (set = skip the node); `+0x04` is the function.
`fn_1_F3A20` links a node in and confirms the layout.

`mgs_dump_tasks` in `host/heaps.c` dumps it. **Following `bl` targets outward
from the main loop will never reach the renderer, however long it is done for.**

---

**F95 — the host could not be stopped, and both long runs outlived their
`timeout`.** One sat at 21 minutes against a 20-minute limit, state R, SIGTERM
delivered and caught, flag set. The run loop reads the flag *between* dispatch
calls; translated code that loops without exhausting its cycle budget never
returns to be asked. `timeout` sends one signal and then waits forever for a
process that caught it and carried on.

A second signal now `_exit(130)`s. Long runs should use `timeout -k`.

**Five stale `twin-snakes` processes skewed an hour of measurements earlier in
this project for the same underlying reason, and `pkill -x` does not end them
— killing by PID does.** Check `ps -C twin-snakes` before trusting a timing.

---

**F96 — `main.dol` is not only SDK, and 198 functions were unattributed
because nobody looked.** Nineteen `__FILE__` strings in the DOL name Konami's
own sound layer (`sd_sound.c`, `sd_stream2.c`, `sd_mem.c`, `sd_ogg.c`) and a
complete **Tremor** — the Xiph fixed-point Vorbis decoder — in `codebook.c`,
`floor0.c`, `floor1.c`, `framing.c`, `info.c`, `mapping0.c`, `res012.c`,
`sharedbook.c`, `vorbisfile.c`, `block.c`. That block is `0x8004E700`-
`0x80062000`, about 34 KB, heavily called by the engine, entirely unnamed.

The **line numbers** come from the same calls: Konami's allocators take
`(…, file, line, …)`. Tracking registers to each `bl` gives the ABI without
assuming it, and all four allocators land inside `sd_mem.c`'s own range.

**Cross-check:** within all nine Tremor/ogg units the line numbers run
strictly *downwards* as the address rises — CodeWarrior emitted them in
reverse order of definition — while the four `sd_*.c` units run strictly
upwards. Every file's functions form a contiguous, non-overlapping run.

**What is NOT claimed:** the line numbers do not match upstream Tremor
(`framing.c` 864 is inside `ogg_stream_pagein` upstream; here it allocates 80
bytes). Konami edited these files. Aligning by ordinal would produce names
with no valid origin, so none are claimed. `config/symbols/main.dol.files.txt`
records **64 attributions**, 31 of them for functions already named — and the
attribution agrees with the name in every one.

---

**F97 — a name in the map was wrong, and the origin column is what found it.**
`0x8001D124` was `DCZeroRange`, origin `mkdd-align`. Its body is a `dcbst`
loop — cache *store*, not zero. It is **`DCStoreRangeNoSync`**. Two
independent routes agree: the SDK's `OSCache.c` order puts exactly one
function between `DCFlushRangeNoSync` and `ICInvalidateRange`, and there is no
`dcbz` anywhere in `main.dol` outside `__LCEnable`'s `dcbz_l` — `DCZeroRange`
and `DCTouchRange` were dead-stripped.

Ordered alignment mis-assigned it because it assumed both survived the link.
**A name carrying `mkdd-align` rests on an assumption about what the *other*
binary contains**, and is worth re-checking against evidence taken from ours.

---

**F98 — the game gets past the logo into real engine rendering, and what it
draws first is a sphere.** With the overlay copy native, the boot reaches
`gcn_emit_sphere_strips` (REL 0x12106C, in the `gcn_dgd.c` / `gcn_spheremap.c`
neighbourhood). The function is unambiguous from its own code:

- outer loop 32, inner loop 33;
- `GXBegin(0x98 = GX_TRIANGLESTRIP, GX_VTXFMT2, 0x42 = 66)` per strip - and
  66 is exactly 33 x 2, which is what the inner loop emits;
- two helpers per vertex, each writing three floats to `0xCC008000`, the
  write-gather pipe: a position and a normal;
- the normal's z is `sqrtf(1 - (x*x + y*y))`, which is what makes it a unit
  sphere. `fn_1_17834` is `sqrtf` - `frsqrte` plus Newton-Raphson refinement,
  returning zero for non-positive input.

`GXEnd` (REL 0x121278) is a bare `blr`, which is correct: the vertex count was
declared in `GXBegin` and `GXEnd` does nothing on hardware.

That is 32 draw calls and about 2,048 triangles, and it is almost certainly a
render-to-texture pass building a sphere map.

---

**F99 — "it is wedged" was wrong, twice, and the instruments were what made it
look that way.** Three separate symptoms all said the host had hung after the
overlay copy went native, and none of them was a hang.

1. **The run outlived its `timeout` with nothing printed.** Fixed by having a
   second signal `_exit`, and by printing the last pc the run loop saw - which
   is the only route that works when the report is unreachable.
2. **A phase marker said `gx raster`, so it looked like the rasteriser.** The
   marker was only ever SET, never cleared, so it reported the last thing that
   had happened rather than what was happening. A marker that is not cleared
   on the way out is not a marker, it is a history. Letting the rasteriser
   abandon its work did not release the run, which should have been the clue.
3. **A 2,000-cycle budget did not help either**, which seemed to rule out "one
   long dispatch call" and left "an infinite loop in translated code". The
   generated C for that loop was then read directly, and it is correct: bounded
   counters, and a cycle-budget check on the back edge.

4. **A per-scanline abandon check in the rasteriser did not release it
   either**, which finally ruled out the renderer altogether.

**What answered it was the host's own call stack**, printed from the signal
handler with `backtrace_symbols_fd` and the host linked `-rdynamic`:

```
on_interrupt  <-  <signal>  <-  feed  <-  dispatch  <-  feed  <-  dispatch
              <-  feed  <-  dispatch  <-  feed  <-  dispatch  <-  feed
              <-  mgs_gx_write  <-  <translated code>  <-  mgs_module_run
```

Four nested `feed`/`dispatch` pairs - which is the display-list depth limit
of four, working correctly. **The host was in the GX parser, not the
rasteriser and not the guest.** See F101 for what it was doing there.

**The lesson is about instruments, not about the bug.** Four observations, all
produced by tools that were wrong or too coarse, all pointing somewhere
confident and false. The heartbeat aliasing (F91) was the same failure a day
earlier. `backtrace()` cost ten lines and answered in one run what four rounds
of inference had not. **Reach for the stack first.**

---

**F101 — the GX parser re-derived every command's length once per byte.**
`feed()` appends one byte, calls `command_length()`, and repeats. That is
correct and it is also the dominant cost in the whole renderer: a triangle
strip of 66 vertices is about 1,500 bytes, so the length was computed 1,500
times instead of three - and the length cannot change once known, because a
draw command's size is fixed by the vertex count in its first two body bytes.

Once the length is definite the remaining bytes are now copied in one
`memcpy`. The bytes land in the buffer in the same order, so nothing about
what is parsed changes; `test_gx`'s "the stream survives being split" case is
what guards that.

**`feed()` also had no way to be interrupted.** A display list is a single
write from the guest's point of view, so the run loop cannot regain control
for as long as the list takes - and the engine's lists run for minutes. The
rasteriser's own abandon checks did not help because most of the time was
never in the rasteriser. The parser now checks once per command.

---

**F100 — the scissor box was parsed and then ignored.** `BP_SCISSOR_TL`/`BR`
were defined in `bp.h` and never read by the rasteriser, so every triangle was
clipped only to the framebuffer. The hardware will not write outside the box,
so this draws pixels the game did not ask for - and for a render-to-texture
pass, which puts a small box in a corner of the embedded framebuffer, it
scrawls over whatever else is in there.

Two halves are needed. Coordinates carry the same 342 bias as the viewport,
and `BP 0x59` shifts the origin in units of two pixels with the bias again;
honouring the box without the origin clips against a rectangle of the right
shape in the wrong place. **Zero is a legal offset**, so `bp.written[]` now
distinguishes "set to zero" from "never set" - reading an unwritten register
as a zero offset shifts the box 342 pixels and clips away nearly everything.

Checked by disabling the feature and confirming the test fails: it reports a
leak at exactly (320,120), the first pixel past the box's right edge.

---

**F102 — RECORDING A DISPLAY LIST IS NOT DRAWING, and we were executing the
recording.** RESOLVED. The route to it is kept because the wrong answer was
available at every step and looked right.

With the staging buffer fixed the boot reaches the sphere-map pass, and the
first draw after the logo still loses the stream. What is established:

- **The draw is sized 20 bytes a vertex, and the parser is faithful to the
  descriptor in doing so.** `GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT2, 66)`, and
  the descriptor in force is `vcd_lo=0x200 vcd_hi=0x1` - position direct and
  texture-coordinate-0 direct - with `vat_a[2]=0x41377009`: position 3 x f32
  (12 bytes) and texcoord 2 x f32 (8). 2 + 66 * 20 = 1,322.
- **The engine asks for exactly that.** Tracing `GXSetVtxDesc` shows only
  `(attr=9, type=1)` and `(attr=13, type=1)` - `GX_VA_POS` and `GX_VA_TEX0`,
  both `GX_DIRECT` - for the whole boot. `__GXSetVCD` flushes 55 times and
  writes `0x200`/`0x1` every time.
- **But the bytes repeat every 24.** The stream at the sphere's first draw is
  `(-1, -1, -2)`, `(-1, -1, 0)`, `(-0.9375, -1, -2)`, `(-0.9375, -1, 0)`:
  a flat grid at z = -2 whose x steps by 1/16, and a second triple that is
  `sqrt(1 - x*x - y*y)` clamped at zero - a position and a sphere normal.
  Read as 20-byte vertices the z jumps from -2 to -1 between consecutive
  vertices, which a flat grid does not do; read as 24 it is consistent.
- **The code writes six floats a vertex.** `gcn_emit_sphere_strips` calls two
  helpers per vertex, and both are `lis r3, 0xcc01 ; stfs f1, -0x8000(r3)`
  three times over - both to `0xCC008000`, the write-gather pipe.

The stride was then measured from the UNDECODED stream - the distance between
consecutive `9A 00 42` opcodes - and it is 1,587 bytes: 1,584 for 66 vertices,
**exactly 24.0000 a vertex**, with no padding. So the bytes were right and the
descriptor reading was right, and they still disagreed.

**The answer is that those bytes were never meant for the parser.**
`GXBeginDisplayList(ptr=0x81791C60, size=51200)` is called once, and after it
the game is RECORDING. On hardware that call points the CPU-side FIFO at a
buffer in main memory and leaves the graphics processor's own FIFO alone: the
game keeps storing to 0xCC008000, but the data is written down rather than
executed. Our host sent every write-gather-pipe write to the parser, so we
executed the recording - under the descriptor that was live at record time
rather than the one the list will be called under.

The two FIFO descriptions are visible in the registers, and the redirection
and its restoration are unmistakable:

```
0xCC00300C <- 0x01791C60   PI  FIFO base  -> the display-list buffer
0xCC003010 <- 0x0179E45C   PI  FIFO end   -> base + 51,196
0xCC003014 <- 0x01791C60   PI  write pointer
       ... the sphere is recorded here ...
0xCC00300C <- 0x00450160   PI  FIFO base  -> restored
0xCC00003C/3E = 0x0045_0160   CP FIFO base, unchanged throughout
```

So the test is simply whether the two agree. `mgs_mmio_recording()` says they
do not, and the sink writes the bytes into the buffer instead of parsing them.

| | before | after |
|---|---|---|
| desyncs | 198 | **0** |
| GX commands | 8,158 | 7,235 |
| triangles drawn | 553 | 106 |
| frames | 55 | 55 |

**The write pointer advances while RECORDING and not while drawing**, and that
asymmetry is not a fudge. On hardware both pointers move and the SDK watches
the distance between them to know how full the FIFO is. This host has no
asynchronous graphics processor - a command is executed the moment it is
written - so there is no read pointer. Advancing the write pointer alone
describes a FIFO that only fills, and the SDK stops issuing: it took the boot
from 55 frames and 8,158 commands to 2 and 578. Recording is the opposite
case, where nothing draining the buffer is the truth rather than an artefact,
and `GXEndDisplayList` needs the pointer to have moved.

**What has already been ruled out**, so as not to be tried again:

- the vertex-format decoder mis-reading the VAT (checked field by field
  against the register layout, and it agrees with the 20-byte reading);
- `vcd_kind` mis-reading the descriptor (normal is bits 11..12 of `vcd_lo`,
  which is 0, correctly);
- float stores to the write-gather pipe taking a different path from integer
  stores - the generated code routes `stfs` through `mem_write32` like
  everything else;
- a missing descriptor write between the logo and the sphere - the windowed
  command trace shows the gap is NOP padding, and nothing else;
- `GXBegin` not being `GXBegin` - it reads `__GXData+0x5AC` and calls a flush
  helper per dirty bit, one of which is `__GXSetVCD` at 0x80041040.

---

**F103 — the engine sets a scissor box OFFSET of 1,024 pixels**, which is
wider than the framebuffer. `BP 0x59 = 0x02ACAB` decodes as x = 683, y = 171
in half-pixel units, so `GXSetScissorBoxOffset(1024, 0)`, alongside
`GXSetScissor(0, 0, 512, 448)`.

Taken at face value that puts the scissor rectangle at x in [-1024, -512),
entirely off the framebuffer, and a renderer that honours it literally draws
nothing at all. Dolphin computes SEVERAL candidate rectangles here precisely
because these coordinates wrap, so the naive reading is not the right one.

`MGS_NO_SCISSOR` exists to tell "the game clipped this" from "we computed the
box wrongly", because those look identical from the outside.

**RESOLVED, once the runs were deterministic enough to compare (F110).** With
the same guest execution either way - 20,429 GX commands, 24,716 triangles
submitted - the scissor clips **two triangles** and 2.7% of the pixels:

| | off | on |
|---|---|---|
| clipped | 0 | 2 |
| drawn | 24,716 | 24,714 |
| pixels | 12,494,848 | 12,156,928 |

So the 1,024-pixel box offset is handled correctly and the implementation
stands. The first attempt at this comparison was run before F110 and said the
scissor made the game *load more*, which is impossible and was the clue that
the runs themselves were not comparable.

---

**F104 — the boot's next two walls were audio, and both were one register.**
With the command stream clean the run stops cleanly and says where, which is
what made these quick rather than hard.

**`__ARChecksize+0x18`, spinning.** The SDK opens the ARAM probe with
`do {} while(!(__DSPRegs[11] & 1))` - a halfword read of `0xCC005016` testing
bit 0, waiting for audio RAM to finish coming up. The register read back zero
and the boot stopped there for good. Because the last thing before it is the
graphics work, it presented as a rendering problem.

`runtime/dsp/aram.c` is the rest of it: 16 MB, and the DMA engine that is the
only way the CPU reaches it. The register contract is the SDK's own, and the
two details that matter are that **the low five bits of every address are not
part of the address** (the hardware moves 32 bytes at a time, and the SDK
masks them off), and that **bit 0x8000 of the length's high half is the
direction**, not part of the length. The transfer completes before the write
returns: `__ARWaitForDMA` polls a busy bit that is already clear, and nothing
in the SDK's use of it can tell the difference.

`__ARChecksize` deliberately addresses memory that may not be there - that is
what a size probe is - so the ARAM address wraps rather than being clamped.
Wrapping is what the hardware's address lines do, and it is what makes the
probe terminate at 16 MB instead of running off the end of the allocation.

**`__AI_SRC_INIT+0x74`, spinning.** It starts the audio interface, then waits
for the sample counter at `0xCC006C08` to change, timing it against
`OSGetTime`. **It is calibrating**, so a counter that merely advances is not
enough - it has to advance in the right proportion to the guest's own clock,
or the game measures an audio clock that does not exist. So the counter is
derived from the same tick source as the timebase:
`samples = ticks * rate / 40,500,000`, with the rate taken from AICR bit 1
(clear 32 kHz, set 48 kHz) because which one it is on is precisely what the
routine is trying to find out. Driving it from the frame tick instead would
have made the game measure a clock about ten times too fast.

**The result:** the boot now passes the whole audio stack -

```
[OSReport] << Dolphin SDK - AR   release build: Apr 17 2003 >>
[OSReport] << Dolphin SDK - ARQ  release build: Apr 17 2003 >>
[OSReport] << Dolphin SDK - AI   release build: Apr 17 2003 >>
[OSReport] << Dolphin SDK - AX   release build: Jul 29 2003 >>
[OSReport] << Dolphin SDK - DSP  release build: Apr 17 2003 >>
```

and runs 12,000,000 steps to its limit without spinning, with 0 desyncs. It
had not passed 2.5 million before.

---

**F105 — the DSP mailbox handshake, and where the boot sits now.** After the
audio hardware came up the boot spun 90,000,000 steps at `fn_800376D4`, which
is four instructions: read `0xCC005004`, extract bit 0x8000, return. That is
`DSPCheckMailFromDSP`, and its caller loops on it.

The SDK's `__DSP_boot_task` waits for the DSP to post **0x8071FEED** - it
asserts on any other value - and then sends a dozen messages, spinning on
`DSPCheckMailToDSP` after each until the DSP takes it. Three mechanics, all
in the mailbox registers (`__DSPRegs[0..3]`, top bit of each high half being
the "full" flag):

- a message the CPU sends is consumed **as the send completes**, because
  nothing here is going to consume it;
- reading the low half of the DSP's mailbox **empties it**, so the next check
  returns zero - leaving it set makes one message readable forever, and the
  handshake is a sequence of distinct ones;
- unhalting the DSP (`DSPInit` sets 0x800, then clears the halt bit) posts
  0x8071FEED, which is what the real boot ROM does.

**This is the handshake and not a DSP.** No microcode runs and the messages
after the first are accepted and dropped. It is enough to get past the
bring-up and no further; phase 4 is where it becomes a coprocessor.

**Where the boot is now:** 60,000,000 steps, no spin, 0 desyncs, cycling
through the OS scheduler - `SelectThread`, `OSRestoreInterrupts` and
neighbours. Frames stay at 55, disc reads at 3 and GX at 7,235 commands, so
it is running threads without progressing. That is the next question, and it
should be asked with a profile rather than by reading code (F92, F99).

---

**F106 — two names worth 472 call sites between them.**

`fn_80013E44` is **`rand`**, at 303 call sites the most-called unnamed
function in the binary. It multiplies a seed by 0x41C64E6D, adds 0x3039 and
returns `(seed >> 16) & 0x7FFF`. Those are 1103515245 and 12345 - the linear
congruential generator from the ISO C standard's own example - and the shift
gives exactly the RAND_MAX range. It sits between `qsort` and `strchr`, which
is where the Metrowerks library puts it.

`fn_80025054` is **`PSMTX44Identity`**, 169 call sites. It matches the SDK's
body instruction for instruction and offset for offset - `stfs` on the
diagonal at 0x00, 0x14, 0x28, 0x3C and paired-single zero stores between -
and sits exactly where `mtx44.c`'s source order puts it. The assembly matcher
missed it because it is written as a C function wrapping an `asm` block
rather than as a whole `asm` function.

**Call sites covered 76.1% to 82.8%**, phase 0 average 65.9% to 67.4%. Two
names. The function count barely moved, which is the point of measuring call
sites at all.

---

**F107 — THE BOOT'S CURRENT WALL, located precisely: it is waiting for a DSP
task to finish.** Not yet fixed. The analysis is here so the next session
starts from it.

A profile of 30,000,000 steps puts **90.4% at one address**, `0x80032A60`,
which is a three-instruction loop at the end of `fn_800329B4`:

```
.L_80032A60:
    lwz   r0, lbl_8027DF04@sda21(r0)
    cmpwi r0, 0
    beq   .L_80032A60
```

`fn_800329B4` clears that global, fills a 0x4A00-based structure with vectors,
calls `OSInitThreadQueue`, submits something through `fn_800377DC`, and then
waits. The only other reference to the global is `fn_80032924`, which is three
instructions - `flag = 1; blr` - and whose address is written into the
structure at +0x28. **So the boot is waiting for a completion callback.**

The structure is a `DSPTaskInfo` and the callback is its `done_cb`. From the
SDK's `dsp_task.c`, `__DSPHandler` runs on the DSP interrupt, reads one mail,
and dispatches on it:

| mail | meaning |
|---|---|
| `0xDCD10000` | task started - calls `init_cb` |
| `0xDCD10001` | resumed - calls `res_cb` |
| `0xDCD10002` | yielded |
| `0xDCD10003` | **done - calls `done_cb`** |

So getting past this needs the DSP to post `0xDCD10000` and then `0xDCD10003`,
raising the DSP interrupt for each. `__DSPHandler` reads exactly one mail per
interrupt and asserts that a current task exists, so interrupts must not be
raised before the guest has booted one - the boot mail being consumed is the
signal that it has.

**This was deliberately not built.** It is the DSP task lifecycle, which is
phase 4 by the project's own ordering (rule 13), and a speculative half of it
would be hard to tell apart from a correct one until much later. What is here
already - ARAM, the sample counter, the mailbox handshake - is hardware coming
up, which the boot needs regardless of when the mixer is written. A task
lifecycle is the mixer's own contract.

**Note what this means for the phase order:** the boot cannot proceed past
audio initialisation without at least a stand-in for task completion, so some
phase-4 work is now on the critical path for phase 2. That is worth deciding
deliberately rather than drifting into.

---

**F108 — the GX surface the game uses is fully named, and the row that
measured it was wrong.**

Seven more names, all confirmed against `dolsdk2004`'s `GXTransform.c` rather
than inferred from position:

| address | name | how |
|---|---|---|
| `0x800460FC` | `GXLoadNrmMtxImm` | `addr = id*3 + 0x400`, `reg = addr\|0x80000`, and a **stride of 16** through the source - rows of a 3x4 `Mtx`, not the 12 that `GXLoadNrmMtxImm3x3` would use |
| `0x80046180` | `GXSetCurrentMtx` | a 6-bit field into `__GXData->matIdxA`, then `__GXSetMatrixIndex(GX_VA_PNMTXIDX)` |
| `0x800461B4` | `GXLoadTexMtxImm` | `id >= 64` selects `0x500`, and `type == GX_MTX2x4` selects 8 floats over 12 |
| `0x80046268` | `GXLoadTexMtxIndx` | the same `id >= 64` shape, indexed |
| `0x800462C0` | `__GXSetViewport` | reads the viewport fields and writes six XF words |
| `0x80046350` | `GXSetViewport` | stores six floats into `__GXData`, calls the above, sets `bpSentNot`. **Not `GXSetViewportJitter`**: that takes a seventh `field` argument and this takes none, so the wrapper was inlined |
| `0x80046398` | `GXGetViewportv` | copies six floats back out of `__GXData+0x4F4` |
| `0x800464E4` | `__GXSetMatrixIndex` | already named; reached through `GXSetCurrentMtx` |

**And the GX row was measuring the wrong thing.** It compared our *count* of
GX-prefixed names against the count in mkdd's symbol table, which is a count
and not a coverage - after these names it read **179/177 = 101.1%**. Two
different games do not have the same GX surface, and mkdd's table also carries
C++ mangled names of its own (`GXDrawBegin__14stParticleDrawFUl`), so that
denominator was never the right set.

`config/gx-surface-used.txt` is the right set: every GX function *this* engine
calls, recovered from the REL's cross-module calls, and the phase 3
specification. Against it the row reads **81 / 81 — all of them**. The
headline average went *down*, from 68.7% to 68.5%, which is what an honest
denominator does.

---

**F109 — the DSP interrupt now routes, the mails are consumed, and the
callbacks still do not run.** PARTIAL. What works is committed; the open
question is one step and is written down here rather than rediscovered.

**What was wrong and is now right.** `DSPCR`'s ARAM completion flags were
forced set on every *read* - a stand-in from before ARAM was modelled. Those
are the *status* bits the operating system's dispatcher reads to decide which
of the DSP line's three sources fired: audio-interface DMA, ARAM, or the DSP
itself. Held permanently set, **the ARAM source always looked pending, so the
dispatcher never reached the DSP handler.** They are raised now where the
transfer actually happens, which ARAM being real makes both possible and
accurate.

The three status bits are also **write-one-to-clear**, which a flat register
store is not. `__DSPHandler` acknowledges with
`tmp = (tmp & ~0x28) | 0x80; __DSPRegs[5] = tmp;` - under a plain store that
*sets* the DSP bit and leaves the line asserted for ever.

**Three mistakes of my own on the way, all worth knowing.**

- The task check was chained onto the graphics one with `else if`. The
  graphics check runs every 64 steps, so **every interval sharing a factor
  with 64 was unreachable** - written as `steps % 4096` it never ran once.
- A post whose interrupt could not be delivered left the message sitting in
  the mailbox, so every retry saw one pending and declined while the guest
  never read it. The message is taken back out now.
- Requiring "the guest has sent something" before posting is right for
  *starting* a task and wrong afterwards: reading a message resets that
  count, so the sequence stuck half way, start delivered and finish never
  posted.

And the task must not be started mid-upload. `__DSP_boot_task` sends a dozen
messages before the task becomes current, and a message posted during that
reaches a handler whose `__DSP_curr_task` is still NULL - in a release build,
with the assertions compiled out, that is a store through a null pointer and
a call through whatever is at +0x28 of it. Waiting for the sends to stop is
the signal, and needs no count of how many the sequence contains.

**Where it stands.** A full cycle now runs: `0xDCD10000` posted, delivered,
read; `0xDCD10003` posted, delivered, read. **And neither callback fires.**
Hooking them directly - `init_cb` at task+0x28, which is the three-instruction
flag-setter the boot waits on, and `done_cb` at +0x30 - shows both at zero
while the mails are demonstrably consumed.

So **something other than `__DSPHandler` is reading the mailbox**, or
`__DSP_curr_task` is not the task whose callbacks were hooked. That is the
next question and it is answerable: find the reader. `fn_800376E4` is
`DSPReadMailFromDSP` and `fn_80037F28` loops on it, so the boot-task path is
the first suspect.

**The struct layout is confirmed from the SDK's own header**, not inferred:
`+0x28 init_cb`, `+0x2C res_cb`, `+0x30 done_cb`, `+0x34 req_cb`. The boot
waits on **`init_cb`**, not `done_cb` - I had that the wrong way round at
first and it changes which mail matters.

**No regression:** 0 desyncs, the logo unchanged at 55 frames and 7,235
commands, 13/13 tests.

---

**F110 — THE PORT WAS NOT DETERMINISTIC, and that invalidated every A/B
comparison made with it.** Two identical invocations, same binary, same step
count:

| | run 1 | run 2 |
|---|---|---|
| DVD reads completed | 30 | **55** |
| GX commands | 7,235 | **16,475** |
| triangles | 108 | **12,412** |

Found by accident. A scissor comparison came back saying the feature made the
game load *more* - which a rasteriser cannot do, since nothing it writes
feeds back into guest execution. The result was noise, and so was the
comparison.

**The cause:** `runtime/dvd/dvd.c` submits reads to a host worker pool, and
`drain` reported a read finished as soon as the worker set its flag. How much
had loaded by a given guest step therefore depended on host thread
scheduling. The design document requires the opposite in as many words -
timing comes from steps and not the wall clock, *so that a run can be
replayed* - which is what the whole Dolphin comparison rests on.

**The fix:** a read now carries a `ready_tick` in the GUEST's clock, set when
it is submitted, and `drain` will not report it before then. If the host
worker somehow has not finished by the time the guest's clock says it should
have, the drain waits - completing early would put the nondeterminism
straight back.

The modelled cost is a constant plus a per-byte one and is **not** the real
drive's timing. It is not trying to be: what it buys is that the same run
produces the same result. Modelling the real rate belongs with the Dolphin
comparison, where there will be something to check it against.

**One mistake worth keeping.** The first version had `drain` adopt whatever
time it was passed, and the *synchronous* read path passes a synthetic future
time to say "treat this one as ready". That dragged the clock forward for
every read submitted afterwards and made them complete early - two of three
runs matched instead of three of three. The clock is now set explicitly by
the pump and `drain` only compares against what it is given.

**Four identical runs**, byte-for-byte on every counter down to the deferred
count of 3,271.

---

**F111 — the thread dump was hiding half the threads, including every blocked
one.** It rejected any thread pointer outside MEM1 as "not a thread" and
stopped the walk there. The engine's overlay lives at 0x7E000000 and upwards
and creates threads in its own memory, so the list ended at the first of
them: a dump reporting four threads was concealing four more, and the ones
that mattered were all in the hidden half.

**An instrument that quietly stops early is worse than one that refuses**,
because a short answer still looks like an answer. That is the third time in
this session - a phase marker that was only ever set (F99), a heartbeat whose
interval aliased with the tick (F91), and now this.

With the second window accepted, there are **eight** threads:

| thread | prio | state |
|---|---|---|
| 0x801ECD70 | 16 | SUSPENDED - parked deliberately, see below |
| 0x8020BCF0 | 31 | running: the idle thread |
| 0x801E8E30 | 0 | waiting on 0x8027DD74 (healthy: 3,627 sleeps, 3,624 wakes) |
| 0x80209D78 | 18 | waiting on 0x8020B95C |
| **0x7F4A5630** | **10** | **waiting on 0x7F4A595C - the engine's main thread** |
| 0x80217D58 | 10 | waiting on 0x8021336C |
| 0x802134E8 | 9 | waiting on 0x802133AC |
| 0x80215920 | 11 | waiting on 0x8021338C |

**The suspended thread is not a bug.** `fn_8004A658` sets up the engine's
threading system, spawns its workers and then calls `fn_8004A630`, which is
`OSGetCurrentThread` followed by `OSSuspendThread` - it parks the thread it
was called on, by design, and the work continues on the threads it made.

---

**F112 — where the boot now stops: the engine's main thread is waiting for a
fourth event that never arrives.** Not yet fixed.

Counting sends against receives per queue is what found it. Three message
queues - 0x802133A4, 0x80213384, 0x80213364 - receive once and are never
sent to; those are the engine's worker threads waiting for jobs, which is
what an idle worker looks like. The interesting one is the engine's **main**
thread's queue at 0x7F4A5954:

```
recv from REL 0x28C          send from REL 0x8C0
recv from REL 0x28C          send from REL 0x8C0
recv from REL 0x28C          send from REL 0x8C0
recv from REL 0x28C          <- waiting, no fourth send
```

A clean alternation, three times, then nothing. `fn_1_888` (REL 0x888) is the
poster: it stores its argument into a structure, sets a type field to 8, and
calls `OSSendMessage`. It has **17 call sites**, almost all in one cluster
between REL 0x88B7C and 0x893AC - a block of small, similar functions that
look like completion callbacks.

Walking the guest stack two frames at the send names the three that did fire.
`fn_1_888` saves its caller at +0x14 of the frame it builds and the wrapper
above it does the same, so +0x14 and +0x24 give the poster and its origin:

| # | origin | function |
|---|---|---|
| 1 | REL 0x13117C | `fn_1_13111C` |
| 2 | REL 0x130AA4 | `fn_1_130A54` |
| 3 | REL 0x130AA4 | `fn_1_130A54` |

`fn_1_130A54(buffer, x, size)` waits for readiness, flushes the buffer's
cache range, and posts a request carrying a completion callback
(`fn_1_130A28`). So these are **requests**, and the thread blocked in
`fn_1_264` is the **server**: that function is a bare
`OSReceiveMessage` that returns the message's first field, called in a loop.

**So nothing is deadlocked on a missing wakeup - the whole system is idle.**
The server waits for a request; the three engine workers wait for jobs on
queues nothing sends to; the boot thread is parked by design. And the
engine's own per-frame task scheduler, `fn_1_F394C`, **does not appear in the
profile at all** - 29 of 30 sampled addresses are in the DOL, and the single
engine address is a byte-copy loop at 0.2%.

**The question is therefore not "who forgot to send the fourth event" but
"what should be driving the engine's main loop, and why is it not running on
any thread".** `fn_1_88`'s loop ran earlier in the boot; no thread is in it
now. Worth checking whether it was expected to continue on the boot thread
that `fn_8004A658` parks, or on one of the spawned threads.

The traces for this are in place: `MGS_TRACE_MSG` prints every send and
receive with queue, message, poster and origin, and `MGS_TRACE_QUEUES` does
the same for `OSSleepThread` and `OSWakeupThread`.

**Also observed, and deliberately not acted on:** `__DVDThreadQueue`
(0x8027DD00) is slept on 52 times and never woken. That is expected rather
than wrong - the DVD reads are served natively by the patch table, so the
SDK's own DVD thread has nothing to do. Worth knowing before someone
"fixes" it.

---

**F113 — the engine's main loop runs four scheduler passes and then its
thread blocks.** This is the wall, stated as precisely as it can be without
fixing it.

`fn_1_88` is an endless loop: clear a flag, run the per-frame task scheduler,
repeat. A thread that enters it never leaves. Tracing both:

```
[eng] main loop entered (#1), lr 0x8004A46C
[eng] task scheduler run #1
[eng] task scheduler run #2
[eng] task scheduler run #3
[eng] task scheduler run #4
```

Four, and then nothing, for the remaining 25 million steps. **So the loop did
not fail to start and was not preempted away - a task it dispatched blocked,
and took the main loop with it.** The scheduler reaches every task through a
`bctrl`, so a task that waits is indistinguishable from the scheduler
stopping.

**Everything else checks out**, which is what makes this precise rather than
a guess:

- **The task table is healthy.** Four tasks on level 0, two on level 1, more
  on 5 and 6, all with real function pointers. The global mask is
  `0x00000000`, so `gate & mask` is zero everywhere and **no level is
  skipped**; no node carries a flag in the skip mask either.
- **The heaps are healthy** - heap 2 has 14,079,520 bytes free in 25 blocks,
  against the zero that caused the `memory.c:1197` panic.
- **Mutexes are balanced** - two locks, two unlocks, one mutex.
- **The event traffic is consistent** - three requests posted, three
  received, the server waiting for a fourth that its own clients would have
  sent.

So the next step is to find **which task blocks**. The scheduler's dispatch is
a `bctrl` through the node's `+0x04`, and the table above lists them:
`0x7F128218`, `0x7F0FC588`, `0x7F0B2108`, `0x7F018390` on level 0 alone.
Logging the target of that indirect call, rather than trying to hook each
candidate, is the way in.

**A limitation of the instruments to know about first:** the call traces
(`MGS_TRACE_MSG`, `MGS_TRACE_QUEUES`, `MGS_TRACE_MUTEX`) compare the pc the
run loop samples, so they see cross-module calls reliably and can MISS a call
made from one part of the DOL to another within a single dispatch. The thread
waiting on queue `0x8020B95C` never appeared in any of them for that reason.
Absence in those traces is not evidence.

---

**F114 — the blocked thread's call chain, read out of guest memory.** The
thread dump now walks each thread's saved stack. PowerPC's ABI has every
frame point at the one below it with the return address a word in, and
`OSContext` sits at the start of `OSThread`, so the saved `r1` is at +4 and
the saved `lr` at +0x84. That turns "waiting on a queue" into a path:

```
fn_1_88        +0x98   the engine's main loop
 fn_1_F394C    +0x98   the per-frame task scheduler, just after its bctrl
  fn_1_12012C  +0x4C   the task it dispatched
   fn_1_11FF60 +0x88
    0x8004C318 +0x60   into the DOL
     OSWaitSemaphore
      OSSleepThread
```

**A resume address says nothing** - every switched-out thread resumes inside
the scheduler, so that column reads identically for all of them. The stack is
the only thing that distinguishes them, and it named the whole chain in one
run after several rounds of guessing had not.

Three names fell out of it, by behaviour next to the already-named
`OSInitSemaphore`: **`OSWaitSemaphore`** (0x80022B54 - reads the count,
sleeps on the queue at +4 while it is not positive), **`OSSignalSemaphore`**
(0x80022BC4 - increments and wakes), and **`OSGetSemaphoreCount`**
(0x80022C24, two instructions).

**`DEMOBeforeRender` at 0x8004C318 is almost certainly a wrong name.** It
came from ordered alignment against Mario Kart, and it sits inside the range
this project's own `__FILE__` evidence assigns to `CR_System.c`
(0x8004B6D8-0x8004C82C). Second mkdd-align error found this session, after
`DCZeroRange` (F97).

---

**F115 — what the main loop is waiting for, and a fix that made it worse.**

The semaphore is at 0x8020B958, waited from `0x8004C378` and signalled from
`0x8004D170` - and `fn_8004D170` has no static callers because it is
registered with **`GXSetDrawDoneCallback`**. So the engine's main loop waits
for the graphics processor to finish the frame.

The tally: **62 waits, 60 signals**, and the callback only signals when a
flag at +0x64 of its state is set.

**The hypothesis that follows is wrong, and it is recorded because it is
attractive.** A graphics processor that finishes instantly finishes *too
soon*: the token is parsed the moment the guest stores it, so the interrupt
could be raised before `GXDrawDone` has armed the flag its callback checks -
the callback would then run, find the flag clear, signal nobody, and the wait
would never end. Modelling a real GPU's completion latency looks like the
principled fix, exactly as it was for the disc (F110).

It is not. Holding the finish interrupt back by 20,000 guest ticks took the
boot from 75 frames, 20,429 GX commands and 55 disc reads to **3 frames, 629
commands and none**. The boot depends on prompt completion long before the
frame loop exists. Reverted.

So the two missing signals are not a systemic race but something specific to
the last frames, and the next step is to watch the callback itself: whether
it runs 62 times and declines twice, or runs only 60. That distinguishes "the
interrupt was not delivered" from "the guest was not ready", and they need
opposite fixes.

---

**F116 — where the remaining unnamed call sites actually are, and why the
headline number oversells them.** 1,161 call sites were unnamed. Sorting them
by the region they land in changes what that number means:

| region | call sites | public source? |
|---|---|---|
| Konami sound / Tremor (0x8004E700-0x80062000) | 602 | no |
| CR_System, Konami (0x8004A000-0x8004E700) | 270 | no |
| CodeWarrior runtime / boot | 184 | partly |
| SDK: GX | 55 | yes |
| SDK: OS | 26 | yes |
| SDK: audio/DSP/AR | 24 | yes |

**872 of the 1,161 are in Konami's own code**, where there is no reference
binary to align against and no upstream source to match — every name there has
to be earned one function at a time by reading the instruction stream, and can
only ever be a description. The identifiable remainder is small and was worth
taking first. Do not read "1,161 call sites left" as "1,161 names available".

The per-region split is worth regenerating whenever this question comes up
again; it is a dozen lines of Python over the REL disassembly and the symbol
map, and it stops the work being aimed at the wrong 75%.

---

**F117 — main.dol's only square root is not `sqrtf`, and our own note about
the REL's `sqrtf` was wrong.** `fn_80006668` is the single most-called unnamed
function in main.dol: **146 call sites**, more than any other. Its body is the
SDK's `sqrtf` exactly — `frsqrte`, three Newton-Raphson rounds against 0.0,
0.5 and 3.0 (verified by reading the constants out of the DOL at 0x800620A0,
0x800620C0, 0x800620C8), then `frsp`.

It is still not `sqrtf`. Before the comparison it replaces its argument with
`|x|`, by storing the float, clearing the sign bit with `clrlwi r0, r0, 1`,
and loading it back — and the replaced value is what both the early-out and
the iterations then use. So it returns a real root for negative input where
`sqrtf` returns the input unchanged. Named `sqrt_abs`, lower-case because that
is a description and not a claim about what Konami called it.

**The check that nearly went wrong.** `mgso_pal.rel.symbols.txt` already had a
`sqrtf`, and its header offered that function as the example of a name proven
"beyond doubt" — describing it as "returning zero for non-positive input".
That is false. The `ble` path does not touch `f1`, so `sqrtf(-3.0f)` returns
`-3.0f`; zero is only what `x == 0` happens to produce. Had the note been
trusted, the DOL function would have looked like a *different* thing from the
REL one for the wrong reason, and the actual difference — the `fabs` — might
have been read as incidental. The note is corrected, and the correction is
recorded in the file itself rather than silently applied.

The engine calls **both**: the genuine `sqrtf` in the REL at `.text 0x017834`
and `sqrt_abs` in the DOL, 146 times. That is a deliberate distinction in the
game's own code, not a duplicate.

---

**F118 — the memory-card module, and a function that is two functions.**
`CARDGetStatus` was reached from the unnamed list and matches `CARDStat.c`
instruction for instruction: the bound is `CARD_MAX_FILE` (0x7F), the
directory stride is 0x40 (`sizeof CARDDir`), and the two `memcpy`s are 4 bytes
to `CARDStat+0x28` and 2 to `+0x2C` — `gameName` and `company`, at exactly
those offsets in the public header. Its callees came with it, and the three
synchronous CARD entry points followed from the Async-plus-`__CARDSync`
shape. Sixteen symbols in total, including `__CARDBlock` (0x220 = 2 x
`sizeof(CARDControl)`) and `__CARDDiskNone`.

**The one that was briefly named wrong.** `fn_8003D4B0`'s head is
`__CARDIsWritable` verbatim — `CARD_RESULT_NOPERM`, then `permission & 0x20`
with two `memcmp`s against `__CARDDiskNone` — and reading only the head gives
that answer, which contradicted `CARDGetStatus` calling it where the SDK calls
`__CARDIsReadable`. The tail resolves it: `permission & 0x4` returning READY
is `__CARDIsReadable`, which in this build has `__CARDIsWritable` **inlined
into it**. One entry point, both tests, 0xF4 bytes.

The general lesson is worth more than the symbol: **a function's first twenty
instructions can belong to a different function than its last twenty**, and a
signature match against a head is not a match. The contradiction was the
useful signal — the call from `CARDGetStatus` disagreed with the head, and
that disagreement was right.

---

**F119 — three GX entry points, named from the register shadow they touch
rather than from their shape.** `__GXData+0x1DC` is PE_CONTROL: GXInit writes
`li r0, 0x43` into its top byte at 0x8003F3EC, which *establishes* the
register rather than assuming it from surrounding code. `+0x204` is genMode,
pinned the same way by the already-named `GXSetCullMode` writing bits 16-17.

- `GXPixModeSync` (0x8004208C, **31 call sites**) — no argument, re-sends
  PE_CONTROL unchanged. That is the entire purpose of the function.
- `GXGetCullMode` (0x800426BC) — sits immediately after `GXSetCullMode` and is
  the same 0x44 bytes, applying the identical 1 <-> 2 remap in reverse,
  because the hardware's cull field has FRONT and BACK swapped relative to
  the API enum.
- `GXInitTexObjWrapMode` (0x80043DE0) — two 2-bit fields at bits 0-1 and 2-3
  of the texture object's mode word, which is `wrap_s` and `wrap_t`.

All three were then confirmed by name **and signature** against the
dolsdk2004 headers. That is two independent legs, so they carry a new origin
`own+sdk2004` rather than either `own` or `sdk2004` alone — the behaviour is
evidence from our copy, the declaration is evidence from a public clean-room
source, and claiming only one of them would understate what was checked.

A side effect worth keeping: deriving PE_CONTROL and ZMODE independently also
re-confirmed `GXSetZMode`, `GXSetZCompLoc` and `GXSetPixelFmt`, which were
carrying `mkdd-align` — an ordering argument — and now have a behavioural one
as well.

---

**F120 — the constant pool identifies the library, then each function's
constants identify the function.** Three more matrix-library entry points
(`PSMTXQuat`, `C_MTXOrtho`, `PSVECDistance`) came from a route worth reusing:
instead of matching function bodies, read the **shared constant pool** they
load from. `.sdata2 0x8027E530`-`0x5C` holds 1.0, 0.0, 0.5, 2.0, -1.0 and
**0.017453292** — pi/180. Nothing but a matrix library keeps that value, so
every function loading from that pool belongs to one, which narrows the
candidate set to a handful of declarations in `mtx.h` before any body is read.

Each function is then pinned by which of those constants it uses and where:
`C_MTXOrtho` writes all sixteen floats with -1.0 at m[2][2] and 1.0 at
m[3][3]; `PSMTXQuat` derives 2.0 as 1.0+1.0 and 0.0 as 1.0-1.0 from a single
load, which is this library's paired-single idiom; `PSVECDistance` takes
**one** Newton-Raphson round against 0.5 and 3.0, where `sqrt_abs` (F117)
takes three. That one-round-versus-three is what separates two functions that
otherwise share a shape.

The constants were read out of `build/phase0/out/asm/auto_09_8027E060_sdata2.s`
rather than assumed from the formula — the direction matters, because
assuming 0.5 and 3.0 and then finding them is not evidence.

---

**F121 — both progress records had drifted, and the bigger one had drifted
silently for longer.** `HANDOFF.md`'s table said 961 functions named while the
committed symbol map yielded 964: commit `0f89358` added three symbols and
skipped the regeneration. `MILESTONES.md` was worse — it claimed **1,082
symbols against a real 1,174**, out by 92, because nothing had recomputed that
line in a long time.

The 92 is the instructive one. It stayed *plausible* the whole way, which is
why nobody caught it; a number that is wrong by a factor would have been
noticed in a week. Rule 14 says a stale progress figure is worse than none,
and this is the mechanism — it does not announce itself.

`tools/progress.py --check` now compares both documents against the evidence
and exits non-zero on disagreement, covering five table rows, two headline
percentages and the symbol total. Both legs were verified by being shown a
stale number and failing on it, because **a check that has never failed has
not been tested**. Run it before committing; it is instant and needs no build.

---

**F122 — the boot is LIVELOCKED, not slow, and five times the budget proves
it.** Every run ends `stopped after 40000000 steps: step limit`, and the
obvious question — is it stuck, or merely slow? — had never been asked
directly. `MGS_STEPS=200000000` answers it:

| | 40M steps | 200M steps |
|---|---|---|
| GX commands | 20,429 | 20,429 |
| triangles | 24,716 | 24,716 |
| EFB copies | 75 | 75 |
| PE finishes delivered | 62 | 62 |
| DVD reads | 55 | 55 |

**Byte-identical.** 160 million additional steps produced nothing at all. The
boot is not running out of time; it stops making progress and then burns
whatever budget it is given. Every future "did that help?" comparison should
be read against this baseline, and a change that only moves the step count is
not a change.

It is also a free re-confirmation of F110's determinism, by a route that was
not designed to test it: two runs with different budgets agreeing exactly.

---

**F123 — the thread burning 100% of the steps is SUPPOSED to spin.** The
profile is dominated by `OSDisableInterrupts` (3,024,420 calls) and
`OSRestoreInterrupts` (3,024,402), with 1,479,243 of them from `0x8004C97C`
and 1,473,478 from `OSYieldThread+0x14`, alongside ~1.485M reads each of PI
`+0x014` and CP `+0x030/032/038/03A` — the FIFO write pointer, the read-write
distance and the GP read pointer.

That looks exactly like a hot spin on hardware that never answers, which is
what it was first read as. It is not. `fn_8004C948` is:

```
loop:  fn_8004C960();      /* poll the GP through GXGetFifoPtrs */
       OSYieldThread();
       goto loop;
```

It has **no callers**, because its address is handed to `OSCreateThread` at
`0x8004BDB4`. It is a dedicated graphics-service thread whose whole job is to
poll and yield, and it is *meant* to run forever.

So the profile is not showing a fault, it is showing an **idle system**: this
thread is simply the only runnable one, so it gets every step. "Where are the
cycles going" was the wrong question — the right one is why nothing else is
runnable, and that is F124.

**What not to do:** do not try to make this loop cheaper or to throttle it.
It is the idle task. Its cost is a symptom of the boot having nothing else to
do, and it will disappear when the boot progresses.

---

**F124 — the deadlock, located precisely, and it is one missing signal.**
With `MGS_TRACE_SEM` and a new `MGS_TRACE_RING` reporting the engine's frame
ring at each completion, the steady state is a clean alternation, 60 times
over:

```
[sem] wait   0x8020B958 count=1   <- passes, submits a frame
[ring] pe #N  prod P cons C flag 1
[sem] signal 0x8020B958 count=0   <- tops it back up
```

and then it ends:

```
[sem] wait   0x8020B958 count=1   <- passes, submits frame 61
[ring] pe #62  cause 0x140  prod 1 cons 0 flag 1
[sem] wait   0x8020B958 count=0   <- BLOCKS, and nothing ever signals
```

**Two waits in a row with no signal between them.** The semaphore starts at 1,
so 1 + 60 signals lets 61 waits through; the 62nd finds zero and sleeps.

The structure is now known. It is a **4-slot ring**: producer index at
`0x8020B918+0x16F0`, consumer at `+0x16F8`, and `fn_8004C4E4` is exactly
`idx = (idx + 1) % 4`. The submitter `fn_8004C318(mode)` arms the slot's flag
at `+0x64`, and the callback `fn_8004D170` signals only when that flag is set.

**Three explanations are now ruled out, each by measurement:**

1. *"The flag is clear because the engine passed mode == -1."* No — `mode ==
   -1` also skips the wait (`cmpwi r28,-1; beq` past it), so it can produce
   neither a wait nor a signal. All 62 waits happened, so all 62 submissions
   armed their flag.
2. *"We failed to deliver the interrupt."* No — 62 delivered, and the guest's
   own acknowledgements at the pixel engine's `PE_INT_CTRL` number **62** as
   well. Every handler ran to the end.
3. *"It is a race with instant GPU completion."* No — the one completion that
   legitimately declines is **pe #1**, delivered before any frame was
   submitted, when producer == consumer == 0 and the flag is the zero `.bss`
   was initialised with. The ring self-corrects: the consumer does not advance
   past an unarmed slot. This is also why F115's latency experiment made
   things worse rather than better.

So: **62 delivered, 62 acknowledged, 61 with the flag set, 60 signalled.**
One completion's handler ran, acknowledged the pixel engine, and did not
signal. That is the whole remaining defect, and it is one event.

**The next experiment**, and it is narrow: the PI cause ends at `0x00000440`
— PE finish **and** DSP still pending and armed — after 5,284 further
interrupts were delivered. At pe #62 the cause reads `0x140`, VI pending
alongside DSP, where pe #59-61 all read `0x40`. So the 62nd completion is the
first to arrive with a **VI interrupt already pending**, and the SDK's
dispatcher services one source per entry by priority. Watch which source
`__OSDispatchInterrupt` picks on that entry, and whether PE is left set behind
a VI it lost to.

---

**F125 — two instrument errors in one session, both of which produced
confident wrong readings.** Recorded because the failure mode is the same
both times: the instrument measured a moment other than the one that mattered.

1. **The ring trace sampled after the raise.** `mgs_interrupt_raise` does not
   call the handler — it performs the state transition the hardware performs
   and returns, and the guest executes the handler on later steps of the run
   loop. Sampling "after" therefore reports a state in which **no guest
   instruction has run**, which is why the before and after values were
   identical on all 62 events. Read naively, that looked like the callback
   never advancing the ring.
2. **The PI cause was printed after the raise had set it.** Bit 10 was
   therefore present on every line, and I read that as proof of a stuck,
   never-acknowledged interrupt. We had just set it one line earlier.

The honest measure of an unserviced interrupt is what is still pending when
the run **ends**, which is now reported separately, and the guest's own
acknowledge count, which is now counted at `PE_INT_CTRL`. Both are in the
run's normal output rather than behind a trace flag, because both are cheap
and both answer a question that comes up every time.

**The general rule this cost twice:** when an instrument straddles a state
transition, say in the code which side of it each value comes from. Both bugs
were invisible in the output and obvious in the source.

**A smaller thing found while reading the run loop**, not yet a fault: the PE
completion check is chained `else if` onto the retrace check, so it is skipped
whenever `steps % 16000 == 0` (lcm of 2,000 and 64). It is harmless today —
the token is put back and retried 64 steps later — but it is the same shape as
the bug that made the DSP branch unreachable, and the comment warning about
that sits directly beneath it.

---

**F126 — THE BOOT'S LIVELOCK WAS ONE MISSING WORD IN THE INTERRUPT MODEL:
the external interrupt is LEVEL-triggered, and we were treating it as
edge-triggered.**

The processor interface asserts its line while **any** armed cause bit is
set. The CPU does not take one interrupt per event — it takes one whenever
that line is high and `MSR[EE]` is on. So a source still pending when a
handler returns is taken **again**, immediately, as `rfi` restores EE.

That matters because of how the SDK's dispatcher works. `__OSDispatchInterrupt`
services **exactly one source per entry**: it builds the pending set, picks
the highest priority from `InterruptPrioTable`, calls that one handler, and
returns through `OSLoadContext`. Everything else stays pending and *relies on
being re-taken*.

`host/interrupt.c` only ever entered the dispatcher when it raised a new
event. So every completion that arrived while a higher-priority source was
pending was serviced as the other source and **never seen again**.

**The evidence, in the order it arrived:**

- `OS_INTERRUPTMASK_PI_VI` sits directly above `OS_INTERRUPTMASK_PI_PE` in
  `InterruptPrioTable`. VI beats PE.
- The trace shows pe #59, #60 and #61 arriving with `cause 0x40` — DSP
  pending, which PE outranks — and all three signalled.
- pe #62 arrives with `cause 0x140`: **VI pending too**. VI wins, PE is left
  set, and no acknowledgement follows it. That is the 61st signal that never
  came, and the entire boot.
- The run ends with `PI cause 0x440` — PE *and* DSP still pending and armed
  after 5,284 further interrupts. Both had been lost the same way.

**The fix** is `mgs_interrupt_pending()`, called from the run loop on a prime
interval: if `cause & mask` is non-zero and `MSR[EE]` is on, re-enter the
dispatcher. It invents no event and sets no cause bit — it only re-takes an
exception the hardware would have taken. A masked source is not re-offered,
because the SDK keeps PI's mask in step with its software mask
(`__OSMaskInterrupts` writes `__PIRegs[1]`), which makes `cause & mask` the
honest test for "still asserted".

**The result, at the same 40,000,000 steps:**

| | before | after |
|---|---|---|
| GX commands | 20,429 | **177,805** |
| primitives | 454 | **8,422** |
| triangles | 24,716 | **514,828** |
| EFB copies | 75 | **474** |
| frame completions | 62 | **223** |
| DVD reads | 55 | **90** |
| PE still pending at exit | yes (`0x440`) | **no** (`0x040`, DSP only) |
| stopped at | `0x8001FCCC`, the SDK's idle spin | **`0x7F0F6AF0`, the engine's own code** |

The last row is the one that matters. The boot was ending inside
`OSDisableInterrupts` with the idle graphics thread holding every step
(F123); it now ends inside the overlay, running the game.

**Determinism is preserved** — three consecutive runs byte-identical, checked
because a change to interrupt timing is exactly the kind that breaks F110.

**Known regression to look at:** GX desyncs went 2 → 77. The rate rose as
well as the count (0.010% of commands to 0.043%), so this is not merely "more
commands". With 20x the geometry now flowing, the parser is meeting
command shapes it never reached before; treat it as newly-exposed rather
than newly-broken, but it is real and it is next after the DSP line.

**THE BOOT STILL LIVELOCKS, FURTHER ON.** This is not a fixed boot, it is a
boot that got 20x further and hit a second wall. `MGS_STEPS=200000000` again
produces output identical to 40,000,000 — same 177,805 commands, same
514,828 triangles, same 223 completions — with only the re-offer count
growing, 165,521 to 871,157. The F122 test still says livelock; it now says
it about a different place.

**Still asserted at exit: DSP (`0x40`)**, and that is where to look. Almost
every one of those 871,157 re-offers is the same stuck line being taken
again, which is what a permanently-asserted source costs once the model is
honest about levels. The DSP interrupt is never cleared — the same class of
fault this finding fixes for PE, but at the device rather than in the model.
F109's question now has a sharper form: **the line is stuck, not the
mailbox**, and the run's own `PI cause` line says so on every exit.

---

**F127 — why there is still nothing to look at, even with half a million
triangles.** The obvious question after F126 is: if the geometry is flowing,
can we see it? No, and the reason is now measured rather than guessed.

Both external framebuffers are **pure black**, and so is the embedded one:

| buffer | content |
|---|---|
| XFB `0x80066480` (112 copies) | 100% black, 1 distinct colour |
| XFB `0x8015A480` (111 copies) | 100% black, 1 distinct colour |
| the EFB itself, at exit | 100% black, 1 distinct colour |

That rules out the copy-out path and the video interface in one step: there
is nothing in the EFB to copy. `MGS_SAVE_FROM=efb` writes the embedded buffer
directly for exactly this reason — it is the one view that separates "the
rasteriser drew nothing" from "the copy out lost it", and those need opposite
fixes.

**But the rasteriser is not drawing nothing.** Of 12,156,928 pixels written,
**1,167,715 are lit** — 9.6%. So colour is being produced; roughly nine in
ten written pixels are black, and by the final frame none of the lit ones
survive. Across 474 copies that is about 2,460 lit pixels per frame out of
229,376, which is under 1% of the screen.

The number that explains it: **778 textured, out of 514,826 triangles.**
Essentially no texturing is happening. The geometry, transform, viewport,
scissor, depth test and rasterisation are all evidently working — a triangle
count that large with only 2 clipped says so — and what is missing is the
shading. That is the renderer gap list (indirect textures, lighting, fog,
blending, TEV stages), and it is now the thing standing between this and a
picture, rather than anything upstream of it.

**Three measurement traps hit while establishing this, all recorded:**

1. `0 frames presented` in a headless run **means nothing**. `mgs_video_framebuffer()`
   is NULL without a window, so `mgs_display_present` returns early by
   design. It is not evidence about pixels.
2. The VI scan address is **not** the last copy destination. The game double
   buffers, so VI names the buffer finished *last* frame, and it is offset a
   further `0x400` — one line at stride 1024 — for the interlaced field.
   Three different addresses are all legitimately "the framebuffer".
3. `MGS_SAVE_FROM=copy` read `s_efb.copy_dest`, which at exit was a **64x64
   render-to-texture target**, not the XFB — the last copy of the run is an
   RTT, not a frame. Reading 512x448 from it produced 15,840 distinct colours
   of unrelated memory, which looked exactly like a real image and was not.
   A plausible picture is not evidence; the colour histogram of a known-black
   buffer is.

---

**F128 — the stuck DSP line was an interrupt raised on behalf of no device.**
F126 left PI's DSP bit asserted at every exit, costing 871,157 re-offers. The
measurement that settled it took one line in the run report:

```
PI cause 0x00000040   <- the DSP line is asserted
DSP control 0x0D50    <- and NO status bit is set (0x0D50 & 0xA8 == 0)
```

`0x0D50` is the three interrupt **mask** bits enabled (`0x10|0x40|0x100`) with
every **status** bit clear. That combination cannot happen on hardware, and
the SDK's dispatcher is built on the assumption that it cannot:

```c
if (intsr & 0x00000040) {         /* PI's DSP bit - ONE line, THREE sources */
    reg = __DSPRegs[5];           /* so read the device to tell them apart */
    if (reg & 0x8)  cause |= OS_INTERRUPTMASK_DSP_AI;
    if (reg & 0x20) cause |= OS_INTERRUPTMASK_DSP_ARAM;
    if (reg & 0x80) cause |= OS_INTERRUPTMASK_DSP_DSP;
}
```

With no status bit set, `cause` stays empty, no handler runs, and **nothing
can ever clear the bit** — because the thing that clears it is the handler
that could not be chosen. `mgs_interrupt_aram` was asserting PI's bit and
setting no status bit at all, so every ARAM completion was announced to
nobody and left the line high.

**The fix is one word of ordering: tell the device, then let the line
follow.** `mgs_mmio_dsp_assert_aram` sets ARAM's status bit, and a new
`dsp_refresh_line` — the exact counterpart of the existing `vi_refresh_line`
— makes PI's DSP bit a mirror of the three status bits rather than a latch.

| | before | after |
|---|---|---|
| PI cause at exit | `0x00000040`, stuck | **`0x00000000`** |
| interrupts re-offered | 871,157 | **1,122** |
| DVD reads completed | 55 | **64** |
| GX commands | 177,805 | 177,806 |

Three runs byte-identical, so determinism survives it.

**THE HALF-FIX THAT MADE IT 12x WORSE, and it is worth the space.** The mirror
was written *first*, on its own, without setting the status bit. It is strictly
more hardware-accurate than what it replaced, and it took the boot from
**177,805 GX commands to 14,324** and 514,828 triangles to 6,260.

The reason is the whole lesson: the host had two ways of announcing a DSP
event — the device model, and `mgs_interrupt_aram` reaching past it to poke PI
directly. A mirror makes PI follow the device, so it *correctly* dropped a
line the device had no reason to assert, and every ARAM completion raised that
way was lost. **Applying accuracy to one half of an inconsistent pair makes
things worse than leaving both wrong**, and the fix was not to revert the
mirror but to make the other half honest.

**The boot still livelocks**, now for a third time: 200,000,000 steps again
equal 40,000,000 at 177,806 commands. But the character has changed — nothing
is pending at exit and re-offers are down to 1,912 across five times the
steps, so whatever holds it now is **not** an interrupt that went missing.
That is a different search.

---

**F129 — there IS a picture, and texturing is not what is missing.** F127 said
the framebuffer was black; that was true of the frame at exit and misleading
about the run. `MGS_SAVE_BEST` keeps the fullest frame any copy ever produces
instead, and the best frame of a boot holds **22,034 lit pixels — 9.6% of
512x448, in 278 distinct colours**:

```
  ............144444444444444444444444444444444444444-............
  ............299999999999999999999999999999999999994.............
  ............29999999999999999999999999999999999993..............
  ............2999999999999999999999999999999999992...............
  ............29999999999999999999999999999999995-................
  ............-33333333333333333333333333333321-..................
```

A banner across the middle of the screen, slightly skewed, dark red ground
with cyan detail. **That is a real, structured image** — the whole route works
end to end: FIFO, vertex decode, transform, viewport, scissor, depth,
combiner, EFB, copy-out. Something the game drew is being drawn correctly.

**The hypothesis that looked obvious and is wrong.** 778 textured out of
514,826 triangles reads like a broken texture path, and the natural guess was
that the game binds textures on later TEV stages while the rasteriser only
resolves stage 0. Measured:

| | |
|---|---|
| triangles by TEV stage count | **1 stage: 514,826** — all of them |
| untextured at stage 0 but textured later | **0** |
| triangles asking for a texture | **778** |
| of those, bind failed | **0** |

Every triangle runs a single TEV stage, no triangle wants a texture on a later
stage, and **every texture the game asks for is supplied successfully**. The
texture path is not the fault. The game really does draw 514,048 of its
triangles untextured.

**So the gap is the untextured path through the combiner**, which is
producing black: nine in ten written pixels are. The combiner does implement
the colour registers (`BP_TEV_REGISTER_L`, 0xE0-0xE7) and the colour
environment (`BP_TEV_COLOR_ENV`, 0xC0+), so this is not a missing feature at
the register level, and the next step is to sample the actual
`TEV_COLOR_ENV` configuration the untextured majority uses rather than guess
again. Per-draw, not at exit — `genMode` and `TEV_ORDER0` both read
*correctly* at exit (`0x000011`, and stage 0's texture-enable bit set), which
says nothing at all about the 514,826 draws that came before.

**Instruments added, all cheap and kept:** `MGS_SAVE_BEST` (fullest frame of a
run), `MGS_SAVE_FROM=efb|copy|<addr>` (which buffer to dump), lit-pixel counts
beside written-pixel counts, TEV stage histogram, and the wanted/failed split
on texture binds. Between them they turned "the screen is black" into "one
element renders, the other 99.8% of geometry is shaded black", which is a
different problem.

---

**F130 — the renderer is not the problem, and F129's conclusion was wrong.**
F129 said the gap was "the combiner's untextured path", because 514,048
triangles are drawn untextured and come out black. Sampling the combiner's
actual configuration per draw refutes it. There are exactly two:

| TEV_COLOR_ENV | a | b | c | d | count | should give |
|---|---|---|---|---|---|---|
| `0x08FACF` | ZERO | **RASC** | **ONE** | ZERO | 348,160 | the vertex colour |
| `0x08FFFF` | ZERO | ZERO | ZERO | ZERO | 165,888 | **black, deliberately** |

and the vertex colour is `0xFFFFFFFF` on all 514,048 of them.

The combiner computes `d + lerp(a, b, c)`. Worked by hand against the actual
implementation: config one gives **255** per channel — white — and config two
gives **0**. Both are exactly what the game asked for. The 348,160 white ones
are small (about 3.4 pixels each, which is the 1,167,715 lit pixels), and the
165,888 black ones are large (about 66 pixels each), which is why the screen
reads as black with a logo on it.

**A mostly-black screen with a logo banner is what a boot screen looks like.**
The renderer is not failing to draw the game; the game has not got as far as
drawing anything else.

**Two wrong calls on the way there, both from the same bad habit.** Reading
`color_input` through a filtered `sed` range twice, and twice concluding a
selector table was incomplete — first that RASC was missing, then that the
texture selectors were. Both times the filter had truncated the function and
both times the table was complete. **A range-filtered read of source is not a
read of source**, and "I found the bug" after one is worth nothing until the
whole function is on screen.

**Where the boot actually stops.** 81.6% of samples are in three addresses in
the engine overlay — `0x7F0F9400` (54.5%), `0x7F0F9268` (13.6%),
`0x7F0F9424` (13.5%) — all inside one function at REL `.text 0xF0F2C`
(runtime `0x7F0F9018`, size 0x4A4). It is a counting/bucketing pass followed
by nested loops: real computation, not a wait. The running thread is priority
14 and its chain out is

```
0x8004A46C (main.dol)  ->  REL 0x130E68  ->  0x130C48
  ->  0xF0CEC  ->  0xEF6C0  ->  0xF14E0  ->  the loop at 0xF0F2C
```

so it is reached from the engine's per-frame task dispatch. No arrivals were
recorded at the function's entry across 40,000,000 steps, which — with the
known caveat that pc hooks miss calls inside one dispatch — points at
**entered once and never returned**.

That is the third wall, and it is an engine loop whose bound is presumably
computed from something our runtime is getting wrong. It is a different kind
of problem from the two interrupt faults and wants a different approach:
find what feeds the loop's bound, not what feeds the interrupt.

---

**F131 — the loop at REL 0xF0F2C is healthy, and F130's reading of it was
wrong.** F130 called it "entered once and never returned", on two pieces of
evidence: 81.6% of samples inside it, and zero arrivals recorded at its entry.
Both were consistent with a hang and neither established one.

Reading the registers at the inner loop settles it:

```
[loop] at REL 0xF1314:  r19=0x00000000 (index)  r21=0x00000004 (stride)
                        r30=0x00000040 (bound)  r25=0x816F2830 (base)
[loop] r24=0 r26=2 r11=6 r12=0
```

Stride 4, bound 64, starting at 0 — **sixteen iterations and out.** The
`slw`-yields-zero theory was worth testing (a zero stride there really would
never end, and DolRecomp's `slw`/`srw` do mask to 6 bits and return zero above
31, exactly like the hardware) but it is not what is happening.

So the function is short, correct, and called an enormous number of times. The
zero arrivals at its entry were the **known pc-hook limitation** — hooks only
see pc at dispatch boundaries and miss calls made inside one chunk. That
caveat is recorded in this file and I used the silence as evidence anyway.

**What this leaves, and it is a sharper question than before.** The engine is
executing real per-frame work, continuously, in a hot bucket-sort kernel — and
producing no new output: 200,000,000 steps give the same 177,806 GX commands
and same 223 frame completions as 40,000,000. So the engine is **repeating
work without advancing state**. The livelock is at the level of what the task
dispatch is being asked to do, not inside any one routine.

Next: compare engine state between two stopping points — the task table, the
frame ring indices, the heap free lists — rather than profiling again. A
profile says where the time goes; it cannot distinguish progress from
repetition, and this whole thread has been that mistake in three different
costumes.

**Method note, since this is the third in one session.** F125 logged two
instrument errors, F130 logged two truncated-source reads, and this is a third
class: **treating the absence of a signal from an instrument with a documented
blind spot as evidence.** All three share a shape — a conclusion drawn from
something the measurement could not have shown. The fix is not more care; it
is asking "what would this look like if I were wrong" before writing the
finding down.

---

**F132 — the boot's wall is DECOMPRESSION, and the engine embeds zlib.** The
routine holding 81.6% of the boot's samples is **zlib's `huft_build`**,
called from **`inflate_trees_dynamic`**. Proved two independent ways:

- **Its own error messages.** `inflate_trees_dynamic` stores five string
  constants into `z->msg`, and all five are in this binary verbatim and in
  zlib's own order: *"oversubscribed literal/length tree"*, *"incomplete
  literal/length tree"*, *"oversubscribed distance tree"*, *"incomplete
  distance tree"*, *"empty distance tree with lengths"*.
- **Its structure.** A 16-entry bit-length histogram (zlib's `BMAX` is 15),
  the literal/length count `0x101` = 257, the table-fill stride
  `1 << (k - w)`, and returns of -3, -4, -5 — `Z_DATA_ERROR`, `Z_MEM_ERROR`,
  `Z_BUF_ERROR`.

Named `huft_build` and `inflate_trees_dynamic`, origin `message`. The call
chain is now known end to end:

```
0x8004A46C (main.dol, task dispatch)
  -> fn_1_130DB8 -> fn_1_130AB8        (Konami's wrappers)
  -> fn_1_F0968  = zlib inflate
  -> fn_1_EEC44  = zlib inflate_blocks
  -> inflate_trees_dynamic -> huft_build
```

**It is stuck, and that is now beyond argument.** `MGS_STEPS=800000000` —
twenty times the original budget — produces **byte-identical** output to
40,000,000: same 177,806 GX commands, same 223 frame completions, same 64
disc reads, same pc. Engine state is frozen too: heap free lists identical to
the byte (10,716,480 free, same list pointer, same 7 blocks) and the task
table unchanged.

**And inflate is not failing.** Scanning guest RAM for pointers to those five
strings finds all five strings present and **zero words pointing at any of
them**, so `z->msg` was never set. The engine is decompressing *successfully*,
for ever, without finishing.

**What that leaves.** No new disc reads are issued across 760,000,000 extra
steps, so the likeliest shape is inflate wanting more input that never
arrives. `Z_BUF_ERROR` sets no message, so the scan above cannot exclude it —
that is a real limit of this evidence and not a gap to paper over.

**The pc hook failed again, and this time it was not believed.** A hook on
`huft_build`'s return site never fired once, exactly as F131 describes: the
call returns inside one dispatch chunk. The question was answered from guest
memory instead, which cannot miss. That is the F131 lesson actually applied
rather than merely written down.

---

**F133 — inflate is spinning, and the proof is that its output buffer never
changes.** F132 left the question "why does the decompression never finish".
Three measurements answer it, and two of them corrected earlier guesses.

**The z_stream, recovered without a working hook.** A hook on inflate's entry
never fires (F131's blind spot), so the stream was reached by walking the
stack from the one hook that does: `huft_build`'s sp -> back chain ->
`inflate_trees_dynamic`'s frame, whose `stmw r24, 0x4a0(r1)` puts the saved
r31 at `+0x4BC`, and r31 is the `z_stream` because that is where it stores
`z->msg`. zlib's layout confirms it — `msg` at `+0x18`, exactly.

```
z=0x81701998  in:  next 0x816D3E8F  avail 59633  total 136975
              out: next 0x80973A2F  avail 3303281 total 267695   msg 0
```

**It is not starved.** 59,633 bytes of input are available and 3.3 MB of
output space. The "needs more input that never arrives" guess in F132 is
wrong.

**And it is not the table-space failure either.** `huft_build` returns
`Z_MEM_ERROR` when `*hn + z > MANY`, and at REL 0xF1208 that constant is
`0x5A0` = 1440 = zlib's `MANY` exactly (a third independent confirmation of
the identification). That return sets **no message**, which is the silent case
the F132 string scan could not see — so it was worth checking, and it is not
happening: every sample reads `*hn=512  z=512  sum=1024  <= 1440  ok`.

**What settles it: hash guest memory per megabyte at two step budgets.**
`total_in` and `total_out` only move when inflate exits, so frozen counters
are also what "inside one long call" looks like — they cannot distinguish
stuck from busy. Memory can:

| megabyte | what lives there | changed between 40M and 120M steps? |
|---|---|---|
| 01-02 | OS and engine globals, threads, timers | **yes** — scheduler churn |
| **09** | **inflate's output buffer** (`next_out` 0x80973A2F) | **NO** |
| **22** | **the Huffman table** (`0x816F2830`) | **NO** |
| 23 | the `z_stream` and zlib's internal state | yes |

**Zero decompressed bytes in 80,000,000 extra steps**, and a byte-identical
Huffman table rebuilt into the same address for ever. Only zlib's own stream
state moves. That is a true infinite loop inside `inflate_blocks`, not a slow
decode and not a starved one.

**Next:** narrow megabyte 23 to the words that change, by hashing it per 4 KB
at two budgets and then per word within the page that differs. That names the
state field that is ping-ponging, which is the bug.

**Method note.** Three guesses died here — starved input, table overflow, and
"maybe it is just slow" — each killed by a measurement rather than by
argument, and each was plausible enough to have been written up as the answer.
The per-megabyte hash is the one that actually decided it, and it is worth
keeping as a standard probe: *what changed in memory* is a question almost
nothing else in this runtime can answer.

---

**F134 — THE BOOT'S WALL WAS THE HOST CLOBBERING CTR. One register, and it
cost 13x the graphics.**

Chasing F133's infinite loop into `huft_build` produced the answer in three
steps, each one narrowing:

1. **Which loop.** `*hn` only grows within one `inflate_trees_dynamic` call,
   so a decrease marks a new call. After the first 200,000 iterations it never
   decreased again: **one `huft_build` call, never returning.**
2. **Which variable.** Its loop variables read `k=7 g=14 h=0 w=0 l=9` — all
   in range — except `a`, the count of codes of length k, which must be small
   and non-negative. It read **-48,492 and falling by exactly 50,000 per
   sample interval.** That loop is `while (a--)`, compiled to a **`bdnz`
   counted loop**.
3. **Why.** CTR and r29 must fall in lockstep. They had not:

```
[ctr] CTR=2130691640 (0x7EFFC638)  r29=-48492  DESYNCHRONISED
```

`0x7EFFC638` is not a count. It is **an address in the overlay's window**.
CTR holds indirect-branch targets as well as loop counts, so something had
done `mtctr <function pointer>` and left it there — giving the loop 2.13
billion iterations to run instead of seven.

**The culprit is ours.** `mgs_module_call_guest` — how the host runs a guest
callback — saved and restored `gpr[32]`, `pc` and `lr`, **and nothing else**.
Not CTR, not CR, not XER, not the floating-point file. It is used by the disc
pump for read callbacks, and a callback that makes one indirect call leaves
CTR holding a function pointer.

**This is not an ABI call.** The host enters guest code at an arbitrary
instruction boundary in whatever the guest was doing, so it is an
asynchronous interruption and everything must come back unchanged. Treating
it as a call — where CTR is volatile and the caller does not care — is the
mistake, and it is an easy one because the code *looks* like a call.

Sixty-four disc callbacks in a boot, and one of them landed inside zlib's
`while (a--)`.

**The fix** saves bytes 0..663 of `CPUState` (gpr, fpr, ps1, pc, lr, ctr, cr,
xer, fpscr) and the graphics quantisation registers, and restores them.
`msr` is deliberately left out: a callback may legitimately change interrupt
state.

**Measured, same 40,000,000 steps:**

| | before | after |
|---|---|---|
| GX commands | 177,806 | **2,294,248** |
| primitives | 8,422 | **63,094** |
| triangles | 514,828 | **3,862,060** |
| EFB copies | 474 | **3,740** |
| frame completions | 223 | **1,857** |
| **disc reads** | **64** | **271** |
| stopped at | zlib's huft_build | `0x800461B4`, in GX |

**Disc reads 64 to 271 is the one that matters**: the game is streaming data
again, which is what inflate was waiting to finish so it could ask for more.
Three runs byte-identical, so determinism survives.

**An anomaly, recorded rather than explained.** The raster counters are
*byte-identical* across the change — `12,156,928 pixels, 1,167,715 lit` both
before and after, with 7.5x the triangles — and reproducibly so across three
runs. 12,156,928 is exactly 53.0 screens of 512x448 in both. That is not
noise and it is not understood; something is capping or short-circuiting the
pixel path. It is the next thing to look at, and guessing at it here would
repeat this session's most expensive habit.

**Desyncs rose 77 to 6,317** — the rate as well as the count (0.043% to
0.28%). Twenty times the command traffic is reaching a parser that has not
seen most of it before.

---

**F135 — the identical raster counters explained, and it is not a fault.**
F134 left an anomaly: after a change that multiplied triangles by 7.5, the
raster counters were byte-identical — `12,156,928 pixels, 1,167,715 lit`
before and after — and so was the best frame, at 22,034 lit pixels. Three
measurements resolve it.

**The pixels are being covered.** Splitting coverage from rejection:

```
coverage: 238,227,136 pixels inside a triangle,
          226,070,208 rejected by the depth test (94.9%)
```

So the geometry reaches the screen; the depth test discards nearly all of it.
`pixels` counts survivors, which is why it looked frozen.

**Why the depth test rejects so much.** The depth buffer is only reset on a
copy that asks for a clear, and the game stops asking:

| copy command | count | clears? |
|---|---|---|
| `0x010063` (render-to-texture, 64x64) | 1,883 | no |
| `0x004403` (to XFB, 512x448) | 1,801 | no |
| `0x004C03` (to XFB, 512x448) | **56** | **yes** |

All 56 clearing copies fall in the **first 59 copies of the run**. After that
there are 3,681 more and not one clears, so the buffer holds one early
frame's depths for ever.

**But that is not what is hiding a picture.** Forcing every pixel through the
depth test (`MGS_NO_DEPTH`) takes lit pixels from 1,167,715 to **12,239,203**
— ten times as many — and leaves the best frame at **exactly 22,034 lit
pixels, unchanged**. The extra light is spread thin: 12,239,203 across 3,740
copies is about 3,272 lit pixels per frame, against the logo frame's 22,034.

**So the later frames are genuinely sparse, and the renderer is not at
fault.** The boot draws ~1,800 near-empty frames and 1,883 small
render-to-texture passes while streaming from disc. That is what a **loading
screen** looks like, and the fullest frame in the run stays the logo because
nothing fuller has been drawn yet.

**What this retires:** "the pixel path is capped" (F134's open question) was
the wrong shape. Nothing caps it. The counters matched because the only
frames with substantial content are the early ones, and those are identical
in both runs by construction — the fix changed what happens *after* them.

**What it leaves:** whether the load ever completes. That is a budget
question, not a rendering one, and it is the next thing to measure.

---

**F136 — the boot now reaches a live idle loop, and it is polling for a
memory card.** After F134 the boot no longer stops. Comparing 40,000,000 with
200,000,000 steps:

| | 40M | 200M |
|---|---|---|
| EFB copies | 3,740 | **28,311** |
| GX commands | 2,294,248 | **21,299,798** |
| triangles | 3,862,060 | **29,060,646** |
| **disc reads** | **271** | **271** |
| best frame | 22,034 lit | 22,034 lit |

**Frames scale with budget** — the engine is alive and rendering continuously,
not wedged. But **disc reads stop at 271**: the load finished. And the best
frame never improves.

**What it is doing instead.** The two hottest MMIO reads in the run are

```
0xCC006800  2,653,706 reads
0xCC006814  2,653,696 reads
```

which are the **EXI channel 0 and channel 1 status registers** — the memory
card bus (EXI base is 0xCC006800, channel stride 0x14). The game is polling
both card slots 2.65 million times.

That fits everything else: it has finished loading, it renders a near-empty
screen every frame, and it asks about memory cards for ever. Twin Snakes
shows a "checking memory card" screen at boot, and the CARD module named in
F132's stage — `CARDGetStatus`, `CARDRename`, `__CARDIsReadable` — is
exactly what would be running.

**So the next wall is EXI, and it is a different kind of wall from the last
three.** The boot is not stuck in the sense of frozen; it is in a legitimate
idle loop waiting for an answer the runtime never gives. `runtime/platform/mmio.c`
completes EXI transfers immediately by clearing the start bit, but nothing
models the **EXT bit** — whether a device is present in the slot — so the
card layer may be waiting on a probe that never resolves either way.

**Note for whoever looks:** "no card present" is a perfectly good answer and
the game must handle it, so the fix is probably not to invent a card. It is
to make the absence *answerable*.

---

**F137 — we were throwing away a display list every frame, on an assertion
the hardware does not make.** Every desync in the run was the same one:

```
[gx] desync 1: display list is not 32-byte aligned  op=0x40 ... cmds=18264
[gx] desync 2: ... cmds=20241
[gx] desync 3: ... cmds=22218
```

Dead regular, **1,977 commands apart** — once per frame. Printing the operands
instead of just the offending low bits:

```
[gx] CALL_DL addr=0x8097CAE0 size=83 (0x53)  addr%32=0 size%32=19
```

The **address is fine** — 32-byte aligned, a plausible heap pointer, and the
*same address and size every single time*. A parser that had lost the stream
would produce varying garbage. The game really is calling
`GXCallDisplayList(0x8097CAE0, 83)`.

**Our check required the SIZE to be a 32-byte multiple too.** The SDK asserts
that, which is where the rule came from — but **asserts are compiled out of a
release build**, so a game can pass any size it likes, and the hardware simply
fetches. 6,317 refusals in a 40,000,000-step boot, every one a real list
discarded.

The address check is what carries the signal and it stays, along with the
mapped-memory check and a new bound against an implausibly large size. The
size alignment requirement is gone.

| | before | after |
|---|---|---|
| **GX desyncs** | 6,317 | **0** |
| pixels written | 12,156,928 | **348,188,074** |
| lit pixels | 1,167,715 | **19,526,875** |
| textured triangles | 2,716 | **14,356** |
| textures decoded | 2 | **13** |
| **best frame** | 22,034 lit (9.6%) | **26,570 lit (11.6%)** |

**And there is finally something to look at.** The best frame has structure —
bands, a centred block, a vertical stem — in **4,258 distinct lit colours**,
against 278 for the flat banner it replaces.

**Why this hid so much.** A desync is not one lost command: the parser keeps
reading at the wrong offset until it happens to resync, so a refusal once a
frame corrupted BP state for a good part of every frame. That is why texture
enable looked absent (F129's "the game draws untextured on purpose" was
measuring a corrupted stream, and is now suspect), and why the earlier finding
that only 778 of 514,826 triangles wanted a texture should be re-measured
rather than trusted.

**Newly visible:** the texture cache now reports **2,964 refusals** out of
17,320 lookups. That is the next thing — a refusal is a texture format or
size the cache will not take, and each one is a surface drawn untextured.

---

**F138 — the boot renders two recognisable logo screens, correctly.** Looking
at the frames rather than counting them, F137's "structure in 4,258 colours"
is the **Silicon Knights logo**: the sword, the green circuit-board cube with
its texture applied, and the gold-edged lettering with the registered mark.
The frame it replaced is the **Konami logo** — flat cyan text on a dark band,
which is what 278 colours looks like.

Both are correct. Not approximately correct, not recognisable-if-you-squint:
the textured cube, the metallic gradients on the type and the thin highlight
along the sword are all there. Everything from the FIFO parse through vertex
decode, transform, viewport, scissor, depth, the texture cache, the TEV
combiner, the EFB and the copy-out is producing the picture the game intended.

**This is the first time the port has drawn something a person would
recognise**, and it is worth recording as a checkpoint separate from the
measurements that got here — a lit-pixel count cannot tell a logo from noise,
and for most of this session the counts were the only thing being read.

**Where the frames are and where they must not go.** Written to
`~/mgs-frames/`, outside the repository, deliberately. **A rendered frame is
the game's own artwork** — our code drew it, but what it depicts is Konami's
and Silicon Knights'. Project rule 8 keeps it out of the tree, and
`README.md`'s notice depends on that staying true. Do not commit frames, do
not attach them to issues, do not upload them.

**What it says about the remaining gaps.** The texture cache refusing 2,964 of
17,320 lookups is now clearly the thing between this and more of the screen:
the cube in this logo IS textured, so the path works and the refusals are
formats or sizes it will not take.

---

**F139 — the memory-card warning screen renders, with its text cut off, and
the cause is one texture.** The boot now reaches an interactive menu: the
game's *"Warning — No Memory Card in Slot B. Please insert into Slot A or
Slot B"* screen, with **Retry** and **Continue without saving**. It sits there
because that is what the game does without a card or a button press.

Every text line is truncated mid-word — "No Memory Car", "Slot B. Please i",
"into Slot A or S", "Continue with". Measured in EFB coordinates the cut is at
**x = 207-209 on 60 rows**, while "Warning" reaches 271 and the white bars
reach 442, so it is not a global clip.

**Two explanations tested and killed:**

1. **Display-list truncation.** The command processor fetches in 32-byte
   units, so running exactly `size` bytes of an 83-byte list looked like it
   would drop the tail. Rounding up to 96 took the boot from **0 desyncs to
   231** with no change to the geometry: the bytes past the game's length are
   padding, and the parser reads them as commands. Reverted. *Do not try this
   again* — it is recorded in `run_dl`'s comment.
2. **The scissor box.** `MGS_NO_SCISSOR=1` leaves the cut at **exactly
   x=207/208/209**, identical. Not clipping; those glyphs are never drawn.

**What it actually is.** Splitting the texture cache's single `refused`
counter into its five reasons (they had all shared one, which made "2,964
refused" unactionable) gives:

```
texture refusals: 0 size, 0 texels, 0 palette, 0 alloc, 2622 decode
[tex] refused DECODE: format=0x6 8x8 addr=0x835006C0     (x2622, one address)
```

**One texture, 2,622 times.** Format 0x6 is RGBA8, 8x8 — the font glyphs. The
decoder implements RGBA8 correctly (two 32-byte halves, alpha+red then
green+blue); it never gets that far, because `guest_ptr` refuses the address.

**`0x835006C0` is not in MEM1**, which ends at `0x81800000` on a 24 MB
machine. Both candidates have now been tested, and **both are wrong**:

- **Not TMEM-preloaded.** `TX_SETIMAGE1` bit 21 is the hardware's
  `image_type`: set, the texture unit uses a preloaded copy in texture memory
  and the SETIMAGE3 address is not a fetch address at all. Counted across a
  whole boot: **zero** draws have it set.
- **Not our BP decode.** Histogramming every distinct `TX_SETIMAGE3` the game
  writes gives **15 values, of which exactly one is bad**:

```
reg=0x00F316 -> 0x801E62C0     reg=0x08B815 -> 0x811702A0
reg=0x007534 -> 0x800EA680     reg=0x08B80B -> 0x81170160
reg=0x04992E -> 0x809325C0     reg=0x08E483 -> 0x811C9060
reg=0x0663E4 -> 0x80CC7C80     reg=0x08F58E -> 0x811EB1C0
reg=0x1A8036 -> 0x835006C0  <-- past MEM1      (+6 more, all valid)
```

Fourteen addresses parse correctly through the identical code path, so the
game really does write `0x1A8036`.

**So it is a GUEST-SIDE pointer that is wrong**, not a parsing fault — and the
OS reports its own arena as `0x8028e700-0x817f8ee0`, so `0x835006C0` is above
the top of the game's own heap. Something the game computed came out wrong,
which puts the cause upstream of GX entirely: an allocation, a file load, or a
pointer derived from data we supplied.

**Next:** find who computes that pointer. The font is loaded once and drawn
2,964 times, so watching that value — or the allocation that should have
produced it — beats any more GX instrumentation.


**Why this is the whole visible gap.** The logo's cube IS textured, so the
path works; 14,356 triangles sample successfully. It is this one font texture
that fails, and the text it draws is most of what a menu screen is.

---

**F140 — an instrument that never armed, caught before it was believed.** The
texture-address histogram first reported "**0 distinct**" — which would mean
the game never writes `TX_SETIMAGE3` at all, a startling result that would
have sent the search somewhere useless.

It was wrong. The line setting the trace flag was inserted by matching
`e = getenv("MGS_TRACE_GXDESYNC")`, and the real source reads
`const char* e = getenv(...)`. The edit silently did not apply, the flag
stayed zero, and the histogram counted nothing. Armed properly, the same run
reports 15 addresses.

This is the fourth instrument failure this session — F125 logged two, F131 a
third — and **the first caught before a conclusion was drawn from it**, by
checking that the flag was actually set rather than trusting the output. The
check cost one command.

**The rule, stated plainly:** an instrument reporting zero is reporting either
"the thing did not happen" or "I did not run". Those are indistinguishable in
the output and have to be distinguished in the source, every time, before the
number is used for anything.

---

**F141 — the font's texture object found, and its address is not a pointer at
all.** Searching guest memory for the bad value found nothing twice, and both
failures were informative rather than dead ends:

- searching MEM1 for `0x835006C0`: **0 occurrences** — the full pointer is
  never stored, because `GXInitTexObj` keeps the address already shifted;
- searching both windows for `0x1A8036`: **0 occurrences** — because it is
  stored PACKED with its BP register byte.

Matching on the low 24 bits finds it, once:

```
overlay 0x7F50068C = 0x941A8036   prev 0x88601C07   next 0x00000000
```

That is a `GXTexObj`, unambiguously. `0x94` is `TX_SETIMAGE3` for map 0 with
base `0x1A8036`; the word before it is `TX_SETIMAGE0` = `0x88601C07`, which
decodes as width `(0x1C07 & 0x3FF)+1 = 8`, height `8`, format
`(0x601C07 >> 20) & 0xF = 6` — **RGBA8 8x8, the font**.

**It lives in the overlay's `.bss`** (`0x7F499B7C + 0x700F8`), so it is a
static global in the engine rather than a heap allocation — initialised once,
used 2,964 times.

**And the address is almost certainly not a pointer.** `0x1A8036 << 5` is
`0x35006C0` = **55.6 MB**, beyond any address a GameCube has: past MEM1's 24
MB, past ARAM's 16 MB, past the game's own arena top of `0x817f8ee0`. A
corrupted pointer would be arbitrary; this has the shape of an **unrelocated
offset** — into an archive or a file — that was never turned into a pointer.

That fits the rest of the picture: disc reads stop at 271 and never resume, so
a texture load that should have happened has not. The bug is an asset that was
never fetched, not a renderer that cannot draw it.

**Next:** find what writes `0x7F50068C`. It is a static address in the
overlay, so a watch on that word names the function that fills the tex obj in
one run — and whether it is a load path that failed or a relocation step that
was skipped.

---

**F142 — the engine's own static data is not at a GameCube address, and the
SDK's pointer arithmetic breaks on it.** Tracing `GXInitTexObj`'s arguments at
the call site gives the whole chain in one line:

```
[texobj] GXInitTexObj(obj=0x7F500680, image=0x7F5006C0, 8x8, fmt=0x6)
         called from REL 0x7F3FB6D8
```

The image pointer is `0x7F5006C0` — **in the second address window at
0x7E000000**, where the engine overlay lives. Nothing is corrupt. The SDK
converts a pointer to a physical address the way the hardware does, by masking
to 26 bits, and stores it shifted down by five:

```
0x7F5006C0 & 0x03FFFFFF = 0x035006C0,  >> 5 = 0x1A8036
```

which is exactly the value in the texture object. Rebuilt as MEM1 that is
`0x835006C0` — 55.6 MB into a 24 MB machine, and refused 2,964 times.

**So the fault is where we put the overlay, not anything the game did.** Any
pointer into the second window that passes through SDK address arithmetic
comes out meaningless, because `0x7E000000` is not an address a GameCube has.
This is a general hazard of the second-window design, not a texture-specific
one, and it is worth looking for elsewhere.

**The information is ambiguous, not lost.** A masked-to-26-bits address that
cannot be in MEM1 must have come from the window above it, and
`0x7C000000 | phys` inverts the mask exactly across
`[0x7E000000, 0x80000000)`. MEM1 is tried first, so ordinary textures are
untouched.

| | before | after |
|---|---|---|
| texture refusals | 2,964 | **0** |
| textured triangles | 14,356 | **17,320** |
| texture cache misses | 2,977 | **14** |
| textures decoded | 13 | 14 |

**AND IT DID NOT FIX THE TRUNCATED TEXT.** The memory-card screen still cuts
every line at x=207-209. I expected the font texture to be the cause and it is
not — the fix is real and worth keeping, but the truncation is a separate bug
that this leaves untouched.

**What is now ruled out for the truncation**, each by measurement:

- the scissor box (`MGS_NO_SCISSOR` leaves the cut identical);
- display-list truncation (rounding the size up costs 231 desyncs and changes
  no geometry);
- the texture path (0 refusals now, cut unchanged);
- the depth buffer (forcing every pixel through gives 10x the lit pixels and
  the same best frame).

The glyphs past x=208 are not drawn at all — no geometry reaches the
rasteriser for them. Since the lines have different character counts but the
same pixel cut, it is spatial rather than a count limit. **Next: trace the
viewport and projection the 2D pass sets**, since the white bars at x=442
plainly use different transform state and are unaffected.

---

**F143 — the viewports are fine, and the EFB is wider than the picture.**
Next suspect for the truncated text was the viewport, since it is the
remaining thing that maps clip space to pixels differently per pass. A
histogram of distinct viewports across a boot:

| half-width | x-origin | x range | triangles |
|---|---|---|---|
| 0.00 | 854 | 512 .. 512 | 2 |
| 256.00 | 598 | **0 .. 512** | 13,554 |
| 32.00 | 886 | **512 .. 576** | **3,860,150** |

The third looked alarming — 3.86 million triangles, almost everything, aimed
at a 64-pixel strip past the right edge of a 512-wide screen — and it is not
a fault. **The EFB is 640x528; 512x448 is only the COPY size.** That strip is
the render-to-texture scratch area, which is exactly what the 1,883 64x64 RTT
copies are reading back. The apparent contradiction (only 2 triangles clipped)
was the thing that gave it away: if they were really off-screen, the scissor
would have rejected all of them.

So the viewport is ruled out too, and a useful fact falls out: **most of this
game's drawing is render-to-texture**, 3.86M triangles against 13,554 for the
main scene.

**And the truncation is now measured to be independent of the texture fix.**
The frame before and after F142 is byte-identical — 10,393 lit pixels, same
rightmost-x histogram (`207: 37 rows, 271: 16, 208: 16, 209: 7`). F142 raised
textured triangles across the run from 14,356 to 17,320, but changed nothing
about this screen.

**Ruled out for the truncation, all by measurement:** the scissor box, the
depth buffer, display-list size, the texture path, and now the viewport.

**What is left.** The text lines start at x≈88 and stop at x≈208, so a run of
about 120 pixels, and they hold different character counts (13, 16, 16, 13) —
so it is a width limit, not a glyph-count limit. Since every transform-stage
explanation is now excluded, the next place to look is **whether the geometry
is emitted at all**: count textured triangles per frame against glyphs
expected, and dump the x-extent of every textured triangle in the 2D viewport
(half-width 256). If none exists past x=208, the engine is not emitting them
and the cause is upstream of GX entirely — as it was for the font pointer.

---

**F144 — the text quads are full width; it is the TEXTURE that stops.** The
question left by F143 was whether the glyphs past x=208 are emitted at all.
They are — and the answer reframes the bug for the third time.

Bucketing every 2D-viewport triangle by its right edge across a boot:

```
96-127:988   288-319:988   384-415:988   416-447:1976
512-543:3674 544-575:988   576-607:2964  608-639:988
```

Two things fall straight out:

1. **There is no bucket at 192-223**, where the text visibly ends. Nothing
   stops there.
2. The totals are all multiples of **988**, and sum to 13,554 — which is the
   whole 2D pass. That is about **13.7 triangles per frame**.

Thirteen triangles cannot be fifty-eight characters of text. **Each line is a
single textured quad**, drawn with a texture that already contains the
rendered string — and those quads reach x=288-447, well past the cut.

**So the geometry is full width and the texture content is not.** The string
texture is only partly filled, which is why every line stops at the same pixel
regardless of how many characters it holds.

**And that points at render-to-texture**, which is where nearly all of this
game's drawing goes: 3,860,150 triangles into the 64x64 strip (F143) against
13,554 for the screen. The engine draws its glyphs into a small EFB region,
copies it out as a texture, and blits the result. If that copy captures only
part of what was drawn, the text is truncated exactly like this — the same
cut for every line, unrelated to character count.

**Next:** dump what the RTT copies actually contain. `MGS_SAVE_FROM=<addr>`
already reads an arbitrary address, and the three RTT destinations are known
(`0x81781BE0`, `0x81785C00`, `0x8178DC40` from F127's copy trace). A 64x64
texture that is blank past a certain column is the whole answer.

**An instrument error worth noting**, though it cost nothing this time: the
first version of this histogram was placed INSIDE the `if (tex_enabled)`
branch, so "all triangles" and "textured triangles" were the same counter
measuring the same thing. The two agreeing looked like a result and meant
nothing. Moving it above the branch is what made the comparison real.

---

**F145 — EFB-to-texture copies are not implemented, which is why the text
texture is blank.** Four findings chased the truncated text through the
scissor, the depth buffer, display lists, textures, the viewport and the
geometry. The answer is in `mgs_efb_copy`, in one condition:

```c
/* A copy to a texture stays on the graphics side: it never touches the
 * external framebuffer... */
if (to_xfb && efb->copy_dest && mem) {
```

**A copy that is not to the external framebuffer does nothing at all.** Every
render-to-texture copy in the run — **1,883 of them** — writes no data. The
engine renders its glyphs into the EFB, the copy that should capture them is
a no-op, and the quad then samples a texture that was never written.

That is the whole chain, and it explains every symptom that made no sense
under a clipping theory: the cut is identical on every line because the
*source texture* is identical, and unrelated to character count because the
geometry was never the limit.

**The copies, measured:**

| command | size | to XFB? | format | count |
|---|---|---|---|---|
| `0x010063` | 64x64 | **no** | **0xC** | **1,883** |
| `0x004403` | 512x448 | yes | 0x0 | 1,801 |
| `0x004C03` | 512x448 | yes | 0x0 | 56 |

Format `0xC` is **GB8** — green and blue stored as an 8-bit pair, two bytes
per texel, tiled like IA8. That is what has to be produced.

**This is a known hole, not a regression.** The design document lists "EFB
copy emulation" under GX and rates that library "Very high" difficulty. The
comment in the code is also wrong in its reasoning and should go with the
fix: an EFB-to-texture copy is *supposed* to write guest memory at the copy
destination — that is the entire point of it. What would corrupt the game's
memory is writing the *wrong* thing, not writing at all.

**Shape of the work:** read the EFB source rectangle, convert each pixel to
the target copy format, and write it tiled at `copy_dest` with `copy_stride`.
GB8 first, since it is what this game uses for its text, with the other
formats following. The texture cache already decodes the formats, so the
encoder is the missing half.

**Scale to expect:** 3,860,150 of the run's 3,873,706 triangles are drawn into
the render-to-texture strip (F143). Almost everything this game draws goes
through the path that currently discards its output.

---

**F146 — F145's CAUSAL CLAIM IS WRONG. The render-to-texture gap is real; it
is not what truncates the text.** F145 concluded that unimplemented
EFB-to-texture copies leave the text's texture blank. Two measurements refute
it.

**The textures the game actually samples**, all 14 of them:

```
0x800EA680  fmt=0xE  512x448      0x811C9060  fmt=0x0  512x512
0x809325C0  fmt=0xE  512x448      0x811EB1C0  fmt=0x0  512x32
0x80CC7C80  fmt=0xE  512x448      0x811EF320  fmt=0x0  512x32
0x80CFFC80  fmt=0xE  512x448      0x811F3480  fmt=0x0  576x32
0x80CABC80  fmt=0xE  512x448      0x811F79E0  fmt=0x0  512x512
0x811702A0  fmt=0x9  160x14   <-- a text strip
0x8116FCE0  fmt=0x9   72x14   <-- a text strip
0x81170160  fmt=0xE   38x14   <-- a text strip
```

**None is at a render-to-texture destination.** Those copies go to
`0x8178DC40` and `0x81785C00`, and nothing samples either. So the RTT output
is never read, and whether it is written cannot be what truncates the text.

**And the text texture is complete.** Column occupancy of the 160x14 C8 strip
at `0x811702A0`, a `#` where any texel in that column is non-zero:

```
.#############.##############........#######.........###############...#########
######...###############...###############....###############...###############.
```

Content runs the full 160 columns, with the gaps between words visible. The
string is all there.

**So the texture is complete and the geometry is full width, and the picture
still stops at x=208.** That leaves the mapping between them — texture
coordinates, or the width used to normalise them — and nothing else. The
visible run is 88..208, which is **120 pixels of a 160-pixel texture: exactly
three quarters.** That ratio is the next thing to explain.

**What is still true from F145:** EFB-to-texture copies really are
unimplemented, 1,883 of them per boot really do discard their output, and
3,860,150 of 3,873,706 triangles really are drawn into that path. It remains
worth implementing. It is simply not this bug, and I should not have asserted
a cause without checking that anything sampled what those copies write.

**The habit this breaks, stated plainly:** "X is broken and Y is broken,
therefore X causes Y" is not an argument. The check that costs one command —
does anything actually read what X writes? — is the one that decides it.

---

**F147 — texture coordinates are correct too, and the truncation is still
unexplained.** Following F146's 3/4 ratio, the obvious suspect was coordinate
normalisation: `mgs_tex_sample` divides by the texture width, while GX
normalises by `SU_SSIZE+1` from the setup-unit registers at BP 0x30+, which
**this renderer references nowhere**. If those ever disagreed, the mapping
would be wrong by exactly their ratio.

They do not disagree. Measured per draw:

```
u 0.000 .. 1.000   SU_SSIZE=159 (scale 160)     <- the 160x14 text strip
u 0.000 .. 1.000   SU_SSIZE= 71 (scale  72)
u 0.000 .. 1.000   SU_SSIZE= 37 (scale  38)
u 0.000 .. 1.000   SU_SSIZE=511 (scale 512)
u 0.000 .. 1.406   SU_SSIZE=511 (scale 512)     <- wraps, legitimately
```

`u` runs exactly 0..1 across each text strip and the scale matches the texture
size exactly, so normalising by the texture width gives the same answer. **The
theory is wrong.** (`SU_SSIZE` should still be honoured rather than assumed
equal — a game that sets them apart would break — but it is not this bug.)

**And the texture holds its full content.** Rendered as ASCII, the 160x14
strip is a warning icon followed by block glyphs running to column 159:

```
......+++......++++++++++++++........................+++++++++++++++...+++++++++++++++...
....+++++++.....++++++++++++.........+++++++.........++................++................
.+++++++++++++......++++.............................+++++++++++++++...+++++++++++++++...
```

**So: complete texture, correct coordinates, full-width geometry, and the
screen still stops at x=208.** Every stage of the pipeline has now been
measured and each one is behaving.

**Excluded by measurement, cumulatively:** scissor box, depth buffer,
display-list size, texture refusals, viewport, geometry extent,
render-to-texture output, texture content, texture coordinates, SU size
registers.

**One observation worth following, not yet a theory.** The 2D pass draws about
**13.7 triangles per frame** — roughly 7 quads — while the screen shows a
warning banner, three body lines, two options and two bars: about 8. But only
**three** text-strip textures are ever sampled successfully (160x14, 72x14,
38x14). If some text quads bind no texture at all they would draw untextured
and vanish, which would look exactly like a truncated line. **Count the
draws that reach `bind_texture` and return NULL**, per frame, and compare with
the quads that are visible.

I have not found this one, and I am recording that plainly rather than
offering a fifth theory. What the session has produced instead is a pipeline
where every stage is now individually verified.

---

**F148 — the opening video is never started, which is one step earlier than
"not implemented".** The boot goes Konami logo, Silicon Knights logo, memory
card warning. A video is meant to play before that screen and does not.

The game **does** open the file:

```
[dvd] DVDOpen("./shared/movie.dat", 0x7F4A4ED0) -> entry 1651
```

alongside `vox.dat` and `demo.dat`, at init — it opens all the big archives up
front. But every one of the boot's **271 disc reads** resolves elsewhere:

| file | reads |
|---|---|
| `stage.dat` | 168 |
| `shared/audio/banks/bank048.spd` | 50 |
| `shared/audio/banks/bank001.spd` | 49 |
| `mgso_pal.rel`, `dummy.tpl`, two `.spt` headers | 1 each |

**Not one byte of `movie.dat` is read.** Playback never begins, so the absent
MPEG support is not yet the blocker — something upstream declines to start it.

**And the decoder is not ours to write.** `mpegGCN.c` is in the REL (stage
5e-DOL), so it is the game's own code, already recompiled to native and ready
to run. What the runtime lacks is the **presentation path** — getting decoded
frames to the screen — not a decoder. That is the same shape as Vorbis, where
the game decodes in software and the translated code simply runs, and it makes
this much cheaper than the design document's "the runtime needs an MPEG
decoder" implies.

**So the next step is not to build anything.** Find why playback is never
requested: whether the sequence that would call it is reached at all, or
whether it is gated on something the runtime reports wrongly — a capability
check, a region or hardware test, or a state the memory-card path sets. The
boot reaching the card screen at all suggests the video was skipped rather
than attempted and failed.

---

**F149 — the boot is not stuck, it is waiting for hardware we do not
provide.** F148 asked why the opening video is never requested. The answer is
that the boot never gets past the screen before it, and it cannot, because
**nothing is listening for a button**:

```
serial interface (controller) reads: 3
```

**Three, in a whole boot.** SI is the controller bus; a screen offering
"Retry" and "Continue without saving" polls it every frame. This one checked
once at init and stopped — the `PAD` library initialised (its banner prints),
found no controller, and gave up.

So the chain is:

1. EXI reports **no memory card** (EXT clear, F136) — correct, we have none;
2. the game shows its "No Memory Card" warning, which is **right behaviour**;
3. SI reports **no controller**, so the game stops polling for input;
4. the warning can never be dismissed;
5. everything downstream — including the video — is never reached.

**The port is not stuck. It is doing exactly what a console with no card and
no controller would do.** That reframes the last several findings: the boot
"idling" at a memory-card probe (F136) is not a wall, it is a prompt nobody
can answer.

**And it means the video is probably fine.** `mpegGCN.c` is the game's own
code, already recompiled to native (F148). It is never reached rather than
broken.

**Both gaps are planned, low-difficulty work.** The design document's SDK
table lists PAD as "SDL3 gamepad; map GC layout" at Low difficulty, CARD as
"files in a per-user save directory" at Low, and "DSP init, EXI, SI" as
"stubs that return success". Neither is a new problem; they simply have not
been done, and until one of them is, this screen is the end of the boot.

**Which to do first:** a controller is the smaller job and unblocks every
menu, not just this one. A memory card additionally makes the card check pass
outright, which is what a Dolphin run with a card does — and is how the video
was seen there.

---

**F150 — something black IS painting over the text, 9.4 million times.** The
suggestion was a black layer covering the text, with the follow-up that 3D
should be *behind* the UI anyway. Counting black pixels written over
already-lit ones:

```
black pixels drawn OVER lit ones: 9,422,255
  64-95:483k   96-127:867k  128-159:890k  160-191:806k
  192-223:951k 224-255:974k 256-287:955k  288-319:853k
  320-351:756k 352-383:817k 384-415:642k  416-447:423k
```

Roughly 2,500 per frame, spread across the whole visible width, and **peaking
at 192-287 — straddling exactly where the text stops**.

**This is the first positive evidence in the whole hunt.** Every earlier
finding excluded something; this one shows a mechanism that is actually
happening.

**And it ties to a number already measured and not joined up.** F127 found the
depth buffer is reset only on a clearing copy, and there are **56 clearing
copies in 3,740** — all in the first 59. After that, "what is in front" is
decided by depths left over from a frame long gone, so geometry that should
lose the depth test wins it. That is precisely how 3D ends up over a UI layer
that was drawn after it.

**Why the earlier depth test looked innocent.** F127 ran `MGS_NO_DEPTH`, which
forces *every* pixel through — that makes black overdraw worse, not better, so
an unchanged picture proved nothing. The test that matters is the opposite:
**clear the depth buffer on every frame's copy**, not only on copies that ask
for a clear, and see whether the text completes.

**An instrument that invalidated itself, recorded because it nearly produced a
wrong answer.** The first version of this suppressed black writes rather than
counting them, to see if the text reappeared. It changed the boot: **7,235 GX
commands instead of 2,476,033**, 55 EFB copies instead of 3,740. Altering the
EFB alters what the copies write into guest memory, and the guest reads guest
memory — so the feedback path exists and the "diagnostic" was steering the
thing it measured. A diagnostic that changes pixels is not a diagnostic. The
counter that replaced it only observes.

---

**F151 — ZMODE was defined and never read; the game's depth state was ignored
for every triangle in the game.** `BP_ZMODE` (0x40) carries the depth test
enable, the comparison function and the depth-write enable. It existed in
`bp.h` and **nothing read it**: `mgs_raster_init` set "test on, less-or-equal,
write on" once and the rasteriser used that for all 3,873,706 triangles.

Disabling the depth test is the normal way to put a layer on top, so ignoring
the register means that request never arrives. Honouring it per draw:

| | before | after |
|---|---|---|
| pixels written | 348,188,074 | **574,258,282** |
| lit pixels | 19,526,875 | **30,598,363** |
| black over lit | 9,422,255 | 25,507,953 |

The black-over-lit rise is **correct, not a regression**: draws that ask for
no depth test now pass, as the game intended. Two runs byte-identical, 13/13
tests, 0 desyncs.

**And it does not fix the truncated text.** The memory-card screen is
byte-identical after the change — 10,393 lit pixels, same rightmost-x
histogram — and the best frame is unchanged at 26,570. So the text bug is not
depth ordering either, and the suggestion that a black layer sits over the
text remains open with F150's 9.4 million black-over-lit writes unexplained by
this.

**The live thread is now the black geometry itself.** F130 found 165,888
triangles configured `a=b=c=d=ZERO` — the combiner asked for literal black —
and verified the combiner computes exactly that. On a menu screen those should
not be black. Either they are meant to sample something (and the TEV stage
that would supply it is not reaching the combiner), or their configuration is
being read from stale BP state. That is where to look next, and it is a
different question from "what is in front of what".


### F152 — blending was not implemented; it is now, and it is not the bug either

`BP_BLEND_MODE` (0x41, CMODE0) was defined in `bp.h` and read nowhere, exactly
like `BP_ZMODE` in F151. It carries two separate things, and both were being
ignored:

- **blending** — a translucent layer written opaque becomes solid paint;
- **colour update (bit 3)** — a pass that writes only depth or only alpha has
  its colour write masked off in hardware, so ignoring the bit turns an
  invisible pass into geometry that covers what is under it.

Both are implemented now, per draw, at the pixel write. The blend factor ids
are named relative to the *other* operand (2 is "the other operand's colour",
so `GX_BL_SRCCLR` and `GX_BL_DSTCLR` are the same number read from opposite
sides), so `blend_factor()` takes which side it is computing.

**It did not fix the text, and the measurements say why.** The best frame is
*byte-identical* to the pre-blend one (md5 `d964c217…`), and the run reports:

| | |
|---|---|
| pixels blended | 562,689,130 of 574,258,282 |
| writes masked off entirely | **0** |

The five CMODE0 values the whole boot uses:

| value | blend | colupd | alpupd | src | dst | draws |
|---|---|---|---|---|---|---|
| 0x0004BC | **off** | 1 | 1 | 4 | 5 | **3,856,384** |
| 0x0004A9 | on | 1 | 0 | 4 | 5 | 6,706 |
| 0x00011D | on | 1 | 1 | 1 (ONE) | 0 (ZERO) | 5,568 |
| 0x0004AD | on | 1 | 0 | 4 | 5 | 4,940 |
| 0x00040D | on | 1 | 0 | 4 | 0 | 108 |

**Three hypotheses die here, by measurement:**

1. **"The black geometry is a translucent overlay painted opaque."** Wrong.
   Blending is *off* for 99.6% of draws, the black ones among them. They would
   paint solid black on real hardware too.
2. **"They are a depth-only or alpha-only pass whose colour write we ignore."**
   Wrong. `write_masked` is 0 — colour updates are never disabled.
3. **"The alpha test would discard them and we do not implement it."** Wrong
   twice over: it *is* implemented (`mgs_tev_alpha_test`), and the game sets
   `ALPHA_COMPARE = 0x3F0000` — op0 = op1 = 7 = ALWAYS. The game deliberately
   disables the alpha test. **0 alpha-killed is correct behaviour, not a gap.**

Two corrections to the record while here:

- The 165,888 black draws are `a=b=c=d=15`, and **15 is `GX_CC_ZERO`**, so
  "a=b=c=d=ZERO" was right about the meaning. But the *other* 3,690,496
  untextured draws are `0x08FACF` = `a=ZERO b=RASC c=ONE d=ZERO`, which
  computes `RASC` — **vertex colour, and the vertex colour is 0xFFFFFFFF**.
  Most untextured draws emit white, not black. Earlier notes implying the
  untextured path emits black in general are wrong.
- F151's depth fix is correct but **inert**: this run rejects **0 pixels of
  574,258,282** on the depth test. The game runs these draws with depth off,
  which is why the frame was byte-identical. Submission order alone decides
  what is in front, so "get them behind the UI layer" cannot be answered with
  ZMODE on this screen.

**Still unexplained, and now with more excluded:** the text truncates at EFB
x=207–209. Ruled out by measurement so far — scissor, depth buffer,
display-list size, texture refusals, viewport, geometry extent, RTT output,
texture content, texture coordinates, SU size registers, depth ordering,
blending, the colour/alpha update masks, and the alpha test.

**The strongest remaining signal is texture coverage: 17,320 of 3,873,704
triangles sample a texture.** All 17,320 binds succeed. For a screen whose
content is text, that is very low, and it is the next thing to measure — not
another raster register.


### F153 — a controller exists: the serial interface was a stub that never finished a transfer

The boot stopped at "No Memory Card" with **3** serial-interface reads in it
(F149). The trace says exactly why. In a whole boot the SDK started **one**
transfer — `SICOMCSR = 0xC0010301`, channel 0, one byte out and three back,
which is `SIGetType` asking for a device id — and then never read the answer.

It never read it because clearing TSTART is not how a transfer finishes.
Hardware also sets TCINT and raises the serial interrupt, and the SDK's
completion handler is what reads the buffer. With no completion the handler
never ran, PAD concluded the port was empty, and nothing could press a button.

Implemented in three parts, because the first two alone made the boot *worse*:

| | GX commands | SI reads | interrupts re-offered |
|---|---|---|---|
| stub (baseline) | 2,476,033 | 3 | 3,775 |
| + transfers complete | 13,060 | 40,070 | 14,421 |
| + vblank polling | 13,060 | 80,024 | 14,414 |
| + **RDST cleared on read** | **2,338,178** | 84,840 | 4,482 |

1. **Transfers complete.** Decode channel and lengths from SICOMCSR, put the
   reply in the transfer buffer, set NOREP in SISR for empty ports, clear
   TSTART, raise TCINT, refresh the line level-triggered (F126). Port 1
   answers `0x09`, a standard controller; ports 2-4 answer nothing.
2. **Vblank polling.** PAD then sets `SIPOLL = 0x01280280` — the low byte
   `0x80` is EN0 — and waits for hardware to poll that port every field and
   raise RDSTINT. Driven from the frame tick, like the video interface.
3. **RDST is cleared when the input buffer is read.** This is the one that
   mattered. RDST means "data you have not taken yet", and hardware clears it
   on reading the high word. Left set, the SDK sees perpetually fresh data
   and a loop that waits for the NEXT poll never waits — an 11x loss of
   graphics and 3.2x the interrupt re-offers.

**Steps 1-2 without 3 are F128's shape again**: half a device is worse than
none. The record now has two instances, and the response both times was to
make the other half honest rather than revert.

**Wrong turns worth keeping:**

- **"The guest is waiting for a button press."** The steady-state trace is
  `SISR, SISR, INBUFH, INBUFL` repeating, which is `SIGetResponse` reading
  data successfully, so it looked like a game waiting at a prompt. It was
  not: holding Start (`MGS_PAD_BUTTONS=0x1000`) produced *byte-identical*
  counters — 13,060 commands, 80,024 reads. A successful read in a loop is
  not evidence of waiting for input; it is equally the signature of a flag
  that never clears. `MGS_PAD_BUTTONS` exists now precisely to tell those two
  apart.
- **"Fewer GX commands means less progress."** Also not safe on its own: the
  controller run emitted *more* initialisation output than the baseline (31
  OSReport lines against 19, with two heap dumps) while drawing 190x less.
  The baseline spends most of its 40M steps re-rendering one warning screen.

**The NOREP bit position is confirmed by behaviour, not assumed.** After the
fix the SDK enumerates all four ports (`0xC0010301/303/305/307`) and then
enables polling on channel 0 only. With the bit wrong it would have enabled
all four.

Input is still synthetic — the poll reply is neutral unless `MGS_PAD_BUTTONS`
holds something. Wiring SDL3 to it is the next step, and is now a small change
rather than a device model.

### F154 — the port was slow because it presented 300 times too often

The port ran "very very slow" while the processor sat near 10%. Low
utilisation with bad throughput is a waiting problem, not a compute one, and
the wait was ours.

`frame_pump()` is called from the run loop every **2000 guest instructions**.
It did a full presentation every time: XFB to RGB for 512x448, a rescale to
640x480, an SDL texture upload and `SDL_RenderPresent`. That is about
**20,000 presentations in a 40M-step boot, against some 60 frames the game
actually produces** - and with vsync each one waits for the display.

Input still has to be pumped on every tick or the window stops responding, so
the two were separated: `mgs_video_pump()` polls events and nothing else,
while the expensive path runs only when the game has produced a frame.

**Keyed on two signals, not one.** The first version keyed on the EFB copy
counter alone. That is right only while GX is what fills the external
framebuffer; anything that writes it directly and flips to it - which is how a
video decoder can present without GX copying - would have frozen the picture
outright. The key is now the copy count combined with the scan-out address.

**A measurement that was worthless, and why.** `ps -o pcpu` was read as "98.8%
CPU, so it is compute bound". That field is the **average since process
start**, not the current rate, and the live figure was about 10% - the
opposite conclusion. Watching the live figure corrected it. For a running
process the instantaneous rate is the only number that means anything.

### F155 — the video corruption is NOT a scan-out geometry problem

The movie plays correct for a few seconds and then degrades into regular
vertical striping with a magenta/green cast. The first hypothesis was that
`mgs_display_present` takes its width, height and stride from the **last EFB
copy** rather than from the video interface, so a 640-wide movie would be read
as 512 wide with a 1024-byte stride - which skews every line and would produce
exactly that striping. The stale-geometry criticism of the code is true and
still worth fixing on its own merits.

**It is not the cause.** Every write to VI's registers was traced: `VI_HSW`
(+0x48) is written **once**, as `0x2040` - width 512, stride 1024 - and never
changes across 4,195 VI register writes in a run that reaches the movie. The
game never reconfigures scan-out for the video, so the geometry cannot drift
from it.

**What the symptom does say.** A static format error - wrong component order,
wrong tiling - is wrong from the first frame. This one is *correct first and
degrades*, which points at a buffer that is not being tracked, or at decode
falling progressively behind presentation, rather than at a misread layout.

**One instrument not to trust as it stands:** the VI trace logs the raw value
of each write, and TFBL showed only `0x06` and `0x15` - clearly 16-bit writes
to the low half, since the resolved address at exit was `0x8015A880`. It
therefore CANNOT be used to argue that only two framebuffers exist. Log the
composed address before drawing that conclusion.


### F156 — the texture cache never looked at the texture

The movie froze on its first frame and any corruption in that frame stayed on
screen for good. The cause is not in the decoder, which is the game's own code
recompiled to native, and not in the arithmetic: `objdump` finds **zero** FMA
or AVX instructions in the module, so the floating point is IEEE-exact and the
`-O3` the generated code is built with (a real breach of the project's
"-O2, never -O3" rule, fixed separately) cannot be altering results.

It is the cache. `mgs_tex_get` matched on address, format, size and palette
and **never examined the bytes**, and `mgs_tex_cache_invalidate` was never
called from anywhere outside `texture.c`. That is correct only for textures
whose contents never change. A video frame is the opposite: the decoder writes
every new frame into the SAME buffer at the SAME address, so after the first
decode every lookup was a hit.

Fixed by hashing the encoded bytes (FNV-1a) into the match. Textures over
4 KB are sampled on a stride rather than read whole, because this runs per
lookup and a 512x448 RGB565 frame is 448 KB; a changed video frame differs in
far more than one sample, while a static texture costs a few hundred bytes to
confirm.

**The first version of this fix was much worse than the bug**, and the way it
failed is the part worth keeping. A changed texture got a NEW cache entry
while the old one stayed valid. With 256 slots, a video re-decoding every
frame filled the cache with stale copies of itself within seconds; after that
every allocation evicted a texture still in use, and the entire texture set
re-decoded every frame. The boot stalled on the Konami logo. **A changed
texture must take its own slot back - one entry per texture identity, never
one per version.**

| | GX commands | decoded | hits | evicted |
|---|---|---|---|---|
| address-keyed (stale video) | 2,338,178 | 14 | 16,186 | 0 |
| hash, new entry per version | stalled on the logo | - | - | thrashing |
| **hash + slot reuse** | **2,336,472** | 19 | 16,159 | **0** |

The five extra decodes are the bug being fixed: five textures whose contents
genuinely changed and which were previously shown stale. Zero evictions says
the thrash is gone. 40M steps headless in 60 seconds.

**Two hypotheses killed on the way, both cheaply:**

- **Scan-out geometry.** `mgs_display_present` really does take width, height
  and stride from the last EFB copy rather than from the video interface, and
  that is worth fixing on its own. It is not this bug: `VI_HSW` is written
  **once**, as `0x2040`, and never changes across 4,195 VI register writes in
  a run that reaches the movie.
- **A copy path unique to the video.** There is none. A run reaching the movie
  uses the same three copy commands as the boot: 21,041 render-to-texture at
  64x64 format 0xC (still discarded, still a real gap), 20,957 EFB-to-XFB at
  512x448, and 56 of those with clear. The movie reaches the screen by the
  ordinary route, which is why the fault had to be in what it samples.


### F157 — nine CARD symbols, by bracketing rather than resemblance

Stage 5 aligned our boundaries against a published symbol map. This aligns
against **source order**: the compiler emits a translation unit's functions in
the order they are written, so a run of unnamed functions between two named
ones must be that file's functions, in that order.

What makes it evidence rather than resemblance is the **count**. A name is
taken only where the gap is bracketed by two NAMED functions and the number of
unnamed functions in it equals the number the source requires exactly:

| bracket | expects | has | taken |
|---|---|---|---|
| `CARDMountAsync` .. `__CARDFormatRegionAsync` | 4 | 4 | yes |
| `__CARDAccess` .. `__CARDIsReadable` | 1 | 1 | yes |
| `CARDGetStatus` .. `CARDRenameAsync` | 2 | 2 | yes |
| `__CARDFormatRegionAsync` .. `__CARDAccess` | 4 | **2** | **no** |
| `__CARDIsReadable` .. `CreateCallbackFat` | 5 | **4** | **no** |

**The two refusals matter as much as the three successes.** Both gaps are short
by functions the linker stripped, and which one was dropped decides every name
in the gap. A method that only ever succeeds is not being checked.

Cross-checked independently by shape: the SDK's sync wrapper around an async
call compiles to 0x48 bytes here. `CARDRename`, named earlier by a different
route, is 0x48; the three wrappers named now - `CARDMount`, `CARDCreate`,
`CARDSetStatus` - are each 0x48 and each sits immediately after a much larger
`*Async`. Two independent signals agreeing.

987 -> **996** named; SDK entry points 193 -> **197** of 336.

### F158 — the FIFO parser hid its own desyncs

The movie corrupts a few seconds in. It is not the decoder, not the scan-out
geometry and not the texture format: the **command parser loses sync**, 388,247
times in a run that reaches the video, against **0** in every run that does not.
Once lost it stays lost, which is exactly "plays, then falls apart".

Finding where it first went wrong took three instruments, and the first two
pointed at the wrong thing:

- A byte ring showed `FF FF` being read as an opcode. That looked like a vertex
  sized too small, ending a run early and landing on an INDEX16 null. **It was
  not:** decoding `vcd=0x1E1D/0x0F` by hand gives PosMatIdx 1 + three TexMatIdx
  3 + Position INDEX16 2 + Normal INDEX16 2 + Tex0 2 + Tex1 2 = **12 bytes**,
  which is exactly what the code computes.
- The desync report printed `vat[op & 7]` where `op` was the garbage byte, so
  it named format 7 while the real draws were format 3. A report that derives
  its own context from the corrupt value describes the corruption, not the
  cause.

**The parser was hiding the evidence.** `command_length` returns 0 for an
opcode it does not know, and the parser only reported that when the opcode was
at or above the draw range. Every unknown byte **below 0x80 was silently
accepted as a valid zero-length command**, so after the stream diverged the
parser walked through garbage one byte at a time, reporting nothing, until it
happened to land on a byte >= 0x80 it could not size. The first desync reported
was never the first desync that happened. A command ring showed it plainly:
`... 20:4 28:4 30:4 9B:50 1E:0 1E:0 03:0` - `0x1E` and `0x03` are not GX
opcodes and were being consumed as no-ops.

Unknown opcodes are now reported wherever they appear. Still open: what
diverges the stream in the first place, with `83:42` - a draw whose 40-byte
body is not a whole number of 12-byte vertices - the current lead.


### F159 — a bracket that matched by coincidence, and was refused

The four unnamed OS functions at `0x80022C2C`, `0x80022C58`, `0x80022C94` and
`0x80022D60` sit in a gap bracketed by two named functions -
`OSGetSemaphoreCount` and `__OSSystemCallVectorStart` - and `OSMessage.c` has
**exactly four functions**. By F157's rule that is a name.

**It is not.** `OSMessage.c` is already named, at `0x80020B14`, in an entirely
different part of the binary. The count agreeing was coincidence, and taking it
would have put four confident wrong names in the map with a valid-looking
origin.

The right-hand anchor turns out to be `OSSync.c`, which holds only
`SystemCallVector` and `__OSInitSystemCall`, so the gap belongs to some
translation unit between `OSSemaphore.c` and `OSSync.c` that adjacency alone
does not identify. **The gap stays unnamed.**

**What this changes about F157's method:** a matching count is necessary, not
sufficient. The file whose functions are being claimed must also be shown not
to live somewhere else already. Checking that costs one grep and is now part of
the method.


### F160 — the video desync is inside a display list, and three theories died getting there

`dl_depth=1` on the first desync: the parser loses the stream **inside a
display list**, not in the outer FIFO. The list is `0x8107F2E0`, 43,499 bytes,
32-byte aligned, reached by a correctly framed `CALL_DL`.

**Theories killed, each by its own measurement:**

- **"The vertex is sized too small."** A truncated 83-byte list was dumped
  whole and hand-parsed. Its first command is `84 00 04` followed by four
  12-byte vertices forming a clean quad - (0,0), (0x2000,0), (0x2000,0x1800),
  (0,0x1800) with matching texcoords. **The size of 12 is exactly right.**
- **"The display-list size operand is wrong."** The raw operand bytes are
  `80 97 CA E0  00 00 00 53` - address then 83, framed correctly. The game
  really does pass 83.
- **"A size that is not a multiple of 32 proves corruption."** It does not, and
  F137 had already established this: the game passes ragged sizes routinely
  (14,717 of 18,000 in a boot) and rounding them up to the fetch unit took the
  boot from 0 desyncs to 231. **The evidence against this was already written
  in the function being read.** Re-read the comment before theorising.

**Display lists ending mid-command are NOT the bug either.** 7,164 of 18,000
lists in a *boot* end mid-command - a boot that reports **0 desyncs**. The
parser saves and restores its state around a list, so a partial command at the
end is discarded and the caller's stream is untouched, which is almost
certainly what the hardware does as well. It is pre-existing, it is not new to
the video, and it is not what corrupts the picture. Worth knowing; not worth
chasing.

**Where this leaves it.** Something inside a 43 KB display list diverges. The
instruments now exist to find it - rings of recent bytes, commands, draws and
display lists, all dumped at the desync - and the next step is to trace
commands from that list's start rather than from the point where the parser
noticed. It noticed late by construction until tonight's fix, and it may still
notice late for reasons that are not opcode-related.


### F161 — NormalIndex3, and a lesson about which desync to look at

`VAT_A` bit 31 is **NormalIndex3**: when the normal is INDEXED and the bit is
set, the vertex carries **three** indices - normal, binormal, tangent - not
one. `mgs_gx_vertex_format` read bits 0..30 and stopped, so every such vertex
was short by four bytes (2 -> 6 for INDEX16).

Caught by following one display list command by command from its first byte
(`MGS_TRACE_DLADDR`). The list diverged at its **fourth** command, 66 bytes in:

| | before | after |
|---|---|---|
| the draw at command 4 | `9B len=50` | **`9B len=66`** |
| commands 5, 6 | garbage | `9B len=66`, `9B len=66` |

`vat_a[3] = 0xD8F76607` - bit 31 set, so the game genuinely uses it.

**The fix had to be made in two places, and one alone was worse than useless.**
Fixing only `mgs_gx_vertex_size` moved the desync count by nothing: the size
said 16 while the decoder still consumed 12, and the parser's own consistency
check turned "unknown opcode" into "vertex decoded to a different size". The
decoder skips the normal with `skip_attr`, which consumes exactly one index
regardless. Both now agree. Same shape as F153's controller: half a fix reads
as no fix.

**And it is not the video bug.** Desyncs went 389,912 -> 388,030 - half a
percent. What it did remove is the entire `unknown opcode` class.

**The lesson is about method, not about GX.** Counting desyncs BY REASON should
have been the first measurement and was close to the last:

```
display list address is not 32-byte aligned   x388,030
```

**One cause, all of them.** Every hour spent on the first desync found was
spent on the rare case. A single instance of 388,030 says nothing about the
other 388,029, and "fix the first thing that fires" only works when there is
one fault.

**Where the real bug now stands.** The misalignment is itself a symptom: the
operand sizes are `0x07`, `0x1A`, `0x57`, and a 7-byte display list does not
exist, so the parser is reading data as commands. It reports at `dl_depth=0`,
in the OUTER stream, where the earlier one was `dl_depth=1` inside a list - so
there are at least two divergence sites. Every check that fires is downstream
of an earlier silent divergence, which is the third time tonight that has been
true.

Boot is unchanged: 2,338,178 commands, **0 desyncs**.


### F162 — render-to-texture, and an argument that lost to a measurement

**EFB-to-texture copies were never implemented.** `mgs_efb_copy` opened with
`if (to_xfb && ...)`, so a copy to a texture did nothing at all - **21,041 of
them in a run that reaches the movie**, every one discarded. Whatever the game
composited into a scratch target and sampled back was uninitialised memory,
which renders as noise. A 64x64 target written 21,000 times is a compositor,
and it starts running when captions appear - which is exactly when the
corruption showed up from the beginning.

Implemented with the hardware's tiling: 4x4 texels for the 16-bit formats,
8x4 for the 8-bit ones, tiles left to right then top to bottom. Writing
linearly gives a picture that is recognisably right but cut into shuffled
squares, which is a distinctive and easily mistaken kind of wrong. The source
rectangle's top-left is read as well: render-to-texture takes a small box out
of the embedded buffer, often the scratch strip right of the visible area,
not the origin.

**Confirmed on screen: the captions render correctly.**

**Two of my own changes were wrong, and both were caught by measurement.**

- **Spacing tile rows by `copy_stride`.** The stride register is shared with
  the framebuffer path and reads 1024 for these 64-wide copies - the external
  buffer's line pitch, not this texture's. A row of 4x4 tiles at 16bpp is
  512 bytes, so spacing by 1024 writes 16 KB into an 8 KB texture, over
  whatever follows. Tiles are packed.
- **Using the exact byte address for a misaligned display list.** The argument
  is good: the command processor only FETCHES in 32-byte units and executes
  from the address it is handed, so masking would begin a list up to 31 bytes
  early. Measured over a run that reaches the movie:

  | address handling | desyncs | triangles |
  |---|---|---|
  | masked to the 32-byte boundary | **0** | 77,772,467 |
  | exact byte address | 26,323,104 | 58,854,957 |

  The lists begin at the unit boundary. The argument was plausible and wrong,
  and is recorded with its numbers so it is not made again.

**`MGS_NO_RTT` is what settled it.** With two changes in flight and a rebuild
that had not completed, packed and strided runs came back byte-identical
across 45 million commands - impossible, and a sign the binary was stale. A
runtime switch cannot be confounded by a build that did not happen: it showed
the texture copies were innocent and the address handling was the variable.

With masking restored and render-to-texture in place: **0 desyncs**,
77,772,467 triangles, and lit pixels up from 4,401,047,447 to **5,391,299,344**
- a billion more, which is the composited content reaching the screen.

**Still open:** the video image itself degrades a few seconds in and then
freezes. It predates all of this work. `MGS_SAVE_SEQ` now captures a numbered
frame sequence and `tools`-side scoring separates noise from picture by
roughness, so the progression can be measured rather than described.


### F163 — the video: what it is not, established the hard way

The movie reaches the screen by **render-to-texture**: the embedded buffer is
copied to `0x80066480` (and two siblings) and sampled back as a texture. That
path was never implemented - `mgs_efb_copy` opened with `if (to_xfb && ...)`,
so every such copy did nothing. That is the original fault and it predates all
of this session's work.

**Implementing it fixed the captions** (confirmed on screen) and did not fix
the video.

**Where the noise actually enters.** The embedded buffer *already* measures
roughness 39 when the copy runs - a picture scores a few, noise scores tens.
The copy is faithful; the picture is broken before it. Every change made to the
copy path this session was therefore downstream of the fault.

**A feedback loop, which is why it never recovers.** The textures sampled
during the noise are the copy destinations themselves (`fmt 0x6` 512x448 at
`0x80066480` and `0x806BC7C0`, roughness 33). Noise in the buffer is copied to
a texture, drawn back into the buffer, and sustains itself. Early frames are
genuinely flat - roughness 0, 100% lit, sampling a clean `fmt 0xE` CMPR texture
at `0x800EA680` - so there is a clean starting state and a definite transition.

**Five diagnoses of mine that measurement overturned.** Each cost a run and is
recorded so the next pass does not spend them again:

| claimed | refuted by |
|---|---|
| the 64-bit store truncation corrupts the FIFO | no 8-byte stores exist: 1, 2 and 4 only |
| the decoder is starved of data | 19,253,874 bytes delivered |
| the video decodes correctly, the fault is downstream | wrong texture: two share 512x448 and the filter matched size only |
| the decoder works then degrades | no frame was ever correct - the "clean" ones are 87% zeros |
| the copy format is 0xC (16bpp) | stride 8192 at 512 wide and 1024 at 64 wide is RGBA8 exactly; the field is bits 4-7 |

The last one produced a correct fix - the format field, RGBA8's two-halves tile
layout and the stride were all wrong together - and it changed the output by
nothing at all, because it is downstream of where the noise enters.

**Two faults of mine, found and fixed.** Encoding full-frame copies as tiled
texels wrote 458 KB over the framebuffer, scrambling the video rectangle and
leaving the letterbox clean. And spacing tile rows by `copy_stride` for the
64x64 case wrote 16 KB into an 8 KB texture, taking a run from 0 desyncs to
26,323,104.

**What to do next, and what not to.** Find the exact frame where buffer
roughness crosses from 0 to 39, and log every draw in that frame with its bound
texture. Everything measured so far is on one side of that transition or the
other. Do **not** spend more time on the copy path: it has been measured
faithful three separate ways.


### F164 — the garbage is gone: RGBA8, and why the fix looked like it failed

The movie no longer shows corruption. The video rectangle is clean and the
captions render correctly over it.

**The fix was the copy format, and three things were wrong together**, which is
why every partial attempt made it worse in a different way:

- **The format field is bits 4-7, not 3-6.** `cmd 0x010063` gives **6**
  (RGBA8, 32bpp), where bits 3-6 give `0xC` (a 16-bit format).
- **RGBA8 is two 32-byte halves per 4x4 tile** - sixteen alpha/red pairs, then
  sixteen green/blue. Written as one run of 32-bit texels it produces a
  plausible wrong image rather than an obviously wrong one.
- **Tile rows are spaced by the programmed stride**, which only agrees with the
  format once the format is right: 8192 for a 512-wide target, 1024 for a
  64-wide one. Both match exactly what the game sets, and that agreement is
  what confirmed the format rather than another guess.

**Why it appeared not to work.** The corrected encoder was measured while
full-frame copies were disabled - I had excluded them an hour earlier to stop
an earlier version writing over the framebuffer. With the format fixed they are
the video, so excluding them removed the thing the fix repaired. Two changes in
flight, and the measurement covered only one.

**The encoder round-trips, verified.** A uniform source of luminance 23 writes
bytes spanning 0..39, which is a flat teal decomposed into alpha/red near zero
and green/blue near 39. Uniform in, uniform out: it manufactures nothing.

**The CMPR path is sound too.** The video samples `fmt 0xE` 512x448 textures.
Their source bytes measure entropy 5.58/8 with 15% of blocks carrying identical
endpoints - real block-compressed data - and they decode to a picture:
roughness 4.1, 84 colours, 27,080 lit texels. Neither the decoder nor the
format was ever at fault there.

**What remains: the picture is frozen, not corrupt.** The visible frame is a
flat teal, and the live `[video]` line names why - the draw producing it samples
a CMPR texture whose roughness is **0** while other CMPR textures in the same
run carry real content. Content exists; the wrong one is reaching the screen.
The freeze coincides with where the subtitles begin.

A live `[video]` line now prints in an ordinary run, every 120 frames and on
any crossing between clean and noisy: frame, size, roughness, lit percentage,
scan-out address, and the last texture sampled with its address and roughness.
That is what identified this in seconds instead of another instrumented build.

### F160 — the renderer's cost, measured instead of read

Three rounds of inner-loop optimisation chosen by reading the code bought
about 6% between them. Building a profiler took less time than the third
round and produced a different answer immediately.

`perf` is not installed on this host, so `runtime/platform/profile.c` samples
the program counter from a `SIGPROF` handler on the **process CPU clock** —
time spent descheduled while another process runs is not sampled, which is
the property the wall-clock ablations lacked. The handler stores a raw PC and
nothing else; addresses are resolved offline against the binary's symbol
table. `MGS_PROFILE=1` enables it.

The first profile, over a full boot:

| share | function |
|---|---|
| 40.8% | `mgs_raster_triangle` |
| 36.1% | `combine` (tev.c) |
| 21.5% | `mgs_tex_sample` |
| 1.6% | `mgs_tev_run_compiled` |

`combine` being 36% of the **whole program** was not something reading the
code had suggested. It is nine lines of integer arithmetic, but it was an
out-of-line call taking eight arguments, made four times per pixel — three
colour components and alpha. The compiler declined to inline it because it
is called from four separate sites. Marking it `inline` and dropping `long`
for `int` (the widest intermediate is about 1.07e9, inside a 32-bit int):
**41.8s → 39.3s**, framebuffer bit-identical.

Two divides per pixel also survived in `mgs_raster_triangle` — `edge(...)
/ area`, evaluated from scratch for every pixel — which is what put that
function at 40%.

### F161 — the rasteriser was handed a worker pool that was then cleared

Band-parallel rasterisation was built, measured, and reported **no speedup at
all**: 13.5 Mpx/s serial against 13.6, 12.9 and 11.1 Mpx/s threaded. The
obvious reading was that the split does not pay.

It was never running. `main` creates the job pool during start-up and handed
it to the rasteriser there, but `mgs_raster_init` is not called until the
guest first configures the video interface — much later — and that init sets
`jobs = NULL`. The pointer was wiped before a single triangle was drawn.
`display.c` now remembers the pool and re-applies it after init.

With the pool actually attached, the same code, same binary, same boot:

| bands | throughput |
|---|---|
| serial | 13.5 Mpx/s |
| 32-row minimum | 34.5 Mpx/s |
| 16-row minimum | 44.5 Mpx/s |
| 8-row minimum | 49.7 Mpx/s |
| 4-row minimum | **51.4 Mpx/s** |

Wall clock for a full boot: **40.3s → 10.6s**. Every variant produces a
bit-identical framebuffer (MEM1 `0x8C8E2DB54E773E25`) and identical raster
counters, which is the check that makes the result trustworthy: bands own
disjoint scanlines, so nothing is shared and no arithmetic changes.

The 32-row minimum — the obvious-looking choice — was the worst of the four.
A 448-row quad splits into only 14 bands, leaving 18 of the machine's 32
cores idle. The size histogram is why this works at all: 1,538 triangles
cover 94.3% of all pixels while 1.6 million tiny ones cover 3.8%, so the work
is concentrated in a few hundred full-screen quads, which is exactly the
shape that bands well. Small triangles stay inline.

**This is a stopgap, not the plan.** The design document (§ GX, and the phase
3 row) specifies a TEV-to-GLSL shader generator on a Vulkan backend. The
software rasteriser is what lets phases 1–2 run at all, and threading it buys
time until that exists. It does not substitute for it.

### F162 — the intermittent boot freeze is the pad poll, and it is not new

A freeze after the `Dolphin SDK - CARD` banner, reported from an ordinary
run. The same failure appears headless: the guest wedges and the run reports
**exactly 13,060 GX commands**.

That number is already written down in `runtime/platform/mmio.c`, in the
comment above `si_poll_frame`, as the signature of a serial-interface failure
fixed earlier — PAD waiting forever for poll data that never arrives. The
wedge PC confirms it: `0x8004C9D4` is `gp_poll_once + 0x74`, called from
`gp_poll_thread`. So this is a recurrence of a known failure mode, now
intermittent rather than constant.

It is intermittent because **runs are not deterministic**. The same binary
completes 271 or 287 DVD reads across runs, because reads finish on host
worker threads. Two consecutive runs wedged; the three after them booted
normally.

**Not yet fixed.** `si_poll_frame` is driven from the frame tick every 2000
steps, which is deterministic, so the race is elsewhere in the SI path.

### F163 — the renderer was the frame limiter, and nothing replaced it

With the rasteriser split across cores the intro logos play far too fast.
Nothing in the runtime ever limited how quickly the guest could finish a
frame; being slow was doing that job by accident, and removing the slowness
removed the pacing with it.

The cap goes on the XFB copy, not on the retrace tick. Retrace fires every
2,000 steps — 13,611 of them in a boot against 800 presented frames — so
pacing it would have throttled the guest by a factor of seventeen. An XFB
copy is one finished game frame, which is the honest unit, and waiting there
throttles the guest itself rather than only the presentation.

`MGS_FPS_CAP` sets it; 0 disables. **Headless runs are uncapped by default**,
because sleeping on the host clock makes a run unreproducible and
reproducibility is what the headless path is for — verified: the headless
MEM1 hash is unchanged at `0x8C8E2DB54E773E25`. The default for a window is
60, which is a guess: this is the PAL build (GGSPA4), so 50 may be the
correct figure for its default video mode and the right value has not yet
been established from VI's own registers.

### F164 — the video corruption is structured, and the text over it is not

A screenshot of the fault, rather than a roughness number, narrows this
considerably. The corrupted video frame is **not random noise**: it is
regular diagonal banding in green and magenta with fine per-pixel vertical
striping. That is the signature of real data being misread — a stride or
channel-phase error — not of uninitialised memory.

Two things are ruled out by the same image. The subtitle "Alaska - Bering
Sea" renders **cleanly on top of the corruption**, so geometry, the TEV
combiner, the rasteriser and the C8 paletted text path are all working; only
the video frame's own content is wrong. And the letterbox bars above and
below stay black, so whatever writes the noise respects the draw's bounds.

Green-and-magenta with alternating bright and dark columns is what YUV 4:2:2
looks like when the luma/chroma phase is off, which is worth holding onto:
the XFB is YUV 4:2:2, and the 512x448 buffers the game hands us as RGBA8
textures (`0x800EA480`, `0x805DC7A0`, roughness 33 and 45) sit near it.

**Also established, and unexplained:** every XFB copy lands one scanline
below where VI scans out. Copies go to `0x80066480` / `0x8015A480` with
stride 1024 bytes — 512 pixels, one row — and VI reads `0x80066880` /
`0x8015A880`, a constant 0x400 further on. The last row of every displayed
frame therefore comes from past the end of the copy. A one-row shift cannot
produce the banding above, so this is a separate defect.

**A hypothesis killed.** The two alternating `xfb` addresses looked like the
game double-buffering with only one buffer ever written, which would have
explained the garbage flashing on and off rather than being constant. It is
wrong: both are written (13 copies to one, 11 to the other in a sample), and
a copy targets whichever buffer VI is *not* currently showing. That is
correct double-buffering.

**Not reproducible headless.** Without input the run sits on the warning
screen — `512x512 fmt 0x0`, the I4 text page — at roughness 2 indefinitely.
Several `MGS_PAD_SCRIPT` variants advanced it but none reached the movie, so
this is currently diagnosed from windowed runs only.

### F165 — the RGBA8 copy encoder is correct, and the old test proved nothing

The corrupted video frames are `512x448 fmt 0x6` — RGBA8, 917 KB. That is not
movie data; it is an EFB copy-to-texture our own encoder produced and the
game then sampled back. RGBA8 tiles are 4x4 in two 32-byte halves, alpha/red
then green/blue, and getting those halves or the tile order wrong gives
green-and-magenta banding, which is what F164 describes.

That encoder had been "verified to round-trip" — with a **uniform** source,
every pixel the same value. That proves essentially nothing: a swapped tile
half, a transposed tile order and a channel rotation are all invisible when
every texel is identical. `tests/test_efb.c` now round-trips a pattern that
differs per pixel and per channel, and compares exactly, RGBA8 being
lossless.

**It passes. The encoder is correct.** An intermediate result said otherwise
— 176 of 192 texels differing, in a pattern that looked exactly like a
transposed tile order — and that was the test's fault, not the encoder's: it
programmed the destination stride as the pixel pitch. The stride is the
distance between **rows of tiles**, `tiles_x * tile_bytes`, which for a
16-wide RGBA8 target is 256 and not 64. Getting that wrong overlaps the tile
rows and makes a correct encoder look transposed.

**What the mistake was worth.** It names the single condition under which the
encoder does scramble: tile rows are spaced by `copy_stride`, so the layout
is right only while the game's stride equals `tiles_x * tile_bytes`. Every
copy observed so far satisfies it — 8192 for a 512-wide RGBA8 target, 1024
for a 64-wide one — but the stride register is **shared with the framebuffer
path**, and an XFB copy of the same 512-wide frame carries stride 1024. A
texture copy inheriting that pitch would overlap its tile rows eightfold and
produce regular diagonal banding rather than noise, which is the shape the
corruption actually has.

`mgs_efb_copy_tex` now prints `[copytex] STRIDE MISMATCH` once per distinct
shape whenever it sees that, so an ordinary run answers the question without
a special build. It does not fire during a boot, and the headless MEM1 hash
is unchanged at `0x8C8E2DB54E773E25`; whether it fires during the movie is
the open question.

### F166 — the memory card cannot be stubbed at the SDK's API, and why

Every headless run stops on a notice screen that needs a button press, which
puts the intro movie — where the outstanding rendering faults are — out of
reach of the reproducible path. Dolphin does not show that screen, and
Dolphin has a working card in slot A, so this is a divergence that localises
to a shim rather than a property of the game.

Stubbing the API was tried, in increasing depth, and does not work:

| stubbed | result |
|---|---|
| `CARDInit`, `CARDProbeEx` → READY | still the notice; game mounts next |
| `+ CARDMountAsync`, `CARDCheckExAsync` | mount and check each run once, then **3,259,600** `CARDProbeEx` polls |
| `+ CARDMount`, `__CARDSync`, `CARDUnmount` | mount → check → **unmount**, then the notice |
| `+ both completion callbacks delivered` | unchanged: mount → check → unmount |

**Why it cannot work at that level.** The callback the game handed to
`CARDCheckExAsync` is `0x800381EC` — `__CARDSyncCallback`, the SDK's *own*
internal helper. The game is not driving the card API directly; it calls the
blocking wrappers, and the SDK runs a state machine underneath. That state
machine's lower half — `__CARDGetControlBlock`, `__CARDGetDirBlock`,
`__CARDAccess`, `__CARDIsReadable` — **reads the card's header, directory and
FAT straight out of the work area**. It never asks a shim anything. Returning
READY to the calls above it does not put a directory in memory, so the SDK
looks, finds nothing usable, and unmounts.

The CARD module is fully named — 33 of 33 symbols in `0x80038000-0x8003F000`
— so this is not a naming gap. It is the wrong interception point.

**What would work: emulate the device on EXI, not the API.** A memory card is
an EXI device with a small command set (read block, write block, erase,
status, id). Backing that with a 2 MB image in host memory, persisted to a
host file, makes the SDK's own code work unmodified — directory, FAT,
checksums and all — which is how Dolphin does it. It also gives real save and
load rather than a card the game can see but not use, and it respects the
translated/native boundary instead of reaching past it: with EXI answering,
none of these CARD shims are needed and they should be removed.

That is phase 5 work by the design document's plan, and doing it now would be
out of phase order. The decision of whether to bring it forward — on the
grounds that it unblocks the reproducible test path for phases 2 and 3 — is
recorded here rather than taken.

**Kept for now:** the shims and the completion-callback queue
(`card_shims.c`, `mgs_card_service` in `dvd_pump.c`). They do not reach the
movie, but the queue is the mechanism an EXI card will need for its
interrupts, and the trace above is what any future attempt should start from
rather than rediscover.

### F167 — a memory card that answers as a device, and where it still stops

Following F166, the card is now emulated on EXI rather than shimmed at the
SDK's API: `runtime/platform/exi_card.c`, with the register side in
`mmio.c`. The CARD API shims are **removed** — with the device answering, the
SDK's own code must run, and a shim above it would hide whether it does.

**What works, measured on the bus** (`MGS_TRACE_EXI=1`):

| the SDK does | the device answers |
|---|---|
| writes `0x00` + a dummy byte | the dummy, `0x80` |
| reads four bytes | `0x00000010` — 16 Mbit |
| `0x89` ClearStatus, `0x83` ReadStatus | `0x41` — READY, UNLOCKED |

and `EXI CSR chan0 = 0x00001000`, so EXT reads back as a present device.

The card id is a **bitfield, not a count**, which is worth writing down
because a plain integer happens to work by accident at 16 and would not at
other sizes: `IsCard` takes the size from `id & 0xFC` and requires one of
4/8/16/32/64/128, and the sector size from a table indexed by
`(id & 0x3800) >> 11`. Ours passes: size 16, index 0 → 8192 bytes, 256
blocks, comfortably over the 8-block minimum.

The image is created already formatted — header, two directories, two
block-allocation tables, each with the checksum pair the SDK verifies — and
persisted to `saves/slot_a.raw` (2,097,152 bytes, `MGS_CARD_PATH` to move
it, git-ignored). A blank card would read as broken and the game would offer
to format it, which is another screen needing a button press, which is the
thing this exists to avoid.

**Where it stops.** The SDK identifies the card and then never mounts it —
three bus commands in a 200,000,000-step run, and the boot still halts in
the same place. Two candidates were checked and cleared: the card-disable
low global at `0x800030E3` reads `00` (enabled), and the probe's start-time
global at `0x800030C0` is populated, so `__EXIProbe`'s ~300 ms debounce is
running rather than stuck.

**Why it stops: the card is fine, the rest of the bus is empty.** Reading
`__CARDBlock[0]` at `0x80208E00` settled it — `attached 0, result -5, size 16
Mbit, sector 8192, mountStep 0`. The SDK stored our geometry correctly, so
identification fully succeeded, and then failed with `CARD_RESULT_IOERROR`
before attaching. The deadlock theory above was wrong: `attached` is 0, not 1.

Counting transfers rather than reasoning about them found it. **Nine EXI
transfers are started in a boot and only five reach the card**; the other
four are addressed to devices that are not there:

| chip select | device | what it is |
|---|---|---|
| 4 | channel 0, device 2 | AD16, probed once at start-up |
| 2 | channel 0, device 1 | **the RTC and SRAM** |

`DoMount` reads SRAM while mounting, so a slot that answers nothing there
fails the mount however sound the card is. That is the next piece to build,
and it is small: SRAM is a 64-byte block with a checksum, and the RTC a
counter. Nothing further about the card protocol needs changing.

A trap worth keeping: `exi_transfer` silently returns when chip select does
not name the card, so a device that is missing looks exactly like a device
that is working. The started-versus-delivered counters exist now, and the
`[exi] DROPPED` line under `MGS_TRACE_EXI=1` names the chip select, because
the silence was what made this take as long as it did.

**The reference used** was `extern/dolsdk2004`, the doldecomp community
decompilation, for behaviour only — THIRD_PARTY.md records why that is not a
rule 9 problem, and no code was copied from it. The card's wire protocol
came from Dolphin's device implementation, recorded there against its pin.

### F168 — the clock and settings device, and a decode bug it hid

Built the second device on channel 0 (`runtime/platform/exi_ipl.c`): the
real-time clock and the 64 bytes of battery-backed SRAM holding the
machine's language, video mode and display offset. `DoMount` reads SRAM while
mounting, so the slot could not stay empty. The font ROM is deliberately not
provided — it is Nintendo's, and nothing has asked for it.

**Transfers reaching a device went from five of nine to seven of nine.** The
remaining two are the AD16 debug device, which nothing needs.

**A decode bug, caught only by tracing what was asked for.** The figures
quoted for these regions — `0x20000000` for the clock, `0x20000100` for SRAM
— are *command* values, and the command carries the address shifted up by
six. The first version compared the decoded address against the undecoded
constants, matched neither, and answered every SRAM read with the zero
reserved for the font ROM. Decoded, the two regions land four apart, so they
are now compared exactly rather than masked — masking off six bits to find
"the region" merges the clock and the settings into one.

**It did not fix the mount.** `__CARDBlock[0]` still reads `attached 0,
result -5, mountStep 0` with the geometry correctly stored. So SRAM was a
real gap and not the blocker.

**What the SDK's own code says about that state**, which is where the next
attempt should start rather than re-deriving it: `card->attached` is set only
inside `CARDMountAsync`, after `EXIAttach` succeeds, and a failing
`EXIAttach` reports NOCARD (-3), not the IOERROR (-5) we see. `attached` is
also cleared on detach. So the -5 was most likely left by a mount that got
further and then unwound — which matches what the API-level stubs showed in
F166, where the game mounted, checked, and then unmounted. The missing
evidence is which operation reports IOERROR, and the cheapest way to get it
is a trace of the bus during the mount rather than more reading: no
`ReadArray` (`0x52`) and no vendor-id (`0x85`) command has ever reached the
device, so whatever fails, fails before the first real card read.

### F170 — the card does mount, and the boot is too unstable to tell

F169 below concluded the notice is not gated on the card. **That conclusion
was reached against poisoned data and should not be relied on.** Two things
were wrong with how it was measured.

**The card image persists between runs, and a bad run poisons every run
after it.** `saves/slot_a.raw` is written back on exit. The first run
formats it and the SDK then writes to it; from the second run onward, that
modified image is what loads. An hour of "why does the mount fail" was spent
on a stale file rather than on the code, and deleting it changed EXI
transfers in a boot from 9 to 100 immediately.

**The boot is nondeterministic enough that none of it was A/B testable.**
Four identical fresh starts, same binary, card deleted before each:

| run | EXI transfers | `card->result` |
|---|---|---|
| 1 | 9 | IOERROR (-5) |
| 2 | 4 | NOCARD (-3) |
| 3 | 9 | IOERROR (-5) |
| 4 | **100** | **READY (0)** |

and three more at a longer budget gave a black screen, a notice screen, and a
failed card respectively. This is F162's nondeterminism — DVD reads finish on
host worker threads — reaching the card path.

**What this does establish:** the device implementation is sound enough to
mount. Run 4 reached `CARD_RESULT_READY` with 98 transfers actually reaching
the card, which is a real mount, not an identification. What it does not
establish is whether a mounted card removes the notice, because the runs that
mount diverge elsewhere.

**So the nondeterminism is now the blocking problem, ahead of the card and
ahead of the video.** Nothing downstream can be judged while three identical
runs disagree. That is where the next work belongs.

Also corrected here: the card's power-on status was `0x41`, where hardware
reports `0xC1` — BUSY is set alongside READY and UNLOCKED. Fixed, though it
changed nothing on its own.

And tried, and reverted: raising EXI's transfer-complete interrupt when a
transfer finishes, which hardware does and which the serial interface already
needed (F153). Asserting it from inside the store that started the transfer
made the boot strictly worse — 9 transfers down to 4, geometry never stored,
IOERROR becoming NOCARD — because it re-enters the guest at a point it did
not choose. If revisited it needs queueing to a safe point the way DVD
completions are, and the note in `mmio.c` says so where the decision lives.

### F169 — the notice is not the card, and the headless route past it

**SUPERSEDED BY F170 — measured against a stale card image and an
unrepeatable boot; the card mounts in some runs.** The notice screen appeared
to be ungated on having a memory card: with the card emulated, identified and
formatted, the game still showed it. The guest is not
blocked while it does: stopped at `0x7F01DC94` in the engine overlay, having
drawn 20,180,002 triangles and 9,764 frames in 120,000,000 steps. It is
running its main loop and waiting for a button, which is what a notice does.

A formatted card is still an *empty* card, and this reads as a "no save data"
notice rather than a "no card" one. So the whole premise of F166-F168 — stub
or emulate the card and the screen goes away — was wrong, though the work it
produced is not: the card is needed for saves regardless, and the EXI bus is
better understood for it.

**What actually gets past it: Down, then A.** Not A on its own, which is what
several attempts this session used. The dialog has a default the cursor must
be moved off first, which is recorded in this file already from an earlier
session and was not read carefully enough:

```
MGS_PAD_SCRIPT="20000:0004,20040:0000,20400:0100,20440:0000,\
60000:0004,60040:0000,60400:0100,60440:0000,\
120000:0004,120040:0000,120400:0100,120440:0000"
```

40-tick holds, in `mgs_mmio_tick_frame` units. With this the intro movie is
reached headless — `lit 71%` from video frame 2280 — and the reproducible
path finally covers the part of the boot where the rendering faults live.

**The corruption does not reproduce there.** Zero noisy frames through video
frame 4440, and no `[copytex]` stride warning ever fires. Roughness sits at
**0**, though, where a moving picture scores 1-5, so this is more likely the
frozen flat frame than real playback. Whatever makes the picture break in a
window is absent, or not yet reached, headless. That difference is now the
most useful lead on the video: the two paths differ in the frame-rate cap
(60 windowed, uncapped headless), in presentation, and in reading a real
keyboard, and nothing else.

### F171 — "damaged", and the serial that has to be earned

A screenshot settled in one frame what several runs had not: the game does
not say there is no card, it says **"The Memory Card in Slot A is damaged and
cannot be used"**, and offers Retry or Continue without saving. So the device
works, the card is read, and the **format** is what is rejected.

`VerifyID` refuses a card for four reasons, and only three are obvious: the
device id must be zero, the size in the header must equal the size the card
reports over the bus, and the header's checksum pair must verify. The fourth
is the one that cost the time:

**The serial must be derived from the machine's flash id.** Bytes 12 to 19 of
the serial seed a linear congruential generator; each of the first twelve
bytes must then equal the matching byte of `OSSramEx::flashID` for that
channel, plus the next value the generator produces. A card whose serial is
anything else passes every checksum and is still refused. Ours held twelve
bytes of `0xA0 + i`, chosen to be recognisable in a hex dump, which is
precisely the kind of placeholder this check exists to reject.

A consequence worth keeping: **a formatted card is only valid for the machine
whose SRAM it was made against.** Ours is generated against the flash id the
IPL device reports, so the two have to be initialised in that order —
`mgs_mmio_attach_card` does SRAM first and passes `&ipl.sram[0x14]` to the
card.

The header layout was wrong too, though harmlessly: the serial is 32 bytes,
and the format time and SRAM fields written at 0x0C-0x1B were landing inside
it. Those fields do not exist in `CARDID` and are gone.

**`tests/test_card.c` now checks a freshly formatted image against every one
of those rules** — device id, size, header checksum, the serial derivation,
and the checksum pairs on both directory copies and both allocation tables —
with no game running. The serial rule is written out in the test from the
specification rather than from the code that generates it, so an arithmetic
slip cannot hide in both. It passes.

**The image on disc is now checked rather than trusted.** A card file
survives between runs, so a stale or foreign one loads in preference to a
good one — and it presents as the game calling the card damaged, not as an
error in the runtime. That is exactly what wasted an hour in F170. The load
path now verifies the size, the header checksum and the serial against this
machine's flash id, and reformats when any of them disagree. Corrupting a
byte of the image and re-running shows it: "not a card this machine can read
- reformatting", then a clean load next time.

**The format is now provably right, and the game still says damaged.**
`tests/test_card.c` also drives a read the way the SDK drives one — the
command, four address bytes, four ignored, then data — and confirms the bytes
that come back are the bytes that were written, at page and block boundaries
alike. So the format, the checksums, the serial and the read addressing are
all correct, and the message persists.

**Which means "damaged" is most likely not a format complaint.** Every
headless run ends with `CARD_RESULT_IOERROR` after three card commands, with
`mountStep 0` — the mount never reaches the block reads where any of the
above would be examined. A game that asks for a card and gets an I/O error
has to say something, and this is plausibly what it says. The format work
was worth doing and is not the fix.

**Where the I/O error comes from is still open, and the 2004 reference does
not explain it.** In that code the operations we see — clear status, read
status — return NOCARD on failure, never IOERROR, and the path after them
sets `mountStep = 1`. We observe IOERROR with `mountStep` still 0, which
those branches cannot produce. The game's SDK is build 0x2301 from 2003 and
the reference is 2004-04-20, so this is a place where they plausibly differ
and reading the reference further will not settle it.

**The card's state is now printed in the terminal as it changes**, decoded,
rather than only dumped at exit — reading a failure should not require
photographing the window. It immediately showed the shape of this:

```
[card] slot A: not attached, mount step 0, result 0 (READY)
[card] slot A: not attached, mount step 0, result -3 (NOCARD)
[card] slot A: attached,     mount step 0, result -1 (BUSY)
[card] slot A: not attached, mount step 0, result -5 (IOERROR)
```

**The card attaches.** `CARDMountAsync` runs, `EXIAttach` succeeds — that is
what sets `attached` and leaves `result` at BUSY — and the mount then fails
at step 0 and detaches. So neither the device, the format, nor the attach is
the problem; the failure is inside the first mount step, between the status
read and the line that would set `mountStep = 1`.

In the 2004 reference nothing on that path can produce IOERROR: clear-status
and read-status return NOCARD when they fail, and the probe that follows
returns NOCARD too. Another reason to think the 2003 build differs there.

**The mount reaches step 1**, which a windowed run showed and headless does
not:

```
[card] slot A: attached, mount step 0, result -1 (BUSY)
[card] slot A: attached, mount step 1, result -1 (BUSY)
[card] slot A: not attached, mount step 0, result -5 (IOERROR)
```

Step 0 therefore completes - the status is read, the card is taken as already
unlocked, and `mountStep` advances. Step 1 is where the system blocks are
read, and no `ReadArray` command has ever reached the device. So step 1
issues a read and waits for something that never arrives, which is the shape
of a timeout rather than a refusal.

**The transfer-complete interrupt is now implemented**, asserted when a
transfer finishes and only while the guest has unmasked it, cleared by the
write-one the guest uses. Delivery is left to the run loop rather than forced
from the store that started the transfer.

**And the earlier verdict on it was wrong.** F170 recorded that raising it
made the boot strictly worse — transfers 9 down to 4, geometry never stored,
IOERROR becoming NOCARD — and reverted it on that basis. Those runs were
against a stale card image, before the load was validated, so they measured
the image and not the interrupt. Retried against a validated card it changes
nothing headless and breaks nothing: same card sequence, same 9 transfers,
14/14 tests, and a healthy boot of 8,389,420 triangles.

It does not fix the mount **in a headless run, which never reaches step 1**.
The windowed path does, and is where it has to be judged.

**The older hypothesis, now confirmed as the mechanism to pursue:**
After `mountStep = 1` the SDK's mount continues from a completion, and we
never signal one; a mount that stalls and then times out would report an I/O
error. That was tried (F170) and reverted because asserting it from inside
the store that starts the transfer re-enters the guest at a point it did not
choose. Doing it properly means queueing the interrupt to a safe point the
way DVD completions are, and that is the next thing to build.

### F172 — the memory card mounts, and it came down to one byte of SRAM

**The card works.** The notice is gone, the mount runs to completion, and a
headless boot now reaches the intro movie with no pad script at all.

```
[card] slot A: attached, mount step 0, result -1 (BUSY)
[card] slot A: attached, mount step 2 ... 7
[card] slot A: attached, mount step 7, result -4 (NOFILE)
```

`NOFILE` is the right answer for a formatted card with no save on it. EXI
transfers in a boot went from 7 reaching the card to **652**.

**What was wrong: the flash id's checksum byte in SRAM.** SRAM holds a
twelve-byte flash id per slot and a separate checksum byte for each — the
complement of the sum of those twelve. Having reached step 1 and decided the
card is already unlocked, the mount sums the flash id, compares it against
that byte, and reports **IOERROR** if they disagree. Ours had a zeroed flash
id and a zeroed checksum; the complement of zero is 0xFF, not zero. One byte
per slot, at 0x3A and 0x3B in the block.

**Why it took so long, which is the part worth keeping.** The error was
IOERROR, so it read as a transfer fault, and three separate investigations
went after transport: the device protocol, the card's on-disc format, and
EXI's transfer-complete interrupt. None of them was wrong to check and none
of them was the fault. The thing that actually found it was printing
`mountStep` as it changed — `attached, step 0` then `step 1` then `IOERROR`
named the exact branch, because only one line in the whole mount sets IOERROR
immediately after setting step 1. **Watch the state machine's own counter
before theorising about the bus underneath it.**

Superseded by this: F171's suspicion that the format was at fault, and
F170's that the interrupt was. The format was already correct and is now
covered by tests; the interrupt is correct hardware behaviour and stays, but
neither was the mount failure.

### F173 — the movie decodes eleven frames and stops asking for more

With the card mounting (F172) a headless run reaches the intro movie **with
no pad script**, so the video is finally on the reproducible path. The first
measurements from there change what the problem is.

**The movie is not failing to decode; it stops being driven.** Textures
decoded by shape over a 200,000,000-step run:

| shape | decodes | roughness |
|---|---|---|
| `fmt 0xE` 512x448 — the movie's own frames | **11** | mean 6, max 51 |
| `fmt 0x6` 512x448 — the per-frame render-to-texture | **4099** | mean 2, max 37 |

Eleven distinct movie frames, each with real content, and then nothing —
while four thousand render-to-texture updates keep arriving, so the game is
alive and drawing throughout. That is what "frozen on the first frame with
the text still animating over it" looks like in numbers, and it matches what
is on screen: a flat field at 71% lit and roughness 0, with a `159x17`
caption sampled on top of it for thousands of frames.

**It is not the disc.** The game asked for 406 reads — 303 `DVDReadAsyncPrio`
and 103 `DVDReadAbsAsyncPrio` — and all 406 completed with all 406 callbacks
run. Nothing is outstanding. **The game stopped asking**, so nothing about
DVD delivery can explain it.

**It is not a hang.** The run stops in `GXSetVtxDesc`, the engine's own
thread is running, and the hottest hardware reads are PI and the command
processor's FIFO status — the shape of a game rendering normally. No audio
register appears at all, so it is not spinning on a device either.

**So the movie player believes it has nothing to do.** It buffered eleven
frames, and whatever should advance it past them is not happening. Audio is
the obvious suspect, being unimplemented and phase 4 by the plan — a player
paced by how many samples have been consumed will wait forever against a
mixer that never consumes any — but that is a hypothesis and not yet
evidence; the absence of audio-register polling argues against the simplest
form of it. ARAM is worth a look: 139 transfers in and 8 out for the whole
run, with 614 of its interrupts refused against 294 delivered.

**What this does close off:** the decoder, the texture cache, the CMPR
format, the copy encoder and DVD delivery. All were suspected across this
session and none of them is it.

### F174 — absolute disc reads were being trimmed to the end of a file

Tracing every read of the intro movie found five of 406 coming back short,
by four to twenty-six bytes, and the game resuming each time from where the
trim left it — so every frame boundary after that point was shifted.

`mgs_disc_read` clamps a request to the end of the file it names, which is
right when a game reads a file: the SDK returns a length and the game checks
it. `mgs_disc_read_abs` reached that clamp too, and there it is wrong. An
absolute read is by **disc offset**, and on real media the bytes after a file
are its alignment padding, so the drive returns everything asked for and the
caller is never told a file ended. A folder layout has no padding, so the
request was trimmed instead. The tail is now zeroed, which is what that
padding holds.

**Fixed and verified: 0 short reads of 406, and 78 more bytes delivered.**

**It did not fix the movie.** Still eleven decoded frames, still roughness 0,
still frozen. So this was a real defect on the streaming path and not the
cause of the freeze — worth having found, and not the answer.

What it does remove is a whole class of doubt: the movie's data now arrives
byte-exact, so anything still wrong with playback is downstream of the disc.

### F175 — the movie prebuffers 256 KB and waits; the clock is not why

Tracing which files the game reads, rather than assuming, corrected two
things I had been treating as fact.

**The sequential stream I took for the movie is audio.** The 101 evenly
spaced reads at 0x2B88EECC are `shared/audio/banks/bank001.spd`. Files read
by absolute offset over a run: `stage.dat` 281, `bank048.spd` 50,
`bank001.spd` 49, `demo.dat` 14, and **`shared/movie.dat` just 8**.

**The movie starts at its beginning, not partway in.** Those eight reads are
at file offsets 0x0, 0x8000, 0x10000 … 0x38000 — sequential from zero. So
"the teal scene appears too early" is not a seek landing in the wrong place;
the movie genuinely plays from frame one and the picture is the first thing
in the file. 256 KB read out of 94,935,040. That is a prebuffer being filled
and then never drained.

**The guest clock is miscalibrated, and that is not the cause.** The Gekko's
time base counts at 40.5 MHz, so a 60 Hz field is 675,000 ticks; ours
advances 32 ticks a step against a retrace every 2,000 steps, making a field
64,000 — about **ten times too slow** against the frame rate it is paired
with. `OSGetTime` is called 715,957 times in a run, so the game is certainly
clock-driven, and this looked compelling.

It is not the fault, and a sweep settles it rather than one sample: 32 gives
11 frames, 337 gives 13, and 2000 gives **one frame with the boot never
reaching the movie at all**. A faster clock does not advance the player, and
far enough out it breaks the boot. `MGS_TICK_RATE` now exposes the figure and
the default is left at 32, because a ten-fold change to guest time is not
something to ship on the strength of a hypothesis it just failed. The
discrepancy is real and wants fixing on its own merits, with its own
evidence.

**What the evidence now favours is audio.** The freeze coincides with where
the voices should start, which is an observation from watching it rather than
from a log, and two voice banks are streaming heavily throughout. A movie
paced by voice playback against a mixer that consumes nothing would fill its
buffer, show its first frames, and stop — which is exactly the shape here.
Audio is phase 4 and unbuilt.

### F176 — audio never starts, and un-stubbing its init still hangs

A run now reports what the audio path attempted, which settles several
guesses at once:

```
audio: 1 mails to the DSP, AI control 0x00000046 (STOPPED, 48kHz), 2 samples
ARAM: 139 transfers in, 8 out
```

**The game never starts audio.** The audio interface is stopped, so playback
was never begun; essentially no mail reaches the DSP, so no command list is
ever submitted. Yet 139 ARAM transfers go *in*, so sample data is uploaded.
The game prepares audio and never plays it.

So the earlier theory — the movie waits for audio to drain — is wrong as
stated. Nothing is draining because nothing started.

**Why it never starts: `__OSInitAudioSystem` is stubbed to a no-op**, on
purpose, as phase 4 deferral. Its note argued the flags it waits on "will
never be raised however carefully the registers are modelled". Since that was
written we gained ARAM DMA-done and DSP status bits and a clock that moves,
so the premise was worth retesting.

**Retested, and it still holds.** With the stub removed the boot produces no
output at all and has to be killed. The reasoning stands; restored, and the
`audio:` line is kept because it is what makes this legible.

**What fits every symptom, including the ones from watching it play.** Music
and SFX begin much earlier than the freeze and voices begin right at it. In
our port no audio plays at all, so the distinction cannot be about sound
being heard — it is about whether the game **waits**. Music and effects are
fired and forgotten, so their silence costs nothing. Voice playback is
synchronised, and the movie is paced against it, so the first thing that
actually waits on the audio system is the first thing that stops.

**The scoped next step is the DSP boot handshake**, not a mixer: enough of
the coprocessor's side that `__OSInitAudioSystem` completes and the audio
interface starts. A real voice mixer is phase 4 and far larger; this is the
part the movie needs, and it is testable on its own — the `audio:` line goes
from STOPPED to playing.

### F177 — two real DSP faults, and the audio init still does not complete

Read what `__OSInitAudioSystem` actually waits on rather than guessing, and
two of our registers were wrong:

**`0x400` means "ARAM DMA still running", and we set it on completion.** The
start-up programmes a transfer and then spins until that bit goes *clear*.
Ours finishes inside the store that starts it, so the bit should never be
seen set at all. Setting it alongside the completion flag was a
contradiction, and an endless loop for anything that read it.

**The DSP never reported in.** Start-up loads a small program into the
coprocessor, takes it out of halt, and waits for mail from the DSP with its
top bit set, carrying a value the SDK checks arithmetically. Nothing sent it.
Clearing the halt bit now hands that mail over, which is what a DSP does at
that point.

Both are corrected and both are right regardless of what they fix.

**They are not enough.** With `__OSInitAudioSystem` un-stubbed the boot still
produces no output and has to be killed. So the wait that hangs is further in
than these two, and the stub is restored again.

**What is now in the log, because this was diagnosed by reading four
scattered tallies:**

- `renderer:` names what actually draws — a software rasteriser on 32 cores
  presenting through SDL3. **There is no OpenGL and no Vulkan**; that is
  phase 3's plan, and a log line naming an API we do not use would mislead in
  the one place someone looks to find out.
- `pipeline:` is the path from disc to screen on one line — megabytes read,
  textures decoded, copies made, frames presented. When the picture stops,
  the question is which of those stopped.
- `reads refused` counts disc reads that failed, and each one is named as it
  happens. A failed read and a read never issued look identical in a log
  otherwise, and both read as "the game is not loading anything".
- **Every disc read names its file by default**, which was behind
  `MGS_TRACE_DVD` and so invisible in an ordinary run. Which file the game is
  reading, and where in it, is the first question asked when loading looks
  stuck, and it could previously only be answered by knowing to re-run with a
  switch set. Reads are few — a boot is about four hundred — so the first
  hundred are named and then every twentieth, which cannot flood a long
  session but still leaves a trail through a stall. It is what identified the
  eight reads of `movie.dat` against 99 of the voice banks.

### F178 — WRONG: the suspended thread is normal, and nothing blocks at all

**The conclusion below is wrong and was checked rather than trusted.** The
main thread is suspended at 20,000,000 and at 60,000,000 steps as well —
long before the freeze — so it is parked from early boot and runs nothing,
which is ordinary for a game that works from its own threads.

Worse for the theory: **the thread states are identical before and during the
freeze.** Same five threads, same queues, same suspended main. Nothing blocks
and nothing changes. So the freeze is not a thread getting stuck; the engine
thread keeps running and drawing and simply stops asking for data — a
decision inside the game's own state machine.

What survives from it is the elimination, which is worth keeping:



When the picture freezes **every disc read stops**, not only the movie's:
`stage.dat` is streaming right up to the last `movie.dat + 0x38000` and then
nothing is read again. That is the whole loading pipeline halting at once,
which is a different fault from a movie player going idle.

**It is not our DVD layer refusing them.** Requests occupy a slot until their
callback runs, and callbacks only run with guest interrupts enabled, so an
exhausted pool was the obvious candidate — 4,538 deferrals are recorded. It
is counted now and the answer is **zero refusals**: 406 reads, 406
completions, 406 callbacks, no slot ever denied.

**The game stops issuing them, because its main thread is suspended:**

```
0x801ECD70  ready  prio 16  SUSPENDED
   OSSuspendThread <- (loader) <- rel_loader_LoadRel <- main
```

Not blocked on a read and not sleeping on a queue — **parked by
`OSSuspendThread`**, waiting for an `OSResumeThread` that never comes. Every
other thread is healthy: the engine thread is still running and drawing,
which is why the game keeps rendering, the pad thread polls, and the vsync
thread sleeps on `VIWaitForRetrace` as it should.

So the question is no longer "why does the movie stop reading" but **"who
should resume thread 0x801ECD70, and why doesn't it"**. That is a much
smaller question, and it is answerable: find the `OSResumeThread` call that
pairs with this suspend.

It also reframes the audio theory. The suspend is a deliberate handover — the
loader parks itself and expects a completion to wake it. If that completion
is an audio one, audio is still implicated, but the mechanism is a missed
wake-up rather than a starved buffer, and a missed wake-up can be found
without building a mixer.

### F179 — un-stubbing the audio init does not hang, it crashes

F176 recorded that removing the `__OSInitAudioSystem` stub made the boot
"produce no output and have to be killed", and read that as the hang its
original note predicted. Run under a debugger instead of a timeout, it is a
**segmentation fault**, and the backtrace says why:

```
#0  mgs_host_patch_dispatch
#1  dolrecomp_dispatch_replacement
#2  func_800195E0          <- EXIGetID + 0x2D0
#3  mgs_dol_call
#4  func_800195E0          <- and again, forever
```

The guest re-enters `EXIGetID` through the dispatch path until the **host**
stack is exhausted. Each guest call costs a host frame, so a guest loop that
never terminates is not a spin here — it is a crash.

**This is not caused by the recent EXI work.** F176 saw the same failure
before the transfer-complete interrupt existed; only its description was
wrong, because a timeout cannot tell a crash from a hang.

**What it means for audio.** The stub's original note reasoned that the DSP
flags it waits on could never be raised. That reasoning may well still be
right, but it is not what stops it today: it never reaches those waits. Only
one DSP control write happens before the crash, and the run does not even
print the DSP banner. The blocker is in the EXI path, before any of the audio
hardware is touched.

**Why a loop becomes a crash, which is the reusable part.** Guest calls nest
on the host stack. Anywhere the guest retries indefinitely — waiting on a
device that answers wrongly — the symptom is a segfault deep in
`mgs_dol_call`, not a visible spin. That is worth knowing before reading the
next such backtrace as memory corruption.

**Next:** find why `EXIGetID` does not terminate. It is called during
start-up against a bus that now has a card, a clock and settings on it, and
something it reads keeps it retrying.

### F180 — the engine parks itself for the movie, and never unparks

The 406 disc reads in a run are not a stream that stopped part way: they are
the **whole loading phase**, finishing as the first video frame appears, with
zero reads after it. The movie fills a 256 KB prebuffer, shows frame one, and
never enters a streaming loop. So the question is not what stopped the reads
but what should be **draining that buffer**.

The engine's own task table answers it. At boot:

```
engine tasks (table 0x7F4BD2A8, global mask 0x00000000)
31 tasks across the table; 0 levels gated off
```

At the freeze:

```
engine tasks (table 0x7F4BD2A8, global mask 0x00000008)
229 tasks; 9 levels gated off, 17 nodes that would not run
  level 0   gate 0x00000000           runs
  level 1   gate 0x00000000           runs
  levels 2-10  gate 0x19 or 0x1F      SKIPPED, gate & mask is set
  level 11  gate 0x00000000           runs
```

**Bit 3 of the global mask gates nine of the twelve levels off.** That is not
a fault: it is what a cutscene does — park ordinary gameplay while a movie
runs, leaving only the few levels that draw. The game is *paused for the
movie*, which is precisely why the picture is frozen, the engine still
renders, and the whole game's progression stops with it. The mask is cleared
when the movie finishes; the movie never finishes; the mask never clears.

**This gives a clean success signal for any future fix.** `global mask` back
to `0x00000000` means the movie completed and the engine resumed. That is a
much better test than watching a picture, and it needs no eyes on a window.

**And it narrows the search.** Whatever advances the movie must live on level
0, 1 or 11 — the only ones still running — so it is not simply "a task got
gated off". Something those levels do each frame is waiting on a condition
that never becomes true, and audio remains the best candidate for it.

### F181 — the parking mechanism works; only the movie's park sticks

F180 found the engine gated nine of twelve task levels off during the movie.
Watching the mask change through a run, rather than reading it once at the
end, shows the mechanism is healthy:

```
[engine] task mask -> 0x00002450   parked
[engine] task mask -> 0x00000000   running
[engine] task mask -> 0x00000001   parked
[engine] task mask -> 0x00000000   running
[engine] task mask -> 0x00000008   parked   <- and never lifts
```

**The engine parks and unparks itself three times before the movie, each
time cleanly.** So this is not our emulation of task gating going wrong —
there is no emulation, the mask is the engine's own global and it drives it
correctly. Only the movie's park, bit 3, is never lifted, because the thing
that would lift it is the movie finishing.

**A trap worth recording:** the first version of this watch read the mask
where the final report does, which is *after* the run, so it never fired
once and reported "no transitions" — which would have read as "the mask is
set once and never touched". The host already watches `OSLink` go past and
keeps its arguments, and the overlay's globals are a fixed offset from the
module it was handed; taking it there is what made the watch work.

**Where this leaves the search.** Everything around the movie is now
accounted for and healthy: the disc delivers every byte asked of it, the card
mounts, no thread blocks, the engine's scheduler behaves, and the parking is
deliberate. What remains is the movie's own advance — eleven frames decoded
from a 256 KB prebuffer and then nothing — and `MGS_TRACE_DSP` plus the
`[engine] task mask -> 0x00000000` line give a headless success signal for
whatever fixes it.

### F182 — the cheap naming seams are exhausted; one name, two dead ends

A pass over the two routes that looked like they had slack left. The yield is
**one symbol**, and the value is mostly in what is now ruled out.

**Self-naming strings: 52 of them, 6 names, 1 usable.** Messages tagged
`:: FunctionName` are the route stage 5g used. There are 52 such strings in
the engine and they name only six functions — the earlier "about 25" counted
strings. Mapping each to the function that references it, five of the six are
emitted from two to four different functions, so the tag names the **module**
and not the emitter. Only `NewFallingFloor` has all its messages referenced
from one function and nowhere else; it is added at `.text 0x28D9E4`, size
`0x26C`, origin `message`. Naming the other five by "most messages wins"
would have produced five plausible unverifiable names, which is F159 again in
a new disguise.

**Tremor and ogg cannot be named from upstream.** `main.dol` carries 198
functions of Xiph's decoder across ten translation units, with upstream
sources sitting in `extern/`. The structure supports the idea — anchors group
into eighteen runs, one per file, strictly ascending and non-overlapping, so
units are emitted contiguously. The counts destroy it: not one of the ten
files matches upstream, 167 against 177 overall and wildly out per file
(`mapping0.c` 22 against 6). Two causes, both fatal: a file's region absorbs
the gap to the next file's first anchor, and `main.dol.files.txt` already
records that **Konami edited these sources**, replacing Xiph's allocator with
their own — which is why the line numbers never matched either.

**What this says about where naming effort should go.** Both remaining cheap
routes are now measured and closed. The engine is 16,667 functions with 10
named, signature matching cannot touch it (stage 6b), and its strings are
spent. What is left is reading code — stage 6c's area classification says
where to look, and the SDK boundary is already at 198 of 336 entry points,
which is the number that actually governs the port.

### F183 — naming by the company a function keeps, and what it cost to trust it

After F182 closed the cheap seams, a route that still had yield: an unnamed
function's **callees and callers** both survive compilation, and both can be
compared against the reference.

It rests on a fact measured rather than assumed — within a translation unit
the linker emitted in **source order**, checked against the CARD module where
24 names were already confirmed: **15 consecutive pairs ascend, none
descend**. The Tremor units run the other way (F182), so this is a property
to measure per module.

**Seven symbols added**, 1,007 to 1,014, origin `callees+callers`, each
confirmed twice: by its callee set against the reference, and independently
by who calls it. `__CARDPutControlBlock` is the clearest — ten named CARD
functions call it, exactly the shape of a control-block release helper.

**Two rules did most of the work, and both are about refusing evidence:**

- A proposal resting only on `OSDisableInterrupts`/`OSRestoreInterrupts` is
  discarded. Nearly every SDK function brackets a critical section, so that
  says the author was careful, not which function this is.
- A name proposed for more than one address is discarded **entirely**. Three
  addresses came back as `Retry`, a static helper name reused across files.
  Picking one would have been a coin toss dressed as analysis.

14 proposals became 8, and the caller check took 8 to 7.

**Withheld, which is the part worth remembering.** `__AXDSPDoneCallback`,
`GXSetCurrentGXThread` and `CARDFreeBlocks` all pass the callee test and have
no named callers in the DOL — one is address-taken, the others are called
from the overlay. Absence of callers there is *expected* and proves nothing
either way, so none is claimed on one route alone. And `0x800393B4` calls
only `__CARDGetControlBlock`, which three different accessors do identically:
three candidates, one function, nothing to separate them.

**This route pays compound interest, and it was collected.** Requiring both
neighbours to land in the *same* reference file left 322 functions untouched,
because a run of unnamed code usually straddles a file boundary. What a
neighbour really gives is a floor or a ceiling — anything after a named
function is later in **its** file, anything before one is earlier in **that**
file — so taking the union of both widens the candidate set and leaves the
discriminating to the callee and caller tests, which is where that burden
belongs. Unbounded fell from 324 to **171**.

The caller check is now inside the tool rather than done by hand, so it only
emits names confirmed by both routes. That second pass added three more:
`__AXDSPDoneCallback`, `ReadArrayUnlock` and `__CARDUpdateDir` — and the
first of those is confirmed by `__AXOutInitDSP`, a name added minutes
earlier, which is the compound interest arriving. **1,014 to 1,017.**

Re-running after that yields nothing further: 0 proposed, 167 with no unique
match, 171 still unbounded. The route is exhausted at this strictness, and
four candidates (`AMInit`, `AXInit`, `CARDCheckEx`, `GXSetCurrentGXThread`)
sit one route short — they pass on callees and have no agreeing caller in the
DOL, because they are called from the overlay. They are listed by the tool on
every run so they are not lost, and not claimed.

### F184 — the audio-init crash is not about audio: shims are load-bearing

F176 and F179 recorded that removing the `__OSInitAudioSystem` stub crashes
the boot, and read that as something about the DSP. **Both attributions are
wrong.**

**The control that should have been run first.** Five runs of the unmodified
build at 8,000,000 steps: **5 of 5 exit 0**. So the segfault is caused by the
change, not by the run-to-run instability of F162.

**The change is not audio-specific.** Removing `OSReport` — an entry with
nothing to do with audio, with the audio stub still in place — crashes
identically. Any entry removed from `config/sdk-implemented.txt` does it.

**And the generated table is innocent.** Regenerating it after removing one
entry changes exactly three lines: the entry, `MGS_PATCH_COUNT`, and one
prototype. The table stays correctly sorted, and `mgs_host_patch_dispatch`
returns 0 cleanly for an address it does not hold, so a stale guard in the
module falls through harmlessly.

**What is actually happening: the shims stand in for guest code that cannot
run here.** Remove one and the *translated* SDK function runs for the first
time — code that has never executed in this port because it was shimmed from
the start — and it reaches hardware we do not model. A guest retry loop
against a device that never answers is not a visible spin in this runtime: a
guest call costs a host stack frame, so it is **208,840 frames and a
segfault** (F179's reusable lesson, arriving again).

`OSReport` is the clearest case. On this hardware the debug console goes out
over **EXI channel 0 device 2**, which we do not implement — our own trace
shows those transfers as `DROPPED: chan 0 cs 4`. Translated `OSReport` talks
to a device that is not there.

**What this means for the movie.** Testing whether audio init can run still
requires removing its stub, and that will still crash for the reason above
until the DSP is modelled well enough to satisfy it. The blocker is real but
it is not "the audio init hangs" — it is that a shim cannot be withdrawn
before the hardware beneath it exists.

**A tool bug found on the way.** `inject-patch-guards.py` claimed to be
idempotent and was not: it writes a comment line and then the guard, but
checked only the *next* line for an existing guard, so it saw the comment and
re-injected every time. The chunks carried **104 guard sites for 35 patched
functions** — duplicates stacked by successive runs. Harmless at runtime,
since a second call only runs when the first returned 0, but the file said
one thing and did another. Fixed; the check now looks at both lines and
reports `0 injected, 36 already present`.

### F185 — two more movie leads closed: the clock and the ARAM interrupts

**The guest clock is not what paces the movie.** F175 tested one alternative
rate and called the 11-to-13 frame change noise. A sweep confirms it:

| tick rate | movie frames | best lit |
|---|---|---|
| 32 (shipped) | 11 | 71% |
| 337 (calibrated) | 13 | 71% |
| 2000 | **1** | 0% — never reaches the movie |

Speeding the clock up does not advance the player; far enough out it breaks
the boot. The calibration error is real and still worth fixing on its own
merits, but it is not this.

**ARAM interrupts are not being lost either.** A run reports 294 delivered
against **614 refused**, which reads like two thirds of the audio path's
completions going missing. They are not: `mgs_interrupt_aram` puts a refused
interrupt **back** on the queue rather than dropping it, so a refusal is a
retry against a guest that currently has interrupts masked. The count
measures how often the guest sits in a critical section, not loss.

With the disc, the threads, the scheduler, the card, the format, the clock
and now the interrupt path all cleared, audio remains the only live
hypothesis — and F184 explains why testing it is expensive.

### F186 — the DSP model already satisfies every wait in audio start-up, and a duplicate boot mail was removed

F184 left audio as the only live hypothesis for the movie freeze, and also
established that testing it by withdrawing the shim is expensive: the
translated SDK then reaches hardware we do not model and crashes for reasons
that have nothing to do with audio. So the sequence was driven directly
instead, without booting the game.

`tests/test_dsp_init.c` walks `__OSInitAudioSystem`'s exact register
sequence against `MgsMmio`: reset, wait for the mailbox to drain, two ARAM
DMAs each raising 0x20, wait for 0x400 to clear, un-halt, wait for boot mail,
final reset. **Every wait passes.** Whatever stalls the movie, it is not the
DSP register handshake — that part of the model is already good enough for
the SDK's own start-up to complete.

**The value assertion was wrong, not the model.** The test first failed on a
boot-mail comparison: it read `0x8071FEED` where the assertion wanted
`0x80544348`. `0x8071FEED` is the authentic DSP boot-ROM value and
`__DSP_boot_task` asserts on exactly it. The SDK does compare the mail
arithmetically, but **the body of that comparison is empty** — the value is
acted on nowhere. Only bit `0x8000`, "a message is present", is load-bearing.
The assertion now checks the authentic value and says why.

**A duplicate boot-mail path was removed from `mmio.c`.** That wrong reading
had also produced a second mechanism posting `0x80544348` whenever the halt
bit cleared, alongside the pre-existing one that posts `0x8071FEED` on the
**rising edge** of bit `0x800`. The pre-existing one is correct, and its
comment records why the edge matters: arming on "the bit is set" re-posts the
boot message over later task mail. The duplicate armed on every halt-clear
and would have reintroduced exactly that bug. Removed; the `MGS_TRACE_DSP`
control trace that had been sharing the block was kept, on its own.

**What this costs the audio hypothesis.** It does not clear audio — it
narrows it. The register handshake completes, so a stall would have to be
above it: in what start-up does between the waits, or in the timing shift
that running audio for real introduces. The next probe should drive that,
not the registers.

### F187 — the audio DMA engine did not exist, and it is what asks for the next buffer

F186 cleared the DSP register handshake and said a stall would have to be
above it. It is: **the audio DMA engine at `0xCC005030` was not modelled at
all** — the registers were plain storage, and AID, bit 3 of the DSP control
register, was raised by nothing in the whole runtime. Bits `0x20` (ARAM) and
`0x80` (mail) both had a source. `0x08` had none.

**Why that stops a stream rather than slowing it.** `AIInitDMA` programmes an
address and a block count, `AIStartDMA` sets the enable bit, and the hardware
then streams 32-byte blocks into the sample-rate converter, raising AID each
time it latches a transfer. `__AIDHandler` acknowledges that and calls
whatever `AIRegisterDMACallback` was given — which is how the audio manager,
and above it a movie player, is told to produce more sound. With no AID the
callback never runs, the buffer is never refilled, **nothing asks for more
data, and the disc is never read again.** That is the shape of what we see:
reads stop dead rather than tailing off.

**Dolphin as the oracle, and it corrected the obvious guess.** The natural
model is "interrupt on completion". `DSP.cpp` is explicit that it is the
opposite: *"The AID interrupt is set when the fifo STARTS a transfer. It
latches address and count into internal registers and starts copying."* That
is what lets the handler point the registers at the next buffer while the
current one drains, and why one enable plays for ever — on completion the
engine relatches from those same registers and fires again. Had this been
modelled as fire-on-completion, `AIInitDMA` called from inside the callback
would have restarted the current transfer and stuttered every buffer.

Two further details taken from the oracle rather than guessed: the blocks-left
register reads **one lower** than the true count (zero-based; reported
honestly, code waiting for it to reach zero waits for ever on the last
block), and the enable bit is **edge-**, not level-triggered.

**Paced, unlike the ARAM DMA.** An ARAM transfer is a memcpy and completing it
inside the store that starts it is merely generous. An audio transfer
completing early is a lie about how long the sound lasted, and the movie
player takes its timing from these completions. So the engine drains on the
guest's own clock: one 32-byte block is 8 stereo 16-bit frames at 32 kHz,
so 4,000 blocks a second, so 10,125 guest ticks per block off the 40.5 MHz
timebase. It rides `mgs_mmio_advance_ticks`, which already existed and is
already called every step.

**Checked by mutation, not by passing.** `tests/test_ai_dma.c` covers eight
cases and passed first time, which is worth distrusting, so the engine was
broken three ways to confirm the test bites: a wrong block rate, a missing
relatch interrupt, and a level-triggered enable. Each was caught, by the case
meant to catch it. 16/16 tests pass.

**What is still not modelled:** the AI's *other* interrupt. `AISCNT` reaching
`AIIT` (`0xCC006C0C`) raises AIS, which drives `__AISHandler` and the
callback from `AIRegisterStreamCallback`. That register is still plain
storage, so that interrupt can no more fire than AID could. It is the disc-
streaming audio path and this game decodes Vorbis in software, so it may
never be used — but "may" is not "does not", and it is the next thing to
check if the stream still stops.

### F188 — the audio DMA works, the game uses it, and it is NOT the movie blocker

F187 built the engine; this is what the game does with it. Both halves
matter, and the second is the one that costs a hypothesis.

**The game uses it, and the model is right.** A 120M-step run reports:

    audio DMA: 1 transfers, 373714 blocks (93.43s of sound), running;
               interrupts 18686 delivered, 4638 refused

One enable and no more is exactly right — the engine relatches itself, so a
stream that plays for ever shows a single transfer. Two numbers then confirm
the rate independently of anything that was assumed while building it:

  - 373,714 blocks over 18,686 interrupts is **20.0 blocks per buffer**. Twenty
    32-byte blocks is 640 bytes, 160 stereo frames, **5.000 ms at 32 kHz** —
    and 5 ms is the AX callback period the design document names. The buffer
    size was never told to our engine; it falls out of the game's own
    programming against our block rate.
  - 93.43 s of sound in 94.81 s of guest time is 98.5%, the shortfall being
    the boot before the DMA is enabled. Audio streams continuously for
    essentially the whole run.

**And the movie still does not advance.** Disc reads: 115 logged, exactly as
before, stopping at the same `shared/movie.dat + 0x38000`. Not one read more.

**So audio is falsified as the explanation.** It had been down as "the only
live hypothesis" since F184, by elimination — the disc, the threads, the
scheduler, the card, the texture format, the copy encoder, the clock and the
ARAM interrupts having each been cleared. Elimination is how it got there and
elimination is why it was weak: it was never positive evidence, just the last
thing standing. The stream now runs for 93 seconds and starves nothing.

**The game is not stuck, which is the other thing this run says.** A previous
run's step limit landed at pc `0x8004186C`, which resolves to the first
instruction of `GXSetTexCoordGen2` — a busy rendering function, not a spin.
The game is alive and drawing the whole time. So this is **a condition that
is never satisfied, not a deadlock**, and the thing to find is what the movie
player is testing rather than what it is blocked on.

**The calibrated clock is worse, not better, and reproducibly so.** The same
build at `MGS_TICK_RATE=337` — the figure derived from 675,000 ticks per
60 Hz field — made **zero** disc reads and never reached the movie at all,
against 115 at the default 32. That agrees with the earlier sweep (32 → 11
frames, 337 → 13, 2000 → 1) in direction while being far more stark, and it
says something is calibrated against the wrong clock and would have to move
with it. Not the movie blocker either way: at 337 the boot does not get that
far.

**Next: measure rather than hypothesise.** The guest PC profiler
(`MGS_PROFILE=1`, resolved through `tools/resolve-addrs.py`) samples the
translated code, and since most of a run's steps fall after the freeze, a
whole-run profile is mostly the frozen behaviour. That is the next evidence,
and it does not depend on guessing which subsystem to suspect.

### F189 — a sampled log answered a question it could not answer

Chasing where the movie stops, the disc trace showed **one** read of
`shared/movie.dat`, at `+0x38000`. Read plainly that says the game opened the
movie, read one chunk and gave up, which is a very different fault from
reading several and then stopping.

It says no such thing. The trace names the first hundred reads and then
**every twentieth** — deliberately, so a long session cannot flood the log.
`+0x38000` is the eighth 32 KB chunk, so seven earlier reads happened and
fell between samples. The sampled log is not evidence about counts at all,
and it was about to be used as if it were.

`mgs_disc_report` now prints a tally per file at exit — reads, bytes, the
last offset and the furthest point reached, busiest file first. Counting is
a few bytes of state and it removes a whole class of re-run: "what was it
reading, how much, and where did it stop" is answerable from any ordinary
run rather than from knowing in advance to raise the cap.

The general shape is worth keeping: **a rate-limited log answers "what kind
of thing is happening" and never "how many".** Anything counted needs a
counter.

### F190 — the movie player is `mpegGCN.c`, and addresses now resolve to a file when they have no name

Looking for what the movie player *is*, rather than for another subsystem to
suspect: `config/symbols/mgso_pal.rel.files.txt` attributes REL offset
`0x149128` (size `0x5CC`, runtime `0x7F151214`) to **`mpegGCN.c`**, lines 905
and 910. The intro is MPEG, decoded by translated engine code — so the
decoder is the game's own CPU work, not a shim, and it is a file we can name
even though the engine has no public decompilation.

The string `"mpegGCN.c"` is referenced six times in the REL and all six sit
inside that one function, so the file's full extent is **not** established —
only that this function is in it. The two asserts bracket a pair of calls:
one returning zero panics at line 905, one returning negative panics at line
910. Their message strings could not be read statically; they are REL
relocation slots, empty in the image.

**`tools/resolve-addrs.py` now falls back to the file attribution.** It read
only `*.symbols.txt`, and the REL symbol map is 87 lines, so REL addresses in
a profile resolved to nothing at all — a column of bare hex. It now also
reads `*.files.txt` and prints `[mpegGCN.c+0xEC]` where no name is known.
Bracketed deliberately: `OSGetTime` is a name and `[mpegGCN.c+0xEC]` is an
attribution, and the two must not be confusable when the output is pasted
into a note. For reading a profile of Konami's own code — which will mostly
never have names — this is most of the available value.

### F191 — every batch run today hung on exit, and took the end of its own report with it

Three runs sat at 100% CPU long after printing "stopped after 120000000
steps", ignoring both `pkill` and `timeout`'s SIGTERM. A short run reproduced
it in seconds, and the wedged report — which the second signal prints, with a
host backtrace — named it exactly:

    [wedged] last pc 0x80023328 in dispatch / gx idle
    [wedged] host stack:
      ... clock_nanosleep <- nanosleep <- libSDL3 <- main+0x27ae

Not the guest at all. `main` ends by **holding the last frame until the user
closes the window**, which is deliberate and is how the log is meant to be
read after a session. Under `SDL_VIDEODRIVER=dummy` there is no window to
close, so it waits on an event that can never arrive. The existing `headless`
guard did not cover it, because headless was a flag and the dummy driver
reports a window perfectly happily.

**The expensive part was not the hang.** stdout to a file is block-buffered,
so the report was written into a buffer that was never flushed, and killing
the process lost whatever was still in it. That is why one run's report
ended mid-word at `EXI transfers: 4 started, 2` — the counters after that
point existed on no medium at all. Every run today had to be re-run or
read incomplete.

Both fixed: a driver that cannot show a window (`dummy`, `offscreen`) is now
treated as headless and skips the hold, and stdout and stderr are flushed
before the hold begins so a run killed during it still has its whole report.
A 2M-step run now exits in **1.2 s with a complete report** instead of
hanging indefinitely.

**The lesson is about the instrument, not the bug.** Two separate
investigations today read truncated reports without noticing they were
truncated, because a report that stops has exactly the shape of a report that
finished. Anything captured to a file and read later needs flushing at the
point the writing stops, not at the point the process does.

### F192 — three zlib names from a struct layout, and seven symbols whose provenance was uncheckable

The guest PC profile taken for the movie work put **24% of the whole run** in
three addresses inside one REL function at `0x0F02F0`. Following that gave
three names by a route the engine has not been open to before.

**Why the engine was closed to naming, and what opens it.** Konami's code has
no reference binary and no upstream source, so stages 4, 5j and 5l cannot
touch it. But the engine **embeds public libraries**, and a library carries
its own struct layouts — which survive compilation as displacement constants
whether or not a single string does.

`inflate_fast` at `0x0F02F0`. zlib 1.1.x declares it with six parameters, the
fifth and sixth being structs, so `r7` and `r8` are those. Its prologue reads
`z->next_in`, `z->avail_in`, `s->bitb`, `s->bitk`, `s->write`, `s->read` and
then computes `m = q < s->read ? s->read - q - 1 : s->end - q` — its literal
first five lines. **Seven displacements agree with the published layout,
field for field** (`r8` `+0x00 +0x04`; `r7` `+0x1C +0x20 +0x24 +0x28 +0x2C
+0x30 +0x34`). Checked by two further routes: the body holds the LZ77 window
copy, two byte loops with the window base reloaded between them for the wrap,
which no other zlib function has; and zlib **1.2.x**'s `inflate_fast` takes
*two* arguments and could not produce this prologue, so the version is
pinned at 1.1.x.

`inflate_trees_bits` at `0x0F13D0` and `inflate_trees_fixed` at `0x0F15EC`,
by a closed region. `inftrees.c` has exactly four functions in a fixed order,
two already fixed by their error messages, and in the binary all four slots
are **exactly contiguous — every gap zero bytes**. Two anchors, two gaps, two
remaining names. Nothing is preferred; it is counted. `0x38` for
`inflate_trees_fixed` agrees independently: without `BUILDFIXED` its body is
four assignments and a return.

Functions named 1,035 → **1,038**; symbols 1,241 → **1,244**. Recorded as
stage 5p, with a new origin `struct-abi`.

**And the part that was not about names at all.** Adding those symbols meant
declaring their origin, which meant reading the provenance ledger — and the
ledger was **missing five origins already in use**: `callees+callers`,
`sequence`, `sequence+opcode`, `closed-region`, `sdk2004-align`. README.md
claims every symbol's provenance is checkable *per symbol*, and for anything
carrying one of those it silently was not.

`tools/progress.py --check` now verifies it, and on its first run found
**two more** nobody had noticed: `callees+order` (`CARDCheckEx`) and
`callees+varargs` (`fprintf`). All seven are now defined. Confirmed to bite
by renaming an origin to `guessed` and watching it fail.

The general point is the same one F189 made about counting: **a claim nothing
mechanically checks is a claim that drifts.** The origin column had been
carefully filled in for 1,244 symbols and nothing had ever read it back.

### F193 — the movie stops after 0.28% of its file, and almost everything believed about this was wrong

With the read tally (F189) and the resolver (F190) in place, the movie fault
is now measured rather than inferred — and nearly every part of the previous
picture turns out to be wrong.

**`movie.dat` is 94,935,040 bytes. The game reads 262,144 of them.** Eight
reads of 32 KB, `+0x0` through `+0x38000`, reaching exactly `0x40000`, and
then never again. **0.28% of the file.** The round number says what it is: a
**256 KB buffer that fills and never drains.**

**The game is not frozen, and never was.** It renders continuously — 120
frames at 20M steps, 840 at 40M, 1,920 at 120M — loads 8.8 MB of `stage.dat`
across 281 reads, decodes 1,134 textures, makes 2,001 EFB copies and streams
93 seconds of audio. "Stuck" was the display, not the machine.

**The freeze is sharp and it coincides with the last movie read.** The frame
trace samples every 120 frames:

  - frames 0–840: 512×448 CMPR textures, `lit` moving 0% → 8% → 0% → 6% →
    11% → 11% → 9% → 9%. The movie is playing.
  - then the last `movie.dat` read.
  - frames 960–1,920: `lit` **exactly 71% for nine consecutive samples**, and
    the only textures drawn are small ones — 379×39, 177×17, 159×17. A static
    bright image with text over it, which is exactly what is reported from
    the window, and 71% is consistent with the full-screen teal.

**Three things ruled out, two of them things I had argued for.**

  - *Not the task gating.* The engine parks task levels through the mask at
    REL bss `+0x23A38`, and it does gate — but the mask is `0x00000000`,
    everything running, across the whole frozen stretch from frame 960 to
    1,560. It only gates to `0x8` **after** frame 1,560, long after the
    picture stopped. Gating follows the freeze; it does not cause it.
  - *Not audio.* F188 already settled this; the stream runs for 93 s.
  - *Not the disc stalling.* Reads continue the whole time — just not of
    `movie.dat`.

**What it actually looks like.** `mpegGCN.c` does not appear anywhere in the
guest profile: **the movie decoder never runs.** And at the end every guest
thread but the current one is blocked, including one in the overlay's own
address space — `0x7F4A5630`, priority 10, **blocked on queue `0x7F4A595C`**,
which is also REL memory. A reader that fills its buffer and waits to be told
it has been consumed, a consumer that never runs, and a queue nobody posts
to, are the same fact seen three ways.

That is the thing to chase next, and it is a much narrower question than
"why does the movie freeze": **what posts to `0x7F4A595C`, and why doesn't
it?**

**Where the profile does go:** `inflate_fast` 24.0% across three addresses,
then `PSMTXMultVec`, `PSVECAdd`, `PSMTX44Concat`, `OSDisableInterrupts` /
`OSRestoreInterrupts`. Decompression and matrix maths — a game loading and
drawing, which is what the rest of this finding says it is doing.

### F194 — every blocked queue is empty, and three names fell out of asking why

F193 ended on "what posts to `0x7F4A595C`". The thread dump said only
*"blocked on queue 0x7F4A595C"*, which cannot distinguish the two cases that
matter: a queue **with** messages and a thread asleep on it is a scheduling
fault; an **empty** queue is a thread waiting for something never sent. Same
line, opposite bugs.

The queue is now decoded. A thread records the *thread-queue it is parked on*,
which sits inside the `OSMessageQueue` — `&mq->queueReceive` at `mq+8` for a
receiver, `&mq->queueSend` at `mq+0` for a sender blocked on a full one. Both
offsets are tried and only a self-consistent decode is reported, because an
`OSThreadQueue` is also used bare by `OSSleepThread` and that is not a message
queue at all.

**Every blocked thread is on an empty queue:**

    0x7F4A5630  receive on queue 0x7F4A5954:  0 of 1 slots used
    0x80217D58  receive on queue 0x80213364:  0 of 72 slots used
    0x802134E8  receive on queue 0x802133A4:  0 of 72 slots used
    0x80215920  receive on queue 0x80213384:  0 of 72 slots used
    0x8027B640  receive on queue 0x8027B4D8:  0 of 72 slots used

Capacities of 1 and 72 rather than garbage is the decode checking itself.

**And the overlay thread is not the bug.** Following its chain named its own
body. `gcn_worker_loop` (REL `0x804`) is the **outermost frame** on that
thread's stack, so it is the thread body; it marks itself idle, blocks in
`gcn_worker_take_request` (`0x264`) — a genuine `OSReceiveMessage` with the
blocking flag — marks itself busy through `gcn_worker_set_status` (`0x2B0`),
and dispatches on the state the message carried. **A worker idling on an
empty mailbox is healthy.** It is waiting correctly for work nobody sends.

Their shared object's fields fall out with them: `+0x8020` status, `+0x8024`
the request, `+0x8068` the request as latched, `+0x8D94` the queue. Functions
named 1,038 → **1,041**.

**A hazard recorded in passing:** state 3 of `gcn_worker_loop` branches back
with the state unchanged — an infinite loop that never yields and never
sleeps. Nothing has been seen to reach it. It is written down because a
thread that did would look exactly like a hang and would have no stack to
explain itself.

**So the search moves up.** Every consumer is parked and correct; no producer
runs. The main thread is alive and drawing (`GXSetVtxDesc`, called from REL
`0x7F11C45C`) and the game reports nothing — the log carries the SDK's
version banners and **no panic, assert or error at all**, so `mpegGCN.c`'s
asserts at lines 905 and 910 never fired. Nothing has failed; something is
simply never asked for.

### F195 — the whole read path is innocent, and "the decoder never runs" was an over-claim

Chasing F194's "no producer runs", the DVD shims now record **who asked** for
each read — three frames of guest call chain, captured from the link register
and the stack back-chain at the moment of the read. A patched shim is native
and pushes no guest frame, so `r1` still points at the caller's frame and the
ABI's chain gives the path in.

It validates against things already known: `mgso_pal.rel` is read by
`rel_loader_LoadRel`, the audio banks by `__AMPushBuffered`.

**The movie's chain ended in `0x0DEADBEC`, which is ours.**
`MGS_GUEST_RETURN_SENTINEL` is the return address the host pushes when it
calls into guest code, so the walk had reached a host→guest entry. That
identifies the caller exactly: `fn_1_320` is the **DVD completion callback**,
and `fn_1_450` the function that issues each chunk. Confirmed from the
instruction stream rather than inferred — `gcn_stream_issue_read` loads the
callback argument for `DVDReadAsyncPrio` with `addi r30, r3, fn_1_320@l`,
so the function handed to the drive *is* the other one. Named
`gcn_stream_read_done` and `gcn_stream_issue_read`; 1,041 → **1,043**.

`gcn_stream_read_done` on each completion:

    if (desc->0x08 > 0)  gcn_stream_issue_read(desc);      /* next chunk */
    else { ...; if (desc->0x0C) (*desc->0x0C)(ctx, status); }

**So the read path is innocent, and provably.** `DVD reads completed: 406,
callbacks run: 406, reads refused: 0`. The movie's eight chunks completed,
the chain ran to its end, `desc->0x08` reached zero, and the stream's own
completion callback was invoked. Nothing was dropped, no request slot leaked
(the documented "NO FREE REQUEST SLOT" path never fired). The movie player
was handed its data and told so.

**And then the correction, which matters more than any of it.**

F193 and F194 both asserted that `mpegGCN.c` *never runs*, on the grounds
that it appears nowhere in the guest profile. Dumping the engine's task table
shows otherwise:

    level  1  gate 0x00000000
        task 0x811CDDE0  fn 0x7F151214  [mpegGCN.c]  flags 0x000080B0

**The decoder is registered, on a level that is not gated, with flags the
dispatcher does not skip** (it skips only bits 16–19, and `0x80B0` has none).
By the dispatcher's own rules it is eligible and should run.

The profile never said it didn't. `mgs_module_profile_dump(stdout, 30u)`
prints the **top 30 of 2,765 distinct addresses**, and its last row was
already at 0.5% — so anything quieter than that was *invisible, not absent*.
I read a truncated instrument as a fact, which is the same mistake F189
recorded about the sampled disc log and F192 about the unread origin column,
for the third time in one day. The dump depth is now `MGS_PROFILE_TOP`.

**What the task table does show**, taken at the end of a run with the global
mask at `0x8`: levels 2 through 6 carry gate bits `0x19`/`0x1F`, all of which
include bit 3, so **one bit in the mask gates five levels off at once** — and
level 3 holds `0x7F151E70` and `0x7F14D568`, both in the movie's
neighbourhood. That is worth following, but carefully: the freeze begins at
frame 960 while the mask still reads `0x00000000`, so this gating arrives
after the picture has already stopped.

### F196 — the movie deadlock, traced end to end: the mask the movie sets is the mask that stops it ending

Following F195's correction — the decoder *is* registered and eligible — the
whole chain is now read out of the instruction stream, and it closes on
itself.

**The task is in state 1 and cannot leave it.** The engine dispatcher calls a
task with `r3` still holding its list node, so the node *is* the task's
object. Dumping the movie task's node (`MGS_TASK_DUMP=0x7F151214`):

    task 0x811CDDE0  fn 0x7F151214  flags 0x000080B0
      +0x40 00000002 00000001 00000000 00000001

`+0x44` is the state machine's selector and reads **1**; `+0x4C` reads 1,
which state 0 sets on its way out, so start-up completed. The two asserts in
state 0 — `mpegGCN.c` lines 905 and 910, a lookup by id and an attach —
both passed, and the log carries no diagnostic at all.

**State 1 advances only on an event.** It reads `(*bss_55EA4)->0x3C` before
and after calling `fn_1_149048`, and returns unchanged if both are zero.
`fn_1_149048` polls an event list and sets that flag on **event code 1**:

    n = fn_1_F52A8(g->0x38, &list);
    for (i = 0; i < n; ++i, list += 0x10)
        if (list->0x08->0x00 == 1) g->0x3C = 1;

**And the poll is gated by the task mask.** The first thing `fn_1_F52A8`
does:

    r0 = *bss_23A38;        /* the global task mask */
    if (r0 != 0) return 0;  /* no events, at all */

So a non-zero mask does not merely gate task *levels* — it **stops every
event in the engine from being delivered**. The mask reads `0x00000008` at
the end of a run.

**Which closes the loop:**

    mask != 0  ->  no events delivered
               ->  the movie task never sees event 1
               ->  it stays in state 1
               ->  the movie never ends
               ->  nothing clears the mask

F180 recorded that a cutscene sets a bit in this mask and clears it when the
movie ends. Both halves are true, and together they are a deadlock: the thing
that ends the movie is exactly what the mask suppresses.

**What this does NOT yet explain, and must not be glossed.** The picture stops
at frame 960, and the mask does not reach `0x8` until after frame 1,560 — the
transitions logged are `0x2450 → 0 → 0x1 → 0 → 0x8`. So for roughly 600
frames the mask reads zero, events can flow, and the picture is *already*
static. The deadlock above is real and certainly explains why nothing ever
recovers, but something stops the picture before it is entered. Treating this
finding as the whole answer would repeat exactly the mistake F195 corrected.

**Next:** what writes `0x8` into `bss_23A38`, and what happens at frame 960.

### F197 — the task dump already existed, and the mask has exactly one writer in the binary

**I rewrote a tool this project already had.** Chasing F196 I wrote an engine
task-table dump in `main.c` — twelve levels, stride `0x44`, head at `+0x00`,
gate at `+0x40`, node fields `next`/`func`/`flags`, skip on flag bits 16–19.
`host/heaps.c` has held `mgs_dump_tasks` with every one of those constants,
derived the same way from the same dispatcher, for some time. It was already
being called; both dumps printed, one above the other, before I noticed.

The duplicate is removed and what was genuinely new — dumping a selected
task's own node — moved into the existing function as `MGS_TASK_DUMP`. Ninety
lines deleted. The lesson is cheap to state and was evidently not cheap to
learn: **search `host/` for the instrument before writing it.** This is the
second thing today that existed and was not looked for, after the `--help`
text that already documented the module search.

**The mask has one writer, and it writes zero.** `bss_23A38` is referenced 212
times in the overlay's text: 106 `lis`, 98 `lwz`, 5 `addi` that take its
address, 2 `lwzu`, and **exactly one `stw`** — in `fn_1_F38CC`, the table
initialiser, which lays out the twelve level entries (taking each level's
gate from a halfword table at `data_D5E0`) and ends by storing **zero**. All
five `addi` sites only read through the address afterwards.

So nothing in the binary writes a non-zero value to that word through its
label, and yet it observably takes `0x2450`, `0x1` and `0x8` during a run.
It must be written through a computed pointer — and the addressing is
visible: the table is 12 × `0x44` = `0x330` bytes, so the mask sits exactly
`0x330` past the table base, and the binary contains a read at
`0x330(r26)` that confirms it. There are 40 stores sharing that
displacement, far too many to read.

So `MGS_WATCH=<guest address>` now reports every change of a guest word with
the pc and link register of the step that changed it — checked **every**
step, which is what makes the answer exact instead of "somewhere in the last
512". The mask lives at `0x7F4BD5D8`, which the task dump now prints for
exactly this purpose.

### F198 — four names out of the deadlock, and the watch confirms the initialiser

Tracing F196's chain named every function in it. All four are described
rather than guessed at, and each claim is checkable against the disassembly:

  - **`mpeg_movie_task`** (`0x149128`) — the function the task table holds for
    the movie; the entry literally reads `fn 0x7F151214`. A state machine on
    its node's `+0x44`: 0 opens the movie by id (the `mpegGCN.c` asserts at
    lines 905 and 910 guard the lookup and the attach), 1 waits, 2 plays,
    3 ends.
  - **`mpeg_poll_stream_events`** (`0x149048`) — what state 1 calls; sets the
    flag it is waiting on when an event of code 1 arrives.
  - **`gcn_event_poll`** (`0x0F52A8`) — the engine's event pump, whose first
    act is to read the global task mask and **return zero events if it is
    non-zero**.
  - **`gcn_task_table_init`** (`0x0F38CC`) — lays out the twelve level entries
    and is the only `stw` to the mask anywhere in the overlay.

1,043 → **1,047 functions named.**

**The watch confirms the static reading exactly.** Watching `0x7F4BD5D8` every
step, the only two changes in a boot are:

    0x00000000 -> 0x00002450  at memcpy, from rel_loader_LoadRel+0x94
    0x00002450 -> 0x00000000  at REL 0x0F38F8  (gcn_task_table_init)

The first is not a write by game logic at all — it is the **loader copying
the module image into memory**, so `0x2450` is simply what that word holds in
the file. The second is the initialiser zeroing it, from the one `stw` the
static scan found. Two instruments, one answer, and the earlier reading of
`0x2450` as "a cutscene parked the game" was wrong: nothing had parked
anything, the module was still being loaded.

**And the watch costs nothing measurable** — 3M steps run in 0.323 s without
it and 0.332 s with it, so it can be left on for any investigation.

### F199 — a second `static` of the same name silently aliased the first, and broke the boot

The watch from F197 broke the game, and it took a bisect to see it because
nothing in the diff looked wrong.

**Symptom:** a 120M-step run that had been reaching frame 1,920 and reading 9
files reached **frame 0 and read 1 file**. I first blamed the watch's cost —
wrongly: 3M steps take 0.323 s without it and 0.332 s with it. Then
nondeterminism — also wrong. A clean baseline with the watch *disabled* was
equally broken, which is what pointed at the code rather than the load.

`git checkout <prev> -- host/module.c` restored it; reverting `main.c` and
`heaps.c` did not. So: `module.c`, and one change in it.

**Cause.** `module.c` already had `static uint32_t s_watch_addr;` holding the
address of **`OSLink`**, watched so the host can read the overlay's `.bss`
base out of its arguments — which `s_engine_bss`, and therefore everything
this session has learned about the engine, depends on. My memory watch
declared `static uint32_t s_watch_addr;` **again**.

In C two file-scope statics of the same name are **tentative definitions of
one object**. It is legal, it is silent, and `-Wall -Wextra` say nothing. So
the new code set OSLink's address from `getenv("MGS_WATCH")` at the top of
every run loop — to zero when unset — the overlay's globals were never
located, and the boot stopped after loading the module. Renamed to
`s_memwatch_addr`; frame 840 and 7 files at 40M steps, matching the baseline
exactly.

**Checked from now on.** `-Wredundant-decls` catches it, but this project
deliberately declares a function immediately before defining it (12 such in
`module.c` alone), and that flag objects to every one — so it is not
imposed. `tools/check-duplicate-statics.py` narrows the check to what bites:
file-scope **variables**, never functions. It runs in `ctest` (17 tests now)
and found a second, pre-existing instance on its first run —
`s_pe_seen`/`s_pe_sent` declared twice in `host/interrupt.c`, harmless
because both sites wanted the same counters, but the same hazard.

**The pattern, for the third time today:** I read a symptom as a property of
the program (first "too slow", then "nondeterministic") when it was a
property of a change I had just made. The bisect took four minutes and should
have come first.

### F200 — the park is traced to one NULL pointer, six links back from the frozen picture

With the watch working on a healthy build, the mask's whole history in a boot
is four game-logic changes, each with its writer named:

    0 -> 0x2450   memcpy, from rel_loader_LoadRel+0x94     (the loader)
    0x2450 -> 0   the module's own init                     (table set-up)
    0 -> 0x1      REL 0x0F45EC, called from REL 0x24A300
    0x1 -> 0      REL 0x0F4600, called from REL 0x249E10
    0 -> 0x8      REL 0x0F45EC, called from REL 0x249CB4    <- never cleared

**Why the static search found only one `stw`.** `gcn_task_mask_set` and
`gcn_task_mask_clear` are three instructions each and use **`lwzu`** — load
with update — so the mask's address is left in the base register and the
store is `stw r0, 0x0(r4)`, naming no label. That is exactly the two `lwzu`
hits the earlier classification counted and dismissed. A label search can
only find what a label names.

**The park is a deliberate `if`, and its condition is a null check.** The
function that sets `0x8` also clears it, a few instructions apart:

    r0 = *(ctx + 0x25E8);
    if (r0 == 0)  gcn_task_mask_set(8);      /* park   */
    else          gcn_task_mask_clear(8);    /* unpark */

So nothing is stuck or missed: the engine parks because **that pointer is
NULL**, and would unpark the moment it were not.

**And the pointer is NULL because an acquire returned NULL.** It has exactly
two writers in the overlay: one zeroing it at init, and one real setter —

    if (ctx->0x25E8) release(ctx->0x25EC);
    ctx->0x25E8 = fn_1_1323C4(ctx->0x25EC, 2);

`fn_1_1323C4` takes a lock, returns **0 immediately if `obj->0x34` is
non-zero**, otherwise searches an array of slots (base `obj->0x14`, count
`obj->0x24`) for one whose first word is `0` or `0xFF`, and returns 0 if it
finds none. Either way the caller stores NULL and the game parks.

**The chain, end to end:**

    fn_1_1323C4 returns NULL
      -> ctx->0x25E8 stays NULL
      -> gcn_task_mask_set(8): the game parks
      -> gcn_event_poll returns zero events while the mask is non-zero
      -> mpeg_poll_stream_events never sees event code 1
      -> mpeg_movie_task never leaves state 1
      -> the movie never ends, so nothing clears the mask

`gcn_task_mask_set` and `gcn_task_mask_clear` named; 1,047 → **1,049**.

**Next, and it needs a runtime answer rather than more reading:** which of
`fn_1_1323C4`'s two failure routes is taken — the `obj->0x34` busy check or
an exhausted slot array — and what `obj` is. That wants a facility to trace
one guest function's arguments and result, which the host does not yet have
in a general form.

**Still unexplained, and still not to be glossed:** the picture stops at
frame 960 and this park arrives after frame 1,560. This is why nothing
recovers. What stops it is still open.

### F201 — the pool that starves the movie, and 1,101 entries that are never freed

F200 ended needing a runtime answer the host could not give. It can now:
`MGS_TRACE_FN=<addr>[,...]` reports a guest function's arguments, its caller
and its return value, and `MGS_TRACE_FN_MAX=0` counts without printing.
Validated first on something already known — `DVDReadAsyncPrio(fileInfo,
0x7F49CBC0, len, off) -> 1` from `gcn_stream_issue_read`, offsets advancing —
and confirmed not to perturb: 40M steps give a bit-identical stop with and
without it.

**The pointer whose NULL parks the game goes NULL exactly once.** Watching
`ctx + 0x25E8` (the parker task's own node, `0x8102D400`, plus `0x25E8`):
**720 changes, all to fresh non-NULL values, then one to zero — the last
event in the run.** The addresses climb monotonically, `0x81741B30` through
`0x8176D110`, and never repeat.

**All of it comes from one shared pool.** Every caller passes the same object
`0x7F4EF794` and distinguishes itself by a `kind`:

    caller       kind          ok     NULL
    0x7F251B84   0x00000002   719      383     <- the movie's context
    0x7F01B0D4   0x0002..7 0004  0     5505    <- five kinds, never any hit
    0x7F01CE60   0x00010004     0     1101
    0x7F0110F4   0x00000001  1101        0     <- never fails
    (four more, 28 calls between them)

The `0xNNNN0004` kinds returning NULL 6,606 times out of 6,606 are not a
fault — they are lookups for things that are simply not there, and their
callers carry on. **79% of all acquires returning NULL is normal**, which is
worth stating because it is exactly the kind of number that reads as a
catastrophe.

**The accounting.** 8,839 acquires, of which **1,848 succeed**; `gcn_pool_free`
is called **747** times. The difference is **1,101** — precisely the number
of successful kind-1 acquires by one caller, `fn_1_8FE8`.

**And that caller has a second disposal path which does not free.** It
acquires, then branches:

    if (fn_1_1325D4(pool, e))  gcn_pool_clear_entry_flag(pool, e);
    else                     { obj->0xB0 = 1; gcn_pool_free(pool, e); }

`gcn_pool_free` writes 0 to the entry's header, marking it free.
`gcn_pool_clear_entry_flag` clears **bit 7** of that header and stores it
back — the entry stays allocated. So entries taken down the first branch are
never returned to the pool, and the pool is finite.

**What this does and does not establish.** The arithmetic closes exactly, and
the alternative path demonstrably does not free. What is *not* established is
that nothing else frees them — a consumer elsewhere could, and if it is
itself parked this is another loop of the same shape as F196. That has to be
measured, not assumed, and it is the next thing to do.

Three names, all describing checkable behaviour: `gcn_pool_acquire`,
`gcn_pool_free`, `gcn_pool_clear_entry_flag`. `fn_1_1325D4` is deliberately
left unnamed — it returns `*(entry - 0xC) - 0x10` in three instructions and
what that field means is not established; a name would be a guess dressed as
a finding. 1,049 → **1,052**.

**One unexplained run.** A 120M-step run stopped at 45.5M with `no code for
that address, pc = 0xCD92A240` — the guest branching to garbage. It did not
reproduce: two identical runs afterwards were bit-identical and healthy, so
the port is deterministic in this configuration. Most likely a leftover
process from a `pkill` overlapping the new one. Recorded rather than
explained.

### F202 — correction: the pool is a record ring, nothing leaks, and the movie simply ran out of data

**F201's central claim is wrong.** I read `gcn_pool_acquire` as an allocator
and built an accounting argument on it — 1,848 successes against 747 frees,
a 1,101 difference matching one caller exactly, a second disposal path that
"does not free". The arithmetic was right and the interpretation was not.

Reading the search loop to the end shows what it actually does:

    r0 = *cursor;                        /* the entry's TAG */
    if (r0 == 0xFF) cursor = pool->0x8;  /* wrap marker: back to the base */
    if (r0 == 0)    ...                  /* empty slot */
    if (r0 == r28)  -> match             /* r28 is the KIND argument */

`cmpw r0, r28` against the second parameter. **It finds a record of a given
kind; it does not allocate one.** So:

  - "79% of acquires return NULL" is not exhaustion, it is **"no record of
    that kind is waiting"** — the ordinary answer to a poll.
  - The five `0xNNNN0004` kinds failing 6,606 times out of 6,606 are polls
    for records that were never produced, which their callers expect.
  - `gcn_pool_free` marks a record **consumed**, not freed to an allocator.
  - `gcn_pool_clear_entry_flag` changes a record's tag rather than removing
    it, so a later search for the original kind misses it.
  - **There is no leak.** Acquires and frees were never required to balance,
    because they are poll and consume, not allocate and free.

**What the numbers do say, read correctly.** The movie polls kind 2 and gets
a record 719 times, then never again; the addresses climb monotonically
through ~178 KB and never wrap. 178 KB of records against the **256 KB** of
`movie.dat` the game read (F193) is the right order: those records *are* the
parsed movie data. **The movie consumed everything it had been given and
then found nothing waiting** — which is exactly what an empty stream buffer
looks like from the consumer's side, and not a fault in the pool at all.

So the chain does not terminate in a leak. It closes on itself:

    no more movie data read
      -> no kind-2 records produced
      -> gcn_pool_acquire finds none, ctx->0x25E8 = NULL
      -> the game parks (mask |= 8)
      -> gcn_event_poll returns nothing while the mask is set
      -> mpeg_movie_task never leaves state 1
      -> the movie never ends, nothing clears the mask, nothing asks for
         more data

**How the error was made, because it is the same one twice.** F195 corrected
"the decoder never runs", read off a truncated profile. This is the same
shape: a name I chose — `acquire` — carried an assumption, and I then fitted
an accounting argument to it without reading the loop to its end. The tell
was in the data the whole time: an allocator whose returns climb
monotonically and never reuse a freed address is not an allocator.

**What is still true from F201:** the tracer works and does not perturb, the
pointer goes NULL exactly once as the last event, all callers share one pool,
and the counts are accurate. Only the reading of them changes.

**Next:** what produces kind-2 records, and what it needs in order to produce
more. That is the same question as "why is no more of `movie.dat` read", now
reached from the other end.

### F203 — the tracer cannot see host-invoked callbacks, and the event table is keyed

**A limitation worth knowing before trusting `MGS_TRACE_FN` again.** Tracing
`gcn_stream_read_done` reports **0 calls** in a run where the host's own
counter says **406 DVD callbacks ran**. Both are right: the tracer matches
`pc` as the run loop dispatches, and a callback the HOST invokes goes
straight into guest code — the path that leaves `0x0DEADBEC` on the stack
(F195) — without passing that check.

So the tracer sees calls the *guest* makes and is blind to calls the *host*
makes. That briefly looked like a contradiction in the evidence: the disc
tally said the movie's last read was issued from `gcn_stream_read_done`,
while the tracer said that function never ran. The tally was right.

This also explains a number that looked wrong: `gcn_stream_issue_read` is
called **151** times and all 151 come from `fn_1_548`, against 406 actual
reads. The rest are issued from inside completions, which is exactly the
invisible path.

**The stream is not at fault, again.** The movie's transfer was started
once, ran its chain to the end, and its `desc->0x08` reached zero — it asked
for 256 KB and got 256 KB. Nothing dropped it and nothing is waiting on it.

**The event table, read out.** `gcn_event_poll(key, &out)` selects one of
several `0x1010`-byte tables (`bss_253F0`, indexed by `bss_253E0`), takes a
count from `+0x800`, and walks `0x10`-byte entries for one whose first word
equals `key` — setting a halfword flag at `+0x6` and returning a count from
`+0x4`. Its first act remains the gate: **zero events while the task mask is
non-zero**.

So the shape is the same as the record ring: a keyed table, polled. Nothing
posts *to the movie*; the movie looks for a key and finds nothing.

**Where this leaves the movie, stated carefully.** At the moment the picture
stops — frame 960 — the mask reads **zero**, so `gcn_event_poll` is working
and the gate is not yet closed. The event with code 1 is simply never
produced. The mask deadlock of F196 is therefore a **consequence** that
arrives ~600 frames later and makes the state permanent; it is not what
stops the picture. That distinction has been held open since F196 and the
evidence keeps supporting it.

**What is now excluded, with measurements rather than argument:** the disc
(406/406 reads, 406/406 callbacks), the read chain, the stream descriptor,
the record ring (not an allocator, nothing leaks), the audio DMA (93 s of
sound), the task registration (registered, ungated, not flag-skipped) and
the task mask at the moment of the stall (zero). What remains is the
producer of kind-2 records and of event code 1 — the same question from two
directions, and both reduce to: **the game read 256 KB of a 90 MB movie and
nothing ever asked for the next byte.**

### F204 — the tracer now sees host-invoked calls, and the port is NOT reliably deterministic

**The tracer's blind spot is fixed.** F203 found that `MGS_TRACE_FN` missed
every callback the host invokes, because it matched `pc` only in the run
loop while `mgs_module_call_guest` has a dispatch loop of its own. The check
is now one function, `fntrace_step`, called from **both** loops — factored
rather than copied, which is the mistake F197 recorded.

**And a determinism claim of mine has to be withdrawn.** F201 said "the port
is deterministic in this configuration" on the strength of two identical
runs. Three runs of 120M steps, same build, same arguments, nothing else
running:

    run 1: 406 reads, 0 refused   |  run 2: 406 reads, 0 refused
    run 3: 407 reads, 1 refused

Runs 1 and 2 are **byte-identical logs**. Run 3 is not. Two matching runs
were never evidence of determinism; they were evidence that two runs matched.

**Where the divergence is.** The difference is exactly one refused read — a
`DVDReadAsyncPrio` that found no free request slot, was refused, and was
retried, costing one extra read. So the varying quantity is **whether a slot
is free at the moment a read is issued**, and that is worth stating plainly
because the DVD model is *designed* not to have this problem:
`mgs_dvd_drain` completes a request when the **guest clock** passes its
`ready_tick`, and busy-waits for the host worker if the bytes are somehow not
there yet, precisely so that host thread scheduling cannot be observed. That
design is right and the divergence means something escapes it. **Not yet
found, and not guessed at here.**

**Why this matters beyond tidiness.** `twin-snakes-native-port-design.md`
makes replay against Dolphin the oracle for divergence, and `module.c`'s own
comment says the retrace cadence is tied to steps rather than wall clock
because "a replayed run must be reproducible, and wall clock is not." A run
that is *usually* reproducible is worse than one that never is: it invites
exactly the reasoning that wasted time today, where a diverged run was read
as a behavioural fact. Two earlier oddities now have a candidate explanation
rather than the ones I gave them: a 120M run that stopped at 45.5M with a
branch to garbage (F201, blamed on a leftover process), and a run with only
65 reads and 5 files that I read as tracer perturbation.

**Practical consequence, applied from here on:** a single run is not
evidence. Anything load-bearing gets repeated, and a difference between two
runs is the first thing to suspect, not the last.

### F205 — the diverging runs differ at a failed disc read, and the tally I added today is a data race

Rather than reason further about F204's nondeterminism, the two diverging
120M logs were **diffed**. They are identical until one line:

    [dvd] READ FAILED: 32768 bytes at 0x2B642960 -> 0x7F49CBC0

Everything after — one extra read, one refusal, small differences in
interrupt and MMIO counts — follows from the guest retrying it. So the
question narrowed from "why do runs diverge" to "why does *this read*
sometimes fail", which is a far better question.

**The offset is valid.** Parsing `sys/fst.bin`: 1,641 files spanning
`0x20BF24`–`0x499C0000`, and `0x2B642960` (694.3 MB) sits inside
`stage.dat` at relative `0xBB2C000`, comfortably within its 198,715,392
bytes. So this is not a read outside the FST; it is a read that should have
worked.

**And on that path I had introduced a data race today.** `read_job` runs on
the **worker pool**, and it calls `mgs_disc_read_abs`, which calls the
per-file `disc_tally` added in F189. That tally did an unguarded
search-then-insert — `++s_tally_n` and a `strcpy` into a fixed table — from
however many worker threads were reading at once. Every field of it raced,
and two threads passing the bounds check together could step past the end of
the table. The sampled trace's `static unsigned long seen` counter raced too,
though that one predates today and is merely a miscount.

Now locked. Reads are few — about four hundred in a boot — so a plain mutex
costs nothing next to the file I/O it guards. The attributions still come out
right (`rel_loader_LoadRel` for the overlay, `__AMPushBuffered` for the audio
banks).

**The requester is approximate under concurrency, and now says so.** It is
written on the guest thread as a read is issued and read on a worker thread
as that read runs, so with several in flight a line can name the wrong
issuer. The guest issues reads one at a time and they usually complete before
the next, which is why the checkable cases were right — but a single line is
a strong hint, not proof.

**`READ FAILED` now says why.** The message gave no reason, and "a race on a
file handle", "a short read" and "an offset in no file" want completely
different fixes. `mgs_disc_read_abs` now distinguishes a read that fell
inside a file and could not be opened or sought from one that lies in no file
at all — the latter being expected rather than a fault, since the boot
header, bi2, the apploader and the FST live in `sys/` on an extracted disc.

**Not yet established:** whether the race *caused* the failed read. It is the
right thing to fix either way, and five repeat runs are in flight to see
whether determinism returns. An out-of-bounds `strcpy` can corrupt anything,
so it is a credible cause — but "credible" is not "shown", and today has
already produced three corrections from exactly that gap.

### F206 — determinism returns with the race fixed, and the pool's producer is found

**Five 120M runs after the lock: byte-identical logs, 406 reads, 0 refused,
0 failures.** Before the fix, one run in three diverged.

**Settled: fifteen consecutive byte-identical runs.** Five, then ten more,
all 406 reads, 0 refused, 0 failures, one distinct log between them. Against
the observed one-in-three divergence rate that is **P = 0.0023**. It agrees
with the other two measurements too: `MGS_JOBS=1`, which removes the
concurrency entirely, gave three identical runs, and the race was in code
reachable *only* from worker threads.

A caution kept for the record: the first pass at this comparison reported
"3 distinct of 5" and was wrong — the glob had swept up two stale logs from a
previous day, and a third file was still being written when it was hashed.
Nothing was amiss; the measurement was. **Check what the glob matched and
that each run finished before comparing anything.**

**What this cost and what it bought.** The race was mine, introduced this
morning in the read tally (F189) — an instrument added to answer a question
about the movie went on to corrupt the runs used to answer it. The failure
mode is worth remembering: *a diagnostic that writes shared state is part of
the program*. Every measurement taken between F189 and F205 is suspect, and
the ones that mattered were re-taken.

**Meanwhile, the record producer is identified.** `fn_1_132368` is a
lock-wrapped call to `fn_1_1321A8`, which is the pool's **pump**:

    if (pool->0x38) return 1;            /* already busy */
    fn_1_9C8();                          /* the early-REL file service */
    if (pool->0x30 || pool->0x34) { pool->0x00 = 0; return 0; }
    ...

Two things connect here. `pool->0x34` is **exactly the flag that makes
`gcn_pool_acquire` return NULL immediately**, so the pump and the consumer
share a stop condition. And `fn_1_9C8` sits in the early-REL cluster with
`gcn_worker_loop`, `gcn_stream_issue_read` and the rest of the file service —
so the pool is fed from the stream, which closes the loop the other way:
records exist because data was read, and data is read because records are
wanted.

**Next**, once the trials land: trace `fn_1_132368` (runtime `0x7F13A454`)
and watch `pool->0x30`, `pool->0x34` and `pool->0x38` at `0x7F4EF794`. The
question is whether the pump stops being called, or starts returning early —
and which of those three flags is set when it does.

### F207 — nothing asks for more data because the ring head is pinned, and the refill fires twice in a whole run

On runs that now reproduce, the producer side measures cleanly.

**The pump is healthy.** `fn_1_132368` → `fn_1_1321A8` is called **2,204
times** across a run and returns 1 every time. I first read that as its early
exit — `if (pool->0x38) return 1` — and said so. Wrong: `.L_1322BC: li r3,
0x1` is the **normal** exit, reached after the work is done. Two independent
checks agree that it is the normal path: `pool->0x38` is watched for a whole
run and is **zero throughout** (its only two changes are the loader's memcpy
and the module's own init), and `fn_1_9C8`, the file-service call the pump
makes, is traced returning **0 on 2,356 of 2,359 calls**. Neither early exit
is being taken. *(Third time today a partial read of a function produced a
confident wrong answer; the fix each time was to read it to the end.)*

**The refill is requested twice.** Just before that normal exit sits the
trigger:

    free = pool->0x0C - fn_1_13200C(pool, pool->0x14, pool->0x20);
    if (free > pool->0x0C / 3) fn_1_132030(pool);    /* ask for more */

`fn_1_132030` is traced at **2 calls in a 120M-step run**, both on the movie's
pool, both returning 0. So the machinery that would fetch the next chunk of
`movie.dat` is not broken, not blocked and not gated — **its condition is
simply never true again.**

**Why `free` never recovers: the ring reclaims from the front only.** The
pump's scan walks from `pool->0x14` and stops at the **first record that is
still occupied**, skipping only a run of consecutive freed ones before it:

    r7 = pool->0x14;
    if (*r7 == 0xFF) r7 = pool->0x8;   /* wrap marker */
    if (*r7 != 0) goto done;           /* STOP - occupied */
    r7 += *(r7 + 4);                   /* skip a freed record */
    ...
    done: pool->0x14 = r7;

That is textbook head-of-line blocking. **One record at the head that nobody
consumes pins the head for ever**, however much is freed behind it, `free`
stays under a third of capacity, and the refill never fires again.

**So the chain finally has a mechanism at its far end**, and it is not
circular after all:

    a record at the ring head is never consumed
      -> the head cannot advance, so free space never recovers
      -> the refill condition (free > capacity/3) is never true
      -> no more of movie.dat is read
      -> no new kind-2 records
      -> the movie task starves, the game parks, the mask deadlock closes

**Next, and it is now a small question:** which record is at the head, and
who was supposed to consume it. `gcn_pool_clear_entry_flag` is the suspect
disposal — it clears bit 7 and leaves the record **occupied**, which is a
re-queue rather than a consume, and `fn_1_8FE8` takes that branch on almost
every one of its 1,101 kind-1 finds. Whether those are legitimately still
wanted or are the thing that pins the head is the measurement to make.

### F208 — the record that pins the head, found: a kind-1 record taken and put back 1,085 times

F207 asked which record sits at the ring head. Watching the head cursor
`pool->0x14` at `0x7F4EF7A8`: it advances **456 times** and then stops for
good at **`0x817789F0`**.

Watching that address — the record's tag — gives the answer outright. It
changes **2,171 times**, and every change is one of two values:

    0x817789F0: 0x00000001 -> 0x00000081   at 0x7F13A510   (gcn_pool_acquire)
    0x817789F0: 0x00000081 -> 0x00000001   at 0x7F13A5F0   (gcn_pool_clear_entry_flag)

`0x7F13A510` is inside `gcn_pool_acquire`, whose match path does
`ori r0, r0, 0x80` — marking the record taken. `0x7F13A5F0` is inside
`gcn_pool_clear_entry_flag`, which clears exactly that bit. So one **kind-1**
record is found, marked, and put straight back, **1,085 times over**, and
never consumed. Its tag never reaches 0, so the head never passes it, so
`free` never recovers, so `movie.dat` is never read again.

**Who does it and why.** `fn_1_8FE8` polls kind 1 and branches on
`fn_1_1325D4(pool, e)`, which returns `*(e - 0xC) - 0x10`. `e` is the payload
(`entry + 0x10`) and `entry + 4` is the record's length — the same field the
ring's own scan advances by — so that function returns the **payload size**,
and the branch reads:

    if (payload size != 0)  put it back;          /* someone else's to take */
    else { obj->0xB0 = 1; free it; }              /* empty: consume, mark done */

So `fn_1_8FE8` is a **peek**, not a consumer: it is looking for an *empty*
kind-1 record — an end-of-stream marker, on the evidence of the flag it sets
— and correctly leaves any record that still has data in it. It is behaving
as written.

**Which makes the open question exact:** a kind-1 record with a payload is
waiting at the head of the ring for a consumer that never comes. **Who is
supposed to take kind-1 records with data?** The other kind-1 callers seen
are `fn_1_8FE8` itself at two sites and `fn_1_912C` once, so the real
consumer is either one of those on a path not taken, or something that never
runs. That is the next thing to find, and it is now a single question rather
than a subsystem.

### F209 — the whole chain, measured end to end, from a frozen picture to two messages that were never sent again

Every link below is a **counter from a reproducible run**, not an inference.
Read upwards it is the freeze; read downwards it is the cause.

    fn_80054D14 (main.dol) sends to the drain's queue ......... 2 sends
      of 60 calls, both from 0x80054F44, none after
    fn_1_8D98 polls that queue (OSReceiveMessage, non-blocking)  1,101 calls
      succeeds ............................................... 2 times
    the destination fill pointer obj->0xBC wraps ............. 2 times
      0 -> 0x2000 -> .. -> 0x8000 -> 0, twice; then it runs
      PAST 0x8000 to 0x10000 and never wraps again
    fn_1_8FE8's space check (0xB8 - 0xBC >= 0x40) then fails
      so kind-1 records are peeked and put back ............... 1,085 times
      one record's tag oscillating 1 <-> 0x81 ................ 2,171 changes
    the ring head pool->0x14 advances ........................ 456 times
      then stops for ever at 0x817789F0
    free space never exceeds capacity/3, so the refill fires .. 2 times
    so movie.dat is read ..................................... 8 times
      262,144 of 94,935,040 bytes: 0.28% of the file
    so kind-2 records are produced ........................... 719, then none
    so mpeg_movie_task never leaves state 1
    so the game parks: gcn_task_mask_set(8) .................. once, never cleared
    so gcn_event_poll returns zero events while the mask is set
    so the movie never ends and nothing clears the mask

**The one number that is not a stall.** `fn_1_8FE8` peeking and replacing a
record 1,085 times is not a bug — it is a poll for an *empty* kind-1 record
(an end-of-stream marker) and it correctly leaves records that still hold
data. Everything in the middle of this chain is code working as written,
waiting on something upstream.

**Where it actually breaks: two messages.** `fn_80054D14` builds a message
with a type byte at `+0x17` — exactly the field the drain reads back with
`lbz r0, 0x17(r3)` — and posts it to the queue the drain polls. It is called
**60 times and sends twice.** After that the drain has nothing to take, the
buffer is never emptied, and the ten links above follow mechanically.

**Not yet known:** which condition inside `fn_80054D14` gates the send, and
whether the two it did send are the expected number. It returns 1 once and 0
on the other 59 calls. It sits just below `sd_mem.c` in the file
attributions, so it is Konami's own streaming layer rather than the SDK.

**A note on what made this possible.** None of it could be measured until the
runs reproduced (F206). Every count here comes from runs that are
byte-identical to each other, and several of these numbers were taken twice
for that reason.

### F210 — the far end is `sd_stream2.c`'s own thread, and it is not starved

Following F209's last link upwards lands in Konami's streaming layer, named
by the file attributions that bracket it: `sd_stream2.c` (lines 730, 1002,
1234 and 1607 sit either side of these functions).

    fn_80055264   0x80055264, size 0x4C4   the stream THREAD's body
    fn_800534D4                            makes a slot ready (state 0 -> 1)
    fn_80054D14                            posts a ready slot to the drain
    fn_80052FAC                            marks a slot done (state 2 -> 3)

**The slot lifecycle, watched directly** at `0x8027ADD4` — five changes in a
whole run and no more:

    00 00  ->  01 01   both slots made ready, once, by fn_800534D4
    01 01  ->  02 01   slot 0 posted to the drain
    02 01  ->  02 02   slot 1 posted
    02 02  ->  03 02   slot 0 marked done
    03 02  ->  03 03   slot 1 marked done

Two slots, made ready **once**, used once, done. Nothing returns them to
state 0 or 1. Two slots of 32 KB is 64 KB, which is exactly where the
destination fill pointer stopped (`0x10000`) — the numbers agree from both
ends.

**`fn_80055264` is a thread, and it is alive.** It is entered once, as
threads are, with zero arguments and its own address in `r6` — the shape of
an `OSCreateThread` trampoline. It is the thread the dump has been showing
all along: blocked in `OSReceiveMessage` with `0x80055708` on its stack,
which falls inside this function's `0x4C4` bytes.

**It is NOT starved of work, which kills the obvious theory.** Its queue
takes **30 messages** in a run, from four sites — `0x800525CC` (2),
`0x80052FF8` (2), `0x800566E4` (13) and `0x80056790` (13). So the thread
wakes, works, and sleeps again repeatedly. What it does *not* do is make a
slot ready more than once.

**An error of mine, of a kind now familiar.** I first measured **zero** sends
to this queue and nearly wrote that down as the root cause. I had grepped
`0x8021336C`, which is the thread-queue *inside* the `OSMessageQueue` — the
address a thread records — where the queue object itself is `0x80213364`,
eight bytes lower. The decoder added in F194 prints both and I used the wrong
one. **The right answer, 30, is the opposite of the wrong answer, 0.**

**So the question is now: which of those 30 messages should have asked for a
refill, and why it never arrives.** `0x80052FF8` is the site that marks a
slot done, so the natural design — "slot finished, prepare another" — has a
candidate path. Whether it is taken, and what it depends on, is the next
measurement.

### F211a — there was a FOURTH copy, in docs/decompilation-process.md

Ben's follow-up — "that's on the main readme upstream" — sent me looking
again, and a sweep for the old figure found one more: the summary table in
`docs/decompilation-process.md`, reading **1,006 named / 5.4%** and **198 SDK
entry points** against the maps' 1,052 and 205, plus per-module rows saying
`main.dol` 997/1,818 and `mgso_pal.rel` **9**/16,667 where the REL now holds
27. Its prose was stale to match ("why 5.4% is the least useful number",
"the remaining 237 need dynamic information" where 336 − 205 = 131).

All corrected, and `--check` now verifies this table too — confirmed to bite.
Four places carried these figures: HANDOFF, MILESTONES, README and this. The
first two were checked and stayed right; the two that were not both drifted.
**Three copies of a number are three chances to be wrong**, so each is now
compared against the maps rather than against each other.

**And a near-miss worth recording.** Computing the per-module rows, I wrote my
own counting loop and got `main.dol` **1,026** where `progress.py` reports a
total of 1,052 with 27 in the REL — 1,026 + 27 = 1,053, one too many. My
regex accepted a symbol whose size column is `?`; `progress.py`'s requires
`0x`. I had just written a commit message warning that lifting a generator's
logic out to call it twice is how a generator and its check stop agreeing,
and then immediately did it by hand. The fix was to use its own regex, which
gives 1,025 + 27 = 1,052 exactly.

### F211 — README's progress block was stale by 46 functions, because nothing checked it

Ben noticed it: the figures in `README.md` had not moved while the symbol
maps gained names all day. The block read **1,006 functions named** against
**1,052** in the maps, `198` SDK entry points against `205`, `6,144` call
sites against `6,154` — stale across a dozen commits that each added names.

**Why it drifted while the other two did not.** `tools/progress.py --check`
verifies `HANDOFF.md` and `MILESTONES.md`, both named by rule 14, and
README's numbers live in a generated block that looked self-maintaining.
It was self-maintaining only as long as someone remembered to run `--readme`
**and paste the result**. Nothing did. HANDOFF's table, being checked, stayed
correct to all five rows throughout.

Three fixes, in the order that matters:

  - `--write` regenerates the block in `README.md` in place, so updating it
    is one command instead of a copy and paste. Deliberately a subprocess of
    this same script running `--readme` rather than a refactor: that path
    reads a dozen locals built up through `main`, and lifting it out to call
    it twice is how a generator and its check quietly stop agreeing. One code
    path, run twice.
  - `--check` now verifies README as well, by regenerating the block and
    comparing. Confirmed to bite by editing a figure and watching it fail.
  - The block itself is regenerated and current.

**The general point, for the third time today.** F192 found an origin column
that nothing read back; F189 found a sampled log used as a count; this is a
generated block nobody regenerated. **A number that no check compares against
its source is a number that is already drifting** — and the ones in README
are the figures strangers read first.

**And a repeat of a specific mistake:** the first cut of `check_readme` used
`P('README.md')` as if it returned the file's contents, where it joins a
path — exactly the bug F192's ledger check had, made again three hours
later. It failed loudly and immediately, which is the argument for writing
the check before trusting the fix.

### F212 — the stream thread's dispatch, and the two message types that refill a slot

`fn_80055264`'s body is a message loop with a **17-entry jump table**
(`jumptable_801E7C00`, types `0`–`0x10`), dispatching on `msg->0x08`:

    r27 = *msg;  r28 = r27->0x0C;          /* the stream object */
    if (r28->0xD6 & 0x20) skip;            /* a gate, watched: stays 0 */
    if (msg->0x08 > 0x10) skip;
    jump jumptable_801E7C00[msg->0x08];

Mapping the table's targets against the code, **exactly two types make a slot
ready** — the cases at `+0x68` and `+0x9C`, reached by types **3** and **5**,
each of which calls `fn_800534D4`. Every other type lands on `+0x220`, the
default, or on cases that do other work. Type 5's case additionally sets both
slot bytes to 4 when `r27->0x4 == 3`.

`fn_800534D4` is called **twice in a whole run**, both from `0x80055310`,
which falls inside the type-5 case. So type 5 arrived twice and type 3 not at
all — or between them, twice.

**What the four senders to that queue emit**, read from the instruction
stream:

    0x800525CC   type 0x0F   x2     -> jump table entry 15 = default
    0x80052FF8   (not yet read) x2  <- the site that marks a slot DONE
    0x800566E4   (not yet read) x13
    0x80056790   type 0x0C   x13    -> a case that does not refill

The interesting one is `0x80052FF8`: it is inside `fn_80052FAC`, the function
that marks a slot **done** (state 2 → 3), and it sent exactly **two**
messages — the same count as the two `fn_800534D4` calls. A design where
"slot finished" posts a message that prepares the next slot would explain
both numbers at once, and would be the loop that has stopped turning.

**What must not be assumed.** I have not read `0x80052FF8`'s type field yet —
a narrow grep window showed a nearby `li r6, 0x3` which is *not* the type
store, and reading it as one would be the same mistake this log already
records three times. The next step is to read that store properly and then
ask why `fn_80052FAC` runs only twice.

**The slot states, for reference** (watched at `0x8027ADD4`, which covers
object `0x8027AD00`'s bytes `0xD4`–`0xD7`): `00 00 → 01 01 → 02 01 → 02 02 →
03 02 → 03 03`, and nothing after. Two slots, each made ready once, posted
once, marked done once, never returned.

### F213 — the refill message type exists, is handled, and is never sent

The stream thread's jump table, mapped to real addresses rather than
offsets:

    type  3 -> 0x800552CC   fn_800534D4(obj), then retype the message to 4
                            and send it on  <- THE REFILL REQUEST
    type  5 -> 0x80055300   fn_800534D4(obj), then send on  <- START
    type  7 -> 0x800553A4   type 8 shares this case
    type 11 -> 0x800553D8   type 12 -> 0x80055410, 16 -> 0x80055448
    all others -> 0x80055484, the wake-up scan

Only **3 and 5** call `fn_800534D4`, the function that returns a slot to
state 1. `fn_800534D4` runs **twice** in a whole run, both times from
`0x80055310`, which falls inside type 5's case.

**Type 5 is a start, not a refill.** `fn_80053930` originates it, and is
called 34 times — 17 per stream object — with the type in `r6`: **5 twice**,
7 sixteen times, 0xB sixteen times. The two type-5 calls come from two
distinct one-shot sites, `0x80053B20` and `0x80053B48`, one per object. So
the two slot-preparations are initialisation, exactly as many as there are
stream objects, and nothing is wrong with them.

**Type 3 is never sent.** Every message that reached the thread's queue in a
run is accounted for: the router at `0x800525CC` forwarding type 5 (×2),
`fn_80052FAC`'s wake-up sentinel — the literal value `1`, which the loop
tests for and skips (×2) — `fn_80056688` (×13) and `0x80056790` sending type
`0x0C` (×13). **None is type 3.** The case that would put a slot back into
service is compiled, reachable and correct, and nothing in the run asks for
it.

**How the pipeline was traced, which is reusable.** The router forwards a
message *unchanged*, so the same pointer appears in several sends. Grouping
the captured `OSSendMessage` calls by their message argument reconstructs
each message's whole journey without tracing a single extra function:

    msg 0x8021A154:  0x80053974 -> queue 0x802133A4     (origin)
                     0x800525CC -> queue 0x80213364     (routed, type 5)
                     0x8005533C -> queue 0x8027B4D8     (thread's reply)
                     0x800562E0 -> queue 0x802133A4
                     0x800525E0 -> queue 0x80213384

while `msg 0x8022AA40` shows the same four-step cycle repeating **13 times** —
a loop that is still turning while the type-5 one has run once and stopped.

**The open question, stated exactly:** what should send type 3, and under what
condition. It is not the wake-up sentinel (that path scans slots and posts
ready ones, which is a different job), and it is not any sender observed. The
answer is either a call site never reached or a condition never satisfied,
and the next step is to find the type-3 construction in the binary the way
`fn_80052FAC`'s was found — by reading the function, not a grep window.

### F214 — the root: one state word is written once, and sixteen links follow from it

The chain now reaches a single word of guest memory. Every step below is a
counter or a watch from a reproducible run.

**`0x8021A078` — the stream object's state at `+0x08` — changes exactly ONCE
in a 120M-step run: `0 -> 3`, at pc `0x80053BDC`. Nothing ever writes it
again.**

`fn_80053988` dispatches on that word to decide what to ask the stream
thread for:

    state 1  ->  fn_80053930(..., r6 = 1)    a header read
    state 2  ->  fn_80053930(..., r6 = 3)    THE REFILL
    state 3  ->  fn_80053930(..., r6 = 5)    the start
    otherwise -> nothing

Stuck at 3, it can only ever ask to start — which it did, twice, once per
stream object, correctly. **The refill request is unreachable not because its
code is wrong but because its selector never moves.**

**The loop that should move it, and where it is cut.** Working out from the
state word:

  - state 2 is set at `0x8005568C`, and is **gated on `obj->0xD6 & 0x4`**.
    That byte is watched for a whole run: **`0x00` throughout.**
  - bit 2 of `0xD6` is set in exactly one place, `fn_80053200`, which finds
    the stream object whose `+0x3C` matches its argument, sets the bit, and
    wakes the thread.
  - `fn_80053200` is traced at **0 calls**. It is never invoked.
  - It is never *called* because it is not a function anyone calls: its
    address is taken once, at `0x80055378`, and handed to
    **`DVDReadAsyncPrio`** as the completion callback of a 96-byte read of
    `obj + 0x3C`, issued from the stream thread's **type-1** case.
  - That read is never issued: all **303** `DVDReadAsyncPrio` calls in a run
    come from `0x7F008618`, the overlay's `gcn_stream_issue_read`, and none
    from `0x80055398`.
  - The type-1 case never runs because a type-1 message needs state 1, and
    the state is 3.

So it closes on itself: **state 3 -> only "start" is ever requested -> the
header read that would fire the callback is never issued -> the bit that
would allow state 2 is never set -> the state never leaves 3.**

**What this does and does not say.** It says precisely where the machine
stops and that every part of it is individually behaving as written — no
corruption, no dropped message, no lost interrupt, no race. It does **not**
yet say what, on hardware, moves that state word off 3 the first time. One
write, at `0x80053BDC`, is all the run contains; the code that would write
1 or 2 exists and is reachable only through the loop above.

**The next question is therefore narrow and answerable:** what is supposed to
advance `obj->0x08` out of 3 — and whether our runtime fails to deliver
something that would, or whether the game is waiting on a condition it sets
for itself. The single write site `0x80053BDC` and its caller `0x80053C6C`
are where to start.

### F215 — two corrections at the root, and the tracer misses fall-through entries

Reading the root's call sites with **real addresses** rather than offsets
corrected two things I had stated.

**1. The starts are one pass, not two one-shot sites.** F213 said the two
type-5 messages came from "two distinct one-shot sites, one per object". The
listing at real addresses:

    80053AF8  bl fn_80053930   with r6 = 3   <- the REFILL, never reached
    80053B1C  bl fn_80053930   with r6 = 5   <- start, object A
    80053B44  bl fn_80053930   with r6 = 5   <- start, object B

Both type-5 calls are in the **same branch**, `.L_80053B00`, run in a single
pass over two stream objects. `fn_80053988` is traced at **exactly 1 call**,
which is consistent with that and was not with my reading. The type-3 site at
`0x80053AF8` sits in the state-2 branch and is never reached — that part
stands, now on the right evidence.

I had mapped a return address to the wrong `bl`: LR `0x80053B20` follows the
call at `0x80053B1C` (type 5), not the one at `0x80053AF8` (type 3). Reading
a return address as if it were the call site is a half-instruction error that
inverts the conclusion.

**2. The tracer misses functions entered by fall-through.** `fn_80053B60`
traces at **0 calls** while demonstrably executing — it calls `fn_80053988`
from `0x80053C80`, inside itself, and that call was traced. Nothing `bl`s to
`0x80053B60`; control falls into it from the end of `fn_80053988`. The
tracer matches `pc` at a dispatch boundary, so a label that is never a branch
target is never seen.

That is the **third** limitation of `MGS_TRACE_FN` found by using it: it
missed host-invoked callbacks (F203, fixed), it pairs returns by
most-recent-match, and now it cannot see fall-through entries. **A zero from
it means "never entered at this address", not "never executed"** — and the
two are different whenever decomp-toolkit's function boundary is a label
rather than a call target.

**The oracle is available, and is the obvious next instrument.** Dolphin is
installed (`org.DolphinEmu.dolphin-emu`, flatpak) and both disc images are on
disk. The design document makes Dolphin the reference for divergence, and the
question left — what advances `obj->0x08` off 3 — is exactly the kind that is
cheaper to *observe* than to derive: sixteen links have been walked
backwards, and each new level reveals another state field. Watching
`0x8021A078` in a working run answers it directly. Dolphin's GDB stub is the
likely route.

### F216 — the oracle answers in one run, and F214's root cause was wrong

`tools/dolphin-watch.py` reads guest memory out of a running Dolphin, and the
first thing it measured overturned the conclusion sixteen links of backward
derivation had reached.

**How it reads memory.** Dolphin backs emulated RAM with a shared-memory
mapping, so guest memory is readable straight out of the emulator's address
space. Offset 0 of that region is physical 0, which is guest `0x80000000`.
The flatpak build has **no GDB stub compiled in** — `strings` finds no "gdb"
at all — so the debugger route was closed, and this needed none. It launches
Dolphin itself because `kernel.yama.ptrace_scope` is 1 here: only an ancestor
may read `/proc/<pid>/mem`, and a sibling is not enough. The game ID at
offset 0 reading `GGSPA4` is the check that the right region was found.

**The measurement, against ours:**

    address                      Dolphin          our port
    0x8021A078  state word       3                3        <- SAME
    0x8021A184  state word (2)   4                4        <- SAME
    0x8027ADD4  slot bytes       cycles 0303 <->  frozen at 0303
                                 0203 and 0302,
                                 ~10 times in 76s

**So F214 was wrong.** It concluded the root was `0x8021A078` being written
once to 3 and never moving, with sixteen links following from that. A working
run holds **exactly the same value**. Three is not a stall, it is the
configured mode, and everything derived from "the state never leaves 3" —
that the refill request is unreachable *because* of it — was reasoning from a
normal value to a fault.

What survives from that chain is the *mechanism*: the slot states, the ring
head, the refill trigger and their gating are all read correctly. What was
wrong is which end of it is the cause.

**What the oracle says the fault actually is:** the slots **recycle** in a
working run and stop in ours. Both reach `0303`; Dolphin leaves it about ten
times in 76 seconds and we never do. Notably the transitions are `3 -> 2`
directly, with no `1` observed at 50 ms sampling — so a slot returns to
service by a route that does not linger in state 1.

**A caveat kept explicit:** it is not established that Dolphin had reached the
same point in the game. 76 seconds of headless boot may still be in the
logos, and the cycling seen could belong to another stream (the audio banks
use the same machinery). The state words matching is strong evidence on its
own; the cycling needs the two runs aligned to the same moment before it is
more than suggestive.

**Two process-level traps hit twice each, worth naming.** `pkill -f
<pattern>` matched this session's own shell — twice — because the pattern
appeared in the command line running it, killing the shell mid-command. And
`pgrep -x dolphin-emu-nogui` matches **nothing**: the kernel truncates `comm`
to 15 characters, so the name to compare against is `dolphin-emu-nog`. The
tool now reads `/proc/*/comm` directly and skips its own pid.

### F217 — MGS_TRACE_FN counts are LOWER BOUNDS, and several findings must be re-read

Chasing what drives the slot cycle, `fn_80053B60` turned out to be called by
exactly one `bl`, at `0x80051830` — yet the tracer reported **0 calls** for
it while it demonstrably ran. One run settles it:

    [watch] 0x8021A078: 0 -> 3  at pc 0x80053BDC      <- inside fn_80053B60
    traced guest functions:
      0x80053B60  0 calls
      0x80051830  0 calls

The watch proves the function executed; the tracer says it was never entered.

**Why.** The run loop sees `pc` only at a **dispatch boundary**. Translated
code calls other translated functions **directly, in C**, without returning
to the loop, so a function is counted only when it happens to be where a
dispatch starts. F215 put this down to fall-through entry; that was too
narrow. It applies to ordinary `bl` calls as well.

**So every `MGS_TRACE_FN` figure in F203–F215 is a lower bound**, not a
count. Re-reading what rests on what:

  - **Safe — watches and host counters are exact.** A watch reads guest
    memory every step; the disc tally, DVD completions and interrupt counts
    are host-side. So: the slot states changing 5 times, the state word once,
    the ring head 456 times, `pool->0x34`/`0x38`/`0xD6` staying zero, the
    mask's history, 406 reads / 406 callbacks / 0 refused, `movie.dat` read
    8 times — all stand.
  - **`fn_80053200` "never invoked" survives, but by the other leg.** The
    trace said 0 calls, which is now worth nothing; the *watch* on
    `0x8027ADD4` showing `0xD6` at `0x00` for a whole run is what actually
    establishes that bit 2 was never set. The conclusion holds; the reason
    given for it was the weaker of the two.
  - **Suspect — anything whose argument was a trace count.** "The pump is
    called 2,204 times", "151 issue_reads against 406 reads", "`fn_1_8FE8`
    1,101 times", "8,839 acquires, 1,848 successes, 747 frees". The
    *ratios* there were doing real work, and a uniform undercount would
    preserve them, but that is an assumption and is now flagged as one.
    F202's correction — that the pool is a record ring and nothing leaks —
    rests on reading the code, not on those counts, and is unaffected.

**What to do instead.** Prefer a watch on a memory location the function
writes; it is exact, costs nothing measurable, and was already the instrument
that caught this. Use `MGS_TRACE_FN` to discover *who* and *with what* — its
argument and caller reporting is sound — and not to count.

**This is the fourth limitation of that tool found by using it**, after
host-invoked callbacks, most-recent-match return pairing, and fall-through
entries. Each was found by disagreeing with another instrument. That is an
argument for keeping two instruments on anything load-bearing, which is how
this one surfaced.

### F218 — the movie is paced by AUDIO CONSUMPTION, and that reframes F188

Following the re-arm rather than the refill found the loop, and it runs the
opposite way round from how I had been reading it.

**What actually recycles a slot.** `fn_800534D4` (types 1/3/5) is *not* the
only way a slot returns to service, which is what made the earlier chain look
closed. `fn_80055114` also sets a slot to 1 — `li r0, 0x1; stb r0, 0xd4(r4)`
— and it is reached from message types **11 and 12**, which *do* repeat in
our run (16 and 13 times). It re-arms a slot only when a **playback
position** reaches that slot's end marker:

    if (*(obj + slot + 0x88) != position) skip;   /* not consumed yet */
    ... slot = 1 ...

Slot 0's end marker is `0x8000` — watched, written once, 32 KB.

**Where the position comes from, and how far ours gets.** Type 12 carries it,
and `fn_80056688` produces it: it copies bytes and advances a byte counter by
however many it copied. Traced, it is called **13 times with a length of
`0x400`** — **13 KB of the 32 KB** a slot needs. It stops less than halfway,
so no slot is ever recycled.

**And `fn_80056688` is a Vorbis read callback.** Its caller invokes it
through a function pointer at `r30->0x118` with `(dest, 1, 0x400)` — the
shape of an Ogg/Vorbis callback — and the file attributions around that
caller read **`framing.c`**, Ogg's own framing layer. So the whole chain is:

    audio consumer
      -> Vorbis/Tremor decode (framing.c)
      -> the game's read callback, 1 KB at a time
      -> the stream's byte position advances
      -> at a slot boundary, fn_80055114 re-arms that slot
      -> fn_80054D14 posts it, the drain empties it
      -> the record ring's head can advance
      -> the refill fires and more of movie.dat is read

**Every link in F209's chain was read correctly and in the wrong direction.**
I traced it as "the disc stops, so nothing decodes". It is "nothing consumes,
so nothing decodes, so the disc is never asked". The movie is **paced by
audio**, which is why the picture freezes and the game keeps running: the
video has nothing to advance *against*.

**This reframes F188, which is mine to correct.** F188 concluded "audio is
not the movie blocker" from the audio DMA running for 93 seconds. That
measured the **hardware** — our engine streaming blocks out of a buffer — and
said nothing about whether the game's mixer consumes decoded PCM. The DMA can
happily play 93 seconds of whatever is in that buffer while the Vorbis
decoder is never asked for a sample. F184's original instinct that audio was
load-bearing was closer than the finding that dismissed it.

**Next:** find what drives the decoder — the AX callback, on the design
document's account, at 5 ms — and whether it runs in our port at all. That is
one measurement away, and it is now a question about our runtime rather than
about Konami's state machines.

### F219 — the DSP task mail is wrong, and un-stubbing audio crashes in EXI, not audio

F218 left the question "what drives the decoder". The answer is AX, and two
distinct faults sit between us and it.

**1. We post the wrong DSP task mail.** `mgs_interrupt_dsp_task` cycles
`0xDCD10000` (init) then `0xDCD10003`. From `dolsdk2004/src/dsp/dsp_task.c`:

    0xDCD10000  init    -> init_cb
    0xDCD10001  resume  -> res_cb
    0xDCD10003  done    -> done_cb, then __DSP_remove_task(): UNLINKED

So our second mail **deletes the task**, and the task is AX's. `AXOut.c`
makes the consequence exact: `__AXOutAiCallback` runs a mixing frame only
when `__AXOutDspReady == 1`, and the single place that sets it is
`__AXDSPResumeCallback` — AX's `res_cb`, reached only by a **resume**. No
resume, no frame, no AX callback, no PCM pulled from Vorbis, and the stream
position never advances. That is the far end of F218's chain, and it is a
bug in **our** code rather than in the game's.

**It is not the default yet, and that is deliberate.** Switching to resume
lets AX actually run, and the boot then reaches audio paths this runtime does
not model: one run ended in an `unhandled exception` at 2.5M steps.
`MGS_DSP_RESUME=1` selects the correct mail so the rest of that work can be
done against it without a rebuild. Flipping a constant is not the fix;
bringing the audio path up with it is.

**2. Un-stubbing `__OSInitAudioSystem` crashes in EXI.** The stub's comment
says the flags it waits for "will never be raised however carefully the
registers are modelled" — which F186 disproved, driving that whole sequence
against our model with every wait passing. So it was withdrawn
(`MGS_UNPATCH=0x8001CDC4`, new: the patch table can now be lifted per
address at runtime, because a shim is a claim that goes stale and checking
it should not need a rebuild).

It segfaults, and `gdb` names the reason precisely: **`func_800195E0`
recursing until the stack dies — `EXIGetID+0x2D0`**, the EXI attach/probe
path, spinning in what the SDK writes as `EXISync`. So F184's instinct that
the crash was "unrelated to audio" was right, and now has an address. The
audio system cannot be brought up until that EXI wait completes.

### F220 — determinism is LOAD-DEPENDENT, so F206's "settled" was conditional

Two runs at HEAD, both **completing** all 120M steps, gave 5 files and 9
files. Not truncation — both printed their step-limit line.

The machine is at **load average 70**, four `quartus_fit` processes taking
~440% CPU each. F206 measured fifteen byte-identical runs and called
determinism settled; that was on an idle machine, and the claim only ever
held there.

The data race fixed in F205 was real and worth fixing. It was not the whole
story: something else in the runtime is sensitive to host scheduling, and it
only shows when the host is contended. The DVD model is explicitly built
against this — completion is decided by the guest clock, with a busy-wait so
worker scheduling cannot be observed — so whatever escapes it is elsewhere
and is not yet found.

**The practical consequence, applied immediately:** no measurement taken
while the machine is loaded is trustworthy, including everything in this
section after the load appeared. Runs to compare need an idle machine, and
"I ran it twice and got the same answer" is evidence about those two runs
only.

### F221 — AX frames can be made to run, and the pacing has to be the audio DMA

Working forward from F219 rather than through EXI, three measurements settle
what AX needs — and one shows the fix is not yet a fix.

**AX is initialised and its callback is registered.** Watching
`__AID_Callback` at `0x8027DE0C` — the global `AIRegisterDMACallback` writes
— it is set **once** to `0x80032868`, which is `__AXOutAiCallback`. So AX
came up, found the audio interface, and registered. The AX banner in a
windowed run says the same. Nothing about AX's own initialisation is broken,
which rules out the theory that `__OSInitAudioSystem` being stubbed had
prevented it.

**With the resume mail, AX frames genuinely run.** `__AXOutDspReady` at
`0x8027DEF0` is written on every `__AXOutAiCallback`, so it counts frames
exactly. Under `MGS_DSP_RESUME=1` it moves through the real cycle —
`__AXDSPResumeCallback` sets 1, the AI callback takes the mix path and
clears it, asserts, resumes — which is the machinery F219 predicted.

**But the pacing matters more than the mail.** Three configurations, same
run length:

    resume posted every 4,099 steps (a timer) ....... 90 transitions
    resume gated on guest->DSP mails ................. 3 transitions
    resume gated on the audio-DMA interrupt ..... 32,989 transitions

The timer floods PI's DSP line — roughly 29,000 resumes in a run — and that
line is shared by the mailbox, ARAM and the audio DMA, with the dispatcher
servicing one source per entry. AX then got **45 frames against 18,686
audio-DMA interrupts: 0.2% of them.** Gating on guest→DSP mails was worse and
for an instructive reason: a whole run sends **two** mails to the DSP, so
`DSPAssertTask` is not producing what I assumed. The audio DMA is the right
clock — one resume per AID is one AX frame, and our engine already raises AID
every 5 ms of guest time (F187), which is the period AX is written around.

**And it still does not play the movie.** Worse: with `MGS_DSP_RESUME=1` the
stream's slot bytes at `0x8027ADD4` change **0 times**, where the default
manages 5. So making AX run has broken the stream's *setup* — the slots are
never armed at all. The default path is unaffected (5 transitions, 9 files,
8 movie reads, verified), and the correct mail stays behind the environment
variable.

**What this is worth.** It converts "audio is the blocker" from a diagnosis
into something exercisable: AX frames can be driven at the right rate, on
demand, with one variable. What remains is that the audio path and the stream
setup now interfere, which is genuine phase-4 work rather than a constant to
flip — and the design document always said audio was phase 4.

### F222 — correction: AX does NOT break the stream, and running it changes nothing

Two things from F221 have to be withdrawn, and a third result is new.

**1. "Making AX run has broken the stream's setup" is wrong.** That rested on
one measurement of the slot bytes reading 0 changes under `MGS_DSP_RESUME=1`
against the default's 5. Repeated, with two runs of each configuration:

    default  run1/run2:  slots 5 | 9 files | movie.dat 8 reads
    RESUME   run1/run2:  slots 5 | 9 files | movie.dat 8 reads

**Identical.** The earlier zero was a bad run, taken while the machine was at
load 20+ — exactly the condition F220 said made measurements untrustworthy,
and I used one anyway. A second stream object at `+0xDC` does differ (2
changes against 0), but object 0 — the one the movie uses — is the same.

**2. Running AX changes the data flow not at all.** The decode buffer's fill
pointer advances **20 times in both configurations**. So 32,989 AX frames
buy exactly nothing: the Vorbis reader still stops at 13 KB.

**3. And it is not for want of a callback.** Watching `0x8027DF00`, the
pointer `__AXOutNewFrame` calls per frame, the game registers **`0x8004EB8C`**
— which sits beside `sd_sound.c` in the file attributions, so it is Konami's
own sound update. AX frames run, the game's callback is registered and
called, and no more Vorbis data is pulled.

**What this does to F218.** Its *structure* stands and is read from code: a
slot re-arms when a playback position reaches its end marker, that position
is advanced by `fn_80056688`, and that function is a Vorbis read callback.
What does **not** stand is the inference I drew next — that driving AX frames
would therefore advance it. It does not. Something between the AX callback
and the decoder is gating, and identifying it needs an instrument the
function tracer cannot provide: both `fn_80060924` and `fn_80060768` report 0
calls while demonstrably running, because translated code calls them directly
in C (F217).

**What is solid after all this**, and worth keeping separate from what is
not: AX is initialised, its AI callback is registered and correct, its frame
cycle can be driven at the right rate by pacing the DSP resume to the audio
DMA, and the game's sound callback is registered and runs. None of that moves
the movie. The blocker is downstream of AX, not at it.

### F223 — AX mixes 16,506 frames either way, so audio mixing is not the blocker

Measuring instead of inferring, at last, and it dismantles most of F218–F221.

**AX mixes, and always has.** `__AXOutDspReady` has two paths out, not one:
the AI callback's `1 -> 0`, and — the one I had ignored —
`__AXDSPResumeCallback`'s `2 -> 0`, which calls `__AXOutNewFrame` directly.
Counting both:

    default  mix via AI callback:    11    via resume callback: 16,495
    RESUME   mix via AI callback:    11    via resume callback: 16,495

**Identical, and ~16,500 frames in both.** I had counted only the first path,
got 11, and concluded AX was not running. It was running the whole time.

**`MGS_DSP_RESUME` changes nothing measurable.** Slots, files read, movie
reads, decode-buffer advances and now mixing frames are the same either way.
The `done`-versus-`resume` analysis (F219) is right about what the SDK's
handler does with each mail; it is not what gates the movie.

**And the mixer is not pulling the movie's audio.** 16,506 mixing frames
against **13** Vorbis read-callback calls. So whatever those frames mix, it
is not the movie's Vorbis stream — most likely the `.spd` banks, which the
disc tally shows being read normally. F218's chain is real where it was read
from code (a slot re-arms on a playback position; that position comes from a
Vorbis read callback) and wrong where I inferred the driver: **the AX mixer
does not drive that decode.**

**What was genuinely fixed here.** Posting task mails while
`__DSP_curr_task` is null makes the SDK's handler execute
`stw r0, 0x0(r5)` through a null pointer, into guest low memory. It showed
as a destroyed thread list — one entry, priority 1081872, a link into
nothing — against nine clean threads by default. Gated on the task pointer
(`0x8027DF94`), and the list is clean again. This runtime's own comment had
warned about the same null for the *init* mail; the resume needed the same
care.

**New diagnostic.** The report now says why a DSP task mail was withheld:
`16507 posted; 420 not booted, 0 mail unread, 0 no current task, 10168 no
frame due, 2172 undelivered, of 29276 offers`. That is what showed the mails
were being posted and read all along, which is what forced the recount above.

**Where this leaves the movie.** Audio init, AX, the AI callback, the mixer
and the DSP task cycle are all working and none of them is the blocker. The
one measured fact that still stands is narrow and unexplained: the Vorbis
read callback runs **13 times, 1 KB each**, and stops — while 64 KB has
already been delivered into the buffer it reads from. What asks it for data,
and why it stops asking, is the question. It is not AX.

### F224 — movie.dat is not Ogg, demo.dat is, and the engine's event system works

Three measurements, each closing off a line of inquiry.

**`movie.dat` contains no Ogg at all.** Scanning it: **zero** `OggS` pages in
the first megabyte, and it opens `00 00 00 10 | 00 00 00 10 | ...`, an index
rather than a container. `demo.dat` has **100** `OggS` pages in 2 MB. So the
Vorbis decoder is not decoding the movie — it is decoding `demo.dat`, and
F218's chain conflated two streams.

They behave the same, which is why it went unnoticed: both are read through
the REL's stream layer from the same call chain, both deliver and then stall
(`movie.dat` 8 reads / 256 KB from offset 0; `demo.dat` 14 reads / 440 KB
from `+0x3E35000`). So the conflation cost less than it might have, but "the
Vorbis reader stopping is why the movie stops" was never established — they
are two streams stalling alike, which points at a common driver rather than
at one feeding the other.

**The engine's event system is alive.** `mpeg_movie_task` waits on event code
1 from `gcn_event_poll`, so whether anything is posted at all is decisive.
The table at `bss_253F0` is double-buffered — `fn_1_F50B8` swaps the index
each frame and clears the retired buffer — and watching buffer 0's count at
`+0x800` shows it climbing `0 -> 1 -> 2 -> 3 ...`, posted by `fn_1_F5118`.
Watching the entry area shows several distinct keys active, their count
halfwords at `entry+4` being set to 1 and 2 by five different engine writers.
**Events are posted and the table is maintained.** So the movie's poll is
not finding a *match*, rather than finding an empty table — a much narrower
fault.

**New instrument: `MGS_WATCH=<addr>:<length>`** reports *which word* in a
range changed, with the pc and link register that changed it. Walking this
stall has meant guessing which field to look at next and paying a run per
guess; a struct diff answers it in one. It is what showed the event entries
above, and it mirrors the range mode already added to the Dolphin oracle.
Capped at 0x400 bytes, since it is compared every step.

**Still open, and now precisely:** the movie task polls for its key and the
table holds keys — which key does it want, and which are present. That needs
reading values rather than changes, which is the one thing the watch cannot
do.

### F225 — the movie waits on an event key that is never posted

With a dump of guest *values* rather than changes, the stall reduces to a
single mismatch.

**New instrument: `MGS_DUMP=<addr>[:<len>][,...]`** prints guest memory at
exit. `MGS_WATCH` reports changes, so a field set before the watch began, or
one that never changes, is invisible to it — which is exactly where this
investigation had arrived.

**The movie's context.** `*bss_55EA4` is **`0x8107F080`**, and the word beside
it, `bss_55EA8`, is `0x811CDDE0` — the movie task's own node from the task
table. Dumping the context:

    +0x00  81062D60  7F151E70  00002080  00000000
    +0x30  00000000  00000000  006647BA  00000000

So it is itself a **task node** — `fn 0x7F151E70`, which the task table lists
at **level 3**, the level gated by mask bit 3.

**The key it polls for is `0x006647BA`.** `mpeg_poll_stream_events` calls
`gcn_event_poll(g->0x38, ...)`, and `g->0x38` reads `0x006647BA`. The event
table, dumped at the same moment, holds eight entries:

    0039D437  002D5221  00C52070  007CD989  00541E36 x2  00541E37 x2

**`0x006647BA` is not among them.** And watching the whole entry area across
a run — **822 writes** to it — that value is never written at all.

So the fault is exact: **the movie task waits for an event under a key that
nothing in the run ever posts.** Not a lost event, not a race, not a gate —
the key is simply absent. Everything downstream (the task never leaving state
1, the stream never refilling, the picture frozen at 71% lit) follows from
that one mismatch.

**Two readings, and I am not choosing between them yet.** Either something
that should register or post under `0x006647BA` never runs — in which case
the question is what, and why — or the key itself is wrong, computed from
state our runtime has left in a different condition. The neighbouring keys
`00541E36` and `00541E37` differ by one, so these look like identifiers with
sub-indices rather than arbitrary hashes, which is worth following.

### F226 — WRONG: "Dolphin never links the overlay". It does; I searched the wrong 24 MB

Recorded in full because the mistake is instructive and the instrument that
produced it is still in the tree.

**What I claimed.** Watching the guest word holding `lis r4, bss_55EA4@ha`
through a Dolphin boot, it read `0x3C800000` — `lis r4, 0`, the unrelocated
file content — from 1.5s until the memory was reused at 8.4s, and never took
a relocated value. Whole-RAM snapshots agreed: 344 of the overlay's 444
string literals resident between 4.6s and 8.1s, 15 at 11.1s, 60s, 120s and
180s. I concluded the real game reads `mgso_pal.rel` and discards it without
linking, and therefore that our port runs a module the game never links and
`mpeg_movie_task` should not exist at all.

**Why it was wrong.** `rel_loader_LoadRel` (`0x800066F8`) does this:

    fn_8004E7BC(size, 1, "rel_loader.c", 0x7B)   temp buffer
    fn_800270D8(&fileInfo, temp, size, 0, 2)     DVDReadPrio, whole file
    memcpy(dest, temp, size)                     the real copy
    fn_8004E830(temp, "rel_loader.c", 0x83)      temp freed
    fn_8004E7BC(dest->bssSize, ...)              bss
    fn_80020AD8(dest, bss)                       OSLink

The address I watched was the **temporary read buffer**, which by design is
never relocated and is freed the moment the copy is made. Its caller passes
`dest = 0x7F008000`:

    lis r4, 0x7f01 ; addi r4, r4, -0x8000     -> 0x7F008000
    lis r5, 0x7c   ; addi r5, r5, 0x3800      -> max 0x7C3800

`0x7F008000` is not in MEM1. It is a **BAT-mapped** address, and Dolphin
backs those with a separate "fake VMEM" mapping at shared-memory file offset
`0x02040000` covering `0x7E000000-0x7FFFFFFF`. Every snapshot and every
search had covered `0x80000000` and up only, so the linked module was never
in the data at all.

**What is actually true.** Reading the right window, the module is at
`0x7F008000` (`0x00000001`, its module id) at 2.6s, and the relocation is
applied immediately:

    2.6s  0x7F1525E0 = 0x3C800000   copied in, unrelocated
    2.6s  0x7F1525E0 = 0x3C80805A   relocated
    2.6s  0x7F1525E4 = 0x93E40024

So `bss_55EA4` is `0x805A0024`, putting the overlay's bss base at
`0x805A0024 - 0x55EA4 = 0x8054A180` — **the same address our port's `OSLink
saw:` line reports across 277 runs**. Both runs link the same module to the
same place. The one number F226 got right, it got right by accident.

**The lesson worth keeping.** A same-section `bl` needs no relocation: it is
already correct in the REL file. `bl fn_1_F3AD4` reading `0x4BFA95E9` and
resolving correctly *relative to the image* looked like proof the module was
linked, and it proves nothing. Only a reference that the linker must rewrite
— an `@ha`/`@l` pair — distinguishes a linked module from a file in a buffer.

### F227 — the key is right, and the divergence is the task mask

With the BAT window readable, the oracle answers F225's open question
directly.

**The movie object exists in a working run.** `*bss_55EA4` becomes non-zero
at 25.2s: context `0x8107F120`, with `bss_55EA8` = `0x811CDE60`, its task
node. Ours are `0x8107F080` and `0x811CDDE0` — 0xA0 apart, which is an
allocation difference, not a structural one.

**The key is identical.** Dolphin's `context + 0x38` reads **`0x006647BA`**,
the same value our run polls for. So F225's second reading is eliminated: the
key is not computed wrong, and the object waiting for it is legitimate. What
remains is F225's first reading — something that should post it never runs.

**The task mask is where the two runs part.** Polling the mask
(`bss + 0x23A38`, `0x8056DBB8` in the emulator) across 150s:

    0.0s   0x00000000
    0.8s   0xC8410070     uninitialised
    2.6s   0x00000000
   24.9s   0x00000001     set while the movie object is built
   25.2s   0x00000000     cleared again, and stays 0

Ours reaches **`0x00000008`** and stays there for the rest of the run. The
working run gates a level for 0.3s and releases it; ours gates one and never
releases it.

**What that gates.** With the mask at 8, levels 2, 3, 4 and 5 are skipped
(gates `0x19`, `0x19`, `0x1F`, `0x1F`). Level 3 holds the movie object's own
node `0x8107F080`/`fn_1_149D84` — so the half of the movie that would advance
its state machine is not being called at all, while `mpeg_movie_task` on
level 1 keeps running and keeps waiting.

Note carefully: mask `1` gates those same four levels too, since each gate
has bit 0 set. So the bit that matters is not "which levels are off" in the
abstract — it is that the working run **clears** the mask 0.3s later and ours
does not. The question is now: who sets bit 3, and what should have cleared
it.

**Named dumps.** The task table now resolves function addresses through
`config/symbols/`, which is how `mpeg_movie_task` and the gated level 3 were
read off in one dump rather than by hand.

---

### F228 — the gate is deliberate: a record ring that never receives tag 2

F227 left "who sets bit 3, and what should have cleared it". Watching the
mask address in our run answers both at once:

    [watch] 0x7F4BD5D8: 0x00000000 -> 0x00002450  pc 0x8000519C lr 0x8000678C
    [watch] 0x7F4BD5D8: 0x00002450 -> 0x00000000  pc 0x7F0080EC lr 0x7F008234
    [watch] 0x7F4BD5D8: 0x00000000 -> 0x00000001  pc 0x7F0FC6D8 lr 0x7F2523EC
    [watch] 0x7F4BD5D8: 0x00000001 -> 0x00000000  pc 0x7F0FC6EC lr 0x7F251EFC
    [watch] 0x7F4BD5D8: 0x00000000 -> 0x00000008  pc 0x7F0FC6D8 lr 0x7F251DA0

The setter is `gcn_task_mask_set` (module `0xF45EC`); the bit-3 call site is
`0x249CB0`, inside `fn_1_249AB8`, and the code there is not a bug:

    00249CA0  lwz    r0, 0x25e8(r27)
    00249CA8  bne    .L_00249CB8
    00249CAC  li     r3, 0x8
    00249CB0  bl     fn_1_F45EC        gcn_task_mask_set(8), return
    .L_00249CB8:
    00249CBC  bl     fn_1_F4600        gcn_task_mask_clear(8), carry on

**Level 3 is gated exactly while `obj->0x25E8` is null**, and released the
moment it is not. The working run's 0.3s gate is this same mechanism doing
its job. So the stuck mask is a symptom, not the fault, and F227's framing of
it as "the divergence" is too strong.

**What fills that field.** `fn_1_249A64`:

    lwz r3, 0x25ec(r31)
    li  r4, 0x2
    bl  fn_1_1323C4          gcn_pool_acquire(ring, tag = 2)
    stw r3, 0x25e8(r31)

and `gcn_pool_acquire` **searches** the ring between its read cursor
(`+0x14`) and write cursor (`+0x24`) for a record whose tag word equals 2,
claiming it by setting bit 7. It does not allocate. So the movie stalls
because **no record tagged 2 is ever in the ring** — which is the same
starvation F225 saw from the other end, reached independently.

The ring itself is `stream->0x0C`, bound by `fn_1_249DB8` when the stream
becomes ready, and the owning object is node `0x8102D400` in our task table.

**Two failure modes, not one, and they are distinguishable.** Either the
producer never writes a tag-2 record, or `ring->0x34` is non-zero — in which
case `gcn_pool_acquire` returns 0 before looking at a single record. Reading
the code cannot separate these; a dump of the ring can, and that measurement
is in flight.

**New instrument.** `MGS_DUMP` accepts a leading `*` to dereference:
`*0x8102F9EC` dumps whatever that word points at. The structures worth
looking at here are reached through pointers only known at run time, and
each one was costing two eight-minute runs — the first purely to read an
address out so the second could use it.

---

### F229 — the ring head is pinned by records nobody consumes

Walking the ring rather than reading code turns F228's two candidates into
one measured answer, and eliminates both of the things I expected.

**The ring is healthy and the refill is not blocked.** Dumped at the stall:

    ring 0x7F4EF794: buffer 0x81741AA0 + 0x40000,
      read 0x817789F0 (+0x36F50), write 0x8176D320 (+0x2B880)
      276 records between the cursors, 1 wrap
        tag 0:  267 waiting
        tag 1:    9 waiting
        tag 2 does not appear at all

`ring->0x30` and `ring->0x34` are both **0**, exactly as Dolphin's are, so
the "stop flag" hypothesis of F228 is dead. And the refill gate `bss+0xB040`
— which `fn_1_9C8` returns and which blocks both the pump and the refill
while non-zero — **cycles normally in our run**:

    0x00000000 -> 0x00080000  pc 0x7F008974
    0x00080000 -> 0x00040000  pc 0x7F00839C
    0x00040000 -> 0x00000000  pc 0x7F00839C

455 times in 112 frames, against Dolphin's identical `0x40000` / `0`
toggling. Reads are being issued constantly. Neither suspect survives.

**What is actually wrong is arithmetic the engine gets right.** In
`fn_1_1321A8`:

    subf r3, r3, r30      free = size - used
    divw r0, r30, 3
    cmpw r3, r0
    ble  skip             no refill unless free > size/3

Our ring holds `0x34930` used of `0x40000`, so free is `0xB6D0` against a
threshold of `0x15555`. The refill **correctly declines** — the ring is 80%
full. It is full because the pump advances the read cursor only past records
tagged **0**, stopping at the first non-zero tag, and the head is sitting on
one of nine **tag-1** records that nothing ever claims.

So the chain is: nobody consumes tag 1 -> the read cursor cannot advance ->
free space stays below a third -> no refill -> no new tag-2 record ->
`obj->0x25E8` stays null -> level 3 stays gated -> the movie waits forever.
Every link after the first is the engine working correctly.

**Who should consume tag 1.** `gcn_pool_acquire` has eleven call sites; only
two pass a literal (the movie's `2`, and `0x10` inside the pool code), and the
rest read the tag from a field of the asking object. The one beside the movie
code is `fn_1_1482FC`, called from three places inside `mpeg_movie_task`
itself with the task node as its argument, asking for `node->0x38` out of
`node->0x3C`'s ring. So the movie task is *both* consumers, and which records
it claims depends on a field of its own node.

That field is the next measurement, and it also bears on F225. `mpeg_movie_task`
dispatches on `node->0x44`, which F196 measured as 1, and the tag-1 consumer
at `0x149194` is on the **state-0** path — so in state 1 the task may simply
never reach the call that would drain them. If so, "waiting for an event that
never comes" and "not consuming the records that would let it advance" are
the same stall seen from two ends.

**New instrument.** `MGS_RING=<addr>` (with `*` to dereference) walks a
record ring the way `gcn_pool_acquire` does and reports the tag histogram.
The layout is read out of that function rather than assumed, and the walk is
bounded so a corrupt ring reports instead of hanging at exit.

---

### F230 — the video is all there; the pin is one consumer whose output buffer is full

F229 said "nobody consumes tag 1" and guessed the movie task was both
consumers. The guess was wrong and the measurement is better than it.

**The movie's own ring is full, and matches the oracle exactly.** There are
two rings, not one — `bss_55BF4` is an array of two 0x40-byte descriptors,
and `fn_1_1322D4` hands out the first free slot. Walking ours at the stall:

    ring 0x7F4EF7D4: buffer 0x81701AA0 + 0x40000, read +0x3610, write +0x3FD70
      321 records, tag 0x0000000E: 321

Dolphin's ring 1 at the same point holds **321 records, all tag 0xE**, the
same number — and then drains: 321 -> 292 -> 248 -> 226. Ours never moves.
**So the video data is present and correct. Nothing is missing from disc.**
`mpeg_movie_task` asks for tag `0xE` (its node's `+0x38`, measured) and never
takes any, because it is parked in state 1 (`+0x44`) behind ring 0.

**Tags are packed, and my first histogram hid it.** The consumer factory at
`0x14FCC` refuses to create a task unless `(arg >> 16)` equals the language
byte at `0x801E7DD8` — which `fn_80006514` sets from `OSGetLanguage` for a
PAL disc, and which reads **1** in Dolphin. The five `fn_1_12FC4` tasks in
our table hold tags `0x00070004`, `0x00050004`, `0x00040004`, `0x00030004`,
`0x00020004`: `(language << 16) | 4`, every language **except 1**. They are
*discard* tasks — `fn_1_12FC4` acquires a record and frees it immediately, so
unselected languages are drained rather than decoded. Language 1's stream
gets the real consumer, `fn_1_14D34`, whose node holds `0x00010004`.

Both walkers now report whole tag words. The rings that matter turned out to
carry plain small tags, so the masking changed no conclusion here — but it
would have hidden the language field exactly where it is load-bearing.

**The pin has a name.** `fn_1_8FE8`, node `0x8109D760`, holds tag
`0x00000001` and ring `0x7F4EF794` — it *is* the tag-1 consumer, it exists,
it sits on level 1, and its flags (`0x8090`) do not skip it. It is running
and refusing the work:

    rec = gcn_pool_acquire(node->0x38, node->0x30)   tag 1
    len = fn_1_1325D4(ring, rec)                     record size - 0x10
    ...
    avail = node->0xB8 - node->0xBC
    if (avail < node->0x40) return                   <- taken every frame

and on the other path it calls `gcn_pool_clear_entry_flag`, which puts the
record back with its claim bit cleared. That is why the walk sees nine
**unclaimed** tag-1 records rather than nine claimed ones: they are being
picked up, rejected and replaced every frame.

So the chain gains a link at the front: **`fn_1_8FE8`'s destination buffer
has no room**, so it refuses tag-1 records, so ring 0's head never advances,
so no tag-2 arrives, so level 3 stays gated, so `mpeg_movie_task` stays in
state 1, so 321 perfectly good video records sit unread.

`node->0xB8`, `0xBC` and `0x40` are the next measurement — a run dumping them
is in flight. If they show a buffer that fills and never drains, the consumer
of *that* buffer is the real fault, and its most likely owner is audio, which
would finally join this investigation to F218-F223.

**Correction worth keeping.** `MGS_BUDGET` is cycles per dispatch call, not a
run length. Setting it to 2600 to "shorten" runs made every run several times
slower; runs are bounded by `MGS_STEPS` alone.

---

### F231 — the whole stall, end to end: the sound layer stops asking for audio

Every link is now measured rather than inferred, and they form one chain from
Konami's sound layer to the frozen picture.

**The consumer's buffer fills and is never drained.** `fn_1_8FE8`'s node
(`0x8109D760`) dumped at the stall:

    +0x40 00002000        chunk it needs free (8 KB)
    +0xB0 00000000        not at end of stream
    +0xB8 00010000        buffer size, 64 KB
    +0xBC 00010000        bytes currently buffered  <- completely full
    +0xC0 810EA600        read pointer, still at the base
    +0xC4 810EA600        write pointer, wrapped
    +0xC8 810EA600        buffer base

`avail = 0xB8 - 0xBC` is **0**, so `avail < 0x40` is taken on every call and
every tag-1 record is handed back with `gcn_pool_clear_entry_flag`. Watching
`+0xBC` across a run: **17 increases, 3 decreases** — and of the three, one
is the allocator's `memset` at init and two are resets from `fn_1_8D98`. It
climbs 0x2000 at a time from 0 to 0x10000, written from module `0x90D0`
(`fn_1_8FE8`'s own `memcpy`), and after the last fill nothing reduces it
again.

**The drain is request-driven, and the requests stop.** `fn_1_8D98` services
a queue:

    addi r3, r28, 0x48          the node's OSMessageQueue
    addi r4, r1, 0x8
    li   r5, 0x0
    bl   fn_80020C3C            OSReceiveMessage, non-blocking

Each message is a request with a destination at `+0x00` and a byte count at
`+0x04`; the handler copies that many bytes out of the ring buffer and
reduces `+0xBC`. Two were served in our run — the two decreases — and then
none. `fn_1_8D98` also calls **`fn_80053178`**, which by the file attribution
in `config/symbols/main.dol.files.txt` falls inside **`sd_stream2.c`**
(between the attributed rows at line 1234 and line 1607). That is Konami's
own streaming **sound** layer.

**So the blocker is audio, and this is the measurement that shows it.**
Earlier sessions inferred it from pacing (F218) and then withdrew most of
that (F222, F223). This is a different and much harder line of evidence: a
buffer that provably fills and stops, a drain that provably runs on messages
that provably stop arriving, and a requester that is provably in the sound
layer's translation unit.

**The chain, in full.** Each arrow is a measurement in this session or an
instruction read in the binary:

    sd_stream2.c stops sending requests
      -> fn_1_8D98's OSReceiveMessage returns nothing
      -> the 64 KB buffer never drains (0xBC pinned at 0x10000)
      -> fn_1_8FE8 refuses each tag-1 record (avail 0 < 0x2000)
         and returns it with the claim bit cleared
      -> ring 0's read cursor cannot advance past the head
      -> the ring stays 80% full, so the refill correctly declines
         (free 0xB6D0 <= size/3 0x15555)
      -> no tag-2 record is ever produced
      -> gcn_pool_acquire(ring 0, 2) returns null in fn_1_249A64
      -> obj->0x25E8 stays null
      -> fn_1_249AB8 sets task mask bit 3 and never clears it
      -> level 3 is gated, so the movie object's own node never runs
      -> mpeg_movie_task stays in state 1 and never claims any of its
         321 tag-0xE video records
      -> the picture is frozen and event 0x006647BA is never posted

F225's "an event key nothing posts" is the far end of this. Nothing is wrong
with the key, the event system, the disc reads, the record rings, the refill
or the video data — all of those were suspected in turn and each has now been
eliminated by measurement.

**Next, and stated as a question rather than an answer.** Why does
`sd_stream2.c` stop sending requests? That is where this rejoins the audio
work, and the open items there are real: `__OSInitAudioSystem` is still
stubbed, blocked behind `EXIGetID+0x2D0` recursing without bound when
`EXILock` never succeeds. Whether the requester is driven by the AX frame
callback, by the audio DMA interrupt, or by a thread waiting on a queue of
its own is **not yet established**, and guessing between them is exactly what
cost F218-F221.

---

### F232 — all four sound threads are asleep, and the wake-up loop is mutual

F231 ended with "why does `sd_stream2.c` stop sending requests". The thread
dump answers it, once it is read with names.

**Four threads, all blocked, all on empty queues.**

    0x80217D58  prio 10  blocked on 0x8021336C
        receive on queue 0x80213364: 0 of 72 slots used
           2  0x80020C94  OSReceiveMessage+0x58
           3  0x80055708                       <- fn_80055264's receive
    0x802134E8  prio  9  receive on 0x802133A4: 0 of 72   from 0x800527E4
    0x80215920  prio 11  receive on 0x80213384: 0 of 72   from 0x8005469C
    0x8027B640  prio 11  receive on 0x8027B4D8: 0 of 72   from 0x800565E4

Each return address lands on the blocking `OSReceiveMessage(queue, &msg, 1)`
at the bottom of that thread's own loop — `fn_80055264`, `fn_80052764`,
`fn_8005440C` and the one at `0x800565E4`. The whole sound subsystem is
parked.

**The loop that should keep them awake is mutual, and has no external
driver.** Tracing every sender to `0x80213364`:

    fn_80053178   <- the ENGINE's fn_1_8D98, when a request completes
    fn_80052FAC   <- fn_80054FB0, inside fn_80055264 itself
    fn_800530F8   <- 0x80055478, inside fn_80055264 itself
    fn_80053200   <- 0x80055378, inside fn_80055264 itself
    fn_8005236C / fn_80052534 / fn_80052604 / fn_800526B0
                  <- all four from fn_80052764, which IS the thread on
                     0x802133A4

So thread A wakes thread B, thread B wakes thread A, and the only entry from
outside the sound subsystem is `fn_80053178`, reached from the engine's
`fn_1_8D98` **when it finishes serving a request**. That is a closed cycle:

    the consumer cannot accept records, because its buffer is full
    the buffer only empties when the sound thread asks for data
    the sound thread only wakes when the consumer finishes a request
    the consumer only finishes a request when one arrives

On hardware the cycle is broken from outside — audio is consumed as it plays,
and something posts periodically. **What that something is has not been
established**, and it is the one thing left to find. `AIRegisterDMACallback`
is called from exactly one place, `__AXOutInit`, registering
`__AXOutAiCallback`, and the `__AXOutNewFrame` path contains no indirect call
to a user hook — so the periodic wake is *not* obviously an AX frame
callback, and I am not going to assert a mechanism I have not traced.

**A diagnostic of ours was lying, and had already cost a wrong answer.**
`describe_queue` printed `<- EMPTY: nothing was ever sent` whenever
`usedCount` was zero. But `usedCount` is the occupancy *now*: a queue that
carried thirty messages and was drained reads exactly the same zero. An
earlier session nearly recorded "zero sends" as the root cause on the
strength of that line. It now prints the read cursor too, which does
distinguish the two, and says "drained (it has carried messages)" when the
cursor has moved.

---

### F233 — the external driver found, and it is present in our run too

F232 left "what breaks the wake-up cycle on hardware, because it is not
obviously AX". It is AX, through a callback I had missed.

**The indirect call I looked straight past.** My first search of
`__AXOutNewFrame` was `grep -E "bl fn_|bctrl"`, which finds neither of the
two forms that matter: the call is a **`blrl`**, at `0x80032758`:

    80032748  lwz    r12, lbl_8027DF00@sda21(r0)
    8003274C  cmplwi r12, 0x0
    80032750  beq    .L_8003275C
    80032754  mtlr   r12
    80032758  blrl

**`0x80032E20` is `AXRegisterCallback`, beyond doubt.** `dolsdk2004`'s
AXOut.c body is five statements — read the old pointer, disable interrupts,
store the new one, restore, return the old — and this function is those five
in that order around `0x8027DF00`, which is therefore
`__AXUserFrameCallback`. It is called exactly once, from `fn_8004ECC8` in
`sd_sound.c`, registering `fn_8004EB8C`.

**The full driver chain**, every step read from the binary:

    AI/DSP interrupt -> __AXOutNewFrame
      -> [*__AXUserFrameCallback] = sd_ax_frame_callback (0x8004EB8C)
      -> sd_stream_pump (0x80054958): eight channels, stride 0x10C,
         from 0x8021A070, acting on those in state 9
      -> fn_80052E84 -> OSSendMessage(0x80213384) -> wakes a sound thread

That is the **only** driver from outside the sound subsystem; every other
sender is one sound thread waking another.

**And it is not missing in our run.** Four things were checked and all four
match the oracle:

| | Dolphin | ours |
|---|---|---|
| `__AXUserFrameCallback` | `0x8004EB8C` at 3.1s | `0x8004EB8C` |
| channel 0 state | `0 -> 3 -> 5 -> 9` | `0 -> 1 -> … -> 9` |
| channel 1 state | reaches 9 | reaches 9 |
| channels 2-7 | 0 | 0 |

So the callback is registered, the channels are configured, and two of them
are in the working state — exactly as in a run that plays the movie. The
sound queues have also each **carried traffic and stopped**: read cursors of
30, 34 and 70 on `0x80213364`, `0x80213384` and `0x8027B4D8`. This is a
system that ran and then quiesced, not one that never started.

**What this rules out, and what it leaves.** Not the registration, not the
channel setup, not the AX frame path existing. What remains is inside the
pump: it posts only when `channel->0x20` is zero and flag `0x10` is clear,
and it does no further work at all when `channel->0x20` is zero. A work
pointer left permanently non-null would silence it for good while every
other symptom looked healthy. That is the measurement in flight.

**A caveat on an instrument, again.** `MGS_TRACE_FN=0x8004EB8C` reported **0
calls**, and that is worth nothing here: F-earlier established these counts
are lower bounds, because translated code calling translated code goes
direct in C and never passes the dispatcher. It is not evidence the callback
did not run, and reading it as such would have sent this the wrong way.

**Named from this:** `AXRegisterCallback` and `__AXUserFrameCallback`
(origin `own+sdk2004` — the instruction sequence here and the name and
signature in `dolsdk2004`), plus `sd_ax_frame_callback` and `sd_stream_pump`
(origin `own`).

---

### F234 — ROOT CAUSE: the AX voice's playback position never advances

The chain from F225 to F233 ends here, at a single word that moves in a
working run and does not move in ours.

**What the pump actually reads.** `sd_stream_pump`, for a channel in state 9
holding a voice, does:

    800549D0  lwz r26, 0x1b2(r3)        r3 = channel->0x20, the AX voice

`0x1B2` is `pb.addr.currentAddress` — `AXVPB.pb` is at `0x138`,
`AXPB.addr` at `0x6E`, `AXPBADDR.currentAddressHi` at `+0x0C`. The pump
compares that position against each block's `[0x48, 0x4C)` range to decide
which block has finished, and everything downstream is driven by it moving.

**Measured, both sides, same address.** The voice is at `0x80206F7C` in both
runs — the allocation matches exactly, which is itself a check that the two
runs are in the same state.

    Dolphin   0x00002000 -> 0x2977 -> 0x3212 -> 0x3AAD -> ... -> 0x9A33
              -> wraps to 0x21F2 -> 0x2B69 ...   (continuous, ~0x900/50ms)

    ours      0x00002000    and never again

Watching the whole PB window `0x80207120:0x20` across a run: **5 writes, all
at setup, all from `AXSetVoiceAddr`**, and nothing after. Ours is sitting on
precisely the value Dolphin starts from before the DSP begins advancing it.

**Why it does not advance, from the SDK.** `dolsdk2004`'s AXVPB.c:

    ppbUser->addr.currentAddressHi = ppbDsp->addr.currentAddressHi;
    ppbUser->addr.currentAddressLo = ppbDsp->addr.currentAddressLo;

The position the pump reads is **copied back from the DSP's** parameter
block. The DSP owns it: AX hands the PBs to the DSP, the DSP mixes and
advances the address, AX copies it back. **Our port runs no DSP mixing**, so
the DSP-side PB never advances and the user-side copy never changes.

**So the whole stall reduces to one thing**, and every link between was
verified rather than assumed:

    no DSP mixing -> the voice's currentAddress never advances
      -> sd_stream_pump never sees a block complete
      -> it never posts to the sound threads
      -> all four sound threads sleep on empty queues (F232)
      -> no data request reaches the engine (only 2 ever arrived)
      -> gcn_stream_fill_task's 64 KB buffer stays full (F231)
      -> it refuses every tag-1 record and puts it back (F230)
      -> ring 0's read cursor cannot advance
      -> the refill correctly declines, so no tag-2 record (F229)
      -> obj->0x25E8 stays null, level 3 is gated (F228)
      -> mpeg_movie_task parks in state 1 (F227)
      -> its 321 tag-0xE video records are never read, and the key
         0x006647BA is never posted (F225)

**Ben was right, and earlier than the evidence was.** "Audio is the blocker"
was asserted around F218 on pacing grounds, mostly withdrawn in F222 and
F223, and is now established by a different and much harder route.

**What this does NOT yet say.** It does not say the fix is to emulate the
DSP. Advancing `currentAddress` in step with the audio DMA may be enough to
unblock the pipeline without mixing a single sample, and that is worth trying
before anything larger — the port needs the *position* to move, not
necessarily the sound to be correct. Nor is it yet separated whether AX's
copy-back runs at all in our run or runs and copies an unchanged value; a
change-watch cannot tell those apart, and the DSP-side PB array has not been
located.

**Also seen, and worth keeping.** One run of this measurement died on
`[dvd] READ FAILED: 5737728 bytes at 0x3DCCE870` followed by `ERROR: module
'shared/mgso_pal.rel' is too big`, leaving a run that looked like a
measurement and was not. F205's read-failure mode is not gone. A repeat run
was clean; any single run that reports no disc reads should be discarded
rather than read.

**Named:** `AXSetVoiceAddr` (`own+sdk2004` — `&p->pb.addr` is the `0x1A6` this
function adds, four word copies under `OSDisableInterrupts`, then a switch on
`addr->format`, matching AXVPB.c). The voice allocator at `0x8003198C` is
left unnamed: its behaviour is plain but `dolsdk2004` does not carry it, so
the SDK spelling would be asserted rather than confirmed.

---

### F235 — F234 was one link short: AX never services the voice at all

F234 called the frozen `currentAddress` the root cause and said the DSP's
copy of the parameter block never advances. The first half is right; the
second was a reasonable reading of the SDK and is **not what our run does**.

**`__AXServiceVPB` is `fn_80033330`, and `__AXPB` is at `0x801F5E00`.** It
matches `dolsdk2004`'s AXVPB.c exactly: increment a counter, index
`base + index*0xF4`, and on `sync == 0` copy back four fields — `state`
(`0x0E`), `ve.currentVolume` (`0x64`) and `addr.currentAddress`
(`0x7A`/`0x7C`). So `__AXNumVoices` is `0x8027DF50` and `__AXGetNumVoices`
is `fn_80033328`.

**The movie's voice has index 61, and its DSP block is untouched.** Watching
`__AXPB[61]` (`0x801F9824`) across a run: **7 writes, all free-list pointers
at init**, `currentAddress` never written at all, every real field zero. And
`__AXNumVoices` reads **0**.

That last number settles it. `__AXServiceVPB` increments it on every call,
so zero means **it never ran**. The user-side `currentAddress` is not frozen
because the DSP failed to advance it — it is frozen because **AX never
services the voice in the first place**, so nothing is ever copied in either
direction. F234's chain and its measurements stand; its attribution of the
first cause does not.

**Why AX does not run a frame, read out of the two callbacks.**
`lbl_8027DEF0` is `__AXOutDspReady`, and it is a three-state handshake:

    __AXOutAiCallback      state == 1 -> state = 0, mix a frame
                           otherwise  -> state = 2, re-add the DSP task
    __AXDSPResumeCallback  state == 2 -> state = 0, mix a frame
                           otherwise  -> state = 1

A frame is mixed only when the **resume** callback is in the loop. Without
one, the AI callback finds the state at 2 every time, sets it to 2 again and
re-adds the task for ever. That is precisely F219's argument, now with the
state machine read out rather than inferred.

**And the resume mail does change something measurable — F222's
"changes nothing" was measuring the wrong thing.** With
`MGS_DSP_RESUME=1`:

    __AXNumVoices    0  ->  2

AX starts servicing voices. That is the first observable effect this flag has
ever been shown to have, and it was invisible before because nobody had
located `__AXNumVoices`.

**It does not fix the movie.** The record ring at the stall is *byte for
byte* what it is by default — read `+0x36F50`, write `+0x2B880`, 276 records,
9 tagged 1, 267 tagged 0, no tag 2. So the resume mail is necessary and not
sufficient, and whatever else is missing is still missing.

**Corrections to my own procedure.** "Discard any run that reports READ
FAILED" (F234) is too coarse: one such run retried a single `stage.dat` read
and completed normally with 9 files and 281 reads. The discriminator is
whether the run **progressed** — a run that reports no disc reads, or never
loads the overlay, is the one to throw away.

---

### F236 — the AUDIO path unblocks. (Its "the movie plays" claim is WRONG; see F238)

Two changes together, neither sufficient alone, and the pipeline runs.

**What was added.** `host/ax_dsp.c` models the one thing the DSP does that
the game can observe: each AX voice's `pb.addr.currentAddress` advances.
Once per resume mail — which is one AX frame — it walks `__AXPB`
(`0x801F5E00`, 64 entries of `0xF4`), and for every voice whose `state` is 1
advances the position by `160 * srcRatio`, carrying the remainder in the
block's own `currentAddressFrac`, looping at `endAddress` when `loopFlag` is
set and stopping the voice when it is not. **It mixes nothing.** It is not
the phase-4 mixer; it is the same principle the design document already
records at line 193 — the DSP *paces* the machine — one level deeper.

**It needs `MGS_DSP_RESUME=1` as well.** F235 showed why: without the resume
mail AX never services a voice at all, so there is no populated parameter
block to advance. With the resume mail and no model, the block is populated
and frozen. Both, and it moves.

**The result, and it is the thing this has been chasing since F187.**

| | before | with both |
|---|---|---|
| ring 0, tag 2 records | **0** | **204** (Dolphin: ~381) |
| ring 0 read cursor | pinned at `+0x36F50` | `+0x96D0`, moving |
| voice `currentAddress` | `0x2000` forever | `0x650E`, looping |
| the voice's loop window | `0x3000`/`0x2FFF` | `0x6000`/`0x6FFF`, advanced by the game |
| task mask | parks at `0x00000008` | `0x1 -> 0x0`, **running** |
| `demo.dat` | 14 reads | 17 reads |
| last texture drawn | 159x17 (a UI element) | **512x448**, `lit 99%` |

**The movie decodes and draws.** A full-screen 512x448 texture at 99% lit is
the video frame; the picture was frozen at a 159x17 overlay at 71% for every
run before this one.

**And it then crashes, deterministically.** Two runs are identical to the
step: `stopped after 112962303 steps: unhandled exception, pc = 0x00000800`.
That is not a mystery and not the game's fault:

    faulting instruction srr0 = 0x7F13A080   -> REL 0x131F94
    msr = 0x00009032                          -> MSR[FP] (0x2000) is CLEAR
    the instruction there is `lfd f1, 0x0(r3)`

A float load with FP disabled raises **Floating-Point Unavailable**, whose
vector is `0x800`. That is the SDK's lazy-FP mechanism working exactly as
designed, and our runtime services it 62,467 times in the same run. This one
is refused because `mgs_fp_unavailable` requires `OS_CURRENTCONTEXT` to be
sane and returns 0 otherwise — "no current context: report, do not guess" —
after which `pc == 0x800` is reported as a real fault.

So the new bug is **ours, in the exception path, not in the audio work**:
an FP-unavailable exception taken while no valid `OSContext` is current. The
faulting code is `fn_1_131F34`, the pool's wrap-copy, reached from
`gcn_pool_refill` — and `r31` is ring 0 itself, so this is the newly flowing
ring hitting a path that had never been reached before. It is the same class
as the `[interrupt] no current OSContext yet` line seen in earlier logs.

**Kept deliberately behind flags.** `MGS_DSP_RESUME` is still not the
default and the model is on by default but disableable with
`MGS_AX_MODEL=0`, because a run that now crashes at 113M steps is not yet
better than one that limps to 400M. Flipping the default belongs with the
fix to the exception path, not before it.

**Honest about what is not shown.** The audio is not correct — nothing here
produces a sample. The rate is derived (`160 * ratio` per 5 ms frame at
32 kHz) and has not been checked against Dolphin's observed
~`0x900` per 50 ms; if it is wrong the movie plays fast or slow, visibly, and
that is a cheap thing to correct later. `movie.dat` is still read only to
`+0x38000`, so the video stream itself has not yet advanced past where it
always stopped — what moved is `demo.dat` and the record ring.

---

### F237 — two attempted fixes for the FP fault, and why neither is judged yet

F236 left one bug: an FP-unavailable exception (`pc = 0x800`) refused because
`mgs_fp_unavailable` requires `OS_CURRENTCONTEXT` to be sane. A one-shot
diagnostic now names the address, and it is a **real thread**:

    [fp] refused: OSCurrentContext = 0x7F4A5630 is not a usable context

`0x7F4A5630` is the prio-10 worker blocked in `gcn_worker_take_request` in
the thread dump. So the context is genuine; `context_is_sane` rejects it only
because it lives in the overlay's BAT-mapped window and the test predates
that window — it demands `addr >> 28 >= 8` and `off + 768 <= 24 MB`, and
`gptr` a few hundred lines above has known about both windows all along.

**Two fixes were tried.** Widening `context_is_sane` to accept the second
window; and, narrower, a structural test — `OSContext` is the first member of
`OSThread` (dolsdk2004 OSThread.h), so a genuine current context *equals*
`OSCurrentThread`, which uninitialised memory cannot satisfy by accident,
applied to the lazy-FP path alone.

**Both runs looked much worse, and both measurements are INCONCLUSIVE.**
They were taken while three `quartus_fit` processes were using roughly 1,800%
CPU between them; `uptime` read a load average of **32**. F220 established
that determinism here is load-dependent, and the evidence that it had gone is
in the numbers themselves: the two "bad" runs stopped at **2,595,625** and
**2,689,294** steps, two different values, where the good configuration
reproduces to the exact step (112,962,303, twice). A run that no longer
reproduces is not measuring the change.

**So nothing is concluded about either fix.** The tree is back to the state
that produced F236's result — strict `context_is_sane`, plus the new
diagnostic, which is worth keeping on its own. The dead helper was removed
rather than left commented-in, because a function nobody calls carrying a
conclusion nobody can support is worse than no function.

**This is the same trap as F222 and F220, walked into again**, and the tell
was available before the conclusion: F220's own rule is that a measurement
taken under load is not a measurement. Check `uptime` before believing a run,
not after.

**What the default path does, checked at the same time:** unchanged and
clean. 40M steps, no fault, `pc = 0x7F0F8450` at the step limit. The audio
work is behind `MGS_DSP_RESUME` and cannot affect it.

---

### F238 — correction: the picture is NOISE, and movie.dat never advanced

Ben ran it and said the screen was garbage. It is, and the evidence was in
the log I had already quoted.

**What F236 got right.** The audio path genuinely unblocked, and that part
stands: ring-0 tag-2 records 0 -> 204, the read cursor moving, the task mask
going `0x1 -> 0x0` instead of parking at `0x8`, four sound threads that had
been asleep now carrying traffic (queue read cursors 49, 68 and 26), and a
thread dump showing **`_vorbis_synthesis1`** actually running and
`gcn_stream_fill_task -> gcn_stream_serve_requests -> OSSendMessage` serving
data requests. None of that was happening before.

**What F236 got wrong.** "The movie decodes and draws." It does not. The
same log line I used as proof says `roughness 36 NOISE`, and the texture
summary says `fmt 0x6 512x448 mean 31 max 39` — the runtime's own measure of
"a few = artwork, tens = noise", which exists precisely to catch this. I
quoted the resolution and the lit percentage out of that line and stepped
over the word NOISE in the middle of it.

**And the reason is one line of the disc tally, unchanged across the fix:**

    demo.dat            17 reads   522240 bytes  last +0x3E48800   (was 14)
    shared/movie.dat     8 reads   262144 bytes  last +0x38000     (unchanged)

`movie.dat` is still read exactly as far as it was before any of this work —
262,144 bytes of a 94,935,040-byte file, 0.28%. **The video stream never
advanced at all.** What unblocked is `demo.dat`, which is the Ogg audio. So
the decoder is being driven over data it has already consumed and is emitting
noise, which is exactly what a full-screen texture at 99% lit with a
roughness of 36 is.

**So the state is:** the audio half of the streamed-media pipeline runs; the
video half is still stalled where F225 found it, and the visible result is
worse-looking than the freeze, not better. The two halves are separate
streams through separate rings — ring 0 (tag 2, from `demo.dat`) and ring 1
(tag `0xE`, the 321 video records) — and only ring 0 moved.

**The lesson, and it is the same one twice in one session.** F237 was about
believing a measurement taken under load; this is about believing a
measurement I had read selectively. The instrument was not missing and was
not wrong — `roughness/NOISE` exists for this exact purpose, was printed, and
I quoted around it.

---

### F239 — the event IS posted now, and the movie reaches its playing state

F238 said the video side was untouched by the audio fix. That was measured at
exit only, and it is wrong in an interesting way: the video side *did* move,
it just does not stay moved.

**F225's central claim no longer holds.** Searching guest memory for the
movie's key with `MGS_FIND_WORD=0x006647BA` finds it in two places:

    MEM1    0x8107F0B8 = 0x006647BA   the movie's own copy (context +0x38)
    overlay 0x7F4BEF90 = 0x006647BA   bss_253F0 + 0 -- THE EVENT TABLE

F225 watched 822 writes to that table and never saw this key. It is there
now. The entry is complete: key, `0x00010001` at +0x04, a payload pointer
`0x7F4BF794` (which is the buffer's own `+0x804`, just past the count word),
and `1` at +0x0C.

**And it is delivered.** Watching the context's `+0x38..+0x4F` through a run:

    +0x3C:  0 -> 1        the event arrives, state 1 is released
    +0x40:  0 -> -1       consumed and cleared, which is fn_1_149048's
                          first path: g->0x3C = g->0x40; g->0x40 = -1
    +0x3C:  1 -> 0
    +0x44:  0 -> 0x200    512
    +0x48:  0 -> 0x140    320

So the movie is told to start, and sets up a 512x320 picture.

**The state machine reaches PLAYING, twice, and is stopped both times.**
Watching the task node's `+0x44`:

    0 -> 2 -> 1 -> 2 -> 1      four transitions, then state 1 for ever

State 2 is where the movie does its work, and the important line is:

    0014948C  lwz r4, 0x3c(r30)     the stream
    00149490  lwz r3, 0x8(r4)       its playback clock
    00149494  addi r0, r3, 0xc
    00149498  stw r0, 0x8(r4)       advance by 12 per call

That clock is exactly what `fn_1_1482FC` compares record timestamps against
before claiming them, so **ring 1 drains only while state 2 runs**. It ran
twice. Hence 321 records still sitting there and `movie.dat` never read past
`+0x38000`.

**What stops it** is `mpeg_poll_stream_events`' code-**0** branch — an event
whose payload code is 0 rather than 1:

    001490D4  li  r0, 0x1
    001490D8  stw r0, 0x44(r3)      task back to state 1
    001490EC  stw r0, 0x3c(r3)      and g->0x3C = 0

Both `2 -> 1` transitions are at that pc. So something posts a stop.

**Not yet judged: whether that is even wrong.** Dolphin's same field cycles
too — `+0x3C` reads 1 at 25.4s, 0 at 25.7s and 1 again at 64.4s — so a start,
a stop 0.3s later and a restart is what the working run does as well. The
difference may be that ours never gets the restart, or that ours never
advances the clock far enough in between. That is the next measurement, and
it wants a quiet machine.

**So the corrected picture:** the audio fix unblocked the event path as well
as the audio path. The movie now starts, sizes its picture and enters its
playing state; it does not stay there long enough to consume a single video
record, and what is drawn is therefore still noise.

---

### F240 — the FP fix works, movie.dat streams, and real video frames decode

F237 recorded two attempted fixes for the `pc = 0x800` crash as *unjudged*,
because the runs that condemned them were taken at load average 32. The
machine went quiet (load 2.06) and the structural fix was re-tested. **It is
correct**, and the runs that rejected it were noise.

**The fix.** `mgs_fp_unavailable` now accepts a context outside MEM1 when the
OS itself vouches for it: `OSContext` is the first member of `OSThread`
(dolsdk2004 OSThread.h), so a genuine current context **equals**
`OSCurrentThread`, which uninitialised memory cannot satisfy by accident.
`context_is_sane`'s range test is untouched, and
`mgs_module_take_exception` still uses it — widening *that* changes interrupt
dispatch for the whole boot, which is a far larger blast radius than one
exception vector.

**The result, at 400M steps with `MGS_DSP_RESUME=1`:**

| | before | after |
|---|---|---|
| outcome | crash, `pc = 0x800` at 113M | **ran to the step limit, no fault** |
| `[fp] refused` | once, `0x7F4A5630` | never |
| AX frames | 17,568 | **62,927** |
| voice advances | 19,168 | **109,886** |
| `shared/movie.dat` | 8 reads, 262 KB, `+0x38000` | **27 reads, 792 KB, `+0xBE800`** |
| `demo.dat` | 17 reads, 522 KB | **110 reads, 3,045 KB** |
| textures decoded | 29 | **150** |

**`movie.dat` is streaming.** That file has been pinned at `+0x38000` —
0.28% of it — since F187, through every finding from F225 to F239. It now
advances.

**And real video frames decode.** The texture table gains a shape that was
never there before:

    fmt 0x1  512x320   x5    roughness mean 1  max 2

`512x320` is exactly what the movie context reported in `+0x44`/`+0x48`
(F239), and roughness 1-2 is the runtime's own scale for **artwork, not
noise**. Five genuine movie frames were decoded.

**What is still wrong, and not overstated this time.** The `fmt 0x6 512x448`
shape is still decoded 121 times at roughness 25 - that is still noise, and
it is what dominates the screen. And there are **3,198 texture refusals, all
`palette`** — the cache is refusing paletted (CI-format) textures, which is a
GX-side problem and was invisible while the movie never got this far. Neither
of those is the movie stream any more; they are what the movie's output path
does with it.

**The process lesson, earned twice today.** F237's caution was right to
refuse to judge, and refusing was worth more than the wrong answer would have
been: the fix that looked catastrophic under load is the fix. Re-test on a
quiet machine before discarding a change.

---

### F241 — 3,198 palette refusals were 3 bits of address the hardware ignores

F240 left "3,198 texture refusals, all `palette`" as the next thread. It is
one line, and the fix is exact.

**What was refused.** The refusal path printed a count and nothing else, so
it was made to say which address, as the decode path already did:

    [tex] refused PALETTE: format=0x9 335x17 tlut=0x88DC2600 entries=256
    2388 x  64x64   tlut=0x88DC1C00 entries=16
     512 x  64x64   tlut=0x84DC1C00 entries=16
      42 x 335x17   tlut=0x88DC2600 entries=256

`guest_ptr` folds an address with `& 0x3FFFFFFF`, so `0x88DC2600` becomes an
offset of **148 MB** into a 24 MB block and `0x84DC1C00` one of 81 MB. Both
are refused, correctly — they are not addresses.

**Why they look like that.** `BP_LOAD_TLUT0` carries the palette's address in
32-byte units, and this code took 24 bits of it. The GameCube decodes 25 bits
of the *shifted* value and ignores anything above. Masking accordingly:

    0x08DC2600 & 0x01FFFFFF -> 0x80DC2600
    0x08DC1C00 & 0x01FFFFFF -> 0x80DC1C00
    0x04DC1C00 & 0x01FFFFFF -> 0x80DC1C00

**The third line is the check.** Two *different* register values collapse onto
the same palette, which a wrong mask would not do — it is what distinguishes
this from a mask that merely brings the number into range.

**Dolphin says the same thing and names the symptom**, in `BPStructs.cpp`:
`addr = addr & 0x01FFFFFF` with the comment "The GameCube ignores the upper
bits of this address. Some games (WW, MKDD) set them." Twin Snakes is another
such game. Recorded in `THIRD_PARTY.md` against `extern/dolphin` @ `ee018d0`
per rule 11.

**Result:**

    texture refusals: 0 size, 0 texels, 0 palette, 0 alloc, 0 decode
    textures: 151 decoded, 29338 hits, 151 misses, 0 refused, 0 evicted

and a texture shape that had never decoded appears, clean:
`fmt 0x9 335x17 mean 0 max 0`.

**What this does NOT fix.** `fmt 0x6 512x448` still decodes at roughness 25,
which is still the noise on screen. That is a different texture and a
different problem; the paletted ones are CI-format artwork (UI and overlays),
not the movie's video frame.

---

### F242 — the TEV sampled one texture for every stage, and the movie needs three

The screenshot Ben sent is the diagnosis: readable subtitles over a video
frame of green-and-magenta **vertical stripes**, with a luminance structure
visibly correct underneath. That is a colour-plane problem, not a decode one.

**The renderer said so itself.** In the GX report:

    TEV stages per triangle:  1:2491121  3:64

Sixty-four triangles use **three** TEV stages; everything else in the game
uses one. And `raster.c` documented the limitation exactly where it bit:

> The combiner runs every stage the general-mode register asks for, but
> exactly one texture is sampled and it is stage zero's. A stage that binds
> its own texture therefore sees stage zero's texel, or none at all.

The movie composites its frame from a luminance plane and two chroma planes
in three stages. Stages 1 and 2 were sampling **luma**, so the chroma came
out as whatever the luminance happened to be — green and magenta over a
correct-looking picture, which is what the screenshot shows.

**The fix.** `MgsTevInput` gains `stage_tex`/`stage_has`, NULL meaning "every
stage shares one texture"; `mgs_tev_run_compiled` takes each stage's own
texel when they are present. The rasteriser resolves every stage's map,
coordinate set, wrap mode and filter once per triangle, and samples each per
pixel — each stage has its own coordinate set, so there is no shortcut that
reuses stage zero's interpolation. All of it is skipped unless more than one
stage actually binds a texture, which is 64 triangles out of 2,491,185.

**Evidence it works:** a texture shape appears that never existed before —

    fmt 0x1  256x160   mean 0  max 0

`256x160` is exactly half `512x320`, the luma plane's size. That is a **chroma
plane at 4:2:0**, and it can only be decoded if a later stage is now binding
its own map. Textures decoded: 151 -> 161.

**Not yet confirmed on screen.** The `fmt 0x6 512x448` surface still reports
roughness 25, but that is the composited EFB copy read back as a texture, and
its roughness is measured at decode rather than after the combiner runs. What
the picture actually looks like now needs a person to look at it. Given how
today has gone, that is stated as pending rather than claimed.

---

### F243 — the palette was not part of a texture's identity, and 5 frames in 200,000

Ben played it and reported three things. Two are progress and one is a new
diagnosis handed over ready-made.

**"Some movie frames are not garbage."** The per-stage TEV fix (F242) works
in part. The decoded shapes confirm the structure is right:

    fmt 0x1  512x320   x5     luma
    fmt 0x1  256x160   x10    chroma, exactly two per luma frame

Two chroma planes per luminance frame is 4:2:0 and is what a correct YUV
triple looks like. Before F242 there were none.

**"Subtitles change colour, pink when garbage and blue when not."** That
correlation is the bug report. The texture cache keyed a paletted texture on
its TLUT **address** and hashed only the **texel** bytes, so a palette
reloaded with different colours at the same address returned the previous
decode. The subtitle's colour therefore tracked whatever the movie had last
loaded into that TLUT — which is precisely the symptom, and it would never
have been found from the metrics, because a stale cache hit looks like a hit.

Fixed by folding the palette bytes into the same content hash, so a
recoloured palette is a content change and the existing "same texture, new
contents: take this slot back" path handles it.

**Honestly: not verified against the symptom.** The run after the fix decodes
the same 161 textures as the run before, so this sequence never actually
recolours a palette in place. The fix removes a real class of stale-cache
bug and costs nothing; whether it is *the* cause of the pink/blue subtitles
needs a look at the screen.

**"About one frame for a few seconds, then it skips."** Measured, and it is
stark: **5 luma frames decoded across 200,000 retraces**. The movie only
decodes while its task is in state 2, and F239 measured that state machine
running `0 -> 2 -> 1 -> 2 -> 1` — **two visits to the playing state in a
whole run**. So this is not a frame-rate problem or a decoder problem: the
movie is being *stopped*, repeatedly, by the code-0 events F239 found, and
spends almost all of its time parked in state 1.

**"Starting the game freezes."** Noted, not yet investigated. Ben attributes
it to unnamed functions; that is unlikely to be the mechanism — names are
documentation, not behaviour — so it deserves its own measurement rather
than an assumption.

---

### F244 — the 8% CPU is the frame cap, and MGS_TICK_RATE=338 makes it WORSE

Ben reported the port using only 8% of a 32-core machine while running
slowly, with the menu slow and the movie showing one frame per chunk, and
sent a per-core graph: **all 32 cores active, none saturated, 21.3% total**.

**The CPU figure is the frame cap, doing its job.** Same 40M steps, headless:

    uncapped          wall  6.17 s   cpu 502%
    MGS_FPS_CAP=60    wall 16.70 s   cpu 177%

502% x 6.17s is 31 core-seconds; 177% x 16.70s is 29.6. **Identical work,
spread over 2.7x the wall clock** — the cap `nanosleep`s to hold 60 XFB
copies a second and the idle time is the sleep. Low CPU here is not a
symptom of anything.

**The emulator is not slow.** Headless it runs **7.1M steps/sec**, producing
3,559 retraces/sec — **59x real time**. Nothing about throughput explains a
slow menu.

**What IS wrong is the ratio.** In the capped run the guest takes **20,000
retraces to draw 893 frames** — one drawn frame per 22 retraces, where
hardware draws one per retrace. The guest is burning steps without
progressing, and the hot-call table says where: `OSDisableInterrupts` and
`OSRestoreInterrupts` at **1,478,914 calls each** in 40M steps — a critical
section entered every 27 steps — with `OSGetTime` at 426,827. That is a
spin, in a timed wait.

**And the obvious fix is measured WRONG.** `host/module.c` documents the
clock as miscalibrated and gives the figure: "the calibrated figure for
60 Hz is 675000/2000 = 337 or 338", noting that with it wrong "anything that
compares elapsed time to a frame number - a movie player, most obviously -
sees almost no time passing". That is our exact symptom, so
`MGS_TICK_RATE=338` looked certain. It regresses:

| | tick 32 (default) | tick 338 |
|---|---|---|
| luma frames decoded | 5 | **1** |
| `movie.dat` reached | `+0xBE800` | **`+0x38000`** |
| task mask at exit | running | **parked at `0x8`** |
| ring 1 | draining | **321 records, untouched** |

With 338 the movie returns to exactly the stall F225 described. So the
documented calibration is either wrong or is not independent of something
else that was tuned around the current value — and the note in `module.c`
should not be trusted as an instruction until that is understood. **Measured
on a quiet machine (load ~2), not the F237 trap.**

**Also from Ben's report, and worth separating:** the intro plays at the
correct speed and the subtitles are correctly timed, while the menu is slow
and the movie shows one frame per chunk. Whatever this is, it is selective —
which argues against a global clock scale and for something per-subsystem.

---

### F245 — video time ran 10.6x ahead of audio time, and the movie clock is audio

Ben's correction — "it plays the first frame or two of each chunk", not
"slow" — pointed at a ratio rather than a rate, and the ratio is exact.

**The movie's clock is the AUDIO position.** In `mpeg_movie_task`'s playing
state:

    001494E0  bl    fn_80056800      gate
    001494F4  bl    fn_8005190C      the sound system's playback position
    001494F8  mulli r5, r3, 0x12c    x 300
    00149508  divw  r4, r5, 0x3e8    / 1000
    00149510  stw   r0, 0x8(r3)      -> stream->0x08, the movie clock

`fn_8005190C` is in `sd_sound.c`. The `+0xC` per call seen in F239 is only
the fallback when the gate returns zero. **F218's original claim — "the
movie is paced by audio consumption" — is right, and here is the
instruction that does it.** It was withdrawn in F222/F223 on pacing
evidence; the code says otherwise.

**The two clocks disagreed by 10.6x.** From one run:

    audio DMA:      62,932 interrupts, 314.66 s of sound
    AX voice model: 62,927 frames            <- one per AID, as designed
    retrace ticks: 200,000                   <- 3,333 s at 60 Hz

That is arithmetic, not mystery. Retrace fired every **2,000 steps**, and at
32 ticks a step that is 64,000 ticks a field where a real 60 Hz field is
**675,000**. So the screen ran 10.5x ahead of the clock the movie is slaved
to, and every decoded frame was held for about ten screen frames — exactly
"the first frame or two of each chunk".

**Fixed by deriving the period from the clock** rather than leaving two
constants that must agree and did not: `675000 / tick_rate` steps, which is
21,094 at the current rate, overridable with `MGS_RETRACE_STEPS`.

| | before | after |
|---|---|---|
| retrace ticks | 200,000 (3,333 s) | **18,964 (316 s)** |
| audio | 314.66 s | 313.82 s |
| video vs audio | **10.6x apart** | **0.7% apart** |
| luma frames | 5 | **7** |
| chroma planes | 10 | **14** |
| `movie.dat` | 27 reads, 792 KB | **34 reads, 1,015 KB** |

**Why `MGS_TICK_RATE=338` was the wrong end of it** (F244). Raising the tick
rate fixes the same ratio by making guest time — and therefore the audio —
run 10x faster, which is why the movie got worse rather than better. Slowing
the screen to match the clock leaves the audio alone, which is the half that
was already right.

---

### F246 — phase 2c begun: a real mixer and an audio device, and it is silent

Audio moved from phase 4 to 2c (see MILESTONES and the design document). The
first half is built; it does not make a sound yet, and the instrument that
says so is part of the work.

**What exists now.** `runtime/platform/sdl_audio.c` opens a 32 kHz stereo
SDL3 device and queues finished frames; it degrades to a no-op headless and
under `MGS_NO_AUDIO`, so a batch run keeps identical guest timing without a
device. `host/ax_dsp.c` is no longer a position model: it walks `__AXPB`,
reads each running voice's samples out of ARAM, applies the voice envelope
(`ve.currentVolume`) and the per-voice mix levels (`mix.vL`/`vR`), sums into
a 160-sample stereo frame and pushes it. **The position now advances because
samples were consumed**, rather than by a formula.

Formats are from `dolsdk2004`'s `AXSetVoiceAddr`: 0 ADPCM (nibble-addressed),
10 PCM16 (sample-addressed), 25 PCM8. PCM16 and PCM8 are decoded; ADPCM is
counted and skipped rather than mixed as zero, because "no sound" and "sound
we cannot decode" are different faults.

**It is silent, and the report says so rather than leaving it to be
discovered:**

    AX mixer: 62763 frames (313.8s of sound), 105958 voice-mixes
      ADPCM skipped: 368; samples outside ARAM: 0; voices starved: 105922
      output: peak 0 of 32767 (0.0%), 0 of 62763 frames not silent

**A wrong fix, caught by the peak meter.** The first version wrapped a voice
whenever `curr > end`, which produced **286,628 loops in 62,763 frames** —
4.5 per frame. A streaming voice holds `loop > end` between refills (ours
sits at loop `0x3000`, end `0x2FFF` before the first one), so wrapping lands
past the end again and re-wraps every sample. That is now treated as a voice
**waiting for data**: hold the position, leave it running, count it. Loops
fell to 0 and starves rose to 105,922, which is the honest picture.

**Where the silence comes from is NOT yet established.** What is known:

- The addressing is right. ARAM is written over `0x00004000`-`0x02000020`,
  and our movie voice reads sample `0x2000`, which is byte `0x4000` — the
  exact start of the written region.
- No read falls outside ARAM (`samples outside ARAM: 0`).
- The field decoding is checked against a raw dump: PB `+0x70..0x7D` reads
  `000A 0000 6000 0000 6FFF 0000 650E`, giving format 10, loop `0x6000`, end
  `0x6FFF`, current `0x650E` — consistent, and `curr < end`.

So the voice reads inside a region the game has written, with sane addresses,
and gets zeros. The candidates are that the region was written with silence,
that the samples live somewhere other than where this assumes, or that the
voice is reading a buffer the game has not filled yet — and those are
distinguishable by dumping ARAM at the read address, which is the next step.

---

### F247 — THERE IS SOUND. The console had two ARAMs and the mixer read the empty one

F246's mixer was correct and produced silence. The cause was not in the
mixer at all.

**Two buffers for one piece of hardware.** `mgs_aram_init` did this:

    a->data = (uint8_t*)calloc(1, MGS_ARAM_SIZE);   /* its own 16 MB */
    a->mem  = mem;                                  /* and the guest's, unused */

So the DMA filled one 16 MB store and everything reading `GuestMemory.aram`
saw a different one, permanently zero. **Nothing noticed for as long as only
the DMA used it** — `__ARChecksize` probes through the same path and passes
either way — and it surfaced the instant something *read* a voice's samples.

**How it was cornered.** Each step ruled out the mixer rather than the data:

- `MGS_DUMP_ARAM` (new) showed real PCM16 at the read address: `FFFD FFFC
  FFFC 0003 FFFD FFFA`, **103 of 128 bytes non-zero**. The data existed.
- `MGS_TRACE_AXMIX` (new) showed the voices behaving correctly — **two of
  them, 62 and 63, a stereo pair**, format 10, volumes `0x7FFF`, advancing
  by exactly **`0xDC` = 220 samples a frame**, which is `160 x 1.3769`, the
  SRC ratio. Addresses, volumes and rate were all right.
- Correct reader, correct address, real data, zero result. The only
  remaining possibility was that the two were not the same memory. They were
  not.

**Fixed** by having the ARAM model borrow `GuestMemory`'s allocation instead
of making its own, and not freeing what it does not own.

    before   peak 0 of 32767,     0 of 62,763 frames not silent
    after    peak 23 of 32767, 52,979 of 62,763 frames not silent

**Peak 23 is quiet and that may be correct** — the samples at the read
address are `+/-5`, which is a fade-in — but it is not yet confirmed that the
level is right, only that it is no longer zero.

**One number is NOT judged.** `movie.dat` read 8 times in this run against 34
before. The machine was at **load average 36** with `quartus_fit` running,
which is the F237 trap exactly, and a streaming count is precisely the kind
of timing-dependent measurement that load destroys. The sound result
survives it (zero versus non-zero is presence, not timing); the movie number
does not, and is to be re-measured on a quiet machine before anyone concludes
the ARAM change regressed it.

---

### F248 — ADPCM decoded, and the level goes from 0.1% to 32.8% of full scale

F247 got sound out of the mixer but only at peak 23 of 32767, because the
only formats decoded were PCM16 and PCM8 and **364 voices a run were ADPCM**,
counted and skipped. Those are the game's own sounds; the PCM voices are the
streamed movie audio, and they are quiet where the stream fades in.

**The decoder.** DSP-ADPCM with the coefficients the parameter block already
carries: `AXPBADPCM` at `pb+0x7E` — eight coefficient pairs, then the
frame's predictor/scale byte at `+0xA0` and the two previous outputs at
`+0xA2`/`+0xA4`. Sixteen nibbles to an 8-byte frame, the first two being the
header, which is exactly what `AXSetVoiceAddr`'s assert is guarding when it
refuses an address whose low nibble is 0 or 1.

**Why it could not be written like the PCM path.** ADPCM is a second-order
predictor: each output depends on the two before it. PCM can be read at
whatever position the resampler asks for; this cannot. Jumping straight to
the target nibble would decode against the wrong history and produce
something that still looks like audio and is noise. So `adpcm_step` decodes
exactly one nibble, and the mixer steps it over **every** nibble the
resampler passed — one or two per output sample at this game's 1.38 ratio.

**Result:**

| | before | after |
|---|---|---|
| peak | 23 (0.1% FS) | **10,750 (32.8% FS)** |
| non-silent frames | 52,979 | 53,159 |
| ADPCM voices | 364 skipped | **0 skipped, 81,072 samples decoded** |

32.8% of full scale is an ordinary game mix, which is the first evidence
that the levels are in the right region rather than merely non-zero.

**Still unverified by ear.** Level, pitch and channel assignment are all
plausible-looking numbers; none of them is a listen. `MGS_NO_AUDIO` keeps
the mixer running without a device, so a batch run's timing is unchanged.

---

### F249 — the voice position IS the stream's clock, and pinning it stalls everything

Ben tested and reported the movie stuck again. It was, and the cause was my
own "tidy" fix from F246.

**Bisected, not guessed.** `MGS_AX_NO_ADPCM` was added to restore the
pre-F248 behaviour exactly: with ADPCM decoding disabled, `movie.dat` still
read **8** times. So F248 was not the cause, and the remaining suspect was
F246's starve handling.

**What that fix did.** When a streaming voice holds `loop > end` — the game
saying "continue at `loop`" before it has extended `end` — F246 pinned the
voice at `end` and stopped advancing it. That is tidy and it is fatal:
`sd_stream_pump` decides a block has been consumed **by watching that
position move**. A position that stops is a stream that is never refilled.

Three behaviours, measured:

| on `loop > end` | movie.dat | non-silent frames |
|---|---|---|
| pin at `end` (F246) | **8 reads** — the F225 stall | 53,159 |
| run on past `end` | **34 reads** | 369 |
| **jump to `loop`** | **34 reads** | 402 |

Jumping to the loop point is what the hardware does and is what is now in
the tree: the position moves forward *and* moves to where the data is.

**The movie is back**: 34 reads of `movie.dat`, 124 of `demo.dat`, 7 luma
frames and 14 chroma planes, with the mixer still at peak 10,750.

**What is still wrong, plainly.** Only **402 of 62,763** frames are
non-silent. The PCM voices spend most of their time reading a block the game
has not written yet, so the movie's own audio is largely silence even though
the ADPCM voices give a healthy peak. The fix is not another guess at the
wrap rule: it is that the voice should not be overrunning at all, which means
the refill is late, which is the next thing to measure.

**The lesson.** F246's starve handling was introduced to fix a real defect
(286,628 wraps in 62,763 frames) and replaced it with a worse one, because
"hold still when you have no data" is correct for an audio device and wrong
for a clock. In this game the voice position is not only how sound is played;
it is how the engine tells the time.

---

### F250 — the missing sound is gain, not samples: 99% of voice-mixes have volume 0

Chasing why the mixer produces 32.8% peak but only 405 of 62,763 non-silent
frames, each measurement eliminated a layer until only one was left.

| measured | result | conclusion |
|---|---|---|
| samples read outside ARAM | **0** | addressing is right |
| loop points holding data on overrun | **253,816** vs 3,287 empty | the refill is neither late nor misplaced |
| PCM samples non-zero | **16,786,653 of 16,953,280 (99%)** | the data is real |
| **voice-mixes with envelope volume 0** | **105,498 of 106,326 (99.2%)** | **this is the silence** |

So the samples are there and correct, and almost every voice is being mixed
at a gain of zero. The ~828 mixes that *do* have gain are what produce the
peak of 10,754 — the sound that exists is loud enough; there is simply
almost none of it.

**Two fixes made on the way, both real, neither the cause.**

1. *Scaling truncated twice.* The mix was `sv * vol / 32768 * vr / 32768`,
   and AX's volumes are `0x7FFF` — a shade **under** unity — so each divide
   truncated toward zero and quiet samples were discarded twice over. Now
   one multiply and one rounded shift. Correct, and worth 3 frames.
2. *The volume is a ramp.* `AXPBVE` is `{currentVolume, currentDelta}` and
   the DSP advances it every sample; a game that starts a voice at 0 with a
   positive delta fades in, and reading the level without applying the delta
   would leave it silent for ever. Implemented — and it changed nothing,
   because the deltas are zero too. **So the voices are not mid-fade; they
   are simply at zero.**

**What that leaves.** `ve.currentVolume` lives in the DSP-side block, and AX
copies it there from the user block only when the sync flags ask
(`__AXServiceVPB`, F235). Either the game never sets a volume on these
voices — plausible if most of the 64 are idle with `state` left at 1 — or our
AX is not propagating volume from the user block to the DSP block. Those are
distinguishable by comparing `ve.currentVolume` in the two blocks for the
same voice, which is the next measurement and is cheap.

**Not claimed:** that the mixer is finished, or that what it produces is
correct. What is established is that the remaining gap is gain, not samples,
not addressing and not the refill.

---

### F251 — PARTLY WRONG (see F252): "end is never extended" came from the first ten overruns

F250 left "is the game not setting a volume, or is AX not propagating it".
Neither: the game is **deliberately fading the voice out**, and the reason it
does so is upstream.

**The volume is propagated and then ramped down.** Watching the DSP-side
`ve.currentVolume` for the movie voice:

    0x7FFF0000   volume 7FFF, delta 0        <- set, by __AXServiceVPB
    0x7FFFFFFE   delta -2
    0x7E880000   volume 7E88
    0x2AFFFF78   volume 2AFF, delta -136
    0x0E42FFD3   volume 0E42, delta -45

Written by `__AXServiceVPB` (`0x80033330`) and two sound-layer functions. So
propagation works and the deltas are real — the sound system is muting this
voice on purpose. It is not a gain bug; it is the sound system reacting to a
stream that is failing.

**Why it is failing, from the geometry.** Logging the first overruns:

    [axovr] voice 62 curr 00003000 end 00002FFF loop 00003000 span -1
            curr 00003001 -> overrun -> back to 3000, for ever

`end` is **never extended**. `loop` is `end + 1`, so the voice ping-pongs
across a one-sample boundary: the position never advances, and
`sd_stream_pump` decides a block is consumed *by watching that position
move*. 257,103 overruns in a run is this, over and over.

**And the oracle shows how wrong the geometry is.** Dolphin's same parameter
block, decoded from the words that straddle the halfword fields:

    0x801F998C = 0x80020074   loopAddressLo 8002, endAddressHi 0074
    0x801F9990 = 0xA0F70060   endAddressLo  A0F7, currentAddressHi 0060

so **end = 0x0074A0F7 — 7,643,383 samples** — and the current address climbs
steadily, its high halfword ticking about every 1.3 s, which is ~50k
samples/sec and therefore a 48 kHz voice playing a large contiguous region.

Ours is `end = 0x2FFF`, later `0x6FFF`: **twelve thousand samples against
seven and a half million.** That is a structural difference, not a timing
one. A voice given a 15 MB region overruns rarely; one given 4 KB overruns
constantly, and everything else here — the fade-out, the silence, the
starves — follows from it.

**Open, and the next thing to establish:** whether Dolphin's voice 62 is the
same voice as ours (the indices need not match) and, if it is, which call
sets that end address and why ours gets a small one. Comparing the two runs'
`AXSetVoiceAddr` arguments would settle it directly.

---

### F252 — correcting F251, and the movie's audio voices are deliberately muted

F251 concluded that the voice's `end` address is never extended and is tiny
against the oracle's 7.6 million samples. **Both halves were read off the
first ten overruns**, which happen before AX has copied the address block
into the DSP-side parameter block — so they show initial values and are not
the steady state. The instrument was sampling the wrong end of the run, and
the trace now fires every 20,000th mix instead of on the first few.

**What the steady state actually shows:**

    voice 62 fmt  0 curr 006025D2 end 0074A0F7 loop 00108002 once vol 7FFF
    voice 62 fmt 10 curr 00003D9C end 00003FFF loop 00004000 loop vol 0000
             fmt 10 curr 00002B18 end 00002FFF loop 00002000 loop vol 0000
             fmt 10 curr 000081B9 end 00008FFF loop 00008000 loop vol 0000

Three corrections to F251:

1. `end` **is** extended — the PCM voices move through 0x1000-sample blocks
   (`0x2FFF`, `0x3FFF`, `0x5FFF`, `0x8FFF`), so the streaming works.
2. The DSP-side block **does** get the big region: the same
   `end = 0x0074A0F7` the oracle has, written by `__AXServiceVPB`. F251's
   "structural difference" was an artefact of comparing our *initial* values
   against Dolphin's *steady* ones.
3. A single PB index is reused by different voices over time, so "voice 62"
   is not one thing across a run.

**What is actually wrong.** The two kinds of voice behave completely
differently:

| | format | volume | window |
|---|---|---|---|
| the game's own sound | ADPCM (0) | **0x7FFF** | 7.6M samples |
| the movie's audio | PCM16 (10) | **0x0000** | 0x1000-sample blocks |

**Every PCM voice carries `vol 0000`.** The movie's audio is muted by the
game, which is why there is no movie sound however well the mixer works. The
ADPCM voice is at full volume and is what produces the peak of 10,754 — but
it is only in `state == 1` for about 507 frames of 62,763, which is why
almost every output frame is silent.

So there are two separate questions, and they had been conflated:

- **Why are the PCM voices muted?** F250 showed the sound system ramping this
  voice down with real deltas, so it is a deliberate fade. The likeliest
  cause remains the stream underrunning — the ping-pong at `loop = end + 1`,
  4 overruns a frame where a 0x1000-sample block consumed at 220 samples a
  frame should last 18.
- **Why is the ADPCM voice active so rarely?** Unknown, and not yet
  investigated.

---

### F253 — the missing movie audio is a CONSEQUENCE of the movie stalling

Watching the user-side `ve.currentVolume` for the movie's voice gives the
whole answer in one sequence:

    7FFF -> 7E88 -> 7FFF -> 2A62 -> 0E08 -> 04A5 -> 0189 -> 0082
         -> 002B -> 000E -> 0004 -> 0

A clean exponential decay — a **fade-out**, not a failure. Written by
`fn_8003667C`, called from `lr 0x8004EBA8`, which is **inside
`sd_ax_frame_callback`** at its call to `fn_80036648`: the game's own
per-AX-frame volume service, ramping the voice down on purpose.

**So the mixer is not the problem.** It decodes PCM16, PCM8 and ADPCM, reads
the right addresses, applies the right gains and reaches 32.8% of full scale
on the voice that *is* audible. The movie's audio is silent because **the
game silenced it**, and the game silenced it because the movie is not
playing — the state machine that reaches `state 2` twice and parks (F239).

**This inverts the order of work.** The audio was moved to phase 2c on the
grounds that it is the clock (F245, and it is), and the remaining silence was
being chased as an audio defect. It is not: it is the movie stall seen from
the audio side. Fixing the movie fixes the sound; polishing the mixer
further cannot.

**What is genuinely finished in the sound path**, and worth stating so it is
not re-litigated:

- an SDL3 device, 32 kHz stereo, no-op headless and under `MGS_NO_AUDIO`
- a voice mixer reading ARAM, with per-voice SRC, envelope and mix levels
- PCM16, PCM8 and DSP-ADPCM decoding, the last stepping nibble by nibble
  because the predictor is stateful
- positions advanced by actual consumption, which is what the engine's
  stream pump reads to tell the time

**What is not:** the movie's audio will stay silent until the movie plays,
and the ADPCM voice that does play is only in `state == 1` for about 507
frames of 62,763, which is a separate and un-investigated question.

---

*Record further findings here as they are established — including the ones that
turned out wrong. They are worth more than a clean narrative.*


### F254 — WRONG (see F264): the port was NOT deterministic, and the two runs that said so agreed by luck

Two headless runs, identical arguments, taken while the machine sat at load
average 19 with three Quartus jobs running:

    zd1.log  211 lines  10638 bytes
    zd2.log  211 lines  10638 bytes

They differ in **one** line, and it is the output filename each was told to
write.

**THIS CONCLUSION WAS WRONG, AND IT CONTRADICTED THE PROJECT'S OWN RECORD.**
F204 had already found "three 120M runs where two were byte-identical and the
third was not", and the comment above `MGS_JOBS` in `host/main.c` says so in
as many words. Two agreeing runs cannot establish determinism when the known
failure mode is *one run in three*; I generalised from the smallest possible
sample and did not check the record first. Measured properly later, the same
build crashed in **4 of 8 runs** (F264). The cause was a genuine data race,
now fixed - but the reasoning here was unsound before the race was found, and
would have been unsound even if the port had turned out to be deterministic.

**What survives.** The three specific mechanisms checked below are all still
true and still worth having: guest time is derived from guest ticks, DVD
completion is decided by the guest's clock, and AX frames are driven by guest
mail. None of that was the source of the divergence. What does not survive is
the conclusion drawn from them, because "I checked three ways host time could
leak in and found none" does not establish that there is no fourth.

 Everything else matches exactly: `retrace ticks: 20000`, `interrupts
delivered: 29207`, `refused while masked: 9097`, `handler failed: 4`,
`best frame: 26570 lit pixels (11.6%)`.

**This corrects the working assumption behind F237.** F237's lesson was real
— two builds were judged at load average 32 and the *reverted* one failed
too — but the conclusion drawn from it grew too broad: "do not measure while
the machine is loaded". That is true only of **wall-clock** judgements (does
it run at full speed, does it finish in time). Anything counted in guest
steps is a function of the guest's own clock and does not move with host
load, so it can be measured whenever.

Why it holds, checked in the code rather than assumed — three places where
host timing could have leaked into the guest, and all three refuse it:

- `runtime/os/os_time.c` — `OSGetTime`/`OSGetTick` return `rt->ticks`, a
  count the frame loop advances. Never `clock_gettime`.
- `runtime/dvd/dvd.c:142` — a read completes when the **guest's** tick
  passes `ready_tick`, and `drain` then spins on an acquire load
  (`while (!__atomic_load_n(&req->done, __ATOMIC_ACQUIRE));`) rather than
  completing early. Host I/O speed changes how long that spin takes, not
  what the guest sees.
- `host/interrupt.c:478` — an AX frame is driven by DSP mail, which the
  guest posts. `mgs_audio_queued()` is a statistic and is fed back to
  nothing; `grep` across the tree confirms no caller outside
  `sdl_audio.c`.

The one place host time does enter is the frame-rate cap in
`host/display.c:318`, and it is **off in headless** for exactly this reason.

**What this costs if forgotten:** a measurement discarded for being "taken
under load" is a measurement that has to be taken again. One was discarded
this session on those grounds and the real fault was elsewhere entirely —
that run was made from a part-edited build, which is why it died at 3.65M
steps with `pc = 0x00000800`. **Check what the binary was before blaming the
machine.**

**What not to re-propose:** waiting for a quiet machine before running a
headless measurement. Judge wall-clock claims that way, nothing else.

### F255 — hardcoded guest addresses go stale, and they fail SILENTLY

A watch on the movie context at `0x8107F0BC` reported **zero writes**, and
zero writes reads exactly like "this field is never written". It is not: the
context is a heap allocation, and the retrace-period change, the mixer and
the ARAM fix had each moved it. The address was live when it was written
down and pointed at nothing by the time it was used.

The overlay's `.bss` base moves too — `0x7F4BEF90` in one run, `0x7F499BA0`
in the next — so even addresses derived from it have to be re-derived per
run, not copied between them.

`mgs_report_movie` (`host/heaps.c`, `MGS_REPORT_MOVIE=1`) resolves the whole
chain from `.bss` instead: the context and task node from `bss+0x55EA4` and
`bss+0x55EA8`, both record rings from `bss+0x55BF4`. Same picture in any
run, and a moved allocation now shows as a null pointer rather than as a
field that is never written.

First use of it contradicted an assumption immediately: at 40M steps the
movie context and task node are **both null**. The movie chain is not
stalled there, it is **not yet allocated** — 40M steps is before the movie
starts, and every earlier conclusion drawn at that step count needs reading
with that in mind.


### F256 — an undelivered interrupt was DESTROYED, not held; the port manufactured a hole the console does not have

`mgs_interrupt_raise` enters the guest's dispatcher by taking an exception.
When that failed - no current `OSContext` - the old code took the cause bit
back off, with the comment "leaving it set would have the guest service a
stale interrupt the moment it does become ready".

**That reasoning is backwards, and it contradicts the rule the very next
comment in the same file spends a paragraph establishing.** An interrupt
raised and never delivered is not stale, it is OUTSTANDING: the device
really did complete, nothing acknowledged it, and the line is still
asserted. Hardware holds it high until a handler writes the bit back.

The window it lands in is a **thread switch** - there is no current
OSContext for a few instructions between `OSClearContext` and the next
`OSSetCurrentContext`. On console that window is covered by `MSR[EE]` being
clear. Our delivery tests EE separately, so the hole is ours, not the
hardware's.

It fires mid-run, not during startup: the one-shot notice lands at line 150
of a 690-line log, long after boot.

Leaving the bit set costs nothing, because `mgs_interrupt_pending` already
re-offers exactly while `cause & mask` says the guest still has that source
armed. Measured, same build otherwise:

    dropped (handler failed)        4  ->      1
    interrupts delivered       29,207  -> 95,026

Sanity check on the new figure rather than trusting the direction of the
change: VI at 50 Hz plus AX frames near 190 Hz over the ~190 s of game time
in the step budget predicts tens of thousands, so 95,026 is the right order
and **29,207 was far too few**.

**It did not fix the movie.** The stall is unchanged at 8 reads of
`movie.dat`. Recorded as a correctness fix on hardware grounds (rule 12,
order of authority 1), not as a movie fix.

### F257 — the oracle says our sound stream runs a state machine the console never enters

The streamed-sound pipeline keeps a state byte per buffer at `+0x2038` of
the two objects hanging off the stream at `+0x28`/`+0x2C` (`0x8022CC58` and
`0x8022ECB8` - **main.dol addresses, so they are the same in Dolphin**,
which is what made this comparable at all).

    Dolphin   1 -> 2 -> 1 -> 2 ...   384 transitions in 125 s, forever
    our port  0 -> 4 -> 1 -> 2 -> 3 -> 4 -> 1 -> 2 ... then STUCK at 2

**The console oscillates between two states. It never sets 3 or 4 at all.**
Ours enters both every cycle and finally parks in 2, where the 2->3 claim
never comes.

The states are reached from a switch on message type, through a jump table
at `0x801E7C48` covering types 5..16:

    type  5  -> calls the buffer servicer with its "reset" argument, which
                is what writes state 4
    type 10  -> normal service; writes 1, then 2
    type 11  -> writes 3 -> 4
    type 16  -> reaches the claim, which takes 2 -> 3
    others   -> ignored

So our port is delivering message types the console does not, or delivering
them in an order it does not. **That is the next thing to chase, and it is
the movie's blocker**: when the pipeline parks, all four sound threads are
drained and waiting -

    0x8027B640  on 0x8027B4D8, 70 messages carried
    0x802134E8  on 0x802133A4, 68
    0x80215920  on 0x80213384, 34
    0x80217D58  on 0x80213364, 30

and the movie's ring consumer dies with them. Against the oracle, on the
ring the movie actually streams through (`ring 0`, buffer `0x81741AA0` -
the same MEM1 address in both):

    Dolphin   consumer 1895 advances, producer   68   (~20/s, never stops)
    our port  consumer   37 advances, producer  521   (stops)

Dolphin's consumer is the busy end and its producer feeds it rarely; ours is
inverted - the producer races to fill the ring and the consumer dies. The
movie then parks in WAITING on top of 468 well-formed records it never
reads.

**What not to re-propose.** Three theories died here, each with evidence:

- *The mixer or ADPCM is starving the movie.* `MGS_AX_MODEL=0` produces a
  byte-identical run - same 8 reads, same 16 distinct pictures, same frozen
  end. **WITHDRAWN (F263): that test was vacuous**, because the default had
  the AX task unlinked and so both sides of the comparison had no mixer at
  all. The movie does still stall at 8 reads with the mixer running, so the
  conclusion may hold - but not on this evidence.
- *Task mask 0x8 is what the movie never recovers from (F200).* The mask
  goes to 8 **after** the movie has already parked; it is a consequence.
  Every one of the movie's state transitions happens while the mask is 0.
- *`mpeg_poll_stream_events` sets `ctx->0x3C` on a code-1 event.* It
  **consumes** that flag; something in the movie-start path sets it. The
  note in `config/symbols/mgso_pal.rel.symbols.txt` had it backwards and is
  corrected there.

### F258 — a one-slot queue defeated the "has this been read from" test, and it cost a wrong root cause

The thread dump distinguishes "empty now" from "never used" by the read
cursor, because `usedCount` cannot. For a capacity of ONE that test is
worthless: `first` advances modulo the capacity, so it is always zero.

The busiest queue in the whole run - 149 sends, `usedCount` seen moving 300
times - was printed as `empty, and never read from`, and was chased as the
head of the deadlock before the contradiction showed up. It is perfectly
healthy.

Fixed in `host/threads.c`: a one-slot queue now says its cursor cannot say
more, rather than asserting something false. The general lesson is the one
the surrounding comment already drew and did not carry far enough - **a
diagnostic that can be confidently wrong is worse than one that is silent.**

### F259 — there is no PowerPC disassembler on this machine, so there is one now

Targeted reading of single functions is explicitly in scope and was the only
way through F257, but nothing here could disassemble PowerPC: the system
`objdump -i` lists i386 and bpf only, the `llvm-objdump` on PATH rejects
`-b binary` and `--binary-architecture`, and Ghidra is a flatpak with no
headless launcher wired up.

`tools/ppc-dis.py` decodes the subset that answers "why did this function
take that branch" - loads, stores, address arithmetic, compares, conditional
branches and calls - against a DOL's own segment table. **Anything it does
not know prints as its opcode numbers rather than a guess**, because a wrong
mnemonic would be believed.

Its output is for humans: never compiled in, never committed, not quoted in
notes. What goes in the record is what a function *does*.


### F260 — the overlay had TWO .bss regions and the engine used both; they are now one

`mgs_clear_overlay_bss` already recorded that "the recompiled overlay and the
game disagree about where .bss is, and both are right about their own
world", and fixed the half of it that bites at startup: it zeroes the
recompiler's region so the globals do not begin life full of relocation
data.

**The other half was still live, and nothing said so.** Which region a global
ends up in depends on how the code reaches it:

- addressed **directly**, the recompiled code's own base → VMEM,
  `module + 0x491BA0`;
- reached **through a pointer held in `.data`**, whose value OSLink wrote →
  the buffer the GAME allocated, MEM1 `0x8054A180`.

Measured in one run, two globals, opposite ways round:

    record-ring pair   VMEM 0x7F4EF794  LIVE     MEM1 0x8059FD74  all zeroes
    "r_open"/"demo50a" VMEM 0x7F4B5FB0  zeroes   MEM1 0x80566590  LIVE

Proved rather than inferred: the REL file holds `lis r4,0x0000` at that site
and the live image holds `lis r4,0x805A`, so the MEM1 address is **OSLink's
relocation**, written by `Relocate+0x80` (`0x800205BC`), not file content.
The relocation table names the `.data` words that target `.bss`, and they
read back as MEM1 addresses.

**The fix.** `OSLink(module, bss)` is watched already; r4 is now rewritten to
the recompiled overlay's own base before the guest executes any of it, so
every address `Relocate` writes agrees with the code that will read it.
Ordering still works: linking reads the relocation tables living in that
span and writes only pointer *values*, never into `.bss` itself, and the
existing post-link clear then zeroes the span.

**It did not fix the movie, and it changed no behaviour at all.** Same 8
reads, same `retrace ticks: 9482`, same `interrupts delivered: 95026`, same
final pc; the only difference in the whole log is MEM1 megabyte 05's hash,
which is the abandoned copy no longer being written. **So the split was
LATENT** - each global was consistently reached one way, and none was
reached both. It is recorded as removing a hazard that would have produced
an impossible-looking bug later, not as a movie fix.

**It broke something on the way in, which is worth keeping.** The clear
computed its region as the maximum over sections "inside the image", relying
on `.bss` pointing at the game's buffer somewhere else. Once OSLink was
handed an in-image `.bss` that maximum landed at the END of `.bss`, and the
clear zeroed `0x680F8` bytes PAST the globals while leaving the globals
themselves full of relocation data - reintroducing the exact bug the
function exists to prevent, silently, in a run whose summary statistics were
otherwise identical. `.bss` is now excluded by size, and the function warns
if the recompiler's base is not inside the span it is about to clear.

**What not to re-propose:** passing a different `--rel-base` to make the two
agree. They cannot be made to agree that way - the image must load at
`0x7F008000` because the game hard-codes it, and no single base puts both
the sections there and `.bss` at the address the game allocates. DolRecomp
has no `--bss-base`.


### F261 — the ARAM completion interrupt was raised on the wrong half of the length register

The streamed-sound pipeline is clocked by ARAM DMA completions:
`__ARQInterruptServiceRoutine` is the origin of the type-11 messages that
advance it, so the whole chain runs at the rate those completions arrive.

Our port raised that completion on the write to the length register's HIGH
half, while the copy itself ran on the LOW half. Dolphin settles which is
right (`Source/Core/Core/HW/DSP.cpp` @ `ee018d0`): `AR_DMA_CNT_H` is a plain
register write, `AR_DMA_CNT_L` is the one whose handler calls
`Do_ARAM_DMA()`, and the comment above it says so outright.

So every completion was announced **before** the copy it belonged to, and a
high-half write not followed by a transfer announced a completion that never
happened at all. The count is the proof, and it is exact:

    before   139 in + 8 out = 147 transfers, 294 interrupts delivered
    after    139 in + 8 out = 147 transfers, 147 interrupts delivered

Two per transfer, to one per transfer. The SDK's handler calls back the ARAM
queue once per completion, so the queue was being drained twice as fast as it
was being filled.

Other numbers from the same pair of runs: ARAM interrupts refused 630 -> 166,
mails reaching the DSP 0 -> 1, DSP task mails 22,248 -> 22,280.

**It did not fix the movie.** Still 8 reads of `movie.dat`, still parked in
WAITING, and the sound state byte still runs `4 -> 1 -> 2 -> 3 -> 4` where
the console oscillates `1 <-> 2`. The final pc moved, so the run is not the
same run, but the stall is unchanged. Recorded as a hardware-correctness fix
(rule 12, order of authority 1) with the count as its evidence.

**Still open, and the next thing to settle:** whether Dolphin's `1 <-> 2`
really is the same object doing the same job. Both runs watch the same
`main.dol` address, but a static slot can be reused - if the console reaches
the movie's audio by a path that never enters states 3 and 4, then the
message types that drive us there (5, 7, 11) are the divergence; if it is
simply a different scene, the whole comparison is worth less than it looks.
**Do not build on F257 without settling this.**


### F262 — the same stream object, side by side: ours stops 12 blocks in

F257's comparison is **valid**, and this settles the doubt recorded there.
The stream object at `0x8022AA40` is a `main.dol` address, and in Dolphin it
holds the same sub-object pointers ours does - `+0x28 = 0x8022AC20`,
`+0x2C = 0x8022CC80`. Same object, same job, so the state-machine comparison
stands.

Every field matches except three, and they are the position:

    field    Dolphin     ours
    +0x30    0x00193C00  0x00003000     stream position
    +0x38    0x00000400  0x00000400     block size (the same)
    +0x3C    0x00194000  0x00003400     next position
    +0x34    0x8021E500  0x8021D900     buffer

**Ours advanced twelve blocks of 0x400 and stopped. Dolphin reached
1,653,760 bytes and was still going.** Twelve matches the thirteen
iterations of the thread body at `0x80055264` exactly.

Per-queue throughput says the same thing in a second way - messages received,
Dolphin over 150 s against our whole run:

    0x8027B4D8  stream      1481   vs   70
    0x802133A4              1350   vs   68
    0x80213384              1477   vs   34
    0x80213364               417   vs   30
    0x8027B618                 0   vs   26

The last row is its own finding: **the console never puts a single message
in `0x8027B618`, and we put twenty-six there** - thirteen pairs of types 9
and 13, all carrying the stream object, sent from the thread body at
`0x80055264` (`0x80055444` and `0x80055670`). It is a real two-slot queue
and it is being read (26 in, 26 out), so it is a live path the console
simply does not take. Thirteen pairs, twelve blocks: the same thirteen
iterations.

And the ARAM side agrees: `8 transfers out` (ARAM -> main memory) in our
whole run. That is the direction a stream reads its audio back through, and
Dolphin does it continuously.

**Where this leaves the movie.** The chain is now traced end to end and each
link is measured:

    ARAM DMA completion -> __ARQInterruptServiceRoutine -> type 11
      -> sound stream state machine -> ring consumer -> mpeg_movie_task

Every link works, briefly, and then the whole pipeline quiesces with all four
sound threads drained and waiting. Three real faults were found and fixed on
the way through (F256, F260, F261) and **none of them moved it**, which is
itself worth knowing: the stall is not an interrupt being dropped, not the
split .bss, and not the doubled ARAM completion.

**The next question, and it is a narrow one.** The thread body at
`0x80055264` (one call, from `0x800237A4`) advances the stream 0x400 bytes a
pass and issues the ARAM read through `0x80055114`, which was entered
**exactly 13 times**, with `r4` stepping 0, 0x400, 0x800, 0xC00 ... and a
context of `0x8027AD00`.

Where the next session should start, already measured so it need not be
re-derived:

- `ctx+0xDA` is the channel count the request loop runs on. It is **2**, not
  zero, so the loop is not being skipped at the top.
- `ctx+0xA4` is read first inside that loop and **never leaves zero**, so the
  wrap branch is taken every time and the comparison that decides whether to
  issue a transfer is `ctx+0x88` against position+size.
- `ctx+0x88` never changes; `ctx+0x90` changes 13 times.
- `ctx+0xD9` toggles 0<->1 sixty-two times from `0x80054D48` (lr
  `0x80054FCC`) - a busy flag that is still being serviced long after the
  stream stopped advancing.
- `ctx+0x7C` is a ring descriptor: base `0x80222920`, size `0x40000`.

So the thread is alive and the loop is entered; what stops is the decision
inside it to issue another transfer. Read `0x80055188` onwards with
`tools/ppc-dis.py`; that is what it is for.


### F263 — there was no sound because the AX task was being UNLINKED, and every sound measurement against the default measured a mixer that was never called

Asked "is sound working", the answer turned out to be that the mixer was not
running at all, and had not been all session.

`mgs_ax_dsp_report` begins `if (!s_frames) return;`, so a run with no mixed
frames prints nothing - and **no run this session printed an `AX mixer:`
line**, including the ones taken before any of tonight's changes. Not a
regression; it had been silent the whole time.

The cause is the DSP task mail. The SDK treats them very differently
(dolsdk2004 `dsp_task.c`):

    0xDCD10000  init    -> init_cb
    0xDCD10001  resume  -> res_cb
    0xDCD10003  done    -> done_cb, then __DSP_remove_task() UNLINKS the task

We posted `done`. That unlinks AX's task, so `__AXOutDspReady` is never set,
`__AXOutAiCallback` never runs a mixing frame, and the mixer is never
called. `MGS_DSP_RESUME=1` selected the correct mail and was **not the
default**, for a reason recorded in the code: "switching it lets AX actually
run and the boot then reaches audio paths this runtime does not model - one
run ended in an unhandled exception at 2.5M steps".

**That reason is gone, and it was re-measured rather than assumed.** Those
paths are now modelled - the voice mixer, the SDL device, the ARAM store the
voices read from, the audio-DMA clock that paces them. With the resume mail:

    200M steps  clean, pc 0x80061584
    400M steps  clean, pc 0x8005BF18

against a recorded failure at 2.5M. So it is now the default, and
`MGS_DSP_RESUME=0` restores the old mail for comparison.

**What that buys, measured:**

    AX mixer: 31,160 frames (155.8s of sound), 43,674 voice-mixes, 1,683 loops
    ADPCM samples decoded: 202,240; read outside ARAM: 0
    samples contributed: PCM 6,675,543 of 6,840,960 non-zero
    output: peak 28,650 of 32,767 (87.4% of full scale)

**And what it does not buy: 681 of 31,160 frames are not silent - 2.2%.**
The reason is the one F250 and F253 already found and is unchanged here:
**42,294 of 43,674 voice-mixes have envelope volume 0**. The samples are
there and at full amplitude; the game is holding the gain down, because the
movie it would be scoring is not playing. The 681 non-silent frames are the
same 681 at 200M and at 400M steps - all the sound happens early and then
stops, which is the fade-out of F253 seen from the mixer's side.

**This also invalidates a test from tonight.** F257 recorded that
`MGS_AX_MODEL=0` produced a byte-identical run and concluded "the mixer is
not involved" in the movie stall. That comparison was **vacuous**: the
default already had AX disabled, so both sides of it had no mixer. The
conclusion may still be true - the movie stalls at 8 reads with the resume
mail as well - but it is not supported by that test, and the claim in F257 is
withdrawn.

**What not to re-propose:** measuring whether there is sound without first
checking that `AX mixer:` appears in the log. A missing line there is not a
quiet mixer, it is no mixer.


### F264 — a DVD request was handed to a worker before it was finished being built, and it crashed half of all runs

`mgs_dvd_read_abs_async` was three lines:

    req = mgs_dvd_read_async(dvd, "", ...);   /* this SUBMITS the job */
    if (req) req->absolute = 1;               /* ...and this sets a field */

`mgs_dvd_read_async` submits to the worker pool as its last act, and
`read_job` reads `req->absolute` as its first. So the flag was written after
the worker could already have read it: a plain data race, lost about half the
time.

**What losing it did.** The worker saw `absolute == 0` and read by PATH -
with the empty path that entry point passes for an absolute read - so the
read failed. The guest was handed a failed DVD read it never sees on
console, and diverged from there. It surfaced a few million steps later as

    [fp] refused: OSCurrentContext = 0x00000000 is not a usable context
    stopped after 3703635 steps: unhandled exception, pc = 0x00000800

an FP-unavailable exception taken while the current context was zero, which
looks nothing like a disc read.

**Measured, same build, 6M steps a run:**

    default pool   4 of 8 runs crashed
    MGS_JOBS=2     5 of 6 crashed
    MGS_JOBS=4     3 of 6 crashed
    MGS_JOBS=1     0 of 8 crashed
    after the fix  0 of 10 crashed

and ThreadSanitizer goes from 1 race to 0.

**How it was found, because the route matters more than the bug.** Three
things had to be in place, and two of them were laid this session:

1. **Every disc failure says why** (committed earlier today). The signature
   is `[disc] path read FAILED: ` with an EMPTY path - an absolute read gone
   down the path branch. Before that change this printed nothing at all, and
   the same failure had already been seen and mis-explained twice.
2. **A sanitizer, rather than more reading.** A TSan build named the file,
   the line and both threads in one run. Two hours of reading the job pool,
   the band splitting and the texture cache had found nothing, because the
   race was not in the rasteriser at all - the worker count only changed how
   often it was lost.
3. **Bisecting on `MGS_JOBS`** to establish it was concurrency at all.

**What not to re-propose:** looking for this in the rasteriser. `MGS_JOBS=1`
makes it disappear, which points there and is misleading - the raster and
DVD workers share one pool, so the knob changes both.

**The general rule this leaves:** a request handed to a worker pool must be
COMPLETE before it is submitted. `dvd_build_and_submit` now takes `absolute`
as a parameter so there is no window in which a worker can see a half-built
request, and the comment above it says why.

**It does not fix the movie.** Still 8 reads of `movie.dat`, still parked in
WAITING. It does mean the game no longer dies at random a few million steps
in, which is the difference between a port that can be tested and one that
cannot.


### F265 — RETIRED by F268: the event was not missing, it was not due yet

The whole stall reduces to a single word. `mpeg_movie_task` in state 1 does
nothing but poll; `mpeg_poll_stream_events` does not generate a wake, it
**copies `ctx+0x40` into `ctx+0x3C`** and resets `+0x40` to -1:

    lwz   r0, 0x40(r4)
    cmpwi r0, -1
    beq   skip              ; nothing pending
    stw   r0, 0x3C(r4)

and the task leaves state 1 only when `+0x3C` is NON-ZERO. `+0x40` is filled
by the event handler at `0x7F151E70`, which reads `payload[0]` as a code:

    code == 1  ->  +0x40 = 1     the task wakes
    code == 0  ->  +0x40 = 0     nothing happens

**The entire wake-up history of a 200M-step run is five writes:** the
start kick (`+0x3C = 1`), its consumption, one **code-0** event, and the
poll resetting `+0x40`. No code-1 event ever arrives.

**The oracle shows what should happen, and it is not what I assumed.**
Walking Dolphin's movie ring over 140 s:

    30.1s   321 records   0xE:321            <- IDENTICAL to our stalled state
    65.2s   292 records   0x0:1  0xE:291     <- consumption starts
    75.2s   127 records   0xE:126  0x8E:1    <- records claimed (0x80|0x0E)

and its movie context at the same moments:

    25.2s  +0x3C = 1      movie start
    25.3s  +0x40 = 0      a code-0 event    <- we get this one too
    64.2s  +0x40 = 1      a CODE-1 EVENT    <- we never get this
    64.2s  +0x3C = 1      the task wakes

**So the console spends about 35 seconds in exactly the state we are stuck
in** - 321 tag-0xE records queued, task waiting, ring full - and then one
code-1 event releases it. Our port waits ~190 s of guest time and never gets
it.

**Two readings corrected along the way, both mine, both from this session:**

- *"Ring 1 is stuck and ring 0 is the live one."* Ring 1 looks stuck on the
  console too, for 35 seconds. A full ring of unconsumed records is the
  NORMAL pre-roll state, not evidence of starvation.
- *"State 1 is the normal playing state, the stream does the work."* No: the
  ring is not consumed at all during state 1. State 2 is where the records
  are claimed and freed.

**And one dead end, recorded so it is not walked again.** `ring+0x30` is 1 on
the console and 0 here, and it gates the stream reaching its state 2 - but
that word is set when the producer meets a record tagged `0xF0`,
end-of-stream, and the stream's state 2 is an ENDING state. Dolphin reads 1
there because its intro movie has finished by 150 s. It is a difference of
*when*, not of behaviour. **Do not chase `ring+0x30`.**

**Next: what posts a code-1 event for key `0x006647BA`.** Events are posted
through `0x7F0FD204`, 65 times in a run, every one of them from
`0x7F011DF0` - inside `fn_1_9CB0`, a script opcode handler with no `bl`
callers (F253). So the question is what the script is waiting for, roughly
35 seconds of console time after the movie opens.


### F266 — the port was running a PAL disc at NTSC field rate, and the movie's clock is slaved to that

Two constants in the runtime were NTSC's, and only NTSC's:

    VI_HALF_LINES_PER_FIELD 525        (PAL is 625)
    675000 ticks per field             (40.5 MHz / 60; PAL is 810000 / 50)

GGSPA4 is the European release. The guest programs `VI_DCR`'s format field
for PAL and we advanced the screen 60 times a second where the console
advances it 50.

That is not cosmetic here. F245 established that this game's movie clock is
**slaved to the sound system's playback position**, so a video clock running
fast against audio is exactly the "plays the first frame or two of each
chunk" symptom. Measured over 200M steps:

    before   video 189.6s   audio 155.8s   ratio 1.220
    after    video 158.1s   audio 155.6s   ratio 1.016

and 810000/675000 is 1.20, which is the drift almost exactly.

**Read, not assumed.** Both numbers now come from `VI_DCR`'s format field
rather than a constant, and the auto-detected run reproduces the forced one -
which is the check that the guest really is in PAL rather than that a
constant happened to fit. NTSC stays the default until `VIConfigure` runs,
because that is what the register reads from reset.

**Honest about what it did NOT do.** The movie still stops after 8 reads of
`movie.dat`, and measured non-silent audio frames went DOWN, from 681 to 285
of ~31,100, with the peak from 87% to 14% of full scale. The mixer runs the
same 31,126 frames either way - it is clocked by the audio DMA, not by VI -
so what changed is which voices the game has active at a given step, not the
mixer. This is filed as a correctness fix with a sync measurement behind it,
not as "more sound".

**AND THE GARBAGE PICTURE IS GONE.** A 400M-step run samples nine frames and
reports `NOISE: 0`. The frame the movie draws now reads
`roughness 0 ok, lit 71%` where the same frame read
`roughness 36 NOISE, lit 99%` before. The movie still does not ADVANCE - it
is one clean frame rather than a moving picture - but what is drawn is no
longer noise, which is the "video is garbage" Ben reported. Sampling is
every 120 frames plus on change, so this is nine clean samples and no noisy
one, not a frame-by-frame proof.

**One thing it exposed:** with the forced-period run the mixer reported
`peak 54629 of 32767 (166.7% of full scale)`. The peak is tracked before the
clamp, so that is real clipping in the mix, not a reporting artefact. Worth
chasing separately.


### F267 — the movie's code-1 event does NOT come from the broadcast subsystem, and the tooling leaked emulators

**The dead end, recorded because it is an expensive one to walk twice.**
Chasing what posts the movie's missing code-1 event (F265) led up a clean
chain:

    post_event(key, code)            0x7F152B9C
      <- broadcast_event(obj, code)  0x7F14E2E8   posts to every key in obj+0xB8
        <- 0x7F14EDD8                code = 1     the one that would wake the movie
          inside the handler         0x7F14E8E4
            registered by            0x7F1500AC
              which is entry 122 of a 599-entry hash->constructor table at
              0x7F470220, under the id 0x0042D7C8

`0x7F1500AC` is called **0 times** in our run, which looked like the answer.
It is not. Searching the CONSOLE's MEM1 for those addresses:

    0x7F14E8E4  director handler   0 hits
    0x7F1500AC  director creator   0 hits
    0x7F151214  mpeg_movie_task    1 hit  (its task node, 0x811CDE60)
    0x7F151E70  movie poller       1 hit  (0x8107F120)

**The console never creates that object either.** So the subsystem is not
the source, the chain is dead, and the code-1 event comes from somewhere
else still. What IS now eliminated:

- the script's own poster - all 65 events in a run carry code 0 (F265);
- the whole `broadcast_event` path - never instantiated, on either side.

The generic poster `0x7F0FD204` has 44 `bl` sites and only one executes.
The remaining 42 are where to look next, and the cheap way to narrow them is
`tools/rel-xref.py`, added here: it reports both `bl` callers AND pointers,
taken from the REL's relocation table rather than by scanning for a
constant - a REL stores its sections unrelocated, so an address that is only
ever formed by a `lis`/`addi` pair is simply not in the file to find. That
is how the director was reached at all, and searching for `bl` alone would
have said "nothing calls this", which is true and useless.

**And the tooling was leaking emulators.** `dolphin-watch.py` killed `proc`,
which is the `flatpak run` launcher - the emulator is its grandchild under
bwrap and survived. The kill was also the last statement of `main()`, so a
`timeout` around the script, a Ctrl-C or any exception skipped it entirely.
Four emulators were running at once before the user noticed, each holding
24 MB of guest RAM and a share of the CPU - on a machine whose load I had
already spent time misreading (F254).

Fixed: the launcher starts in its own session, cleanup kills the process
GROUP and the emulator pid directly, and it runs from `atexit` and from
SIGTERM/SIGINT/SIGHUP handlers rather than from the end of a happy path.
Verified both ways - normal exit and killed by `timeout` - each leaving zero
emulators.


### F268 — the guest's clock ran four times faster than the guest's work, and that was the "it is slow"

`mgs_tick_rate` is guest timebase ticks per interpreted step, and it was 32.
Everything timed hangs off it - the retrace period is a field's ticks
divided by it, and the audio DMA drains on it - so video and audio stay in
step with **each other** whatever it is. That is why the ratio looked
healthy (F266) and hid this.

What it does not keep in step is the clock against the WORK. Counting XFB
copies against fields over 200M steps:

    rate 32   7903 fields    920 frames   1 frame per 8.6 fields
    rate 16   3952 fields    918 frames   1 frame per 4.3
    rate  8   1977 fields   1095 frames   1 frame per 1.8
    rate  4    989 fields    750 frames   1 frame per 1.3

A PAL game at 25 fps on 50 Hz fields is one frame per two. At 32 the game
rendered one frame every 8.6 fields, about 6 fps.

**It is not a host performance problem.** The host simulates 1578 s of guest
time in 224 s of wall clock - seven times real time. The game was being told
far more time had passed than it had had steps to act on, so it skipped
work, faded voices and retired them on a clock running ahead of the code
that feeds them.

**Eight is what the hardware suggests, not only what the measurement
prefers.** The Gekko's timebase is the 162 MHz bus over four, 40.5 MHz,
against a 486 MHz core - twelve CPU cycles a tick. Eight ticks per dispatch
is about ninety-six guest instructions per chunk, which is a plausible
chunk. Thirty-two would be nearly four hundred, which is not.

**Measured, 2B steps, same build either side:**

    rate 32   audio   6,216 of 315,570 frames not silent    (2%)
    rate  8   audio  51,120 of  78,557                      (65%)
    rate 32   movie.dat 63 reads in 1578 s of guest time
    rate  8   movie.dat 73 reads in  395 s                  (4.6x the rate)
    rate  8   11 files read, against 9

**And it retires F265.** That finding concluded the movie stall was "one
missing code-1 event", from a 200M-step run. It was not missing, it was not
due yet: at 2B steps the movie streams 73 chunks, the context is created and
torn down normally, and the engine moves on to `shared/audio/stream/0058L`
and `0058R`. **The movie was never stalled - it was being run at a sixth of
its proper rate.** Every conclusion in F257, F262 and F265 that rests on
"the ring is full and nothing consumes it" is a measurement taken before the
game had had the steps to consume it, and should be read that way.

**What it exposes:** the mix now clips - `peak 65532 of 32767`, 200% of full
scale, where the peak is tracked before the clamp. With most frames silent
that was invisible; with 65% of them audible it is not.

**Measured before acting on it**, because a peak says the mix clipped
somewhere and cannot say whether that is one sample in a run or one in
three - a tuning note and a bug respectively. A clipped-sample count now
sits beside the peak, and over 600M steps it reads:

    clipping: 11,256 of 3,719,680 output samples (0.30%)

So it is the tuning note: rare peaks in busy moments, audible as occasional
crackle, not gross distortion. Left as a measured figure rather than
"fixed" by a guessed master volume - AX does apply an output volume we do
not model, and that is the right place to look if 0.3% turns out to matter.
