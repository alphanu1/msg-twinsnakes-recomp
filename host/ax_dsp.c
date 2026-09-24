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
#include <time.h>
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
#define PB_SRC_SELECT      0x08u      /* AXPB.srcSelect                  */
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

/* HOW LATE IS THE REFILL, in AX frames.
 *
 * A starved voice stands still, and this game's movie clock IS the sound
 * position (F245), so every frame a voice does not advance is a frame the
 * movie's pacing does not advance either - which is what keeps the movie
 * task falling out of its playing state (F272). "8.7% of voice-mixes
 * starved" says it happens; it does not say whether the refill is one frame
 * late or twenty, and those want different fixes.
 *
 * So: remember the frame a voice starved on, and when the game finally
 * extends that voice's `end`, record the gap. */
static uint64_t s_starve_at[AXPB_COUNT];
static uint32_t s_seen_end[AXPB_COUNT];
static uint64_t s_refill_n, s_refill_frames, s_refill_max;

/* HOW MUCH RUNWAY A STREAMING VOICE HAS, in samples ahead of the read
 * position. The console keeps a streaming voice about twelve AX frames
 * ahead of its end; if ours starts with that and loses it, something is
 * consuming the headroom, and if it never has it the game is simply not
 * being asked for more. Those want opposite fixes. */
static uint64_t s_played_on;
static uint64_t s_head_n, s_head_sum;
static uint32_t s_head_min = 0xFFFFFFFFu, s_head_first;
static uint64_t s_mixed_voices, s_adpcm_skipped, s_silent_reads;
static uint64_t s_starved, s_nonzero_frames, s_adpcm_samples;
static uint64_t s_rd_pcm, s_nz_pcm, s_rd_adpcm, s_nz_adpcm;
static uint64_t s_vol_zero, s_mix_zero;
/* IS ANYTHING PANNED AT ALL?
 *
 * The two output channels come out bit-identical, correlating at exactly
 * 1.000 at lag 0, and there are two very different explanations: the game
 * mixes every voice dead centre (which for this game on GameCube would be
 * right), or we are applying one volume to both channels. The difference is
 * visible here and nowhere else, so it is counted here: how many mixes ask
 * for different left and right levels, and the distinct pairs. */
static uint64_t s_panned;
static uint32_t s_pan_key[8];
static uint64_t s_pan_hits[8];
static unsigned s_pan_n;
static uint64_t s_loop_has_data, s_loop_empty;
static int      s_peak;
static uint64_t s_clipped, s_out_samples;
static int      s_trace = -1;
static unsigned s_traced, s_ovr_logged;

/* THE RESAMPLER'S HISTORY, PER VOICE, ACROSS FRAMES.
 *
 * This was a local, re-seeded at every AX frame, and that was a real fault
 * rather than an inefficiency: the reader runs ONE INPUT SAMPLE AHEAD of
 * the position it plays from, so at the end of a frame the ADPCM predictor
 * in the parameter block sits at `curr + 1`. Re-seeding decoded `curr`
 * again with that predictor - wrong history, wrong output - and then
 * re-decoded `curr + 1` on top of it. Once per frame is two hundred times a
 * second, and Ben heard it as a gargle over the voices. Only ADPCM voices
 * are affected, because only they carry state, and the dialogue is ADPCM.
 *
 * Kept per voice and carried across frames, so the decoder walks each
 * position exactly once for as long as the game leaves the voice alone. */
static int      s_h0[64], s_h1[64];
static uint32_t s_hpos[64];
static uint8_t  s_hvalid[64];
static uint64_t s_reseeds;

/* Where each voice was left at the end of the last frame, so a rewind by
 * the game can be seen rather than inferred. */
static uint32_t s_left_curr[64];
static uint8_t  s_left_curr_valid[64];
static uint64_t s_rewinds, s_rewind_total;
static uint32_t s_rewind_max, s_rewind_samples[8];
static unsigned s_rewind_n;

/* ONE INPUT SAMPLE, whichever format the voice is in.
 *
 * ADPCM is stateful - each call assumes the predictor is at `at - 1` - so
 * the resampler above must walk positions in order and never revisit one.
 * That is why the history below is a two-entry queue rather than random
 * access to the source. */
static int read_one(void* cpu, uint32_t pb, unsigned format,
                    uint32_t at, int* ok)
{
    if (format == AX_FMT_ADPCM) {
        ++s_adpcm_samples;
        return adpcm_step(cpu, pb, at, ok);
    }
    return sample_at(format, at, ok);
}

/* MGS_AX_NEAREST restores point sampling, so "did interpolating change
 * what Ben hears" is a switch rather than an argument. */
static int ax_nearest(void)
{
    static int v = -1;
    if (v < 0) v = getenv("MGS_AX_NEAREST") != NULL;
    return v;
}

static int no_playon(void)
{
    static int v = -1;
    if (v < 0) v = getenv("MGS_NO_PLAYON") != NULL;
    return v;
}

/* One AX frame: 5 ms of 32 kHz stereo. */
#define AX_FRAME_SAMPLES AX_SAMPLES_PER_FRAME

/* ---- THE MIXER'S OWN THREAD -------------------------------------------
 *
 * SOUND IS 32 kHz AND HAS NOTHING TO DO WITH HOW OFTEN WE DRAW.
 *
 * Until now the mixer was called from the DSP interrupt's delivery path, on
 * the guest thread - so a frame of audio was produced only when the guest
 * got round to being interrupted. When the guest slowed down, fewer samples
 * came out per real second, and Ben heard exactly that: "as the video slows
 * and speeds up so does the audio". Audio was a faithful reporter of the
 * frame rate, which is precisely what it should never be.
 *
 * It also could not be fixed by making the guest faster, and Ben said so:
 * "my PC resources are still really low, hardly anything used." Thirty-one
 * cores idle while sound waited on the one that was busy.
 *
 * THE CONSOLE AGREES WITH HIM. The DSP is a separate processor. It reads
 * ARAM and updates the voice parameter blocks concurrently with the CPU,
 * and the game is written for that - so running the mixer on its own thread
 * is MORE faithful than running it inside an interrupt, not less. The race
 * on the parameter blocks is the race the hardware has.
 *
 * The clock it runs on is the sound card's: mix another 5 ms whenever the
 * device holds less than the target, and otherwise wait. That is the only
 * clock audio should ever have.
 *
 * MGS_AX_THREAD=0 puts it back on the interrupt, for bisecting.
 */
#include <pthread.h>

static pthread_t  s_ax_thread;
static int        s_ax_thread_on;
static volatile int s_ax_stop;
static void*      s_ax_cpu;

int mgs_ax_thread_active(void);
int mgs_ax_thread_active(void) { return s_ax_thread_on; }

/* Monotonic nanoseconds, for the case where there is no card to pace on. */
static uint64_t ax_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void* ax_thread_main(void* arg)
{
    /* Roughly 120 ms of slack at 32 kHz. Deep enough that a slow frame on
     * the guest thread cannot be heard, shallow enough that a sound follows
     * its picture. */
    const unsigned target = 3840u;
    /* WITH NO CARD, THE WALL CLOCK IS THE CARD.
     *
     * mgs_audio_queued answers 0 for "caught up" AND for "no device", so
     * the queue-depth test was always true on a headless run and this loop
     * mixed as fast as the machine allowed: 8.9 billion samples in 95
     * seconds, one core saturated, and every headless audio measurement
     * taken against a mixer running thousands of times too fast.
     *
     * So when there is no device the thread keeps its own sample clock at
     * the nominal rate. Audio is still never paced by the frame rate -
     * which is the whole point of this thread - it is paced by a clock
     * that runs at 32 kHz whether or not anything is listening. */
    const int have_dev = mgs_audio_have_device();
    uint64_t t0 = ax_now_ns();
    uint64_t produced = 0ull;      /* output samples, no-device path only */
    (void)arg;
    while (!s_ax_stop) {
        int behind;
        if (have_dev) {
            behind = mgs_audio_queued() < target;
        } else {
            /* How many samples a 32 kHz card would have consumed by now,
             * plus the same slack the real path keeps ahead. */
            uint64_t due = (uint64_t)((double)(ax_now_ns() - t0) *
                                      (double)AX_MIX_RATE / 1e9);
            behind = produced < due + target;
        }
        if (behind) {
            mgs_ax_dsp_frame(s_ax_cpu);
            produced += AX_FRAME_SAMPLES;
        } else {
            struct timespec ts;
            ts.tv_sec = 0; ts.tv_nsec = 1000000L;   /* 1 ms */
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

void mgs_ax_thread_start(void* cpu);
void mgs_ax_thread_start(void* cpu)
{
    const char* e = getenv("MGS_AX_THREAD");
    if (s_ax_thread_on || (e && e[0] == '0')) return;
    s_ax_cpu = cpu;
    s_ax_stop = 0;
    if (pthread_create(&s_ax_thread, NULL, ax_thread_main, NULL) == 0) {
        s_ax_thread_on = 1;
        fprintf(stderr, "[ax] mixing on its own thread, paced by the audio "
                        "device (MGS_AX_THREAD=0 to put it back on the "
                        "interrupt)\n");
    }
}

void mgs_ax_thread_stop(void);
void mgs_ax_thread_stop(void)
{
    if (!s_ax_thread_on) return;
    s_ax_stop = 1;
    pthread_join(s_ax_thread, NULL);
    s_ax_thread_on = 0;
}

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
        /* The resampler's two-sample history: `hist1` is the input sample
         * at `hpos`, `hist0` the one before it. Loaded from the per-voice
         * state below, once the position is known. */
        int hist0 = 0, hist1 = 0, hvalid = 0, src_sel, nearest;
        uint32_t hpos = 0;

        if (rd16(cpu, pb + PB_STATE) != 1u) continue;      /* not running */

        ratio = rd32pair(cpu, pb + PB_SRC_RATIO_HI);
        if (!ratio) continue;

        format  = rd16(cpu, pb + PB_ADDR_FORMAT);
        looping = rd16(cpu, pb + PB_ADDR_LOOPFLAG);
        /* AX_SRC_TYPE_NONE is 0 and means "step through the source without
         * interpolating"; everything else is linear or four-tap polyphase,
         * and both of those interpolate. */
        src_sel = (int)rd16(cpu, pb + PB_SRC_SELECT);
        nearest = (src_sel == 0) || ax_nearest();
        curr    = rd32pair(cpu, pb + PB_ADDR_CURR_HI);

        /* DID THE GAME REWIND US?
         *
         * Playing on past `end` into a block the game has already filled
         * stops the gaps, and it measures as an ECHO - a repeat at a
         * constant 3,040 samples through the whole cinematic, which
         * disappears when the play-on is withdrawn. The obvious mechanism
         * is that we read ahead and the game then writes `currentAddress`
         * back to where IT thinks the voice is, so the same samples are
         * played twice.
         *
         * That is a guess until it is counted, and this counts it: what we
         * left the voice at last frame against what it holds now. */
        {
            unsigned vi = i;
            if (vi < 64u && s_left_curr_valid[vi] &&
                curr < s_left_curr[vi]) {
                uint32_t back = s_left_curr[vi] - curr;
                ++s_rewinds;
                s_rewind_total += back;
                if (back > s_rewind_max) s_rewind_max = back;
                if (s_rewind_n < 8u) {
                    s_rewind_samples[s_rewind_n++] = back;
                }
            }
        }
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

        /* CARRY THE HISTORY IN whenever it is AT OR BEHIND the position the
         * game says the voice is at, because the walk below catches it up
         * one sample at a time - which is exactly the contiguous decode
         * ADPCM needs.
         *
         * "At or behind", not "exactly one ahead". The reader finishes a
         * frame holding the sample after the position it last PLAYED from,
         * and the position is then advanced once more before the loop ends,
         * so `hpos == curr + 1` is true only when that last advance moved
         * nothing - at this game's ratio of 1.3769, almost never. Requiring
         * it made 62,149 of 62,162 voice-mixes re-seed, which is to say the
         * fix did nothing at all, and the counter is what said so.
         *
         * A history further behind than one AX frame's worth of input is
         * from before a seek and is not worth walking; that re-seeds. */
        if (i < 64u && s_hvalid[i] && s_hpos[i] <= curr + 1u &&
            curr - s_hpos[i] < 4096u) {
            hist0 = s_h0[i]; hist1 = s_h1[i]; hpos = s_hpos[i]; hvalid = 1;
        } else {
            ++s_reseeds;
        }
        /* Runway: how far `end` is ahead of where we are about to read. */
        if (looping && end > curr) {
            uint32_t head = end - curr;
            ++s_head_n; s_head_sum += head;
            if (head < s_head_min) s_head_min = head;
            if (!s_head_first) s_head_first = head;
        }

        /* The refill's latency, measured where both halves are visible: the
         * mixer knows when it starved, and it re-reads `end` every frame. */
        if (end != s_seen_end[i]) {
            if (s_starve_at[i] && end > s_seen_end[i]) {
                uint64_t late = s_frames - s_starve_at[i];
                ++s_refill_n;
                s_refill_frames += late;
                if (late > s_refill_max) s_refill_max = late;
                s_starve_at[i] = 0;
            }
            s_seen_end[i] = end;
        }

        /* THE LAST LINK. 99% of samples read are non-zero and the output is
         * silent, which leaves only the gain between them. */
        if (!vol) ++s_vol_zero;
        if (!vl && !vr) ++s_mix_zero;
        if (vl != vr) ++s_panned;
        {
            uint32_t key = (vl << 16) | vr;
            unsigned pi;
            for (pi = 0; pi < s_pan_n; ++pi)
                if (s_pan_key[pi] == key) break;
            if (pi < 8u) {
                if (pi == s_pan_n) { s_pan_key[pi] = key; ++s_pan_n; }
                ++s_pan_hits[pi];
            }
        }

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
                            "loop %08X %s vol %04X vl %04X vr %04X ratio %08X "
                            "src %d\n",
                    i, format, curr, end, loop,
                    looping ? "loop" : "once", vol, vl, vr, ratio, src_sel);
        }

        for (k = 0; k < AX_FRAME_SAMPLES; ++k) {
            int ok, sv;

            /* THE OUTPUT SAMPLE SITS BETWEEN TWO INPUT SAMPLES, NOT ON ONE.
             *
             * This voice runs at ratio 0x1607D - 1.3769, which is 44.1 kHz
             * stepped down to the DSP's 32 kHz - and taking the nearest
             * input sample at a ratio like that is the classic aliasing
             * artefact: the quantisation of the sampling instant is itself
             * a signal, at the beat between the two rates, and it lands in
             * the audible band as a thin metallic ring over the voices.
             * Ben described it as "like someone talking into a tin cup",
             * which is what a comb filter at a few hundred Hz sounds like.
             *
             * AX's SRC interpolates: linear for AX_SRC_TYPE_LINEAR, and a
             * four-tap polyphase filter for the 8K/12K/16K types, whose
             * coefficients live in the DSP's own ROM. We do not have those
             * coefficients and will not be dumping them - that ROM is
             * Nintendo's code - so both cases resample linearly here, which
             * is exactly what Dolphin falls back to when the coefficients
             * are unavailable (AXVoice.h, ResampleAudio: "srctype ==
             * SRCTYPE_LINEAR || srctype == SRCTYPE_POLYPHASE"). Linear
             * removes the aliasing that is audible; the difference between
             * linear and four-tap is a gentle treble roll-off.
             *
             * Walking the history forward one input sample at a time is not
             * an optimisation, it is a requirement: ADPCM decoding carries
             * the predictor from the previous sample, so positions must be
             * visited in order and exactly once. That is also why the walk
             * that used to catch up the skipped nibbles is gone - this loop
             * is that walk. */
            if (!hvalid) {
                hist1 = read_one(cpu, pb, format, curr, &ok);
                if (!ok) { ++s_silent_reads; hist1 = 0; }
                hist0 = hist1;
                hpos  = curr;
                hvalid = 1;
            }
            /* A history from before a jump is not a history. */
            if (hpos > curr + 1u) {
                hist1 = read_one(cpu, pb, format, curr, &ok);
                if (!ok) { ++s_silent_reads; hist1 = 0; }
                hist0 = hist1;
                hpos  = curr;
            }
            while (hpos < curr + 1u) {
                hist0 = hist1;
                hist1 = read_one(cpu, pb, format, ++hpos, &ok);
                if (!ok) { ++s_silent_reads; hist1 = 0; }
            }

            if (frac && !nearest) {
                int64_t mix = (int64_t)hist0 * (int64_t)(65536u - frac)
                            + (int64_t)hist1 * (int64_t)frac;
                sv = (int)(mix >> 16);
            } else {
                sv = hist0;
            }
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
                    hvalid = 0;      /* the history is from before the jump */
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
                    int probe_ok = (format == AX_FMT_ADPCM) || (ok3 && probe);
                    if (probe_ok)
                        ++s_loop_has_data;
                    else
                        ++s_loop_empty;
                    ++s_starved;
                    if (!s_starve_at[i]) s_starve_at[i] = s_frames;
                    /* PLAY ON IF THE NEXT BLOCK IS ALREADY THERE.
                     *
                     * A streaming voice holds `loop == end + 1` and the game
                     * extends `end` only after the block at `loop` is
                     * filled - measured at a mean of 2.1 AX frames later.
                     * Stopping for those frames puts a 5 ms hole in the
                     * voice every block: the runway sawtooths from 4,095
                     * samples to nothing 5,596 times a run, and each hole is
                     * a discontinuity you can hear as judder.
                     *
                     * But the data is nearly always already there - the
                     * probe above finds it 99% of the time - because the
                     * DMA lands well before `end` moves. The DSP, reaching
                     * `end` with the loop flag set, loads `loopAddr` and
                     * keeps reading; it does not fall silent waiting for a
                     * bookkeeping field to catch up.
                     *
                     * So when the probe finds data, carry on for the rest of
                     * this frame and no further: `end` is moved out by
                     * exactly what the resampler can still consume before
                     * the frame ends, which can never run past the block the
                     * game has just filled. When the probe finds nothing,
                     * the voice stops as before, because then the data
                     * really is absent and reading on would be inventing
                     * sound. */
                    /* MGS_NO_PLAYON=1 withdraws this, for bisecting.
                     *
                     * Ben reports an echo through the cinematic, and it
                     * measures as a repeat at a CONSTANT 3,040 samples -
                     * 95.00 ms - in twelve of nineteen windows, with no
                     * harmonics, so it is a real repeat and not a bass
                     * note. A fixed delay points at a buffer, and this is
                     * the one place the mixer plays data the game has not
                     * yet told it to. Withdrawing it is the A/B that says
                     * whether this is the cause. */
                    if (probe_ok && !no_playon()) {
                        /* KEEP `curr` WHERE IT IS. This is what caused the
                         * echo, and it is an ordering mistake rather than a
                         * mistaken idea.
                         *
                         * `curr = loop` used to run BEFORE this branch. A
                         * streaming voice holds `loop == end + 1`, so on
                         * the first overrun of a block that assignment is
                         * nearly a no-op. But `end` is re-read from the
                         * parameter block every frame, and the play-on only
                         * moves the LOCAL copy - so the next frame overruns
                         * again with `curr` now a couple of hundred samples
                         * past `end`, and `curr = loop` drags it back to
                         * `end + 1`. The same samples are played again, once
                         * per frame, until the game finally extends `end`.
                         *
                         * Measured: a repeat at a constant 3,040 samples -
                         * 95.00 ms - through the cinematic, with no
                         * harmonics, gone entirely when the play-on is
                         * withdrawn. Ben heard it as an echo and it was only
                         * audible once the GPU stopped the judder from
                         * masking it.
                         *
                         * The data is contiguous - `loop` IS `end + 1` - so
                         * carrying on from where the resampler actually got
                         * to is both correct and what the hardware does. */
                        uint32_t left = (uint32_t)(AX_FRAME_SAMPLES - k);
                        end = curr + (uint32_t)((((uint64_t)left * ratio)
                                                 + frac) >> 16) + 1u;
                        ++s_played_on;
                        continue;
                    }
                    curr = loop;
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
        if (i < 64u) {
            s_h0[i] = hist0; s_h1[i] = hist1;
            s_hpos[i] = hpos; s_hvalid[i] = (uint8_t)(hvalid ? 1u : 0u);
        }
        wr16(cpu, pb + PB_SRC_FRAC, frac);
        wr16(cpu, pb + PB_VE_VOLUME, vol);        /* the ramp's new level */
        wr32pair(cpu, pb + PB_ADDR_CURR_HI, curr);
        if (i < 64u) { s_left_curr[i] = curr; s_left_curr_valid[i] = 1u; }
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
    /* LIMIT, DO NOT CLIP.
     *
     * Two voices at full into both channels sum past full scale, and
     * hard-clamping them is what the mixed output showed: 139,044 samples
     * at the rails, 1.24% of them, and 57,514 sample-to-sample jumps of
     * more than a quarter of full scale - 328 a second. A clamp IS a
     * discontinuity, and 328 a second is heard as constant crackle. That
     * matches the user's report exactly, and their guess that the clipping
     * was causing it.
     *
     * The real machine does not clip here: AX runs a COMPRESSOR. Dolphin
     * implements it (`AXUCode::RunCompressor`) - a threshold test over the
     * frame, then an attack or release ramp whose coefficients come from a
     * table the GAME supplies through a DSP command. We do not parse the
     * DSP command list, so that table and its threshold are not available
     * to us, and this is not a model of it.
     *
     * What it is: a limiter with the same purpose, so the output stops
     * being spliced. The gain is derived from the frame's own peak, moves
     * quickly downwards and slowly back up, and is INTERPOLATED ACROSS THE
     * FRAME - a gain that jumped at frame boundaries would only replace one
     * discontinuity with another every 5 ms.
     *
     * The clipped-sample counter stays: with a real compressor in place it
     * should read zero, and if it starts reading anything again that is the
     * signal that something upstream has changed. */
    /* ONE FRAME OF LOOK-AHEAD, which is what makes it exact.
     *
     * The version above computed the gain from the frame it was about to
     * emit and then ramped INTO it across that same frame - so a peak in
     * the first few samples was multiplied by the gain the limiter had
     * before it knew about the peak, and still hit the rails. That left
     * 3,988 clipped samples of 9.3 million: not many, but every one of them
     * is a splice, and "not many" is not the same as none.
     *
     * Delaying the output by one AX frame fixes it by construction rather
     * than by tuning. Emitting frame N only once frame N+1 has been mixed
     * means both `need[N]` and `need[N+1]` are known, and the gain at the
     * END of frame N can be set to the smaller of the two. A linear ramp
     * between two values never exceeds either of them, so if the ramp
     * starts at a value that already fitted frame N and ends at one that
     * fits both N and N+1, no sample in frame N can reach full scale.
     * Induction does the rest.
     *
     * The cost is 5 ms of latency, against a pacing target that starts at
     * 60 ms, and one silent frame at the very start.
     *
     * This is still NOT AX's compressor. The real machine runs a threshold
     * test and attack/release ramps from a table the GAME supplies through
     * a DSP command we do not parse (Dolphin: `AXUCode::RunCompressor`).
     * This is a limiter with the same purpose, and the clipped-sample
     * counter stays so that a return to non-zero is visible. */
    {
        static int32_t gain = 1 << 16;          /* 16.16, 1.0 = unity */
        static int32_t held_l[AX_FRAME_SAMPLES], held_r[AX_FRAME_SAMPLES];
        static int32_t need_held = 1 << 16;
        static int     held_valid;
        int32_t peak = 0, need_cur, g0 = gain, g1;

        for (i = 0; i < AX_FRAME_SAMPLES; ++i) {
            int32_t a = acc_l[i] < 0 ? -acc_l[i] : acc_l[i];
            int32_t b = acc_r[i] < 0 ? -acc_r[i] : acc_r[i];
            if (a > peak) peak = a;
            if (b > peak) peak = b;
        }
        /* The gain that would just fit THIS frame under full scale. */
        need_cur = peak > 32767
                 ? (int32_t)(((int64_t)32767 << 16) / peak)
                 : (1 << 16);

        /* Where the ramp across the HELD frame has to end: low enough for
         * the held frame and for the one that follows it, and coming back
         * up only slowly. */
        g1 = (need_held < need_cur) ? need_held : need_cur;
        if (g1 > gain) g1 = gain + ((g1 - gain) >> 6);       /* release */
        if (g1 > need_held) g1 = need_held;                  /* never above */
        if (g1 > need_cur)  g1 = need_cur;

        for (i = 0; i < AX_FRAME_SAMPLES; ++i) {
            int32_t g = g0 + (int32_t)(((int64_t)(g1 - g0) * (int32_t)i)
                                       / (int32_t)AX_FRAME_SAMPLES);
            int32_t l = held_valid
                      ? (int32_t)(((int64_t)held_l[i] * g) >> 16) : 0;
            int32_t r = held_valid
                      ? (int32_t)(((int64_t)held_r[i] * g) >> 16) : 0;
            ++s_out_samples;
            if (l > 32767 || l < -32768) ++s_clipped;
            if (r > 32767 || r < -32768) ++s_clipped;
            if (l > 32767) l = 32767; if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; if (r < -32768) r = -32768;
            out[i * 2u] = (int16_t)l;
            out[i * 2u + 1u] = (int16_t)r;
        }
        gain = g1;

        for (i = 0; i < AX_FRAME_SAMPLES; ++i) {
            held_l[i] = acc_l[i]; held_r[i] = acc_r[i];
        }
        need_held = need_cur;
        held_valid = 1;
    }
    /* MGS_AUDIO_WAV=<path>: the mixed output, so it can be JUDGED.
     *
     * Every audio measurement in this file until now has been a count -
     * frames, voice-mixes, starves - and a count cannot say whether the
     * result sounds right. The faults actually reported (words breaking
     * into pieces, a soundtrack drifting slower) were audible long before
     * any counter noticed them, and one of them could not be seen headless
     * at all (F284).
     *
     * A file can be measured: silence runs, sample-to-sample discontinuities
     * and clipping are exactly the artefacts those faults produce, and
     * tools/check-audio.py reports them. Raw 16-bit stereo; the header is
     * written by the checker, so a truncated run still leaves a readable
     * file. */
    {
        static FILE* wav = NULL;
        static int tried = 0;
        if (!tried) {
            const char* path = getenv("MGS_AUDIO_WAV");
            tried = 1;
            if (path && *path) wav = fopen(path, "wb");
        }
        if (wav) fwrite(out, sizeof(int16_t) * 2u, AX_FRAME_SAMPLES, wav);
    }
    mgs_audio_push(out, AX_FRAME_SAMPLES);

    /* MGS_TRACE_AUDIOQ: THE DEVICE'S SIDE, which is the side that is heard.
     *
     * Every audio measurement before this one looked at what the mixer
     * PRODUCED - the sample stream, its gaps, its clipping - and all of it
     * was taken from headless runs, where `mgs_audio_open` is never called
     * at all. The exit report said so in a line I did not read:
     * "5616320 dropped (no device), 0 underruns". A mixer can produce a
     * perfect stream and still judder, because what judders is the device
     * running dry between pushes.
     *
     * So: how deep is the queue, is it draining, and how does the guest's
     * production rate compare with real time. A queue that trends down is
     * a guest falling behind, and "it gets worse and worse" is what that
     * sounds like. */
    {
        static int on = -1;
        static uint64_t n, first_ns;
        if (on < 0) on = getenv("MGS_TRACE_AUDIOQ") != NULL;
        if (on) {
            struct timespec ts;
            uint64_t now;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
            if (!first_ns) first_ns = now;
            ++n;
            if ((n % 200u) == 0u) {          /* once a second of sound */
                double real = (double)(now - first_ns) / 1e9;
                double made = (double)(n * AX_FRAME_SAMPLES) / AX_MIX_RATE;
                /* THE RATE NOW, not the average since the run began.
                 *
                 * The cumulative figure carries the startup lag forever -
                 * it read -10% while the last four seconds had produced
                 * exactly four seconds of sound - and that made a working
                 * pacer look like a broken one. */
                static uint64_t last_ns; static double last_made;
                double d_real = last_ns ? (double)(now - last_ns) / 1e9 : 0.0;
                double d_made = made - last_made;
                uint64_t pushed = 0, dropped = 0, under = 0;
                mgs_audio_stats(&pushed, &dropped, &under);
                fprintf(stderr, "[audioq] %6.1fs real  %6.1fs produced  "
                        "(now %+.2f%%, overall %+.2f%%)  "
                        "queue %5u samples (%.0f ms)  underruns %llu\n",
                        real, made,
                        d_real > 0.01 ? 100.0 * (d_made - d_real) / d_real
                                      : 0.0,
                        real > 0.1 ? 100.0 * (made - real) / real : 0.0,
                        mgs_audio_queued(),
                        1000.0 * mgs_audio_queued() / (double)AX_MIX_RATE,
                        (unsigned long long)under);
                last_ns = now; last_made = made;
            }
        }
    }
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
    printf("  resampler: %llu of %llu voice-mixes had to re-seed the "
           "history (a re-seed decodes ADPCM from a predictor that is not "
           "its own)\n",
           (unsigned long long)s_reseeds, (unsigned long long)s_mixed_voices);
    printf("  panning: %llu of %llu voice-mixes asked for different left "
           "and right levels\n",
           (unsigned long long)s_panned, (unsigned long long)s_mixed_voices);
    {
        unsigned pi;
        printf("   distinct (vL, vR):");
        for (pi = 0; pi < s_pan_n; ++pi)
            printf("  %04X/%04X x%llu",
                   (unsigned)(s_pan_key[pi] >> 16),
                   (unsigned)(s_pan_key[pi] & 0xFFFFu),
                   (unsigned long long)s_pan_hits[pi]);
        printf("%s\n", s_pan_n >= 8u ? "  (list full)" : "");
    }
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
    printf("  played on into an already-filled block: %llu of %llu overruns\n",
           (unsigned long long)s_played_on, (unsigned long long)s_starved);
    if (s_rewinds) {
        unsigned q;
        printf("  the game REWOUND a voice %llu times, by %.0f samples on "
               "average, worst %u\n    first few: ",
               (unsigned long long)s_rewinds,
               (double)s_rewind_total / (double)s_rewinds, s_rewind_max);
        for (q = 0; q < s_rewind_n; ++q) printf("%u ", s_rewind_samples[q]);
        printf("\n");
    } else {
        printf("  the game never rewound a voice\n");
    }
    printf("  runway: first %u samples, mean %.0f, min %u  (a frame consumes "
           "about 220)\n",
           s_head_first,
           s_head_n ? (double)s_head_sum / (double)s_head_n : 0.0,
           s_head_min == 0xFFFFFFFFu ? 0u : s_head_min);
    printf("  refill latency: %llu refills after a starve, mean %.1f AX "
           "frames, worst %llu\n",
           (unsigned long long)s_refill_n,
           s_refill_n ? (double)s_refill_frames / (double)s_refill_n : 0.0,
           (unsigned long long)s_refill_max);
    printf("  clipping: %llu of %llu output samples clipped (%.2f%%)\n",
           (unsigned long long)s_clipped, (unsigned long long)s_out_samples,
           s_out_samples ? 100.0 * (double)s_clipped / (double)s_out_samples
                         : 0.0);
}
