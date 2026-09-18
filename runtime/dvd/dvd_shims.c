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

/* Resolve the path a DVDFileInfo refers to, by matching its recorded disc
 * offset back to an FST entry. The guest gives us an offset and a length, not
 * a name, so this is how an async read knows which file to read.
 */
static int path_for_fileinfo(uint32_t fi, char* out, size_t cap)
{
    uint32_t start = guest_read32(&s_rt->mem, fi + DVD_FI_START_ADDR);
    uint32_t i;

    for (i = 1u; i < s_dvd->disc->fst.entry_count; ++i) {
        MgsFstEntry e;
        if (!mgs_fst_entry(&s_dvd->disc->fst, i, &e) || e.is_dir) continue;
        if (e.offset_or_parent != start) continue;
        snprintf(out, cap, "%s", mgs_fst_name(&s_dvd->disc->fst, &e));
        return 1;
    }
    return 0;
}

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
    char path[256];

    if (!fi || !path_for_fileinfo(fi, path, sizeof path)) {
        mgs_set_guest_gpr(s_rt, 3, 0u);
        return;
    }

    /* The game reads these back while it waits. */
    guest_write32(&s_rt->mem, fi + DVD_CB_ADDR, dest);
    guest_write32(&s_rt->mem, fi + DVD_CB_LENGTH, length);
    guest_write32(&s_rt->mem, fi + DVD_CB_OFFSET, offset);
    guest_write32(&s_rt->mem, fi + DVD_FI_CALLBACK, callback);

    mgs_set_guest_gpr(s_rt, 3,
        mgs_dvd_read_async(s_dvd, path, dest, offset, length, callback, fi)
            ? 1u : 0u);
}

/* DVDReadAsync is a macro expanding to this in the SDK header, so the game's
 * code only ever calls the Prio form. The non-Prio entry point exists here for
 * readability and is what the macro's callers mean. */
void mgs_DVDReadAsyncPrio(CPUState* ctx) { mgs_DVDReadAsync(ctx); }
