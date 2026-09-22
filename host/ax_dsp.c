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
#define PB_SRC_RATIO_HI    0xA6u
#define PB_SRC_FRAC        0xAAu

/* Sample formats, from dolsdk2004's AXSetVoiceAddr. The address units differ
 * per format and that is the whole of the arithmetic below: PCM16 counts
 * samples, PCM8 counts bytes, ADPCM counts NIBBLES - which is why the SDK
 * asserts that no address lands on an ADPCM frame header. */
#define AX_FMT_ADPCM       0u
#define AX_FMT_PCM16       10u
#define AX_FMT_PCM8        25u

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
    return 0;              /* ADPCM: not decoded yet, see the note below */
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
static uint64_t s_starved, s_nonzero_frames;
static int      s_peak;
static int      s_trace = -1;
static unsigned s_traced;

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
        vl  = rd16(cpu, pb + PB_MIX_VL);
        vr  = rd16(cpu, pb + PB_MIX_VR);

        /* ADPCM is not decoded yet. Counted rather than silently mixed as
         * zero, because "no sound" and "sound we cannot decode" are
         * different faults and a single silence cannot tell them apart. */
        if (format == AX_FMT_ADPCM) { ++s_adpcm_skipped; continue; }

        ++s_mixed_voices;
        ++s_advanced;

        /* MGS_TRACE_AXMIX: the first few voices as the mixer sees them.
         * "Silent output" has three very different causes - no samples, no
         * volume, or a voice pinned at its end address - and the numbers
         * that separate them are these five. */
        if (s_trace && s_traced < 12u) {
            ++s_traced;
            fprintf(stderr, "[axmix] voice %02u fmt %2u curr %08X end %08X "
                            "loop %08X %s vol %04X vl %04X vr %04X ratio %08X\n",
                    i, format, curr, end, loop,
                    looping ? "loop" : "once", vol, vl, vr, ratio);
        }

        for (k = 0; k < AX_FRAME_SAMPLES; ++k) {
            int ok, sv = sample_at(format, curr, &ok);
            if (!ok) { ++s_silent_reads; sv = 0; }
            acc_l[k] += (int32_t)((long long)sv * (int)vol / 32768
                                  * (int)vl / 32768);
            acc_r[k] += (int32_t)((long long)sv * (int)vol / 32768
                                  * (int)vr / 32768);

            /* ADVANCE BY WHAT WAS CONSUMED, which is the whole point: the
             * position the game reads back is now the position the mixer
             * actually played from, not a number this file invented. */
            frac += ratio;
            curr += frac >> 16;
            frac &= 0xFFFFu;

            if (end && curr > end) {
                /* A LOOP POINT PAST THE END IS NOT A LOOP, IT IS A VOICE
                 * WAITING FOR DATA.
                 *
                 * A streaming voice has its loop and end addresses rewritten
                 * by the game as each buffer is refilled, and between those
                 * updates it can hold `loop > end` - our movie voice sits at
                 * loop 0x3000, end 0x2FFF before the first refill. Treating
                 * that as a loop wraps forward, lands past the end again and
                 * wraps every single sample: 286,628 wraps in 62,763 frames,
                 * 4.5 per frame, which is a runaway rather than playback.
                 *
                 * Holding the position instead models what the hardware does
                 * while it has nothing to play, and leaves the voice running
                 * so the refill still finds it. */
                if (looping && loop <= end) {
                    curr = loop + (curr - end - 1u);
                    ++s_looped;
                } else if (looping) {
                    curr = end;
                    ++s_starved;
                    break;
                } else {
                    curr = end;
                    wr16(cpu, pb + PB_STATE, 0u);
                    ++s_ended;
                    break;
                }
            }
        }
        any = 1;
        wr16(cpu, pb + PB_SRC_FRAC, frac);
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

    for (i = 0; i < AX_FRAME_SAMPLES; ++i) {
        int32_t l = acc_l[i], r = acc_r[i];
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
    printf("  ADPCM voices skipped (not decoded yet): %llu;"
           " samples read outside ARAM: %llu; voices starved: %llu\n",
           (unsigned long long)s_adpcm_skipped,
           (unsigned long long)s_silent_reads,
           (unsigned long long)s_starved);
    printf("  output: peak %d of 32767 (%.1f%% of full scale), "
           "%llu of %llu frames not silent\n",
           s_peak, 100.0 * s_peak / 32767.0,
           (unsigned long long)s_nonzero_frames,
           (unsigned long long)s_frames);
    printf("  device: %llu frames queued, %llu dropped (no device), "
           "%llu underruns\n",
           (unsigned long long)pushed, (unsigned long long)dropped,
           (unsigned long long)under);
}
