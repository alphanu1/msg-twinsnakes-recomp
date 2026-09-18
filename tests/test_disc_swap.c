/* The virtual two-disc swap.
 *
 * The behaviour under test is the one the game depends on: asking for the
 * other disc succeeds and changes which disc is active, without any drive
 * state machine in between.
 */
#include "dvd/dvd.h"
#include "os/os_runtime.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

void mgs_disc_set_bind(MgsRuntime* rt, MgsDisc* disc1, MgsDisc* disc2);
unsigned mgs_disc_set_current(void);
int mgs_disc_set_select(unsigned index);
MgsDisc* mgs_disc_set_active(void);
void mgs_DVDGetCurrentDiskID(CPUState* ctx);
void mgs_DVDCompareDiskID(CPUState* ctx);

/* Minimal seam so the shims can run without a recompiled module present. */
static MgsRuntime g_rt;
static uint32_t g_gpr[32];
MgsRuntime* mgs_runtime_from(CPUState* ctx) { (void)ctx; return &g_rt; }
uint32_t mgs_guest_gpr(const MgsRuntime* rt, unsigned i) { (void)rt; return g_gpr[i]; }
void mgs_set_guest_gpr(MgsRuntime* rt, unsigned i, uint32_t v) { (void)rt; g_gpr[i] = v; }

#define WANT 0x80200000u    /* a DVDDiskID the game builds to say what it wants */

int main(void)
{
    MgsDisc d1, d2;

    {
        FILE* probe = fopen("discs/GGSPA4/disc1/sys/boot.bin", "rb");
        if (!probe) { printf("(no extracted disc; skipped)\n"); return 0; }
        fclose(probe);
    }

    CHECK(guest_memory_init(&g_rt.mem));
    CHECK(mgs_disc_mount(&d1, "discs/GGSPA4/disc1"));

    /* Disc 2 may not be extracted in every checkout; the single-disc case
     * must still behave, so it is exercised either way. */
    if (!mgs_disc_mount(&d2, "discs/GGSPA4/disc2")) {
        printf("(disc 2 not extracted; single-disc behaviour only)\n");
        mgs_disc_set_bind(&g_rt, &d1, NULL);
    } else {
        CHECK(d2.disc_number == 1u);
        mgs_disc_set_bind(&g_rt, &d1, &d2);
    }

    /* The identity is published where the SDK keeps it, at the start of MEM1. */
    mgs_DVDGetCurrentDiskID(NULL);
    CHECK(g_gpr[3] == 0x80000000u);
    CHECK(memcmp(guest_ptr(&g_rt.mem, 0x80000000u, 6u), "GGSPA4", 6) == 0);
    CHECK(guest_read8(&g_rt.mem, 0x80000006u) == 0u);      /* disc 1 */
    CHECK(mgs_disc_set_current() == 0u);

    /* Asking for the disc already in the drive is simply yes. */
    {
        uint8_t* want = guest_ptr(&g_rt.mem, WANT, 32u);
        memset(want, 0, 32); memcpy(want, "GGSPA4", 6); want[6] = 0u;
        g_gpr[3] = 0x80000000u; g_gpr[4] = WANT;
        mgs_DVDCompareDiskID(NULL);
        CHECK(g_gpr[3] == 1u);
        CHECK(mgs_disc_set_current() == 0u);
    }

    /* A different game is never a match, whatever the disc number. */
    {
        uint8_t* want = guest_ptr(&g_rt.mem, WANT, 32u);
        memset(want, 0, 32); memcpy(want, "GALE01", 6); want[6] = 0u;
        g_gpr[3] = 0x80000000u; g_gpr[4] = WANT;
        mgs_DVDCompareDiskID(NULL);
        CHECK(g_gpr[3] == 0u);
        CHECK(mgs_disc_set_current() == 0u);
    }

    /* Asking for disc 2: satisfied instantly if it is mounted, refused if not.
     * Either way there is no drive state to wait on. */
    {
        uint8_t* want = guest_ptr(&g_rt.mem, WANT, 32u);
        memset(want, 0, 32); memcpy(want, "GGSPA4", 6); want[6] = 1u;
        g_gpr[3] = 0x80000000u; g_gpr[4] = WANT;
        mgs_DVDCompareDiskID(NULL);

        if (d2.mounted) {
            CHECK(g_gpr[3] == 1u);
            CHECK(mgs_disc_set_current() == 1u);
            /* And the published identity followed the swap. */
            CHECK(guest_read8(&g_rt.mem, 0x80000006u) == 1u);
            CHECK(mgs_disc_set_active() == &d2);

            /* Back again, because the story does come back. */
            CHECK(mgs_disc_set_select(0u));
            CHECK(guest_read8(&g_rt.mem, 0x80000006u) == 0u);
        } else {
            CHECK(g_gpr[3] == 0u);
            CHECK(mgs_disc_set_current() == 0u);
        }
    }

    /* A disc index that does not exist is refused rather than clamped. */
    CHECK(mgs_disc_set_select(7u) == 0);

    mgs_disc_unmount(&d1);
    if (d2.mounted) mgs_disc_unmount(&d2);
    guest_memory_free(&g_rt.mem);
    printf(failures ? "%d failure(s)\n" : "all disc swap checks passed\n", failures);
    return failures != 0;
}
