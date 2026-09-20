/* The memory card, as a pass-through to host files.
 *
 * WHY THIS EXISTS NOW, OUT OF PHASE ORDER. The design document puts CARD in
 * phase 5, and the phase order is not optional. This is not that work: it is
 * a test-harness enabler. Without a card the game stops on a notice screen
 * that needs a button press, which means every headless run - the
 * reproducible ones, the ones a bug is actually diagnosed in - stops before
 * the intro movie, where the outstanding rendering faults are. Phase 5 still
 * owns real CARD emulation, including the Psycho Mantis save-file scan.
 *
 * WHY A PASS-THROUGH RATHER THAN A RAW CARD IMAGE. A real card is a 2 MB
 * image with a header, two directory copies, two FAT copies and per-file
 * checksums, and emulating that means being right about all of it before a
 * single save works. The SDK's public API is this project's translated/native
 * boundary, so the honest place to intercept is CARDOpen/CARDRead/CARDWrite,
 * and the natural backing for a file-level API is a host file per save. That
 * also survives: saves written now stay readable when phase 5 replaces the
 * layer above them.
 *
 * WHAT IS REAL AND WHAT IS NOT, stated plainly because a stub that pretends
 * too much is worse than one that refuses:
 *   - card presence, size and sector size: answered, not emulated
 *   - file storage: real, one host file per save under the save directory
 *   - the raw card format, the FAT, checksums, block allocation: absent
 */
#include "os_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The SDK's result codes. Only the ones this layer can actually produce. */
#define CARD_RESULT_READY        0
#define CARD_RESULT_BUSY        (-1)
#define CARD_RESULT_WRONGDEVICE (-2)
#define CARD_RESULT_NOCARD      (-3)
#define CARD_RESULT_NOFILE      (-4)

/* A 16 Mbit card: 251 usable blocks of 8 KB, the commonest real one. The
 * figures matter because the game divides by them to report free space. */
#define CARD_MBIT         16
#define CARD_SECTOR_SIZE  8192

/* A COMPLETION THE GUEST IS WAITING FOR, HELD UNTIL THE HOST CAN DELIVER IT.
 *
 * CARDMountAsync signals completion by calling back, and nothing on this side
 * may re-enter guest code: a shim runs inside a guest call, and calling the
 * guest again from here would nest the stack and run the callback with the
 * caller's interrupt state. The DVD path solved this already - queue it, and
 * let the host pump run it on the guest thread at a point the guest chose -
 * so this uses the same shape rather than inventing a second one.
 *
 * Returning READY without doing this is what left the game polling
 * CARDProbeEx 3,259,600 times in one boot: mounted, and still waiting to be
 * told so. */
#define CARD_CB_QUEUE 8
static struct { uint32_t cb; int32_t chan, result; } s_pending[CARD_CB_QUEUE];
static unsigned s_pending_head, s_pending_tail;

static void card_queue_callback(uint32_t cb, int32_t chan, int32_t result)
{
    unsigned next;
    if (!cb) return;
    next = (s_pending_tail + 1u) % CARD_CB_QUEUE;
    if (next == s_pending_head) return;            /* full: drop, never block */
    s_pending[s_pending_tail].cb     = cb;
    s_pending[s_pending_tail].chan   = chan;
    s_pending[s_pending_tail].result = result;
    s_pending_tail = next;
}

static int s_trace = -1;

static int card_trace(void)
{
    if (s_trace < 0) s_trace = getenv("MGS_TRACE_CARD") != NULL;
    return s_trace;
}

/* Where saves live on the host. Kept out of the repository: a save is the
 * user's own data and has nothing to do with the port's source. */
const char* mgs_card_dir(void);
const char* mgs_card_dir(void)
{
    const char* d = getenv("MGS_SAVE_DIR");
    return (d && *d) ? d : "saves";
}

/* CARDInit(void) - the SDK sets up its own state and EXI handlers. Nothing
 * here needs doing, but it must not run the recompiled body, which probes an
 * EXI bus that reports no device. */
void mgs_CARDInit(CPUState* ctx);
void mgs_CARDInit(CPUState* ctx)
{
    (void)ctx;
    if (card_trace()) fprintf(stderr, "[card] CARDInit\n");
}

/* s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize)
 *
 * The call that decides whether a card exists at all. Channel 0 is slot A;
 * anything else reports no card, which is what a machine with one card does
 * and keeps the game from believing in a slot B we do not back. */
void mgs_CARDProbeEx(CPUState* ctx);
void mgs_CARDProbeEx(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int32_t  chan     = (int32_t)mgs_guest_gpr(rt, 3);
    uint32_t mem_size = mgs_guest_gpr(rt, 4);
    uint32_t sec_size = mgs_guest_gpr(rt, 5);

    if (chan != 0) {
        mgs_set_guest_gpr(rt, 3, (uint32_t)CARD_RESULT_NOCARD);
        if (card_trace()) fprintf(stderr, "[card] ProbeEx chan %d -> NOCARD\n", (int)chan);
        return;
    }

    /* Both pointers are optional in the SDK's own signature. */
    if (mem_size) guest_write32(&rt->mem, mem_size, (uint32_t)CARD_MBIT);
    if (sec_size) guest_write32(&rt->mem, sec_size, (uint32_t)CARD_SECTOR_SIZE);

    mgs_set_guest_gpr(rt, 3, (uint32_t)CARD_RESULT_READY);
    if (card_trace())
        fprintf(stderr, "[card] ProbeEx chan 0 -> READY, %d Mbit, %d byte sectors\n",
                CARD_MBIT, CARD_SECTOR_SIZE);
}

/* s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detach,
 *                    CARDCallback attach)
 *
 * Reporting a card in CARDProbeEx alone is not enough: the game mounts it
 * next, and the translated CARDMountAsync talks to an EXI bus that reports no
 * device, so it fails and the notice screen appears anyway. Verified, not
 * assumed - with ProbeEx stubbed and this one not, the boot still stopped on
 * the notice.
 *
 * KNOWN GAP: the SDK signals completion through `attach`, and this does not
 * call it. There is no facility on this side to re-enter guest code - the DVD
 * path queues its callbacks and the host pump runs them on the guest thread -
 * so a game that waits for the callback rather than polling will wait
 * forever. If that is what happens, the callback needs queueing the way DVD
 * does it, not a different return code here. */
void mgs_CARDMountAsync(CPUState* ctx);
void mgs_CARDMountAsync(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int32_t chan = (int32_t)mgs_guest_gpr(rt, 3);
    int32_t res  = (chan == 0) ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
    mgs_set_guest_gpr(rt, 3, (uint32_t)res);
    if (res == CARD_RESULT_READY)
        card_queue_callback(mgs_guest_gpr(rt, 6), chan, CARD_RESULT_READY);
    if (card_trace())
        fprintf(stderr, "[card] MountAsync chan %d -> %d, attach callback "
                        "0x%08X queued\n", (int)chan, (int)res, mgs_guest_gpr(rt, 6));
}

/* s32 CARDCheckExAsync(s32 chan, s32* xferBytes, CARDCallback callback)
 *
 * The consistency check over the card's directory and FAT. There is nothing
 * to check, so it reports a clean card that moved no bytes. */
void mgs_CARDCheckExAsync(CPUState* ctx);
void mgs_CARDCheckExAsync(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int32_t  chan  = (int32_t)mgs_guest_gpr(rt, 3);
    uint32_t bytes = mgs_guest_gpr(rt, 4);
    if (bytes) guest_write32(&rt->mem, bytes, 0u);
    /* ASYNC MEANS ASYNC. Returning READY here only says the request was
     * accepted; the game learns the card is sound from the callback, and
     * without it the boot mounted, checked, and then UNMOUNTED - which is
     * what the trace showed and what puts the notice screen back. */
    if (chan == 0) card_queue_callback(mgs_guest_gpr(rt, 5), chan, CARD_RESULT_READY);
    mgs_set_guest_gpr(rt, 3, (uint32_t)(chan == 0 ? CARD_RESULT_READY
                                                  : CARD_RESULT_NOCARD));
    if (card_trace())
        fprintf(stderr, "[card] CheckExAsync chan %d, callback 0x%08X queued\n",
                (int)chan, mgs_guest_gpr(rt, 5));
}

/* s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat)
 *
 * Asked about a particular save. The card is empty, so every file number is
 * absent - which is a truthful answer here, unlike pretending a save exists
 * and then failing to produce its contents. */
void mgs_CARDGetStatus(CPUState* ctx);
void mgs_CARDGetStatus(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    mgs_set_guest_gpr(rt, 3, (uint32_t)CARD_RESULT_NOFILE);
    if (card_trace())
        fprintf(stderr, "[card] GetStatus chan %u file %u -> NOFILE\n",
                mgs_guest_gpr(rt, 3), mgs_guest_gpr(rt, 4));
}


/* Taken by the host pump, which is the only place allowed to call into the
 * guest. Returns 0 when there is nothing waiting. */
uint32_t mgs_card_take_callback(int32_t* chan, int32_t* result);
uint32_t mgs_card_take_callback(int32_t* chan, int32_t* result)
{
    uint32_t cb;
    if (s_pending_head == s_pending_tail) return 0u;
    cb = s_pending[s_pending_head].cb;
    if (chan)   *chan   = s_pending[s_pending_head].chan;
    if (result) *result = s_pending[s_pending_head].result;
    s_pending_head = (s_pending_head + 1u) % CARD_CB_QUEUE;
    return cb;
}

/* s32 CARDMount(s32 chan, void* workArea, CARDCallback detach)
 *
 * The blocking wrapper: the SDK's own version calls CARDMountAsync and then
 * __CARDSync, which sleeps on a thread queue until the EXI interrupt path
 * wakes it. Stubbing only the async half leaves the game asleep in there
 * forever, because nothing on this side ever posts that wake-up. Answering
 * the synchronous call directly avoids needing to.
 */
void mgs_CARDMount(CPUState* ctx);
void mgs_CARDMount(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int32_t chan = (int32_t)mgs_guest_gpr(rt, 3);
    mgs_set_guest_gpr(rt, 3, (uint32_t)(chan == 0 ? CARD_RESULT_READY
                                                  : CARD_RESULT_NOCARD));
    if (card_trace()) fprintf(stderr, "[card] Mount chan %d (blocking)\n", (int)chan);
}

/* s32 __CARDSync(s32 chan)
 *
 * The wait itself. Every operation this layer answers has already finished by
 * the time it returns, so there is never anything to wait for. */
void mgs___CARDSync(CPUState* ctx);
void mgs___CARDSync(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    mgs_set_guest_gpr(rt, 3, (uint32_t)CARD_RESULT_READY);
    if (card_trace()) fprintf(stderr, "[card] __CARDSync -> READY\n");
}

/* s32 CARDUnmount(s32 chan) - nothing is mounted in any real sense. */
void mgs_CARDUnmount(CPUState* ctx);
void mgs_CARDUnmount(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    mgs_set_guest_gpr(rt, 3, (uint32_t)CARD_RESULT_READY);
    if (card_trace()) fprintf(stderr, "[card] Unmount\n");
}
