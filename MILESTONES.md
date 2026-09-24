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

## Status, 2026-09-22

**Phase 0, in progress — 1,288 symbols, 18,485 function boundaries.**

Note on terminology, since the numbers here are easy to misread: **instructions
translated (99.87%) is not the same as decompiled.** Translation is a
mechanical rewrite of machine code into C; decompilation in the matching-source
sense is out of scope by design and sits at ~0%. Phase 0 progress is 70.8%. The toolchain is
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
| 1 | Boot in ModernGekko | Title screen renders through recompiled CPU code, no interpreter fallback on the boot path | 1–2 weeks | **BYPASSED, not completed.** This phase means running under the *Dolphin-derived* template so Dolphin supplies GX and audio. We never did: the own runtime came first. Its purpose — proving the recompiled CPU before blaming our own shims — was therefore never bought, and every CPU-level doubt since has had to be settled another way. The host-instruction fallback now reports **unhandled: 0**. |
| 2b | *(within 2)* Our own renderer | — | — | **the Konami logo is drawn by `runtime/gx/`**, 0 parser desyncs; the 86% of the boot that was `memcpy`/`__fill_mem` is now native (F92) — measured before the change, effect not yet re-measured |
| 2 | Native OS + DVD + PAD, headless | Main loop runs headless, reads assets, responds to input, `OSReport` matches Dolphin | 3–4 weeks | **THIS IS WHERE WE ARE.** Runs **400M steps with no fault**, reads 9 files / 21 MB, **reaches the main menu and responds to input**, streams `movie.dat` and `demo.dat`. Exit criterion **not** met: the `OSReport`-against-Dolphin comparison has never been run. |
| **2c** | **Audio — moved from phase 4 on 2026-09-22.  JUDGED WORKING 2026-09-24 by Ben, on hearing it: sound and voices play. Its written exit criterion - matched against Dolphin within tolerance - was NOT met, and is not being claimed.** | AX voice mixer, per-voice SRC, SDL output; music, codec and SFX match Dolphin within tolerance, **and the movie plays at the right rate** | 3–6 weeks | **IN PROGRESS** — SDL3 device + real voice mixer built (F246): PCM16/PCM8 decoded from ARAM, volume and per-voice mix applied, position advanced by actual consumption. **THE AX TASK WAS BEING UNLINKED AND THE MIXER NEVER RAN** (F263) — the DSP `done` mail calls `__DSP_remove_task`, so every "is there sound" measurement against the default was measuring a mixer that was never called. The correct `resume` mail is now the DEFAULT (clean to 200M and 400M steps, against the 2.5M failure that had kept it off), and the mixer reaches **87.4% of full scale** — but only **681 of 31,160 frames are non-silent**, because 42,294 of 43,674 voice-mixes still have envelope volume 0. **THERE IS SOUND** (F247): peak 23, **52,979 of 62,763 frames non-silent**. The silence was *two* ARAM buffers — the DMA filled one, the mixer read the other, and nothing noticed while only the DMA used it. **ADPCM decoded too** (F248) — peak now **10,750 of 32,767, 32.8% of full scale**, 81,072 ADPCM samples a run, 0 voices skipped. **The mixer is built and working; the remaining silence is the MOVIE STALL seen from the audio side** (F253) — the game fades its movie voice out deliberately (7FFF → 7E88 → 2A62 → … → 0) from `sd_ax_frame_callback`, because the movie is not playing. Fixing the movie fixes the sound. **The blocker is now located** (F257): against Dolphin, our sound pipeline runs a state machine the console never enters (1↔2 there, 1→2→3→4 here) and deadlocks with all four sound threads drained; the movie's ring consumer dies with it — 37 advances against the oracle's 1895. Earlier note kept: **gain, not samples** (F250): 99% of PCM samples read are non-zero and **99.2% of voice-mixes have envelope volume 0**, so only ~828 of 106,326 mixes contribute. **The audio now has a verdict, not a count** (F285): `MGS_AUDIO_WAV` writes the mixed output and `tools/check-audio.py` measures it, because every earlier audio measurement here was a count and a count cannot say whether the result sounds right. Over 175.5 s: **gaps >= 1 ms: 0**, clipping **1.24% -> 0.07%**, and steps larger than full scale - which cannot come from a waveform - **47 -> 1**. The crackle WAS the clamp: AX runs a compressor (Dolphin's `RunCompressor`, ramp table supplied by the game through a DSP command we do not parse), so a limiter stands in for it, marked as such. Two candidates eliminated by measurement: verbose output stalls nothing (4.5 lines/s), and pacing frames on the audio queue made it worse and is reverted (F284) - a control loop sampling a 5 ms queue 12 to 25 times a second oscillates. ****THE RESAMPLER WAS POINT-SAMPLING AT 1.3769** (F312) - the streaming voice's SRC ratio is 44.1 kHz stepped down to 32 kHz, and the mixer took the nearest input sample at each output instant, throwing `frac` away after using it to advance. That is an aliasing floor, not an echo, and it is what Ben heard as "someone talking into a tin cup" once F310 had removed the 95 ms repeat. AX interpolates and this game asks for it - `AXPB.srcSelect` reads 1, `AX_SRC_TYPE_LINEAR`, traced. Linear interpolation over a two-entry history walked one input sample at a time (ADPCM's predictor forbids random access): 12-16 kHz noise over the speech band **-29.84 dB -> -37.40 dB**, steps between samples larger than half full scale **607 -> 188**, ADPCM samples decoded identical at 2,692,086 either way, 0 underruns, no cost in speed. **THE INTERPOLATION INTRODUCED A GARGLE AND BEN CAUGHT IT BY EAR** (F321): the resampler's two-sample history was a local, so every AX frame re-seeded it by decoding the current position with a predictor that had already consumed the next one - 200 times a second, on ADPCM voices only, which is the dialogue. The first fix for it did nothing (62,149 of 62,162 mixes still re-seeding) and only a counter said so; the corrected condition gives **17 of 18,704**. The lesson recorded: a measurement that improves is not a measurement that nothing got worse. **`tests/test_dsp_init.c` was failing and nothing noticed** (F311): the ARAM DMA started only on a 16-bit store to the count's low half, so the test's single 32-bit store never started one. The suite takes 0.06 s and four commits went by without running it. 19 of 19 pass again. **Exit criterion still unmet**: nothing is yet compared against Dolphin within tolerance, and the movie does not play at the right rate. |
| 3 | GX renderer | Title screen, the Dock and the Heliport render correctly at native resolution, frame-compared against Dolphin | 2–4 months | **STARTED 2026-09-24, AND IT IS NOW THE DEFAULT PATH** (`MGS_NO_GPU=1` goes back to the software rasteriser, which remains the reference and the fallback): 12,013,042 triangles a run in 17,164 batches, a content-keyed texture cache hitting 9,808 times against 7,356 uploads, and **the port running 35% FASTER than real time where the software rasteriser was 27% slower** (78s of sound produced in 107.9s against 116s in 85.6s - a 1.9x swing). Two of four sampled frames are BYTE-IDENTICAL to the software path; the logos render correctly. The device is up and tested: Vulkan, a 640x528 colour target with depth, and a fenced readback into the embedded buffer's own ARGB layout - verified pixel for pixel by `tests/test_gpu.c`, which checks the channel order with four distinct channels and honours a stride wider than the region. The readback is deliberate: it keeps the existing copy, framebuffer and presentation path working while the rest is built, and it is what makes every later step comparable with the software rasteriser pixel for pixel. **Why now:** with the vertex arrays fixed (F302) the game draws what it should - 2.36 billion pixels a run - and the CPU rasteriser cannot keep up, which is heard as the audio device starving. The design document predicted exactly this: "far too slow for the intro movie... it buys time until the Vulkan backend exists". **THE TEV COMBINER NOW RUNS ON THE GPU** (F313), interpreted from a uniform block by one fixed shader rather than generated per state with shaderc - the design document is updated in the same change (rule 12), and the reason that matters most is that it is the SAME arithmetic as `runtime/gx/tev.c`, so the two paths can be compared pixel for pixel rather than approximately. Against the software rasteriser over five sampled frames, the two scene frames go from **71.21% of pixels wrong by more than 8 counts, mean error 105.30**, to **23.39%, mean error 14.17** - a 7.4x improvement - and the three frames that were already identical stay identical. **AND THEN FOUR TEXTURE UNITS AND PER-TEXTURE SAMPLERS** (F314) closed the rest of it: a TEV stage names its own map and its own coordinate generator, and GX gives every map its own wrap mode and filter, both of which the software path had always honoured and the GPU path had not. The two scene frames end at **0.07% and 0.56% of pixels differing by more than 8 counts, mean error 0.72 and 0.65** - from 71.21% and 105.30 with the base shader. What remains is rounding: GX blends in truncating integers and the GPU in UNORM with round-to-nearest, a +/-1 disagreement on any blended pixel. Four units is enough, measured: 0 overflows in 929,123 multi-texture draws. Cost about five points of throughput (+59.9% over real time against +64.6%), still comfortably ahead. **THE DOLPHIN COMPARISON HAS ITS CLOCK** (F319): both the port and `tools/dolphin-watch.py` now snapshot MEM1 at the same field of the SDK's own retrace count - an address derived from `VIGetRetraceCount`'s single instruction rather than assumed - and `tools/compare-mem.py` reports where two snapshots differ, by page, by contiguous run and with the nearest symbol. First measurement at frame 1500 of a boot with no input: **67.5% of the pages that hold anything on either side are byte-identical** between the port and the emulator. It is NOT yet the exit criterion, and the reason is now established twice over: the same field number is not the same point in the game (at frame 200 the port had drawn and Dolphin had not), **and two Dolphin runs to field 1500 disagree with each other** about whether the frame has been drawn - so the field counter does not name a state even within one emulator. `DOLPHIN_USER=<dir>` is what makes Dolphin write its framebuffer to guest memory at all; its defaults keep EFB and XFB copies on the GPU, and two runs were spent reading that as "the game has not drawn yet". **An anchor on a guest word rather than on the field clock** now exists on both sides (`MGS_MEM_ADDR` / `DOLPHIN_FRAME_ADDR`), found by dumping the port at its 100th and 200th framebuffer copy and looking for words that advance by exactly 100. Anchored on 0x8020D0EC instead of the field counter, the two agree on **89% of all memory at counter 800** - the best figure this project has - and disagree on 56% at counter 1000, **so the divergence is bracketed between them** and a bisection can close it. **The bracket is closed and the harness has caught something** (F320): between counter 850 and 880 the port overwrites **8.7 MB at 0x8079C000..0x80FF2000** that Dolphin leaves untouched, inside the OS arena, and it is not the same data at a different offset. Two port runs to counter 850 are byte-identical, so the port is deterministic and the difference is real. **AND THE INDEXED MATRIX LOADS WERE BEING THROWN AWAY** (F322): `command_length` knew how long `GX_LOAD_INDX_A..D` were and `dispatch` had no case for them, so every matrix the game loaded by index stayed zero and **406,076 triangles a run were rejected as behind the eye with an all-zero position matrix** - Ben's "objects are either incorrect or not even there" in the 3D cinematic. Now 2,163 indexed loads are served a run and that counter reads **0**. **AND THE TEXTURE MATRICES WERE NEVER APPLIED** (F324): GX keeps them in the same XF matrix memory as the position matrices, so they were stored and nothing multiplied by them - and every one of 1,398,946 vertices carrying a texture-matrix index used a NON-identity one. What the game puts there is a 57-degree rotation at a scale of exactly 0.5, so those surfaces were drawn at twice the texture size and unrotated. **It changes nothing on screen**, though, and that is recorded as such: thirteen cinematic frames are byte-identical with and without it, and two explanations offered for that were both wrong and are recorded as such - coordinate 0 IS transformed (10,675,352 times), and the matrices are NOT identity-valued (28,707,300 of 72,060,564 transforms move the coordinate). What the numbers support is that the thirteen frames sampled, taken from copy 700 onward, land in the pre-rendered movie and not in real-time 3D; the A/B has to be re-run against frames that contain the affected geometry before anything is claimed. ~~Counting the rest of the XF address space also settled the texgen registers, and they turn out not to matter here: every configuration written is a plain 2x4 transform of the coordinate the stream already carries, which is what the code assumes.~~ **WRONG, and it was the flat textures** (F339): that reading came from a list of distinct texgen values that holds eight entries, which GXInit's own eight defaults filled before the game wrote anything. Counted properly, the game generates 16.2M coordinates a minute from the NORMAL, 4.4M from each binormal, and builds outputs 1-3 from TEX0 and TEX1 - none of which the "output i from input TEXi" assumption could produce. **Texture coordinate generation is now implemented** from Dolphin's software TransformUnit (source row, 2x4/3x4 matrix, dual-texture normalise and post-matrix), normals and binormals are read instead of skipped, and **90.4M coordinates a run come from their real source with zero unsupported**. The models are textured for the first time. The VAT's texture fractions for coordinates 1-7 were also read from the wrong bits (F340). **AND THE GPU PATH HAD NO WORKING DEPTH** (F342): GX screen z is in 24-bit units (the game's viewport scale and offset are 16,777,216) and was handed to the GPU as if it were 0..1, so the pipeline clamped nearly every triangle to depth 1.0 and the frame was painted in draw order - see-through figures, a hologram drawn over the people in front of it. Normalised by 16,777,215 into the D24 buffer; the crew scenes now layer correctly. Triangles are now clipped at GX's near plane (z = -w) as Dolphin's clipper does, not only at the eye (F347): 220k a run were drawn that should not have been. Back-face culling now runs at all (F348) - it never had, the field it read was never written - and removes ~15M triangles a run. **Ben reached the menus and GAME START; choosing YES froze** - a music stream's last read refused where the retail SDK issues it (F349). Fixed; **gameplay reached** (the first scene), at 28-42 fps against 50. The guest and mixer threads now run at raised priority (F350), so a busy machine no longer takes the frame rate with it. **Every loop in the game was yielding to the host on every iteration** (F351): `downcount` is 64 bits and the refill wrote 32, halving dispatch count once fixed. The module is rebuilt at `-O2` as the design document requires (it had been `-O3` since 2026-09-19): 1.3% less CPU, identical frames. Display lists recorded in the engine's relocated .bss were being thrown away (F352) - fixed, 0 desyncs. **The engine's .bss is now generated at the game's own MEM1 allocation** (F355, a local `--rel-bss` option for DolRecomp): the jump to 0x4E923A7C that froze the Codec and the Dock is gone, and the script that hit it at copy 4,008 now runs 420 s through the Codec into gameplay. Engine data in the second window is read inline rather than through the host (F356). Still to do: those two, name the 8.7 MB write (`MGS_TRACE_DEST` is in for that), get Dolphin's picture into guest memory at the moment being compared (three separate settings have to be right and each failure looks identical), the vertex converter, the texture decoder's upload path and GPU-side EFB copies. **Made the default 2026-09-24**, on the evidence above: leaving it opt-in would mean an ordinary run still gets the judder, and the reason it was opt-in - that it drew white rectangles for multi-stage geometry - no longer holds. |
| ~~4~~ | *moved to 2c, 2026-09-22* | — | — | — |
| 5 | Saves and completeness | Game completable start to finish on both platforms | 1–2 months | blocked on 3 |
| 6 | Port features | Public release | ongoing | blocked on 5 |
| **7** | **Enhancements** *(added 2026-09-22)* | Optional, off by default, and **never** a prerequisite for 6: FSR/DLSS upscaling, TXAA or similar temporal AA, and whatever else improves the picture without changing the game | ongoing | blocked on 6 |

---

## One renderer backend, not two (2026-09-22)

The renderer targets **SDL3's GPU API**, written once. SDL3 (3.4.14 here)
supports **Vulkan, D3D12 and Metal** through it, selected at runtime by
`SDL_HINT_GPU_DRIVER`, so Windows and Linux are one code path and the driver
is a setting rather than a second port. The design document said "Vulkan with
a D3D12 backend later"; that is two backends to write and keep in step, for
one game.

**Correction to the idea as first put:** *OpenGL is not an SDL GPU backend.*
SDL3 ships GL headers and a 2D `SDL_Renderer`, but the GPU API is
Vulkan/D3D12/Metal. "OpenGL or Vulkan" in the options menu would mean writing
a second backend by hand, and should be costed as such if it is ever wanted.
What can honestly be offered is the **GPU driver** choice.

## Phase 7 — Enhancements, deliberately separated from phase 6

Added 2026-09-22. Upscaling (FSR, DLSS) and temporal anti-aliasing (TXAA)
are wanted, and they are **not** port features.

The distinction is what makes the split worth having. Phase 6 is what the
port must do to be a port — widescreen, resolution scaling, input, the
launcher — and its exit criterion is a public release. Phase 7 is what makes
it look better than the original, which is a different kind of claim and a
different kind of risk: a reconstruction filter that invents detail is the
opposite of "frame-compared against Dolphin", which is the standard every
phase up to 6 is held to.

So they are separated rather than folded in, and phase 7 items are **off by
default**. Shipping is not allowed to wait on them, and a divergence found
while one is enabled is not evidence about the port.

## Where we actually are (2026-09-22)

**Phase 2**, with parts of 3 underway. Worth stating plainly because the
numbering had drifted from the truth and the table said "runs 2M steps"
while the game was reaching its main menu.

**Phase 1 was bypassed, not completed.** It means running under the
Dolphin-derived template so Dolphin supplies GX and audio, and we never did
it — the own runtime came first. Its whole purpose was to prove the
recompiled CPU *before* our own shims could be blamed for anything, and that
insurance was never bought. Every CPU-level doubt since has had to be settled
some other way, which is a real cost and is why it is recorded rather than
quietly ticked.

Audio is **2c** because it sits inside phase 2, alongside 2b's renderer —
not after phase 3.

## Audio moved out of phase 4 (2026-09-22)

**It is not an output, it is the clock**, and that is why it cannot wait.

`mpeg_movie_task` takes the movie's playback position straight from the sound
system — `sd_sound.c`'s position function, scaled by 300/1000, stored as
`stream->0x08` — and every record timestamp in the streamed-media pipeline is
compared against it. Audio does not decorate the picture; it paces it.

**What was believed:** that the game "runs silently without it", so audio
could be last. **What is now known,** established over F218–F245:

- With no DSP mixing, the AX voice's `currentAddress` never advances → the
  stream pump never sees a block complete → four sound threads sleep on empty
  queues → the decode buffer never drains → the record ring's head pins → the
  movie parks in state 1 with **321 decoded video records unread**. Every link
  measured (F231–F236).
- Modelling that one field — no mixing, no samples — unfroze it (F236), and
  the same change let **`movie.dat` stream for the first time since F187**
  (F240).
- Guest video time was running **10.6× ahead** of guest audio time; correcting
  it improved streaming again (F245).
- **The oracle now says where the pipeline goes wrong** (F257). The streamed
  sound keeps a state byte per buffer, at a `main.dol` address — so the same
  address in Dolphin, which is what made the comparison possible at all:

      Dolphin   1 → 2 → 1 → 2 …   384 transitions in 125 s, forever
      our port  0 → 4 → 1 → 2 → 3 → 4 → … then STUCK at 2

  **The console only ever oscillates between two states. It never sets 3 or
  4.** Ours enters both every cycle, from a jump table on message type
  (`0x801E7C48`, types 5–16): type 5 writes 4, type 11 writes 3→4, type 16
  reaches the claim that takes 2→3. So the port is delivering message types,
  or an order of them, that the console does not — and that, not the mixer
  and not the task mask, is what parks the movie.
- **Three real faults found and fixed on the way through, none of which moved
  the stall** (F256, F260, F261) - an undelivered interrupt being destroyed
  rather than held, the overlay's two disjoint `.bss` regions, and the ARAM
  completion being raised on the wrong half of the length register. Each is
  justified on hardware grounds with a measured count; none is a movie fix,
  and the record says so.
- Two earlier explanations are now **dead** and must not be re-proposed:
  `MGS_AX_MODEL=0` gives a byte-identical run, so the mixer is not starving
  the movie; and task mask `0x8` is set **after** the movie has parked, so
  F200's "the one the movie never recovers from" is a consequence.

**Two consequences for the order.** Deferring audio does not defer its cost —
it pays it as mis-attributed *rendering* bugs, because the renderer is being
judged against a picture whose timing is wrong for audio reasons. And it
withholds the best debugging instrument available: **a sound output is a
continuous, audible check on pacing** that no counter in a log replaces.

`twin-snakes-native-port-design.md` is updated in the same change (rule 12).

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
- [x] **The video garbage was alpha read as red** (F286). `tev_register`
      took the TEV colour registers' alpha from bits 0-10 and red from
      12-22, and it is the other way round - Dolphin's `TevReg::RA` and
      libogc's `GX_SetTevColor` agree, and the game's own writes settle it a
      third time (0xE2's high field ramps 0,4,9,14,19 across frames, which is
      a fade). The movie's last pass blends the previous frame by its own
      alpha; returning 255 made it opaque, so it painted a buffer nothing had
      written yet - uninitialised heap - over the picture at full strength,
      and then fed on its own output. **Noisy frames 33 -> 1**, and reverting
      just this change puts all 33 back. The movie's planes were never at
      fault: they measure 2.9 on mean neighbour difference against 83.8 for
      random bytes (`tools/check-planes.py`). Fixed alongside: konst was
      hard-coded to 255 for every stage (KSEL now read), 0xE0-0xE7 now route
      to the colour or konst register by bit 23 as the hardware does, and the
      second drifted copy of the stage loop is gone. **Colour is still wrong**
      - the picture is legible but too purple - and the TEV swap tables stay
      unimplemented because the two references contradict each other (F288).
- [x] **Copies ran late, and one in five never ran** (F287). The parser held
      the copy command in a single slot for a periodic hook to execute, while
      draws run inline - so every draw in a frame preceded every copy in it,
      and a second copy issued before the hook replaced the first: **2,464 of
      9,350 copies dropped**. They now run where the command sits in the
      stream.
- [x] **The texture cache went blind, and the video had frozen** (F291). Its
      content hash sampled on a stride of `bytes / 4096`, which at 512x448
      RGBA8 is 224 - a multiple of the 64-byte tile - so the walk only ever
      read the alpha and green of texel 0 of the tiles it touched, and the
      alpha was constant. The hash sat at one value for hundreds of lookups
      while a plain sum of the same memory moved; one decode was served for
      the whole scene. An odd stride is coprime with every power of two.
      Decodes of that buffer **1 -> 946**. `tests/test_texture.c` covers it
      and fails on 62 of 64 tile offsets with the old stride.
- [x] **A texture built by an EFB copy is validated by which copy made it**
      (F292, F293). The game copies its frame both to a texture and to the
      framebuffer through one buffer, which is legal because the graphics
      processor serves textures from its own memory - so main memory holds
      YUV 4:2:2 while the texture still reads as texels. Reading main memory
      at each bind gave smooth PURPLE, which every roughness-based metric
      called fine. A serial per copy was not enough on its own - the decode
      happens at BIND time, and a framebuffer copy in between still rewrote
      the memory first (445 decodes against 1,086 copies for one buffer) -
      so the bytes each copy deposits are kept and decoded from. Frames
      measurably purple **33% -> 10% -> 0%**.
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
- [x] **The overlay IS linked; I searched the wrong 24 MB** (F226, wrong and
      recorded as such). `rel_loader_LoadRel` reads the REL into a temporary
      buffer, copies it to **`0x7F008000`**, frees the temp, then calls
      `OSLink`. `0x7F008000` is BAT-mapped, and Dolphin backs those addresses
      with a separate "fake VMEM" mapping — so every snapshot taken over
      `0x80000000` was watching the temp buffer, which by design is never
      relocated. **A same-section `bl` needs no relocation**, so its
      correctness is not evidence of linking; only an `@ha`/`@l` pair is.
- [x] **The key is right; the divergence is the task mask** (F227). With the
      BAT window readable, Dolphin creates the same movie object (context
      `0x8107F120` against our `0x8107F080`) and its `+0x38` holds the **same
      key `0x006647BA`**. So the key is not miscomputed. What differs is the
      task mask: the working run sets it to `1` for 0.3s while the object is
      built and returns it to **0**; ours reaches **`0x00000008`** and stays,
      gating levels 2–5 — including level 3, which holds the movie object's
      own node.
- [x] **The FP fault is named, and two fixes are UNJUDGED** (F237). A
      one-shot diagnostic gives the refused address: `OSCurrentContext =
      0x7F4A5630`, which the thread dump shows is a **real** worker thread —
      `context_is_sane` rejects it only for living in the overlay's
      BAT-mapped window, a window `gptr` has always known about. Widening the
      test, and a narrower structural test (`OSContext` is `OSThread`'s first
      member, so a genuine context equals `OSCurrentThread`), both *looked*
      far worse — **and both measurements are void**: taken at load average
      **32** under three `quartus_fit` processes, and the tell is that they
      stopped at 2,595,625 and 2,689,294 steps, two different values, where
      the good configuration reproduces to the exact step. **F220's rule
      again: check `uptime` before believing a run.** Tree reverted to
      F236's state; the diagnostic kept.
- [x] **The TEV sampled ONE texture for every stage** (F242). The renderer's
      own report said it: `TEV stages per triangle: 1:2491121 **3:64**`, and
      `raster.c` documented that only stage zero's texture is ever sampled.
      The movie composites luma + two chroma planes across three stages, so
      stages 1 and 2 were reading **luma** — green-and-magenta stripes over a
      correct-looking luminance structure, exactly as seen on screen.
      `MgsTevInput` now carries a texel per stage (NULL = all stages share
      one, the path 2,491,121 of 2,491,185 triangles take), and the raster
      resolves and samples each stage's own map, coord set, wrap and filter.
      **Evidence:** a new shape decodes — `fmt 0x1 256x160`, exactly half the
      `512x320` luma, i.e. a **4:2:0 chroma plane**; 151 → 161 textures.
      *On-screen result not yet confirmed by eye.*
- [x] **Palette refusals: 3,198 → 0** (F241). `BP_LOAD_TLUT0` took 24 bits of
      the TLUT address; the GameCube decodes **25 bits of the shifted value**
      and ignores the rest, which this game sets. Unmasked they folded to
      offsets of 148 MB and 81 MB into a 24 MB block, so every CI-format
      texture was refused. Masking with `0x01FFFFFF` collapses the three
      observed values onto **two** real palettes — two different register
      values landing on the same palette is the check that the mask is right,
      not merely in range. Dolphin does the same and names the symptom
      ("Some games (WW, MKDD) set them"); recorded in `THIRD_PARTY.md`
      against `extern/dolphin` @ `ee018d0` per rule 11. Now: **0 refusals,
      151 decoded, 29,338 hits**, and `fmt 0x9 335x17` decodes clean at
      roughness 0. **Does not fix** the `fmt 0x6 512x448` noise, which is a
      different texture.
- [x] **movie.dat STREAMS, and real video frames decode** (F240). The
      `pc = 0x800` crash is fixed: `mgs_fp_unavailable` now accepts a context
      outside MEM1 when **the OS vouches for it** — `OSContext` is
      `OSThread`'s first member, so a genuine current context *equals*
      `OSCurrentThread`. `context_is_sane` and `mgs_module_take_exception`
      are untouched. At 400M steps the run reaches the **step limit with no
      fault** (was a crash at 113M); AX frames 17,568 → **62,927**;
      **`movie.dat` 8 reads/262 KB/`+0x38000` → 27 reads/792 KB/`+0xBE800`**,
      unpinned for the first time since F187; `demo.dat` 17 → 110 reads;
      textures 29 → 150. And a new texture shape appears —
      **`fmt 0x1 512x320` at roughness 1–2**, exactly the dimensions the movie
      context reported, on the runtime's own scale for *artwork, not noise*.
      **Still wrong:** `fmt 0x6 512x448` decodes 121× at roughness 25, and
      there are **3,198 `palette` texture refusals** — both in the movie's
      output path, not its stream.
      **This was F237's "unjudged" fix**: it looked catastrophic at load 32
      and is correct at load 2.
- [x] **The event IS posted now; the movie reaches PLAYING** (F239).
      `MGS_FIND_WORD=0x006647BA` finds the key in the **event table**
      (`bss_253F0 + 0`), where F225 watched 822 writes and never saw it. It is
      delivered too: the context's `+0x3C` goes `0 → 1`, `+0x40` is consumed
      and cleared to `-1`, and `+0x44`/`+0x48` pick up **512 × 320**. The task
      state runs `0 → 2 → 1 → 2 → 1`: it reaches **state 2 (playing) twice**,
      where it advances the stream clock by `0xC` per call — and that clock is
      what record timestamps are compared against, so **ring 1 drains only
      while state 2 runs**. It ran twice, hence 321 records still unread and
      `movie.dat` still at `+0x38000`. Both `2 → 1` transitions come from
      `mpeg_poll_stream_events`' code-**0** branch: something posts a *stop*.
      **Not yet judged** — Dolphin cycles the same field too (1 at 25.4s, 0 at
      25.7s, 1 at 64.4s), so a start/stop/restart may be normal and the
      difference may be that ours never restarts.
- [x] **The AUDIO path unblocks — and the picture is NOISE** (F236, corrected
      by F238). `host/ax_dsp.c` plus `MGS_DSP_RESUME=1` genuinely unblocks the
      audio half: ring-0 tag-2 records **0 → 204**, the read cursor moves, the
      task mask goes `0x1 → 0x0` instead of parking at `0x8`, the four sleeping
      sound threads carry traffic again (queue cursors 49/68/26), and
      `_vorbis_synthesis1` is running in the thread dump. **But the screen is
      garbage**, and the runtime said so in the same line I read as success —
      `roughness 36 NOISE`, texture `fmt 0x6 512x448 mean 31 max 39`. The
      reason is one unchanged line of the disc tally: **`movie.dat` is still
      read only to `+0x38000`**, 0.28% of the file, exactly as before. The
      *video* stream never advanced; only `demo.dat` (the Ogg audio) did. The
      two halves are separate rings — ring 0 (tag 2, audio) moved, ring 1
      (tag `0xE`, the 321 video records) did not.
      `host/ax_dsp.c` models the DSP's one observable effect: each AX voice's
      `currentAddress` advances by `160 * srcRatio` per frame, looping at
      `endAddress`. **It mixes nothing** — it is the design document's line
      193 ("the DSP paces the machine") one level deeper, not phase 4's
      mixer. It needs `MGS_DSP_RESUME=1` too: without the resume mail AX
      never services a voice, so there is no block to advance (F235).
      Together: ring-0 tag-2 records **0 → 204**, the read cursor moves, the
      task mask goes `0x1 → 0x0` instead of parking at `0x8`, and the last
      texture drawn is **512x448 at 99% lit** instead of a 159x17 overlay at
      71%. **The picture is no longer frozen.**
      **New fault, deterministic to the step** (two identical runs):
      `unhandled exception, pc = 0x800` at 112,962,303 steps. It is *ours* —
      `lfd` with `MSR[FP]` clear is a Floating-Point Unavailable exception,
      which we service 62,467 times in the same run and refuse here because
      `OS_CURRENTCONTEXT` is not sane. Same class as
      `[interrupt] no current OSContext yet`. Both flags stay non-default
      until that is fixed.
- [x] **ROOT CAUSE: the AX voice's position never advances** (F234).
      `sd_stream_pump` reads `voice+0x1B2` = `pb.addr.currentAddress` and
      advances the stream only as that moves. The voice is at `0x80206F7C`
      in **both** runs; Dolphin's position runs `0x2000 → 0x2977 → 0x3212 →
      … → 0x9A33` and wraps, ours is written **5 times at setup by
      `AXSetVoiceAddr` and never again**, sitting on exactly the value
      Dolphin starts from. `dolsdk2004` shows why: that word is **copied back
      from the DSP's** parameter block, and our port runs no DSP mixing.
      Every link from here to the frozen picture (F225–F233) is verified.
      **Not yet established:** whether advancing the position in step with
      the audio DMA is enough, without mixing a sample — worth trying before
      anything larger.
- [x] **The external driver found — and it is present in our run** (F233).
      `__AXOutNewFrame` calls a user hook through a **`blrl`** (not a
      `bctrl`, which is why the first search missed it) at `0x8027DF00` =
      `__AXUserFrameCallback`; `0x80032E20` is `AXRegisterCallback`, matching
      `dolsdk2004`'s five-statement body instruction for instruction. The
      chain is AI/DSP interrupt → `sd_ax_frame_callback` → `sd_stream_pump`
      (8 channels, stride `0x10C`, acting on state 9) → `OSSendMessage`, and
      it is the **only** driver from outside the sound subsystem. **All of it
      is present in our run**: the callback pointer, both active channels in
      state 9, six idle — identical to Dolphin — and all three sound queues
      have carried traffic (cursors 30, 34, 70) and stopped. So the fault is
      inside the pump, which posts only while `channel->0x20` is zero.
      **`MGS_TRACE_FN` reported 0 calls for the callback and that means
      nothing** — those counts are lower bounds.
- [x] **The whole stall, end to end — the sound layer stops asking** (F231).
      `fn_1_8FE8`'s 64 KB buffer is **completely full** (`+0xBC` = `+0xB8` =
      `0x10000`), and across a run `+0xBC` shows **17 increases and 3
      decreases** — one the allocator's `memset`, two resets from
      `fn_1_8D98`. That drain is **request-driven**: it services an
      `OSMessageQueue` at `node+0x48` via `OSReceiveMessage`, and it also
      calls `fn_80053178`, which the file attribution places inside
      **`sd_stream2.c`** — Konami's streaming sound layer. Two requests were
      served, then none. **Audio is the blocker, by measurement**: buffer
      fills and stops, drain runs on messages, messages stop, requester is in
      the sound layer. Everything downstream — the pinned ring, the gated
      level 3, the movie parked in state 1 with 321 unread video records,
      F225's unposted key — follows from that one stop.
      **Open and deliberately not guessed:** what drives the requester (AX
      frame callback, audio DMA interrupt, or its own thread). Guessing
      between those is what cost F218–F221.
- [x] **The video is all there; the pin is one consumer** (F230). There are
      **two** rings (`bss_55BF4` is an array of two descriptors). The movie's
      own ring holds **321 records, all tag `0xE`** — the *same 321* Dolphin
      has, which then drains while ours never moves. **No video data is
      missing from disc.** The pin is `fn_1_8FE8`, node `0x8109D760`, tag
      `0x00000001`: it exists, runs on level 1, picks each record up and puts
      it back with `gcn_pool_clear_entry_flag` because
      `node->0xB8 - node->0xBC < node->0x40` — **its destination buffer has no
      room**. Also: tags can be packed `(language << 16) | id`, and the five
      `fn_1_12FC4` tasks are *discarders* for every language but ours
      (`0x0007/5/4/3/2 0004`), the selected one (`0x00010004`) going to
      `fn_1_14D34`.
- [x] **The ring head is pinned by records nobody consumes** (F229). Walking
      the ring instead of reading code kills both of F228's candidates:
      `ring->0x30` and `ring->0x34` are **0**, as Dolphin's are, and the
      refill gate `bss+0xB040` cycles 455 times in 112 frames against
      Dolphin's identical toggling. What is left is arithmetic the engine
      gets right — the pump advances the read cursor only past **tag-0**
      records and stops at the first non-zero, the head sits on one of nine
      unclaimed **tag-1** records, so the ring stays 80% full
      (`0x34930` of `0x40000`) and the refill correctly declines because
      `free <= size/3`. **Nobody consumes tag 1**, and every link after that
      is the engine working as designed.
- [x] **The gate is deliberate, and it is a starved record ring** (F228).
      `fn_1_249AB8` gates level 3 exactly when `obj->0x25E8` is null and
      releases it the moment it is not. That field is filled by
      `gcn_pool_acquire(obj->0x25EC, 2)` — a **search for a record tagged 2**,
      not an allocation. So the stuck mask is a symptom: no tag-2 record ever
      reaches the ring. `gcn_pool_acquire` also fails outright when
      `ring->0x34` is non-zero, which is a second, distinct failure mode to
      rule out by measurement rather than by reading.
- [x] **Dumps name their addresses** (`host/symbols.c`). `mgs_dump_threads`
      had always taken a `symbol` callback and nothing ever passed one; the
      task table now resolves through `config/symbols/`, which is how
      `mpeg_movie_task` and the gated level 3 were read off in one dump.
- [x] **The movie waits on an event key nothing posts** (F225). Dumping
      values rather than changes: the movie context is `0x8107F080` (itself a
      **level-3 task node**, `fn 0x7F151E70`), and it polls for key
      **`0x006647BA`**. The event table holds eight keys - `0039D437`,
      `002D5221`, `00C52070`, `007CD989`, `00541E36` x2, `00541E37` x2 - and
      **`0x006647BA` is not one of them**, nor is it among the 822 writes to
      the entry area across a run. **The fault is one absent key.** New:
      `MGS_DUMP=<addr>:<len>`.
- [x] **movie.dat is not Ogg; the event system works** (F224). `movie.dat`
      has **zero** `OggS` pages; `demo.dat` has 100 in 2 MB - so the Vorbis
      decoder reads `demo.dat`, and the audio-pacing chain conflated two
      streams that merely stall alike. And the engine's event table is alive:
      double-buffered, swapped each frame, counts climbing, several keys
      active. **The movie's poll fails to match a key, rather than finding an
      empty table.** New: `MGS_WATCH=<addr>:<len>` reports which word in a
      range changed.
- [x] **AX mixes 16,506 frames either way** (F223). `__AXOutDspReady` has two
      exits and I counted one: including `__AXDSPResumeCallback`'s path, both
      configurations mix ~16,500 frames. **AX was never stopped**, and
      `MGS_DSP_RESUME` changes nothing measurable. The mixer is not pulling
      the movie's audio either - 16,506 frames against **13** Vorbis read
      calls. Genuinely fixed: posting task mails with `__DSP_curr_task` null
      wrote through a null pointer into guest low memory and destroyed the
      thread list; now gated, list clean. The report also says why each mail
      was withheld.
- [x] **Correction: AX neither breaks nor fixes the stream** (F222). Repeated
      runs show `MGS_DSP_RESUME=1` and the default are **identical** - slots
      5, 9 files, 8 movie reads - so "AX breaks the stream setup" was one bad
      measurement taken under load. And 32,989 AX frames leave the decode
      buffer's fill advancing **20 times in both**: running AX changes
      nothing. The game's AX callback *is* registered (`0x8004EB8C`, beside
      `sd_sound.c`). **The blocker is downstream of AX, not at it.**
- [~] **AX frames can be driven, paced by the audio DMA** (F221). AX is
      initialised and its callback registered (`__AID_Callback` = 
      `__AXOutAiCallback`, watched). Under `MGS_DSP_RESUME=1` the real frame
      cycle runs, and the pacing dominates: a timer gives **90** frames, 
      gating on guest->DSP mails **3**, gating on the audio-DMA interrupt
      **32,989**. One resume per AID is one AX frame. **But the movie still
      does not play** - with AX running the stream's slots are never armed
      (0 transitions against the default's 5), so the two now interfere.
      Default path verified unregressed.
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
- [ ] **The frame rate reaches the guest's own rate — 50 fps in gameplay,
      25 in cutscenes** (F331). *Not met.* Partly done and measured rather
      than guessed:
      - With the drawing ablated (`MGS_NO_RASTER=1`) the run sits at exactly
        **50.0 fps**, so guest execution, FIFO parsing, the copies, the
        readback and presentation all fit the budget together. The whole
        shortfall is inside `mgs_raster_triangle`, which is ours.
      - A diagnostic roughness scan of the whole bound texture was running
        on **every textured triangle** behind no flag — a 917 KB
        cache-hostile walk, 600,000 times per fifty frames. Moved to the
        decode, where the value actually belongs. Heaviest scene
        **12.0 → 18.1 fps**, 4.00 → 2.55 us per triangle.
      - Then five more pieces of our own overhead, each measured (F336):
        the patch lookup's miss path (5.0% of all samples), `getenv` on the
        per-vertex and per-BP-write paths (1.4%), the texture upload's
        per-pixel byte shuffle (2.0%), a kernel submit per batch (`ioctl`,
        4.5%) and an `mmap`/`munmap` pair per batch (0.9%).
      - **Heavy scene 12.0 -> 24.0 fps over the session**, against a
        cutscene's correct rate of 25. Gameplay's 50 is still unmeasured
        because a headless run cannot press Start.
      - Next, and probably worth more than all of the above: there are ~6
        readbacks per frame (6.6 GB over 95 s) because every EFB copy,
        including every render-to-texture pass, is downloaded so the CPU
        can encode it into guest memory. That encode belongs on the GPU.
      - The dispatch loop behind one deadline and one diagnostics flag
        (F343): +19% throughput on the step clock, heavy scenes 21.9-23.8
        fps. Textures made from EFB copies are now cached on the GPU
        instead of re-uploaded per batch (F345): uploads ~1,900 -> ~270 per
        fifty frames, heaviest scene 24.0 fps. Render-to-texture copies
        now stay on the GPU (F346): 7% less CPU and 9% less wall time for
        the same guest work, readbacks 13,324 -> 4,331 a run. Remaining:
        the patched-call round trip through the host loop (~5%, recompiler
        structure), per-primitive draw state (~5%), and presenting the frame
        copy from the GPU instead of reading it back.
      Measure it with `MGS_TIME_FRAME=1`; run headless with
      `SDL_VIDEODRIVER=offscreen` (a real Vulkan device, no window) and end
      the run with `MGS_RUN_SECONDS` so the exit report survives. Read the
      profile's BY OBJECT table, not its address split (F335). Check
      `uptime` first: a shared machine is not a measurement.

---

## Phase 4 — Audio

*Exit: music, codec calls and SFX match Dolphin's output within tolerance.*

- [ ] AX voice mixer, `AXRegisterCallback` at 5 ms, mix at 32 kHz into SDL3.
      The rate is confirmed from the hardware register rather than assumed:
      the game starts the audio interface with AICR bit 1 clear, which is
      32 kHz (F333). The mixer runs on its own thread with the sound card's
      clock, and its own 32 kHz clock when there is no card (F332).
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
