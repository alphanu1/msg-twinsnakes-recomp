/* The one thing the DSP does that the game can observe: voices advance.
 *
 * WHY THIS EXISTS. The engine's streamed-media pipeline is clocked by a
 * single field. `sd_stream_pump` reads `pb.addr.currentAddress` out of the
 * AX voice each frame and uses it to decide which block of audio has been
 * consumed; when a block completes it asks the engine for more data, which
 * drains the decode buffer, which lets the record ring's read cursor move,
 * which is what eventually delivers a video record to the movie decoder. If
 * that one field never moves, none of it happens and the picture freezes
 * with 321 perfectly good video records sitting unread (HANDOFF F225-F235).
 *
 * On hardware the DSP owns that field: AX copies each voice's parameter
 * block into `__AXPB`, the DSP mixes a frame and advances the position, and
 * `__AXServiceVPB` copies it back. We run no DSP, so it never advances.
 *
 * THIS IS NOT A MIXER, AND IT IS NOT PHASE 4. The design document already
 * separates the two: line 190 puts the native voice mixer - PCM decode, per
 * voice SRC, 32 kHz output - in phase 4, while line 193 records that the
 * DSP's *pacing* is load-bearing much earlier, because "the audio DMA's
 * completion is what asks the game for the next buffer of sound, so with it
 * unmodelled a streaming game does not run silently, it STOPS". This is that
 * same finding one level deeper: the position paces the machine exactly as
 * the interrupt does. Nothing here produces a sample of audio.
 *
 * WHAT IT COSTS TO BE WRONG. Advancing too fast makes the game read ahead of
 * itself; too slow and the movie plays in slow motion. Neither corrupts
 * anything - the game re-derives everything from the position it reads - so
 * the failure mode is visible and cheap, which is why this is worth doing
 * before a real mixer rather than after.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "module.h"
#include "../runtime/memory/guest.h"
#include "../runtime/platform/sdl_audio.h"

/* `__AXPB`, the array AX hands to the DSP. Established in F235: the address
 * is the one `__AXServiceVPB` (0x80033330) indexes with `index * 0xF4`, and
 * `dolsdk2004`'s AXVPB.c declares it `AXPB __AXPB[AX_MAX_VOICES]`. A build
 * constant, so it is overridable rather than compiled in as an assumption. */
#define AXPB_DEFAULT_BASE  0x801F5E00u
#define AXPB_STRIDE        0xF4u
#define AXPB_COUNT         64u

/* Offsets within AXPB, from dolsdk2004's ax.h. */
#define PB_STATE           0x0Eu      /* 0 stopped, 1 running            */
#define PB_ADDR_LOOPFLAG   0x6Eu
#define PB_ADDR_FORMAT     0x70u
#define PB_ADDR_LOOP_HI    0x72u
#define PB_ADDR_END_HI     0x76u
#define PB_ADDR_CURR_HI    0x7Au
#define PB_MIX_VL          0x12u      /* AXPBMIX.vL                      */
#define PB_MIX_VR          0x16u      /* AXPBMIX.vR                      */
#define PB_VE_VOLUME       0x64u      /* AXPBVE.currentVolume            */
#define PB_VE_DELTA        0x66u      /* AXPBVE.currentDelta, per sample */
#define PB_SRC_RATIO_HI    0xA6u
#define PB_SRC_FRAC        0xAAu

/* Sample formats, from dolsdk2004's AXSetVoiceAddr. The address units differ
 * per format and that is the whole of the arithmetic below: PCM16 counts
 * samples, PCM8 counts bytes, ADPCM counts NIBBLES - which is why the SDK
 * asserts that no address lands on an ADPCM frame header. */
#define AX_FMT_ADPCM       0u
#define AX_FMT_PCM16       10u
#define AX_FMT_PCM8        25u

/* AXPBADPCM, at pb+0x7E: eight coefficient pairs, then gain, the frame's
 * predictor/scale byte, and the two previous output samples the filter needs. */
#define PB_ADPCM_COEF      0x7Eu      /* s16 a[8][2]                      */
#define PB_ADPCM_PRED      0xA0u      /* pred_scale                       */
#define PB_ADPCM_YN1       0xA2u
#define PB_ADPCM_YN2       0xA4u

/* AX mixes at 32 kHz in 5 ms frames. */
#define AX_MIX_RATE        32000u

/* AX runs a 5 ms frame and outputs 32 kHz, so a frame consumes 160 output
 * samples' worth of each voice, scaled by that voice's own SRC ratio. */
#define AX_SAMPLES_PER_FRAME 160u

/* Guest memory, for the samples. Voices read from ARAM: the game DMAs
 * decoded audio there (the run reports hundreds of ARAM transfers in), and
 * the DSP's accelerator reads it directly, which is why nothing in MEM1
 * holds the waveform. */
static GuestMemory* s_mem;

void mgs_ax_dsp_set_memory(GuestMemory* mem);
void mgs_ax_dsp_set_memory(GuestMemory* mem) { s_mem = mem; }

/* One sample from a voice, as a signed 16-bit value.
 *
 * `at` is in the format's own units. Out-of-range reads return silence
 * rather than folding, because a voice pointing outside ARAM is a bug to
 * hear as silence and count, not to paper over with wrapped noise. */
static int sample_at(unsigned format, uint32_t at, int* ok)
{
    const uint8_t* a;
    uint32_t off;

    *ok = 0;
    if (!s_mem || !s_mem->aram) return 0;
    a = s_mem->aram;

    if (format == AX_FMT_PCM16) {
        off = at * 2u;
        if (off + 1u >= GUEST_ARAM_SIZE) return 0;
        *ok = 1;
        return (int)(int16_t)(((uint16_t)a[off] << 8) | a[off + 1u]);
    }
    if (format == AX_FMT_PCM8) {
        if (at >= GUEST_ARAM_SIZE) return 0;
        *ok = 1;
        return (int)(int8_t)a[at] * 256;
    }
    return 0;              /* ADPCM has state; decoded by adpcm_step below */
}

/* Defined below, next to the other guest accessors. */
static uint32_t rd16(void* cpu, uint32_t at);
static void wr16(void* cpu, uint32_t at, uint32_t v);

static int clamp16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

/* ONE ADPCM SAMPLE, AND WHY IT CANNOT BE POINT-SAMPLED.
 *
 * DSP-ADPCM is a second-order predictor: each output depends on the two
 * before it, so the decoder has to walk every nibble in order. PCM can be
 * read at whatever position the resampler asks for; this cannot, and
 * skipping to the target nibble would decode against the wrong history and
 * produce noise that still looks like audio.
 *
 * So this steps ONE nibble and returns its sample, and the caller steps it
 * as many times as the resampler advanced. At the game's ratio of about
 * 1.38 that is one or two decodes per output sample.
 *
 * Layout: sixteen nibbles per 8-byte frame, the first two being the
 * predictor/scale header - which is what the SDK's assert in
 * `AXSetVoiceAddr` is checking when it refuses an address whose low nibble
 * is 0 or 1.
 */
static int adpcm_step(void* cpu, uint32_t pb, uint32_t at, int* ok)
{
    const uint8_t* a;
    uint32_t frame, idx, byte;
    int ps, scale, ci, nib, pred, out, yn1, yn2;

    *ok = 0;
    if (!s_mem || !s_mem->aram) return 0;
    a = s_mem->aram;

    frame = at >> 4;
    idx   = at & 0xFu;
    if (idx < 2u) idx = 2u;                  /* never decode the header */
    byte = frame * 8u + (idx >> 1);
    if (byte >= GUEST_ARAM_SIZE) return 0;

    /* The frame's header is re-read at every sample rather than cached: the
     * position can be moved by the game between frames, and a cached scale
     * from a frame we are no longer in decodes to noise. */
    ps = a[frame * 8u];
    scale = 1 << (ps & 0xFu);
    ci = (ps >> 4) & 0x7u;

    nib = (idx & 1u) ? (a[byte] & 0xFu) : (a[byte] >> 4);
    if (nib > 7) nib -= 16;                   /* sign-extend the 4 bits */

    yn1 = (int)(int16_t)rd16(cpu, pb + PB_ADPCM_YN1);
    yn2 = (int)(int16_t)rd16(cpu, pb + PB_ADPCM_YN2);
    pred = (int)(int16_t)rd16(cpu, pb + PB_ADPCM_COEF + (uint32_t)ci * 4u) * yn1
         + (int)(int16_t)rd16(cpu, pb + PB_ADPCM_COEF + (uint32_t)ci * 4u + 2u) * yn2;

    out = clamp16((((nib * scale) << 11) + 1024 + pred) >> 11);

    wr16(cpu, pb + PB_ADPCM_YN2, (uint32_t)(uint16_t)(int16_t)yn1);
    wr16(cpu, pb + PB_ADPCM_YN1, (uint32_t)(uint16_t)(int16_t)out);
    wr16(cpu, pb + PB_ADPCM_PRED, (uint32_t)ps);
    *ok = 1;
    return out;
}

static uint32_t base(void)
{
    static uint32_t cached;
    if (!cached) {
        const char* env = getenv("MGS_AXPB");
        cached = env ? (uint32_t)strtoul(env, NULL, 0) : AXPB_DEFAULT_BASE;
    }
    return cached;
}

/* The guest is big-endian and these fields are halfwords at offsets that are
 * not all 4-aligned, so every access goes through the 32-bit accessor and
 * picks its half explicitly. Casting a guest structure is forbidden for
 * exactly this reason. */
static uint32_t rd16(void* cpu, uint32_t at)
{
    uint32_t w = mgs_module_guest_read32(cpu, at & ~3u);
    return (at & 2u) ? (w & 0xFFFFu) : (w >> 16);
}

static void wr16(void* cpu, uint32_t at, uint32_t v)
{
    uint32_t aligned = at & ~3u;
    uint32_t w = mgs_module_guest_read32(cpu, aligned);
    if (at & 2u) w = (w & 0xFFFF0000u) | (v & 0xFFFFu);
    else         w = (w & 0x0000FFFFu) | ((v & 0xFFFFu) << 16);
    mgs_module_guest_write32(cpu, aligned, w);
}

/* A 32-bit field stored as two halfwords, which is how AX keeps every
 * address in a parameter block: the game reads them with one unaligned
 * `lwz`, so hi is at `at` and lo at `at + 2`. */
static uint32_t rd32pair(void* cpu, uint32_t at)
{
    return (rd16(cpu, at) << 16) | rd16(cpu, at + 2u);
}

static void wr32pair(void* cpu, uint32_t at, uint32_t v)
{
    wr16(cpu, at, v >> 16);
    wr16(cpu, at + 2u, v & 0xFFFFu);
}

static uint64_t s_frames, s_advanced, s_looped, s_ended;
static uint64_t s_mixed_voices, s_adpcm_skipped, s_silent_reads;
static uint64_t s_starved, s_nonzero_frames, s_adpcm_samples;
static uint64_t s_rd_pcm, s_nz_pcm, s_rd_adpcm, s_nz_adpcm;
static uint64_t s_vol_zero, s_mix_zero;
static uint64_t s_loop_has_data, s_loop_empty;
static int      s_peak;
static uint64_t s_clipped, s_out_samples;
static int      s_trace = -1;
static unsigned s_traced, s_ovr_logged;

/* One AX frame: 5 ms of 32 kHz stereo. */
#define AX_FRAME_SAMPLES AX_SAMPLES_PER_FRAME

void mgs_ax_dsp_frame(void* cpu);
void mgs_ax_dsp_frame(void* cpu)
{
    uint32_t pb = base();
    int32_t acc_l[AX_FRAME_SAMPLES], acc_r[AX_FRAME_SAMPLES];
    int16_t out[AX_FRAME_SAMPLES * 2];
    unsigned i;
    int any = 0;

    ++s_frames;
    if (s_trace < 0) s_trace = getenv("MGS_TRACE_AXMIX") != NULL;
    for (i = 0; i < AX_FRAME_SAMPLES; ++i) { acc_l[i] = 0; acc_r[i] = 0; }

    for (i = 0; i < AXPB_COUNT; ++i, pb += AXPB_STRIDE) {
        uint32_t curr, end, loop, ratio, frac, format, looping;
        uint32_t vol, vl, vr;
        int vdelta;
        unsigned k;

        if (rd16(cpu, pb + PB_STATE) != 1u) continue;      /* not running */

        ratio = rd32pair(cpu, pb + PB_SRC_RATIO_HI);
        if (!ratio) continue;

        format  = rd16(cpu, pb + PB_ADDR_FORMAT);
        looping = rd16(cpu, pb + PB_ADDR_LOOPFLAG);
        curr    = rd32pair(cpu, pb + PB_ADDR_CURR_HI);
        end     = rd32pair(cpu, pb + PB_ADDR_END_HI);
        loop    = rd32pair(cpu, pb + PB_ADDR_LOOP_HI);
        frac    = rd16(cpu, pb + PB_SRC_FRAC);

        /* AX's volumes are 15-bit, 0x7FFF being unity - which is what the
         * game writes for a voice at full level. */
        vol = rd16(cpu, pb + PB_VE_VOLUME);
        vdelta = (int)(int16_t)rd16(cpu, pb + PB_VE_DELTA);
        vl  = rd16(cpu, pb + PB_MIX_VL);
        vr  = rd16(cpu, pb + PB_MIX_VR);


        /* MGS_AX_NO_ADPCM restores the pre-F248 behaviour - ADPCM voices
         * skipped entirely, so their positions never move - purely so that
         * "did decoding ADPCM change the guest's behaviour" can be answered
         * by measurement instead of by argument. It is a bisect switch, not
         * a mode anyone should run in. */
        if (format == AX_FMT_ADPCM) {
            static int no_adpcm = -1;
            if (no_adpcm < 0) no_adpcm = getenv("MGS_AX_NO_ADPCM") != NULL;
            if (no_adpcm) { ++s_adpcm_skipped; continue; }
        }

        ++s_mixed_voices;
        ++s_advanced;
        /* THE LAST LINK. 99% of samples read are non-zero and the output is
         * silent, which leaves only the gain between them. */
        if (!vol) ++s_vol_zero;
        if (!vl && !vr) ++s_mix_zero;

        /* MGS_TRACE_AXMIX: the first few voices as the mixer sees them.
         * "Silent output" has three very different causes - no samples, no
         * volume, or a voice pinned at its end address - and the numbers
         * that separate them are these five. */
        /* EVERY 20,000th MIX, NOT THE FIRST FEW. The opening mixes happen
         * before AX has copied the address block into the DSB-side PB, so
         * they show initial values - format 10 and a 4 KB window - and
         * reading them as the steady state produced a whole wrong finding
         * (F251's "end is never extended"). The real state is format 0 and
         * a 7.6-million-sample region. */
        if (s_trace && (++s_traced % 20000u) == 1u) {
            fprintf(stderr, "[axmix] voice %02u fmt %2u curr %08X end %08X "
                            "loop %08X %s vol %04X vl %04X vr %04X ratio %08X\n",
                    i, format, curr, end, loop,
                    looping ? "loop" : "once", vol, vl, vr, ratio);
        }

        for (k = 0; k < AX_FRAME_SAMPLES; ++k) {
            uint32_t prev = curr;
            int ok, sv;

            if (format == AX_FMT_ADPCM) {
                /* Walk every nibble the resampler passed over, so the
                 * predictor's history is the one the encoder assumed. */
                sv = adpcm_step(cpu, pb, curr, &ok);
                ++s_adpcm_samples;
            } else {
                sv = sample_at(format, curr, &ok);
            }
            if (!ok) { ++s_silent_reads; sv = 0; }
            /* NON-ZERO SAMPLES, BY FORMAT. "253,816 loop points held data"
             * and "402 non-silent frames" cannot both be true, and the
             * number that separates them is how many samples each kind of
             * voice actually contributes. */
            if (format == AX_FMT_ADPCM) {
                ++s_rd_adpcm; if (sv) ++s_nz_adpcm;
            } else {
                ++s_rd_pcm; if (sv) ++s_nz_pcm;
            }
            /* ONE ROUNDED SCALE, NOT TWO TRUNCATING ONES.
             *
             * This was `sv * vol / 32768 * vr / 32768`, and each division
             * truncates toward zero. AX's volumes are 0x7FFF - a shade UNDER
             * unity - so a quiet sample came out as `1 * 0.9995 * 0.9995`
             * and truncated to silence twice over. The movie's audio fades
             * in at plus or minus a few counts, so almost all of it was
             * being discarded: 99% of samples read were non-zero and only
             * 402 of 62,763 output frames were.
             *
             * Multiplying first and rounding once keeps them. 2^30 is the
             * product of the two 15-bit scales. */
            {
                long long pl = (long long)sv * (int)vol * (int)vl;
                long long pr = (long long)sv * (int)vol * (int)vr;
                acc_l[k] += (int32_t)((pl + (1LL << 29)) >> 30);
                acc_r[k] += (int32_t)((pr + (1LL << 29)) >> 30);
            }

            /* THE VOLUME IS A RAMP, NOT A LEVEL.
             *
             * `AXPBVE` is {currentVolume, currentDelta} and the DSP advances
             * it EVERY SAMPLE; the game starts a voice at volume 0 with a
             * positive delta so it fades in. Reading `currentVolume` without
             * ever applying the delta therefore leaves almost every voice
             * silent for ever - 105,498 of 106,326 voice-mixes had volume 0,
             * which is the whole of the missing sound. */
            if (vdelta) {
                int nv = (int)vol + vdelta;
                if (nv < 0) nv = 0;
                if (nv > 0xFFFF) nv = 0xFFFF;
                vol = (uint32_t)nv;
            }

            /* ADVANCE BY WHAT WAS CONSUMED, which is the whole point: the
             * position the game reads back is now the position the mixer
             * actually played from, not a number this file invented. */
            frac += ratio;
            curr += frac >> 16;
            frac &= 0xFFFFu;

            /* For ADPCM the samples between `prev` and `curr` still have to
             * be decoded, or the filter state is wrong from here on. */
            if (format == AX_FMT_ADPCM) {
                uint32_t step_at = prev + 1u;
                while (step_at < curr) {
                    int ok2;
                    adpcm_step(cpu, pb, step_at, &ok2);
                    ++s_adpcm_samples;
                    ++step_at;
                }
            }

            if (end && curr > end) {
                /* WHAT TO DO AT THE END OF A BLOCK, AND WHY THIS IS NOT
                 * COSMETIC (F249).
                 *
                 * `sd_stream_pump` decides a block has been consumed by
                 * watching this very position move, so how the overrun is
                 * handled is not an audio detail - it is whether the engine
                 * can tell the time. Three behaviours were measured:
                 *
                 *   pin at `end`     movie.dat 8 reads  - the F225 stall
                 *   run on past it   movie.dat 34 reads - reads unwritten ARAM
                 *   jump to `loop`   movie.dat 34 reads - and the data is there
                 *
                 * A streaming voice holds `loop > end` between refills - the
                 * game saying "continue at `loop`" before it has extended
                 * `end` to cover that block - so the ordinary wrap and this
                 * case are separated. Probing the loop point shows the data
                 * is genuinely present: 253,816 held data against 3,287
                 * empty, so the refill is neither late nor misplaced. */
                if (looping && loop <= end) {
                    curr = loop + (curr - end - 1u);   /* an ordinary loop */
                    ++s_looped;
                } else if (looping) {
                    int ok3;
                    /* HOW BIG IS THE BLOCK THE GAME IS GIVING US?
                     *
                     * 4.1 overruns per frame against 220 samples consumed
                     * implies blocks of about 54 samples, which would be
                     * absurd for a streamed voice - so either the game is
                     * not extending `end`, or we are misreading it. Logging
                     * the geometry of the first few settles which. */
                    if (s_trace && (++s_ovr_logged % 50000u) == 1u) {
                        fprintf(stderr, "[axovr] voice %02u curr %08X end %08X"
                                        " loop %08X span(loop..end) %d\n",
                                i, curr, end, loop, (int)(end - loop));
                    }
                    int probe = (format == AX_FMT_ADPCM)
                              ? 1 : sample_at(format, loop, &ok3);
                    if (format == AX_FMT_ADPCM || (ok3 && probe))
                        ++s_loop_has_data;
                    else
                        ++s_loop_empty;
                    curr = loop;
                    ++s_starved;
                    /* AND STOP FOR THIS FRAME.
                     *
                     * `loop` is `end + 1` here - the console shows the same
                     * geometry, so this is the game's real streaming
                     * hand-off and not a misread: voice 60 in a Dolphin
                     * snapshot reads loop 0x0000D000, end 0x0000CFFF,
                     * loopFlag 1, with `curr` still well inside the block.
                     *
                     * So after jumping, `curr > end` is STILL true, and
                     * continuing the sample loop re-entered this branch for
                     * every remaining sample of the frame: the voice stuck
                     * at one position repeating a sample, and the overrun
                     * counter read 388,532 against 36,720 voice-mixes -
                     * about seventeen per frame, where a real block boundary
                     * can only happen once. That is the "voices cut out, the
                     * full sample is not played" the user reported.
                     *
                     * A voice whose queued block is exhausted has nothing to
                     * play until the game extends `end`, and what hardware
                     * emits then is silence, not the last sample over and
                     * over. Leaving `curr` at `loop` is what the engine
                     * watches to decide the block was consumed (F249), so
                     * the refill still gets its signal. */
                    break;
                } else {
                    /* A one-shot voice that reaches its end stops, and
                     * `__AXServiceVPB` copies that state back to the game. */
                    curr = end;
                    wr16(cpu, pb + PB_STATE, 0u);
                    ++s_ended;
                    break;
                }
            }
        }
        any = 1;
        wr16(cpu, pb + PB_SRC_FRAC, frac);
        wr16(cpu, pb + PB_VE_VOLUME, vol);        /* the ramp's new level */
        wr32pair(cpu, pb + PB_ADDR_CURR_HI, curr);
    }

    /* PUSHED EVEN WHEN SILENT. The device is the guest's pacing partner; a
     * gap in the stream is a gap in time, and dropping silent frames would
     * make the port sound correct while running wrong. */
    (void)any;
    /* PEAK LEVEL, because "the mixer ran" and "the mixer produced sound" are
     * different claims and a frame count cannot separate them. */
    for (i = 0; i < AX_FRAME_SAMPLES; ++i) {
        int32_t m = acc_l[i] < 0 ? -acc_l[i] : acc_l[i];
        int32_t n = acc_r[i] < 0 ? -acc_r[i] : acc_r[i];
        if (m > s_peak) s_peak = (int)m;
        if (n > s_peak) s_peak = (int)n;
        if (m || n) { ++s_nonzero_frames; break; }
    }

    /* HOW MUCH IS CLIPPED, not just how loud the loudest sample was.
     *
     * A peak above full scale says the mix clipped SOMEWHERE; it cannot say
     * whether that was one sample in a run or one in three, and those are a
     * tuning note and a bug respectively. The peak read 200% as soon as most
     * frames stopped being silent (F268), so the count decides which. */
    for (i = 0; i < AX_FRAME_SAMPLES; ++i) {
        int32_t l = acc_l[i], r = acc_r[i];
        ++s_out_samples;
        if (l > 32767 || l < -32768) ++s_clipped;
        if (r > 32767 || r < -32768) ++s_clipped;
        if (l > 32767) l = 32767; if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        out[i * 2u] = (int16_t)l;
        out[i * 2u + 1u] = (int16_t)r;
    }
    mgs_audio_push(out, AX_FRAME_SAMPLES);
}

void mgs_ax_dsp_report(void);
void mgs_ax_dsp_report(void)
{
    uint64_t pushed = 0, dropped = 0, under = 0;
    if (!s_frames) return;
    mgs_audio_stats(&pushed, &dropped, &under);
    printf("AX mixer: %llu frames (%.1fs of sound), %llu voice-mixes, "
           "%llu loops, %llu ended\n",
           (unsigned long long)s_frames,
           (double)s_frames * AX_FRAME_SAMPLES / (double)AX_MIX_RATE,
           (unsigned long long)s_mixed_voices,
           (unsigned long long)s_looped, (unsigned long long)s_ended);
    printf("  ADPCM samples decoded: %llu (voices skipped: %llu);"
           " samples read outside ARAM: %llu; voices starved: %llu\n",
           (unsigned long long)s_adpcm_samples,
           (unsigned long long)s_adpcm_skipped,
           (unsigned long long)s_silent_reads,
           (unsigned long long)s_starved);
    printf("  on overrun: %llu loop points held data, %llu were empty\n",
           (unsigned long long)s_loop_has_data,
           (unsigned long long)s_loop_empty);
    printf("  samples contributed: PCM %llu of %llu non-zero, "
           "ADPCM %llu of %llu non-zero\n",
           (unsigned long long)s_nz_pcm, (unsigned long long)s_rd_pcm,
           (unsigned long long)s_nz_adpcm, (unsigned long long)s_rd_adpcm);
    printf("  gain: %llu voice-mixes had envelope volume 0, "
           "%llu had both mix levels 0, of %llu\n",
           (unsigned long long)s_vol_zero, (unsigned long long)s_mix_zero,
           (unsigned long long)s_mixed_voices);
    printf("  output: peak %d of 32767 (%.1f%% of full scale), "
           "%llu of %llu frames not silent\n",
           s_peak, 100.0 * s_peak / 32767.0,
           (unsigned long long)s_nonzero_frames,
           (unsigned long long)s_frames);
    printf("  device: %llu frames queued, %llu dropped (no device), "
           "%llu underruns\n",
           (unsigned long long)pushed, (unsigned long long)dropped,
           (unsigned long long)under);
    printf("  clipping: %llu of %llu output samples clipped (%.2f%%)\n",
           (unsigned long long)s_clipped, (unsigned long long)s_out_samples,
           s_out_samples ? 100.0 * (double)s_clipped / (double)s_out_samples
                         : 0.0);
}
