#include "sdl_audio.h"

#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static uint64_t s_pushed, s_dropped, s_underruns;
static unsigned s_rate;

#if defined(MGS_HAVE_SDL3)
#include <SDL3/SDL.h>

static SDL_AudioStream* s_stream;

int mgs_audio_open(unsigned rate)
{
    SDL_AudioSpec spec;

    if (s_stream) return 1;
    if (!rate) return 0;
    /* MGS_NO_AUDIO exists for batch runs and for bisecting a divergence:
     * the mixer still runs and still advances voice positions, so the guest
     * sees the same timing with and without a device. */
    if (getenv("MGS_NO_AUDIO")) return 0;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "[audio] no audio subsystem: %s\n", SDL_GetError());
        return 0;
    }
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = (int)rate;

    s_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                         &spec, NULL, NULL);
    if (!s_stream) {
        fprintf(stderr, "[audio] no device: %s\n", SDL_GetError());
        return 0;
    }
    SDL_ResumeAudioStreamDevice(s_stream);
    s_rate = rate;
    fprintf(stderr, "[audio] %u Hz stereo, 16-bit\n", rate);
    return 1;
}

void mgs_audio_close(void)
{
    if (!s_stream) return;
    SDL_DestroyAudioStream(s_stream);
    s_stream = NULL;
}

void mgs_audio_push(const int16_t* stereo, unsigned frames)
{
    if (!stereo || !frames) return;
    if (!s_stream) { s_dropped += frames; return; }
    /* A device that has run dry has been starved by the guest, which is the
     * symptom worth counting: it means the mixer is not being called often
     * enough, and on this game that is the same fault as a movie playing one
     * frame per chunk. */
    if (SDL_GetAudioStreamAvailable(s_stream) == 0) ++s_underruns;
    SDL_PutAudioStreamData(s_stream, stereo,
                           (int)(frames * 2u * sizeof(int16_t)));
    s_pushed += frames;

    /* NO SLEEPING HERE. See mgs_audio_pace.
     *
     * The pacing used to happen in this function, which is called from the
     * AX mixer, which runs inside the DSP interrupt, on the guest thread.
     * Sleeping there stops the guest in the middle of servicing an
     * interrupt: the audio interrupts keep being offered while it sleeps,
     * and when it wakes the arrears logic in host/interrupt.c lets the
     * backlog through in a burst - several AX frames mixed back to back
     * from voice state the game has not had a chance to advance between
     * them. Ben reported an echo on the GPU build, which is what that
     * sounds like, and the mixer's own output shows no echo at all
     * (autocorrelation peaks at 0.17, where a real one would be past 0.5) -
     * so whatever it is, it is on this side of the mixer and not in it.
     *
     * The pacing now happens in the run loop, between instructions, where
     * stopping the guest is something the guest is already prepared for. */
}

/* Hold the guest to the device's clock, called from the RUN LOOP.
 *
 * An absolute deadline rather than "sleep for the excess": sleeping by a
 * computed duration two hundred times a second accumulates the scheduler's
 * slack, and measured that way production fell from +2% to -11.6% with 720
 * underruns, which is worse than not pacing at all. A deadline taken from
 * the total frames produced cannot drift, because every sleep is measured
 * from the same origin rather than from the last one.
 *
 * The target finds its own level: a fixed 60 ms is right only if the guest
 * keeps up every frame, and it does not. It grows on an underrun and gives
 * the latency back when the pressure passes. */
void mgs_audio_pace(void)
{
    static int checked, off, fixed_target;
    static uint64_t epoch_ns, target_ns, last_under, last_grow, last_pushed;
    struct timespec ts;
    uint64_t now, due, total;

    if (!s_stream || !s_rate) return;
    if (!checked) {
        const char* e = getenv("MGS_AUDIO_TARGET_MS");
        checked = 1;
        off = getenv("MGS_NO_PACE") != NULL;
        fixed_target = (e && *e) ? 1 : 0;
        target_ns = (uint64_t)(fixed_target ? strtoul(e, NULL, 10) : 60ul)
                  * 1000000ull;
    }
    if (off) return;

    total = s_pushed;
    if (total == last_pushed) return;      /* nothing new to pace against */
    last_pushed = total;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (!epoch_ns) { epoch_ns = now; return; }

    due = epoch_ns + total * 1000000000ull / (uint64_t)s_rate;
    due = (due > target_ns) ? due - target_ns : 0;

    if (now < due) {
        ts.tv_sec = (time_t)(due / 1000000000ull);
        ts.tv_nsec = (long)(due % 1000000000ull);
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
    } else if (now - due > 250000000ull) {
        epoch_ns += now - due;             /* far behind: re-anchor */
    }

    if (!fixed_target) {
        if (s_underruns != last_under) {
            last_under = s_underruns;
            if (target_ns < 400000000ull) target_ns += 40000000ull;
            last_grow = now;
        } else if (now - last_grow > 5000000000ull &&
                   target_ns > 60000000ull) {
            target_ns -= 10000000ull;
            last_grow = now;
        }
    }
}

int mgs_audio_have_device(void) { return s_stream != NULL; }

unsigned mgs_audio_queued(void)
{
    int bytes;
    if (!s_stream) return 0u;
    bytes = SDL_GetAudioStreamAvailable(s_stream);
    if (bytes < 0) return 0u;
    return (unsigned)bytes / (2u * (unsigned)sizeof(int16_t));
}

#else  /* built without SDL3 */

int  mgs_audio_open(unsigned rate) { (void)rate; return 0; }
void mgs_audio_close(void) { }
void mgs_audio_push(const int16_t* s, unsigned n) { (void)s; s_dropped += n; }
unsigned mgs_audio_queued(void) { return 0u; }
int  mgs_audio_have_device(void) { return 0; }
void mgs_audio_pace(void) { }

#endif

void mgs_audio_stats(uint64_t* pushed, uint64_t* dropped, uint64_t* underruns)
{
    if (pushed) *pushed = s_pushed;
    if (dropped) *dropped = s_dropped;
    if (underruns) *underruns = s_underruns;
}
