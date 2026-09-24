/* The audio device, and the reason this exists before the mixer is finished.
 *
 * Audio is not an output in this game, it is the CLOCK - `mpeg_movie_task`
 * takes the movie's playback position from the sound system and every record
 * timestamp is compared against it (HANDOFF F245, and the phase-2c note in
 * MILESTONES.md). A silent port therefore does not merely lack sound: it
 * mis-times everything downstream, and it withholds the one diagnostic that
 * reports pacing continuously rather than as a number at exit.
 *
 * Deliberately small. It takes finished 16-bit stereo frames and queues
 * them; deciding what those frames contain is the mixer's job, not this
 * file's. Everything here degrades to a no-op when SDL has no audio device,
 * because a headless batch run must not care.
 */
#ifndef MGS_PLATFORM_SDL_AUDIO_H
#define MGS_PLATFORM_SDL_AUDIO_H

#include <stdint.h>

/* Opens a stereo device at `rate` Hz. 0 on failure, which is not fatal. */
int  mgs_audio_open(unsigned rate);
void mgs_audio_close(void);

/* Queue `frames` interleaved stereo samples. Silently dropped if no device
 * was opened. */
void mgs_audio_push(const int16_t* stereo, unsigned frames);

/* Frames still waiting to be played. The mixer uses this to tell whether it
 * is feeding the device too slowly, which is the audible form of the pacing
 * bug this whole subsystem exists to expose. */
unsigned mgs_audio_queued(void);

/* Whether a device was actually opened. mgs_audio_queued answers 0 both for
 * "the card has caught up" and for "there is no card", and the mixer thread
 * cannot pace itself on a number that means both: with no device it read 0
 * for ever and mixed flat out, producing 8.9 BILLION samples in 95 seconds
 * and burning a core. A headless run needs a clock of its own. */
int mgs_audio_have_device(void);

/* Hold the guest to the device's clock. Called from the RUN LOOP, not from
 * the mixer: sleeping inside the DSP interrupt stops the guest mid-service
 * and lets the audio interrupt backlog through in a burst when it wakes. */
void mgs_audio_pace(void);

/* Totals for the exit report: pushed, and dropped for want of a device. */
void mgs_audio_stats(uint64_t* pushed, uint64_t* dropped, uint64_t* underruns);

#endif
