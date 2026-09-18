/* Disc mounting and reading, both backends.
 *
 * The property that matters is that the two backends are interchangeable: the
 * same path returns the same bytes whether the disc is an image or a folder.
 * If they can diverge, the development path stops predicting the shipped one.
 */
#include "dvd/disc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    MgsDisc disc;
    const char* folder = "discs/GGSPA4/disc1";

    /* Nonexistent paths fail rather than half-mounting. */
    CHECK(mgs_disc_mount(&disc, "does/not/exist") == 0);
    CHECK(mgs_disc_mount(&disc, NULL) == 0);

    if (mgs_disc_file_size(&disc, "anything") == -1) { /* unmounted is safe */ }

    {
        FILE* probe = fopen("discs/GGSPA4/disc1/sys/boot.bin", "rb");
        if (!probe) {
            printf("(no extracted disc; skipped the disc tests)\n");
            printf(failures ? "%d failure(s)\n" : "all disc checks passed\n", failures);
            return failures != 0;
        }
        fclose(probe);
    }

    CHECK(mgs_disc_mount(&disc, folder));
    CHECK(disc.kind == MGS_DISC_FOLDER);
    printf("mounted %s  id=%s  disc_number=%u  fst=%u entries\n",
           folder, disc.game_id, disc.disc_number, disc.fst.entry_count);

    /* The header is read, not assumed: this is the PAL build, disc 1. */
    CHECK(strcmp(disc.game_id, "GGSPA4") == 0);
    CHECK(disc.disc_number == 0u);

    /* The REL the port depends on, by size. */
    CHECK(mgs_disc_file_size(&disc, "shared/mgso_pal.rel") == 5737716L);
    CHECK(mgs_disc_file_size(&disc, "no/such/file") == -1L);

    /* Read its first bytes. A REL begins with its module id, which is 1. */
    {
        uint8_t head[16];
        CHECK(mgs_disc_read(&disc, "shared/mgso_pal.rel", head, 0u, sizeof head)
              == (long)sizeof head);
        CHECK(head[0] == 0 && head[1] == 0 && head[2] == 0 && head[3] == 1);
    }

    /* Reading from an offset lands where it should. */
    {
        uint8_t a[8], b[8];
        CHECK(mgs_disc_read(&disc, "shared/mgso_pal.rel", a, 0x100u, 8u) == 8L);
        CHECK(mgs_disc_read(&disc, "shared/mgso_pal.rel", b, 0x100u, 8u) == 8L);
        CHECK(memcmp(a, b, 8) == 0);            /* and is repeatable */
    }

    /* Running off the end is a SHORT READ, not an error: the SDK's DVDRead
     * returns a length and the game checks it, so failing here would diverge
     * from the hardware the game was written against.
     */
    {
        uint8_t tail[64];
        long n = mgs_disc_read(&disc, "shared/mgso_pal.rel", tail,
                               5737716u - 16u, sizeof tail);
        CHECK(n == 16L);
        CHECK(mgs_disc_read(&disc, "shared/mgso_pal.rel", tail, 5737716u, 8u) == 0L);
    }

    mgs_disc_unmount(&disc);
    CHECK(disc.mounted == 0);

    printf(failures ? "%d failure(s)\n" : "all disc checks passed\n", failures);
    return failures != 0;
}
