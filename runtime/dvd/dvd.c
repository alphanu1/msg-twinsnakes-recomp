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
    return NULL;
}

MgsDvdRequest* mgs_dvd_read_async(MgsDvd* dvd, const char* path,
                                  uint32_t guest_dest, uint32_t offset,
                                  uint32_t length, uint32_t guest_callback,
                                  uint32_t guest_block)
{
    MgsDvdRequest* req = alloc_request(dvd);
    if (!req || !path) return NULL;

    memset(req, 0, sizeof *req);
    req->in_use = 1;
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

MgsDvdRequest* mgs_dvd_read_abs_async(MgsDvd* dvd, uint32_t disc_offset,
                                      uint32_t guest_dest, uint32_t length,
                                      uint32_t guest_callback,
                                      uint32_t guest_block)
{
    /* The path is unused for an absolute read, but passing "" rather than
     * NULL keeps the one allocation path: mgs_dvd_read_async refuses a null
     * path, and duplicating the request setup to avoid that is how the two
     * would drift apart. */
    MgsDvdRequest* req = mgs_dvd_read_async(dvd, "", guest_dest, disc_offset,
                                            length, guest_callback, guest_block);
    if (req) req->absolute = 1;
    return req;
}

unsigned mgs_dvd_drain(MgsDvd* dvd, MgsDvdRequest** completed, unsigned max)
{
    unsigned i, n = 0;

    for (i = 0; i < MGS_DVD_MAX_PENDING && n < max; ++i) {
        MgsDvdRequest* req = &dvd->pending[i];
        if (!req->in_use) continue;
        if (!__atomic_load_n(&req->done, __ATOMIC_ACQUIRE)) continue;

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
    while (mgs_dvd_drain(dvd, done, 1u) == 0u) { /* the job may have run inline */ }
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
