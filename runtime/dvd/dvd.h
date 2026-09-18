/* Asynchronous disc reads, with the SDK's own contract.
 *
 * The contract that shapes this: DVDReadAsync's callback runs on the GUEST
 * thread, not on whatever finished the read. The game's callbacks touch game
 * state, and game state is only safe to touch from the one guest thread that
 * is allowed to run at a time (os_thread.h). A callback fired from a worker
 * would break the atomicity the game was written against - silently, since
 * nothing crashes when two things touch the same structure a microsecond
 * apart.
 *
 * So a read is three steps in two places:
 *
 *   guest thread   submit  -> allocate a host buffer, queue the job
 *   worker         read    -> fill the HOST buffer; never touches guest memory
 *   guest thread   drain   -> copy into guest RAM, then run the callback
 *
 * The middle step is where the cores go. The other two are serialised with
 * everything else the guest does, which is the point.
 */
#ifndef MGS_DVD_H
#define MGS_DVD_H

#include "disc.h"
#include "../platform/jobs.h"
#include "../memory/guest.h"

#include <stdint.h>

/* DVDCommandBlock states, as the SDK defines them; the game polls for these. */
#define DVD_STATE_END           0
#define DVD_STATE_BUSY         (-1)
#define DVD_STATE_WAITING      (-2)
#define DVD_STATE_FATAL_ERROR  (-3)
#define DVD_STATE_CANCELED     (-10)

#define MGS_DVD_MAX_PENDING 64

typedef struct MgsDvdRequest {
    int       in_use;
    int       done;              /* set by the worker, read by the guest thread */
    long      result;            /* bytes read, or -1 */

    char      path[256];
    uint32_t  guest_dest;        /* where the guest wants it */
    uint32_t  offset;
    uint32_t  length;
    uint8_t*  host_buffer;       /* the worker's target: host memory only */

    uint32_t  guest_callback;    /* guest address, 0 for none */
    uint32_t  guest_block;       /* DVDCommandBlock*, for the status the game polls */

    struct MgsDvd* owner;
} MgsDvdRequest;

typedef struct MgsDvd {
    MgsDisc*      disc;
    MgsJobPool*   jobs;
    GuestMemory*  mem;
    MgsDvdRequest pending[MGS_DVD_MAX_PENDING];
} MgsDvd;

void mgs_dvd_init(MgsDvd* dvd, MgsDisc* disc, MgsJobPool* jobs, GuestMemory* mem);

/* Queue a read. Returns the request, or NULL if the table is full - the
 * caller then reports failure to the guest rather than blocking, because
 * blocking here would stall the only thread allowed to run.
 */
MgsDvdRequest* mgs_dvd_read_async(MgsDvd* dvd, const char* path,
                                  uint32_t guest_dest, uint32_t offset,
                                  uint32_t length, uint32_t guest_callback,
                                  uint32_t guest_block);

/* Called from the GUEST thread. Copies finished reads into guest memory and
 * returns how many completed, so the caller can run their callbacks. Nothing
 * else may write guest memory on behalf of a read.
 */
unsigned mgs_dvd_drain(MgsDvd* dvd, MgsDvdRequest** completed, unsigned max);

/* Synchronous read, for the paths where the SDK offers one. Still goes
 * through the same code so there is one implementation to be wrong.
 */
long mgs_dvd_read_sync(MgsDvd* dvd, const char* path, uint32_t guest_dest,
                       uint32_t offset, uint32_t length);

/* Free a drained request. The caller runs the callback first, then
 * releases - so a callback that inspects the request still can. */
void mgs_dvd_release(MgsDvdRequest* req);

unsigned mgs_dvd_pending_count(const MgsDvd* dvd);

#endif
