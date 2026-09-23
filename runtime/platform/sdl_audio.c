#include "sdl_audio.h"

#include <stdio.h>
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
