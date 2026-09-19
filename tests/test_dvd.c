/* Asynchronous disc reads.
 *
 * The properties under test are the ones the SDK's contract depends on: the
 * data lands in guest memory, it lands correctly when many reads are in
 * flight at once, the status the game polls moves BUSY -> END, and nothing
 * writes guest memory except the drain on the guest thread.
 */
#include "dvd/dvd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define REL_PATH "shared/mgso_pal.rel"
#define BLOCK    0x80300000u        /* a fake DVDCommandBlock in guest RAM */
#define DEST     0x80400000u

int main(void)
{
    MgsDisc disc;
    MgsDvd dvd;
    MgsJobPool* jobs;
    GuestMemory mem;
    MgsDvdRequest* done[MGS_DVD_MAX_PENDING];

    {
        FILE* probe = fopen("discs/GGSPA4/disc1/sys/boot.bin", "rb");
        if (!probe) { printf("(no extracted disc; skipped)\n"); return 0; }
        fclose(probe);
    }

    CHECK(guest_memory_init(&mem));
    CHECK(mgs_disc_mount(&disc, "discs/GGSPA4/disc1"));
    jobs = mgs_jobs_create(0u);
    CHECK(jobs != NULL);
    mgs_dvd_init(&dvd, &disc, jobs, &mem);

    /* One read, with a command block the game would poll. */
    {
        MgsDvdRequest* req = mgs_dvd_read_async(&dvd, REL_PATH, DEST, 0u, 64u, 0u, BLOCK);
        unsigned n;
        CHECK(req != NULL);

        /* BUSY is visible immediately: the game must never see a request that
         * is neither busy nor finished. */
        CHECK((int32_t)guest_read32(&mem, BLOCK + DVD_CB_STATE) == DVD_STATE_BUSY);

        mgs_jobs_wait(jobs);
        n = mgs_dvd_drain(&dvd, done, MGS_DVD_MAX_PENDING, ~0ull);
        CHECK(n == 1u);
        CHECK(done[0]->result == 64L);
        CHECK((int32_t)guest_read32(&mem, BLOCK + DVD_CB_STATE) == DVD_STATE_END);

        /* A REL's first word is its module id, which is 1 - and it must be
         * readable through the byte-swapping accessor, i.e. it landed in
         * guest memory big-endian and intact. */
        CHECK(guest_read32(&mem, DEST) == 1u);
        mgs_dvd_release(done[0]);
        CHECK(mgs_dvd_pending_count(&dvd) == 0u);
    }

    /* Many reads in flight at once, each to its own destination: this is
     * where a worker writing guest memory directly, or a shared buffer,
     * would show up as crossed data. */
    {
        enum { N = 32 };
        unsigned i, n, total = 0;
        for (i = 0; i < N; ++i) {
            uint32_t off = (uint32_t)i * 256u;
            CHECK(mgs_dvd_read_async(&dvd, REL_PATH, DEST + 0x10000u + i * 0x100u,
                                     off, 256u, 0u, 0u) != NULL);
        }
        CHECK(mgs_dvd_pending_count(&dvd) == N);

        mgs_jobs_wait(jobs);
        while ((n = mgs_dvd_drain(&dvd, done, MGS_DVD_MAX_PENDING, ~0ull)) > 0u) {
            for (i = 0; i < n; ++i) {
                CHECK(done[i]->result == 256L);
                mgs_dvd_release(done[i]);
            }
            total += n;
            if (total >= N) break;
        }
        CHECK(total == N);
        CHECK(mgs_dvd_pending_count(&dvd) == 0u);

        /* Each destination must hold ITS OWN slice, not another read's. */
        for (i = 0; i < N; ++i) {
            uint8_t expect[8];
            uint32_t addr = DEST + 0x10000u + i * 0x100u;
            CHECK(mgs_disc_read(&disc, REL_PATH, expect, (uint32_t)i * 256u, 8u) == 8L);
            CHECK(memcmp(guest_ptr(&mem, addr, 8u), expect, 8) == 0);
        }
    }

    /* A read of a file that is not there fails rather than half-succeeding. */
    {
        MgsDvdRequest* req = mgs_dvd_read_async(&dvd, "no/such.dat", DEST, 0u, 16u, 0u, BLOCK);
        unsigned n;
        CHECK(req != NULL);
        mgs_jobs_wait(jobs);
        n = mgs_dvd_drain(&dvd, done, 1u, ~0ull);
        CHECK(n == 1u);
        CHECK(done[0]->result == -1L);
        CHECK((int32_t)guest_read32(&mem, BLOCK + DVD_CB_STATE) == DVD_STATE_FATAL_ERROR);
        mgs_dvd_release(done[0]);
    }

    /* The synchronous path shares the asynchronous implementation, so there
     * is only one thing to be wrong. */
    CHECK(mgs_dvd_read_sync(&dvd, REL_PATH, DEST + 0x20000u, 16u, 32u) == 32L);

    mgs_jobs_destroy(jobs);
    mgs_disc_unmount(&disc);
    guest_memory_free(&mem);
    printf(failures ? "%d failure(s)\n" : "all DVD checks passed\n", failures);
    return failures != 0;
}
