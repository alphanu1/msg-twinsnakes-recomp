/* The virtual two-disc swap.
 *
 * Twin Snakes ships on two discs and asks for the second mid-story. On real
 * hardware the player opens the lid, swaps, and closes it.
 *
 * The design document anticipated having to emulate that - cover-open,
 * disc-inserted and cover-closed reported on the SDK's timing. MEASURING what
 * the engine actually calls says otherwise. Of every DVD function in the
 * binary, the engine calls exactly five, and the two that matter here are:
 *
 *     DVDGetCurrentDiskID   2 call sites
 *     DVDCompareDiskID      2 call sites
 *
 * No cover polling, no DVDGetDriveStatus, no DVDLowWaitCoverClose. The game
 * asks "which disc is in the drive?" and compares it against the one it
 * wants. So the swap is: change the answer.
 *
 * That is worth stating because it removes a whole class of work - there is
 * no drive-state machine to model and no timing to match, and both would have
 * been guesswork.
 *
 * DVDDiskID is 32 bytes: gameName[4], company[2], diskNumber, gameVersion,
 * streaming, streamingBufSize, padding[22]. The disc number is one byte at
 * offset 6, which is also what distinguishes the two discs' headers on the
 * discs themselves.
 */
#include "dvd.h"
#include "../os/os_runtime.h"

#include <string.h>

#define DVD_DISK_ID_GAME_NAME   0x00u
#define DVD_DISK_ID_COMPANY     0x04u
#define DVD_DISK_ID_DISK_NUMBER 0x06u
#define DVD_DISK_ID_VERSION     0x07u
#define DVD_DISK_ID_SIZEOF      0x20u

/* The SDK keeps the current disc ID at the start of MEM1, where the
 * apploader left the boot header. The game reads it through
 * DVDGetCurrentDiskID, which returns that address.
 */
#define DVD_CURRENT_DISK_ID     0x80000000u

typedef struct MgsDiscSet {
    MgsRuntime* rt;
    MgsDisc*    disc[2];        /* index 0 = disc 1 */
    unsigned    current;        /* 0 or 1 */
} MgsDiscSet;

static MgsDiscSet s_discs;

void mgs_disc_set_bind(MgsRuntime* rt, MgsDisc* disc1, MgsDisc* disc2);
unsigned mgs_disc_set_current(void);
int mgs_disc_set_select(unsigned index);
MgsDisc* mgs_disc_set_active(void);

/* Publish a disc's identity where the game looks for it. */
static void publish_disk_id(const MgsDisc* disc)
{
    GuestMemory* m = &s_discs.rt->mem;
    uint8_t* p;

    if (!disc || !disc->mounted) return;

    p = guest_ptr(m, DVD_CURRENT_DISK_ID, DVD_DISK_ID_SIZEOF);
    if (!p) return;
    memset(p, 0, DVD_DISK_ID_SIZEOF);

    /* game_id is the six characters of gameName+company, in that order. */
    memcpy(p + DVD_DISK_ID_GAME_NAME, disc->game_id, 6);
    p[DVD_DISK_ID_DISK_NUMBER] = disc->disc_number;
}

void mgs_disc_set_bind(MgsRuntime* rt, MgsDisc* disc1, MgsDisc* disc2)
{
    s_discs.rt = rt;
    s_discs.disc[0] = disc1;
    s_discs.disc[1] = disc2;
    s_discs.current = 0u;
    publish_disk_id(disc1);
}

unsigned mgs_disc_set_current(void) { return s_discs.current; }

MgsDisc* mgs_disc_set_active(void)
{
    return s_discs.disc[s_discs.current];
}

/* Swap. Instant, because there is nothing to wait for: the game learns which
 * disc is present by asking, not by watching a drive.
 */
int mgs_disc_set_select(unsigned index)
{
    if (index > 1u || !s_discs.disc[index] || !s_discs.disc[index]->mounted)
        return 0;
    s_discs.current = index;
    publish_disk_id(s_discs.disc[index]);
    return 1;
}

/* DVDDiskID* DVDGetCurrentDiskID(void) */
void mgs_DVDGetCurrentDiskID(CPUState* ctx)
{
    (void)ctx;
    mgs_set_guest_gpr(s_discs.rt, 3, DVD_CURRENT_DISK_ID);
}

/* BOOL DVDCompareDiskID(const DVDDiskID* id1, const DVDDiskID* id2)
 *
 * Compares the identifying fields only. gameVersion and the streaming fields
 * are NOT part of identity - the SDK ignores them, and a game that asked for
 * "disc 2" would otherwise be refused by a version byte it never set.
 *
 * If the ids differ only in disc number, and the other disc IS mounted, this
 * is the swap request: satisfy it and answer yes. That is the whole two-disc
 * mechanism, and it is why no drive-state machine is needed.
 */
void mgs_DVDCompareDiskID(CPUState* ctx)
{
    GuestMemory* m = &s_discs.rt->mem;
    uint32_t a = mgs_guest_gpr(s_discs.rt, 3);
    uint32_t b = mgs_guest_gpr(s_discs.rt, 4);
    uint8_t ga[6], gb[6];
    unsigned i;
    uint8_t na, nb;

    (void)ctx;
    if (!a || !b) { mgs_set_guest_gpr(s_discs.rt, 3, 0u); return; }

    for (i = 0; i < 6u; ++i) {
        ga[i] = guest_read8(m, a + DVD_DISK_ID_GAME_NAME + i);
        gb[i] = guest_read8(m, b + DVD_DISK_ID_GAME_NAME + i);
    }
    if (memcmp(ga, gb, 6) != 0) { mgs_set_guest_gpr(s_discs.rt, 3, 0u); return; }

    na = guest_read8(m, a + DVD_DISK_ID_DISK_NUMBER);
    nb = guest_read8(m, b + DVD_DISK_ID_DISK_NUMBER);
    if (na == nb) { mgs_set_guest_gpr(s_discs.rt, 3, 1u); return; }

    /* Same game, different disc: the swap the player would be asked for. */
    if (nb <= 1u && mgs_disc_set_select((unsigned)nb)) {
        mgs_set_guest_gpr(s_discs.rt, 3, 1u);
        return;
    }
    mgs_set_guest_gpr(s_discs.rt, 3, 0u);
}
