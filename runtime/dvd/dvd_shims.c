/* The guest-facing DVD API.
 *
 * These are what the translated code calls. Each one is reached through the
 * patch table, so the SDK's own translated body never runs - which is the
 * design document's central decision: the boundary between translated and
 * native is the SDK's public API.
 *
 * Every argument and every result crosses the guest/host boundary explicitly.
 * A DVDFileInfo is guest memory laid out by a 2003 PowerPC compiler, so it is
 * read and written field by field at the SDK's offsets, never cast.
 */
#include "dvd.h"
#include "../os/os_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The runtime the shims act on. Set once at startup; the alternative -
 * threading it through every shim - would mean every shim signature
 * diverging from the SDK's, which makes the patch table harder to check
 * against the real API.
 */
static MgsDvd* s_dvd;
static MgsRuntime* s_rt;

void mgs_dvd_bind(MgsRuntime* rt, MgsDvd* dvd);
void mgs_dvd_bind(MgsRuntime* rt, MgsDvd* dvd) { s_rt = rt; s_dvd = dvd; }

/* Copy a guest string out, bounded. Guest strings are not trusted to be
 * terminated: an unbounded read here walks 24 MB.
 */
static void guest_string(uint32_t addr, char* out, size_t cap)
{
    size_t i = 0;
    out[0] = '\0';
    if (!addr) return;
    for (; i + 1u < cap; ++i) {
        uint8_t ch = guest_read8(&s_rt->mem, addr + (uint32_t)i);
        if (!ch) break;
        out[i] = (char)ch;
    }
    out[i] = '\0';
}

/* s32 DVDConvertPathToEntrynum(char* pathPtr) */
void mgs_DVDConvertPathToEntrynum(CPUState* ctx)
{
    char path[256];
    uint32_t entry;

    guest_string(mgs_guest_gpr(s_rt, 3), path, sizeof path);
    entry = mgs_fst_find(&s_dvd->disc->fst, path);

    /* The SDK returns -1 for "not found", and 0 is the root - which is a
     * valid entry number but never a file, so it cannot be used as failure.
     */
    mgs_set_guest_gpr(s_rt, 3, entry ? entry : (uint32_t)-1);
}

/* BOOL DVDFastOpen(s32 entrynum, DVDFileInfo* fileInfo) */
void mgs_DVDFastOpen(CPUState* ctx)
{
    uint32_t entry = mgs_guest_gpr(s_rt, 3);
    uint32_t fi = mgs_guest_gpr(s_rt, 4);
    MgsFstEntry e;

    if (!fi || !mgs_fst_entry(&s_dvd->disc->fst, entry, &e) || e.is_dir) {
        mgs_set_guest_gpr(s_rt, 3, 0u);        /* FALSE */
        return;
    }

    /* Zero the whole structure first: the game may reuse a DVDFileInfo, and
     * a stale callback or state left in the embedded command block would be
     * acted on.
     */
    {
        uint8_t* p = guest_ptr(&s_rt->mem, fi, DVD_FI_SIZEOF);
        if (p) memset(p, 0, DVD_FI_SIZEOF);
    }
    guest_write32(&s_rt->mem, fi + DVD_FI_START_ADDR, e.offset_or_parent);
    guest_write32(&s_rt->mem, fi + DVD_FI_LENGTH, e.length_or_next);
    guest_write32(&s_rt->mem, fi + DVD_CB_STATE, (uint32_t)DVD_STATE_END);
    mgs_set_guest_gpr(s_rt, 3, 1u);            /* TRUE */
}

/* BOOL DVDOpen(char* fileName, DVDFileInfo* fileInfo) */
void mgs_DVDOpen(CPUState* ctx)
{
    char path[256];
    uint32_t fi = mgs_guest_gpr(s_rt, 4);
    uint32_t entry;

    guest_string(mgs_guest_gpr(s_rt, 3), path, sizeof path);
    entry = mgs_fst_find(&s_dvd->disc->fst, path);
    if (getenv("MGS_TRACE_DVD"))
        fprintf(stderr, "[dvd] DVDOpen(\"%s\", 0x%08X) -> entry %u\n",
                path, fi, entry);
    if (!entry) { mgs_set_guest_gpr(s_rt, 3, 0u); return; }

    /* Reuse DVDFastOpen rather than repeating it, so there is one place that
     * knows how a DVDFileInfo is filled in.
     */
    mgs_set_guest_gpr(s_rt, 3, entry);
    mgs_set_guest_gpr(s_rt, 4, fi);
    mgs_DVDFastOpen(ctx);
}

/* BOOL DVDClose(DVDFileInfo* fileInfo) */
void mgs_DVDClose(CPUState* ctx)
{
    /* Nothing to release: an open file is an offset and a length, and the
     * disc stays mounted. Still patched rather than left translated, because
     * the translated body would touch hardware registers that do not exist.
     */
    mgs_set_guest_gpr(s_rt, 3, 1u);
}

/* s32 DVDGetCommandBlockStatus(const DVDCommandBlock* block) */
void mgs_DVDGetCommandBlockStatus(CPUState* ctx)
{
    uint32_t block = mgs_guest_gpr(s_rt, 3);
    mgs_set_guest_gpr(s_rt, 3,
                      block ? guest_read32(&s_rt->mem, block + DVD_CB_STATE)
                            : (uint32_t)DVD_STATE_FATAL_ERROR);
}

/* s32 DVDGetFileInfoStatus(const DVDFileInfo* fileInfo) - the command block is
 * the first member, so this is the same question asked of a file. Implemented
 * and ready; not yet in the patch table because the symbol map has not reached
 * the address. */
void mgs_DVDGetFileInfoStatus(CPUState* ctx) { mgs_DVDGetCommandBlockStatus(ctx); }


/* BOOL DVDReadAsyncPrio(DVDFileInfo* fileInfo, void* addr, s32 length,
 *                       s32 offset, DVDCallback callback, s32 prio)
 * and the four-argument DVDReadAsync, which differs only in the priority.
 */
void mgs_DVDReadAsync(CPUState* ctx)
{
    uint32_t fi       = mgs_guest_gpr(s_rt, 3);
    uint32_t dest     = mgs_guest_gpr(s_rt, 4);
    uint32_t length   = mgs_guest_gpr(s_rt, 5);
    uint32_t offset   = mgs_guest_gpr(s_rt, 6);
    uint32_t callback = mgs_guest_gpr(s_rt, 7);
    uint32_t start, size;

    (void)ctx;

    if (!fi || !dest) {
        if (getenv("MGS_TRACE_DVD"))
            fprintf(stderr, "[dvd] async REFUSED: fi=0x%08X dest=0x%08X\n",
                    fi, dest);
        mgs_set_guest_gpr(s_rt, 3, 0u);
        return;
    }

    /* READ BY DISC OFFSET, NOT BY NAME.
     *
     * The SDK's own DVDReadAsyncPrio does exactly this: it adds the file
     * info's start address to the caller's offset and hands the result to
     * DVDReadAbsAsyncPrio. It never looks a name up, because the file info
     * already IS the answer.
     *
     * The previous version searched the FST for a file whose start address
     * matched, and used its NAME - which is not a path, so anything in a
     * subdirectory failed to resolve and the read was refused. The engine
     * does not treat a refused read as fatal; it retries, for ever. That
     * presented as the game running happily and loading nothing, with
     * `DVDReadAsyncPrio` the second-hottest function in the profile and one
     * completed read in a hundred million steps.
     */
    start = guest_read32(&s_rt->mem, fi + DVD_FI_START_ADDR);
    size  = guest_read32(&s_rt->mem, fi + DVD_FI_LENGTH);

    /* The SDK refuses a read that runs past the file; so does this, rather
     * than silently returning a short one the game did not ask for. */
    if (offset > size || length > size - offset) {
        if (getenv("MGS_TRACE_DVD"))
            fprintf(stderr, "[dvd] async REFUSED: past end - start=0x%08X "
                            "size=%u offset=%u length=%u\n",
                    start, size, offset, length);
        mgs_set_guest_gpr(s_rt, 3, 0u);
        return;
    }
    if (getenv("MGS_TRACE_DVD"))
        fprintf(stderr, "[dvd] async fi=0x%08X start=0x%08X size=%u "
                        "off=%u len=%u -> 0x%08X cb=0x%08X\n",
                fi, start, size, offset, length, dest, callback);

    /* The game reads these back while it waits. */
    guest_write32(&s_rt->mem, fi + DVD_CB_ADDR, dest);
    guest_write32(&s_rt->mem, fi + DVD_CB_LENGTH, length);
    guest_write32(&s_rt->mem, fi + DVD_CB_OFFSET, offset);
    guest_write32(&s_rt->mem, fi + DVD_FI_CALLBACK, callback);

    mgs_set_guest_gpr(s_rt, 3,
        mgs_dvd_read_abs_async(s_dvd, start + offset, dest, length,
                               callback, fi)
            ? 1u : 0u);
}

/* DVDReadAsync is a macro expanding to this in the SDK header, so the game's
 * code only ever calls the Prio form. The non-Prio entry point exists here for
 * readability and is what the macro's callers mean. */
void mgs_DVDReadAsyncPrio(CPUState* ctx) { mgs_DVDReadAsync(ctx); }

/* BOOL DVDReadAbsAsyncPrio(DVDCommandBlock* block, void* addr, s32 length,
 *                          u32 offset, DVDCBCallback callback, s32 prio)
 *
 * The read the SDK's SYNCHRONOUS path actually uses. DVDReadPrio resolves a
 * DVDFileInfo to an absolute disc offset and calls this, then sleeps on the
 * DVD thread queue until the callback wakes it. Patching only the by-path
 * async read left that whole path translated, so the game's loader queued a
 * read into the SDK's own command queue - which nothing here services - and
 * the main thread slept in DVDReadPrio for the rest of the run.
 *
 * The callback contract is the SDK's: callback(s32 result, DVDCommandBlock*),
 * run on the guest thread when the read lands. The host's pump does that.
 */
void mgs_DVDReadAbsAsyncPrio(CPUState* ctx)
{
    uint32_t block    = mgs_guest_gpr(s_rt, 3);
    uint32_t dest     = mgs_guest_gpr(s_rt, 4);
    uint32_t length   = mgs_guest_gpr(s_rt, 5);
    uint32_t offset   = mgs_guest_gpr(s_rt, 6);
    uint32_t callback = mgs_guest_gpr(s_rt, 7);

    (void)ctx;

    if (!block || !dest) { mgs_set_guest_gpr(s_rt, 3, 0u); return; }

    /* The game reads these back while it waits, exactly as for the by-path
     * read - the command block is the same structure either way. */
    guest_write32(&s_rt->mem, block + DVD_CB_ADDR, dest);
    guest_write32(&s_rt->mem, block + DVD_CB_LENGTH, length);
    guest_write32(&s_rt->mem, block + DVD_CB_OFFSET, offset);
    guest_write32(&s_rt->mem, block + DVD_CB_CALLBACK, callback);

    mgs_set_guest_gpr(s_rt, 3,
        mgs_dvd_read_abs_async(s_dvd, offset, dest, length, callback, block)
            ? 1u : 0u);

    if (getenv("MGS_TRACE_DVD"))
        fprintf(stderr, "[dvd] abs queued: block=0x%08X state=%d cb=0x%08X "
                        "dest=0x%08X len=%u off=0x%08X\n",
                block, (int)guest_read32(&s_rt->mem, block + DVD_CB_STATE),
                callback, dest, length, offset);
}
