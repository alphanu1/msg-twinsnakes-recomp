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

    /* HOLD THE GUEST TO THE DEVICE'S CLOCK, ON A DEADLINE.
     *
     * The guest now runs a few per cent FASTER than real time, so the queue
     * grows without bound: measured at 1,500 to 1,800 ms of buffered sound,
     * which is the "audio further and further delayed" half of the
     * complaint. Picture and sound stay in step with each other - both come
     * off the guest clock - but both drift away from the wall.
     *
     * This is the place, and F284 says why: pacing was tried once at the
     * framebuffer copy and made things worse, because frames are copied 12
     * to 25 times a second and that samples a 5 ms queue every 40 to 80 ms.
     * Here it runs once per AX frame, 200 times a second.
     *
     * AN ABSOLUTE DEADLINE, not "sleep for the excess". Sleeping by a
     * computed duration 200 times a second accumulates the scheduler's
     * slack - every nanosleep overshoots a little and nothing ever gives
     * it back. Tried: production fell from +2% to -11.6% and the device
     * underran 720 times, which is worse than not pacing at all. A
     * deadline derived from the total frames produced cannot drift,
     * because each sleep is measured from the same origin rather than
     * from the last one.
     *
     * The origin is re-anchored when the guest falls far behind, so a slow
     * patch does not leave it sprinting to catch up afterwards.
     *
     * Never paces without a device, so a headless batch run stays
     * reproducible; MGS_NO_PACE turns it off for bisecting. */
    {
        static int checked, off, fixed_target;
        static uint64_t epoch_ns, total, target_ns, last_under, last_grow;
        struct timespec ts;
        uint64_t now, due;

        if (!checked) {
            const char* e = getenv("MGS_AUDIO_TARGET_MS");
            checked = 1;
            off = getenv("MGS_NO_PACE") != NULL;
            fixed_target = (e && *e);
            /* 60 ms of slack against a 5 ms production period: enough to
             * absorb a late frame, small enough not to be heard as lag.
             * It GROWS from here if this machine cannot hold it. */
            target_ns = (uint64_t)(fixed_target ? strtoul(e, NULL, 10) : 60ul)
                      * 1000000ull;
        }
        if (off || !s_rate) return;

        total += frames;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
        if (!epoch_ns) { epoch_ns = now; return; }

        due = epoch_ns + total * 1000000000ull / (uint64_t)s_rate;
        if (due > target_ns) due -= target_ns; else due = 0;

        if (now < due) {
            ts.tv_sec = (time_t)(due / 1000000000ull);
            ts.tv_nsec = (long)(due % 1000000000ull);
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
        } else if (now - due > 250000000ull) {
            /* A quarter of a second behind: re-anchor rather than chase. */
            epoch_ns += now - due;
        }

        /* THE TARGET FINDS ITS OWN LEVEL.
         *
         * A fixed 60 ms is right only if the guest keeps up every frame,
         * and it does not: it averages a few per cent faster than real time
         * and dips to -12% when a scene gets heavy. Measured with a real
         * device at a fixed target, the queue swung 303 -> 209 -> 51 -> 19
         * ms and the device underran on the way down.
         *
         * So the target grows when the device runs dry and shrinks slowly
         * when it does not. That buys latency only on a machine that needs
         * it, and gives it back when the pressure passes. The floor is the
         * 60 ms above; the ceiling is 400 ms, past which the lip-sync error
         * would be worse than the gap it is avoiding.
         *
         * MGS_AUDIO_TARGET_MS pins it, for measuring the guest's own speed
         * without this moving underneath. */
        if (!fixed_target) {
            if (s_underruns != last_under) {
                last_under = s_underruns;
                if (target_ns < 400000000ull) target_ns += 40000000ull;
                last_grow = now;
            } else if (now - last_grow > 5000000000ull
                       && target_ns > 60000000ull) {
                target_ns -= 10000000ull;      /* 10 ms every quiet 5 s */
                last_grow = now;
            }
        }
    }
}

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

#endif

void mgs_audio_stats(uint64_t* pushed, uint64_t* dropped, uint64_t* underruns)
{
    if (pushed) *pushed = s_pushed;
    if (dropped) *dropped = s_dropped;
    if (underruns) *underruns = s_underruns;
}
