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
#define PB_SRC_RATIO_HI    0xA6u
#define PB_SRC_FRAC        0xAAu

/* AX runs a 5 ms frame and outputs 32 kHz, so a frame consumes 160 output
 * samples' worth of each voice, scaled by that voice's own SRC ratio. */
#define AX_SAMPLES_PER_FRAME 160u

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

void mgs_ax_dsp_frame(void* cpu);
void mgs_ax_dsp_frame(void* cpu)
{
    uint32_t pb = base();
    unsigned i;

    ++s_frames;
    for (i = 0; i < AXPB_COUNT; ++i, pb += AXPB_STRIDE) {
        uint32_t curr, end, ratio, frac, step;

        if (rd16(cpu, pb + PB_STATE) != 1u) continue;      /* not running */

        /* The SRC ratio is 16.16 relative to the 32 kHz output, so the
         * samples this voice consumes in a frame is the output frame length
         * scaled by it. The fractional remainder lives in the parameter
         * block's own `currentAddressFrac`, which is where the DSP keeps
         * it - so a voice at a non-integer ratio does not drift. */
        ratio = rd32pair(cpu, pb + PB_SRC_RATIO_HI);
        if (!ratio) continue;
        frac = rd16(cpu, pb + PB_SRC_FRAC);

        {
            uint64_t total = (uint64_t)AX_SAMPLES_PER_FRAME * ratio + frac;
            step = (uint32_t)(total >> 16);
            wr16(cpu, pb + PB_SRC_FRAC, (uint32_t)(total & 0xFFFFu));
        }
        if (!step) continue;

        curr = rd32pair(cpu, pb + PB_ADDR_CURR_HI) + step;
        end  = rd32pair(cpu, pb + PB_ADDR_END_HI);
        ++s_advanced;

        if (end && curr > end) {
            if (rd16(cpu, pb + PB_ADDR_LOOPFLAG)) {
                /* Carry the overshoot across the loop point rather than
                 * dropping it, so a looping voice keeps time. */
                uint32_t loop = rd32pair(cpu, pb + PB_ADDR_LOOP_HI);
                uint32_t over = curr - end - 1u;
                curr = loop + over;
                ++s_looped;
            } else {
                /* A one-shot voice that reaches its end stops, and
                 * `__AXServiceVPB` copies that state back to the game. */
                curr = end;
                wr16(cpu, pb + PB_STATE, 0u);
                ++s_ended;
            }
        }
        wr32pair(cpu, pb + PB_ADDR_CURR_HI, curr);
    }
}

void mgs_ax_dsp_report(void);
void mgs_ax_dsp_report(void)
{
    if (!s_frames) return;
    printf("AX voice model: %llu frames, %llu voice-advances, "
           "%llu loops, %llu voices ended\n",
           (unsigned long long)s_frames, (unsigned long long)s_advanced,
           (unsigned long long)s_looped, (unsigned long long)s_ended);
}
