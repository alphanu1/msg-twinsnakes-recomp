/* Host worker pool: the runtime's own parallelism.
 *
 * WHAT MAY RUN HERE, AND WHAT MAY NOT.
 *
 * Guest threads may NOT. GameCube OS threads are cooperative on a single
 * core, so the game assumes every sequence between two yield points is
 * atomic. Running two guest threads in parallel breaks that assumption
 * silently - as occasional corruption, not as a crash - and no amount of
 * locking in the runtime can restore a guarantee the game has already been
 * written against. See os_thread.h.
 *
 * The runtime's OWN work may, and there is a lot of it. The design document
 * names the places: the audio callback, file prefetch, and GPU submission.
 * Add to those the two that dominate phase 3 - texture decoding and shader
 * compilation - both of which are pure functions over independent inputs and
 * scale with cores.
 *
 * THE RULE THAT MAKES THIS SAFE: a job never touches guest memory. It works
 * on host buffers, and its result reaches the guest through a completion
 * queue drained by the guest thread at a point the guest chose. That is the
 * same discipline the DVD shim needs anyway - the SDK's own contract is that
 * a DVDReadAsync callback runs on the guest thread, not on whatever finished
 * the read.
 */
#ifndef MGS_JOBS_H
#define MGS_JOBS_H

#include <stddef.h>

typedef struct MgsJobPool MgsJobPool;

typedef void (*MgsJobFn)(void* user);

/* workers == 0 asks the pool to size itself from the host's core count,
 * leaving one core for the guest thread and the OS: the guest thread is the
 * critical path, and starving it to run background work is a net loss.
 */
MgsJobPool* mgs_jobs_create(unsigned workers);
void        mgs_jobs_destroy(MgsJobPool* pool);

/* Queue work. Returns 0 if the pool is shutting down or out of room, so a
 * caller can always fall back to running the job inline - which is what the
 * single-core path does, and what keeps the code identical either way.
 */
int  mgs_jobs_submit(MgsJobPool* pool, MgsJobFn fn, void* user);

/* Block until every queued job has finished. Used at frame boundaries, where
 * the guest is about to need the results.
 */
void mgs_jobs_wait(MgsJobPool* pool);

unsigned mgs_jobs_worker_count(const MgsJobPool* pool);
unsigned mgs_jobs_hardware_threads(void);

#endif
