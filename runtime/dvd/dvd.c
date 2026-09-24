#include "dvd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mgs_dvd_init(MgsDvd* dvd, MgsDisc* disc, MgsJobPool* jobs, GuestMemory* mem)
{
    memset(dvd, 0, sizeof *dvd);
    dvd->disc = disc;
    dvd->jobs = jobs;
    dvd->mem = mem;
}

/* Runs on a WORKER. Touches the host buffer and the disc, and nothing else.
 * In particular it never writes guest memory and never runs a guest callback.
 */
static void read_job(void* user)
{
    MgsDvdRequest* req = (MgsDvdRequest*)user;
    req->result = req->absolute
        ? mgs_disc_read_abs(req->owner->disc, req->host_buffer,
                            req->offset, req->length)
        : mgs_disc_read(req->owner->disc, req->path,
                        req->host_buffer, req->offset, req->length);

    /* MGS_TRACE_DVD=1 names every read. A read that silently returns -1 is
     * indistinguishable from one the game never issued, and both look like
     * "the game is not loading anything". */
    if (getenv("MGS_TRACE_DVD"))
        fprintf(stderr, "[dvd] %s %s off=0x%08X len=%u -> %ld  dest=0x%08X\n",
                req->absolute ? "abs" : "path", req->absolute ? "" : req->path,
                req->offset, req->length, req->result, req->guest_dest);
    /* Published last, so a drain that sees done==1 also sees result. */
    __atomic_store_n(&req->done, 1, __ATOMIC_RELEASE);
}

static MgsDvdRequest* alloc_request(MgsDvd* dvd)
{
    unsigned i;
    for (i = 0; i < MGS_DVD_MAX_PENDING; ++i)
        if (!dvd->pending[i].in_use) return &dvd->pending[i];

    /* NO SLOT LEFT, AND THAT IS SILENT TO THE GAME.
     *
     * A request occupies its slot until the completion callback has run, and
     * callbacks only run when the guest has interrupts enabled. If they stop
     * being delivered the slots never come back, every later read is refused,
     * and ALL loading stops at once - which is exactly what a freeze that
     * takes the whole game with it looks like. Counted, because a refusal
     * here is indistinguishable in a log from a read the game never issued.
     */
    ++dvd->refused_full;
    fprintf(stderr, "[dvd] NO FREE REQUEST SLOT: all %u are still pending, "
                    "so this read is refused\n", (unsigned)MGS_DVD_MAX_PENDING);
    return NULL;
}

unsigned mgs_dvd_in_flight(const MgsDvd* dvd)
{
    unsigned i, n = 0;
    for (i = 0; i < MGS_DVD_MAX_PENDING; ++i) if (dvd->pending[i].in_use) ++n;
    return n;
}

uint64_t mgs_dvd_refused_full(const MgsDvd* dvd) { return dvd->refused_full; }

/* EVERY FIELD IS SET BEFORE THE JOB IS SUBMITTED, AND THAT IS THE WHOLE
 * CONTRACT OF THIS FUNCTION.
 *
 * `mgs_jobs_submit` hands the request to a worker that may read it on the
 * very next instruction, so a field written after the submit is a data race
 * with the worker reading it. `mgs_dvd_read_abs_async` used to do exactly
 * that - call this, then set `req->absolute = 1` - and lost the race about
 * half the time.
 *
 * What losing it did: the worker saw `absolute == 0` and read by PATH, with
 * the empty path that entry point passes, so the read failed, the guest was
 * handed a failed DVD read it never sees on console, and the run diverged
 * from there. It presented as a crash at `pc = 0x00000800` a few million
 * steps later - an FP-unavailable exception taken while `OSCurrentContext`
 * was zero - which looks nothing like a disc read and cost a long hunt
 * (F264). ThreadSanitizer names it in one line.
 *
 * So `absolute` is a parameter now rather than something a caller patches
 * in afterwards, and there is no window in which a worker can see a
 * half-built request. */
static MgsDvdRequest* dvd_build_and_submit(MgsDvd* dvd, const char* path,
                                           int absolute,
                                           uint32_t guest_dest, uint32_t offset,
                                           uint32_t length,
                                           uint32_t guest_callback,
                                           uint32_t guest_block)
{
    MgsDvdRequest* req = alloc_request(dvd);
    if (!req || !path) return NULL;

    memset(req, 0, sizeof *req);
    req->in_use = 1;
    req->absolute = absolute;
    req->owner = dvd;
    snprintf(req->path, sizeof req->path, "%s", path);
    req->guest_dest = guest_dest;
    req->offset = offset;
    req->length = length;
    req->guest_callback = guest_callback;
    req->guest_block = guest_block;
    req->result = -1;

    req->host_buffer = (uint8_t*)malloc(length ? length : 1u);
    if (!req->host_buffer) { req->in_use = 0; return NULL; }

    /* WHEN THE GUEST WILL SEE IT.
     *
     * THIS IS A MODELLED DELAY AND IT IS NOW OFF BY DEFAULT. It held every
     * read for a millisecond of guest time plus a per-byte cost, spinning
     * on a deadline with the bytes already in host memory - a drive being
     * emulated on a machine that has no drive in the path at all. This is a
     * static recompilation, not an emulator: the only thing that should
     * pace the game is the frame rate.
     *
     * What it bought was determinism: the same run produced the same
     * result, which host thread scheduling cannot promise. That still
     * matters for the Dolphin comparison harness, which needs two runs of
     * ours to be byte-identical before a difference against the emulator
     * means anything (F320's control). So it is kept, off, behind
     * MGS_DVD_LATENCY=1 for that harness rather than deleted. */
    {
        /* ON BY DEFAULT AGAIN. Removing it is right - a modelled drive on a
         * machine with no drive in the path is exactly the emulation that
         * does not belong in a static recompilation - but it landed in the
         * same build as the wall clock, and that build broke audio and
         * video together. Two changes, one symptom, so both go back until
         * they can be tried one at a time. MGS_DVD_LATENCY=0 removes it. */
        static int modelled = -1;
        if (modelled < 0) {
            const char* e = getenv("MGS_DVD_LATENCY");
            modelled = !(e && *e == '0');
        }
        req->ready_tick = modelled
            ? dvd->now + MGS_DVD_LATENCY_TICKS
                       + (uint64_t)length * MGS_DVD_TICKS_PER_BYTE
            : dvd->now;
    }

    /* The game polls this while it waits. Set before queuing, so it can never
     * observe a request that is neither BUSY nor finished.
     */
    if (guest_block)
        guest_write32(dvd->mem, guest_block + DVD_CB_STATE, (uint32_t)DVD_STATE_BUSY);

    if (!mgs_jobs_submit(dvd->jobs, read_job, req)) {
        /* Pool refused - shutting down, or full. Run it here rather than
         * failing: correctness does not depend on the read being elsewhere,
         * only performance does.
         */
        read_job(req);
    }
    return req;
}

MgsDvdRequest* mgs_dvd_read_async(MgsDvd* dvd, const char* path,
                                  uint32_t guest_dest, uint32_t offset,
                                  uint32_t length, uint32_t guest_callback,
                                  uint32_t guest_block)
{
    return dvd_build_and_submit(dvd, path, 0, guest_dest, offset, length,
                                guest_callback, guest_block);
}

MgsDvdRequest* mgs_dvd_read_abs_async(MgsDvd* dvd, uint32_t disc_offset,
                                      uint32_t guest_dest, uint32_t length,
                                      uint32_t guest_callback,
                                      uint32_t guest_block)
{
    /* MGS_TRACE_DEST=<addr>: which read lands on a given guest address.
     *
     * The disc log says what is being read and how much; it does not say
     * WHERE it goes, and "where" is the question when a region of guest
     * memory is found holding something the console never put there. The
     * comparison against Dolphin found 8.7 MB at 0x8079C000 that the port
     * writes and the emulator does not (F320), and a destination is the
     * only thing that names the read responsible. */
    {
        static long watch = -1;
        if (watch == -1) {
            const char* e = getenv("MGS_TRACE_DEST");
            watch = e && *e ? (long)strtoul(e, NULL, 0) : 0;
        }
        if (watch && guest_dest <= (uint32_t)watch &&
            (uint32_t)watch < guest_dest + length)
            fprintf(stderr, "[dest] read of %u bytes from disc 0x%X lands on "
                            "0x%08X..0x%08X, covering 0x%08X\n",
                    length, disc_offset, guest_dest, guest_dest + length,
                    (uint32_t)watch);
    }
    /* The path is unused for an absolute read, but passing "" rather than
     * NULL keeps the one allocation path: the builder refuses a null path,
     * and duplicating the request setup to avoid that is how the two would
     * drift apart. */
    return dvd_build_and_submit(dvd, "", 1, guest_dest, disc_offset, length,
                                guest_callback, guest_block);
}

unsigned mgs_dvd_drain(MgsDvd* dvd, MgsDvdRequest** completed, unsigned max,
                       uint64_t now)
{
    unsigned i, n = 0;

    for (i = 0; i < MGS_DVD_MAX_PENDING && n < max; ++i) {
        MgsDvdRequest* req = &dvd->pending[i];
        if (!req->in_use) continue;

        /* THE GUEST'S CLOCK DECIDES, not the worker's. */
        if (now < req->ready_tick) continue;

        /* Its time has come, so wait for the bytes if they are somehow not
         * here yet. Host I/O is orders of magnitude faster than the modelled
         * latency, so this should never spin - but "should never" is not
         * "cannot", and completing early would put the nondeterminism back. */
        while (!__atomic_load_n(&req->done, __ATOMIC_ACQUIRE))
            ;

        /* The copy into guest memory happens HERE, on the guest thread, for
         * the same reason the callback does: guest memory has exactly one
         * writer.
         */
        if (req->result > 0 && req->guest_dest) {
            uint8_t* dst = guest_ptr(dvd->mem, req->guest_dest, (uint32_t)req->result);
            if (dst) memcpy(dst, req->host_buffer, (size_t)req->result);
        }

        if (req->guest_block) {
            guest_write32(dvd->mem, req->guest_block + DVD_CB_STATE,
                          req->result >= 0 ? (uint32_t)DVD_STATE_END
                                           : (uint32_t)DVD_STATE_FATAL_ERROR);
            /* The game reads this back to learn how much actually arrived,
             * which for a short read is less than it asked for. */
            guest_write32(dvd->mem, req->guest_block + DVD_CB_XFERRED,
                          req->result > 0 ? (uint32_t)req->result : 0u);
        }

        completed[n++] = req;
    }
    return n;
}

void mgs_dvd_release(MgsDvdRequest* req);
void mgs_dvd_release(MgsDvdRequest* req)
{
    if (!req || !req->in_use) return;
    free(req->host_buffer);
    req->host_buffer = NULL;
    req->in_use = 0;
}

long mgs_dvd_read_sync(MgsDvd* dvd, const char* path, uint32_t guest_dest,
                       uint32_t offset, uint32_t length)
{
    MgsDvdRequest* req = mgs_dvd_read_async(dvd, path, guest_dest, offset,
                                            length, 0u, 0u);
    MgsDvdRequest* done[1];
    long result;

    if (!req) return -1;
    mgs_jobs_wait(dvd->jobs);
    /* A SYNCHRONOUS read is finished when the caller asks, by definition -
     * the guest is blocked on it and no amount of guest time will pass while
     * it waits. So the modelled latency does not apply here; passing the
     * request's own ready tick says "it is time" without weakening the rule
     * for the asynchronous path, which is where the nondeterminism was. */
    while (mgs_dvd_drain(dvd, done, 1u, req->ready_tick) == 0u) { }
    /* NOTE the clock is untouched above: `drain` compares against the time
     * it is given and does not adopt it. Adopting a synthetic future time
     * here dragged the clock forward for every read submitted afterwards,
     * which made them ready early and put back some of the nondeterminism
     * this was meant to remove. */
    result = done[0]->result;
    mgs_dvd_release(done[0]);
    return result;
}

unsigned mgs_dvd_pending_count(const MgsDvd* dvd)
{
    unsigned i, n = 0;
    for (i = 0; i < MGS_DVD_MAX_PENDING; ++i)
        if (dvd->pending[i].in_use) ++n;
    return n;
}

void mgs_dvd_set_clock(MgsDvd* dvd, uint64_t now) { dvd->now = now; }
