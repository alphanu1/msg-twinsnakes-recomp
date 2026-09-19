/* FST parsing, against the real disc's FST when it is present.
 *
 * The synthetic cases pin the format rules; the real FST is the one that
 * catches assumptions. 1,653 entries of a 2004 disc will break a parser that
 * a hand-built 3-entry fixture will not.
 */
#include "dvd/fst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void put_be32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* root -> dir "shared" -> file "a.rel", plus a top-level file "b.dat". */
static void build_fixture(uint8_t* buf, size_t* size)
{
    const char* strings = "\0shared\0a.rel\0b.dat";
    size_t str_len = 1 + 7 + 6 + 6;
    uint32_t entries = 4;

    memset(buf, 0, 256);
    /* 0: root, length = entry count */
    buf[0] = 1; put_be32(buf + 8, entries);
    /* 1: dir "shared" at string offset 1, parent 0, next = 3 */
    buf[12] = 1; buf[13] = 0; buf[14] = 0; buf[15] = 1;
    put_be32(buf + 16, 0); put_be32(buf + 20, 3);
    /* 2: file "a.rel" at string offset 8, disc offset 0x1000, length 0x40 */
    buf[24] = 0; buf[25] = 0; buf[26] = 0; buf[27] = 8;
    put_be32(buf + 28, 0x1000); put_be32(buf + 32, 0x40);
    /* 3: file "b.dat" at string offset 14, disc offset 0x2000, length 0x80 */
    buf[36] = 0; buf[37] = 0; buf[38] = 0; buf[39] = 14;
    put_be32(buf + 40, 0x2000); put_be32(buf + 44, 0x80);

    memcpy(buf + entries * 12, strings, str_len);
    *size = entries * 12 + str_len;
}

int main(void)
{
    uint8_t buf[256];
    size_t size;
    MgsFst fst;
    uint32_t off, len;

    build_fixture(buf, &size);
    CHECK(mgs_fst_load(&fst, buf, size));
    CHECK(fst.entry_count == 4u);

    /* A nested file resolves through its directory. */
    CHECK(mgs_fst_file(&fst, "shared/a.rel", &off, &len));
    CHECK(off == 0x1000u && len == 0x40u);

    /* A leading slash is accepted, as DVDConvertPathToEntrynum accepts it. */
    CHECK(mgs_fst_file(&fst, "/shared/a.rel", &off, &len));

    /* Case-insensitive, matching the SDK. */
    CHECK(mgs_fst_file(&fst, "SHARED/A.REL", &off, &len));

    /* A top-level file is not shadowed by the directory that precedes it:
     * the scan must skip the directory's whole child range rather than
     * descend into it.
     */
    CHECK(mgs_fst_file(&fst, "b.dat", &off, &len));
    CHECK(off == 0x2000u && len == 0x80u);

    /* A file inside a directory is NOT visible at the top level. */
    CHECK(mgs_fst_file(&fst, "a.rel", &off, &len) == 0);
    CHECK(mgs_fst_find(&fst, "nope") == 0u);
    /* A directory is not a file. */
    CHECK(mgs_fst_file(&fst, "shared", &off, &len) == 0);
    CHECK(mgs_fst_find(&fst, "shared") != 0u);

    mgs_fst_free(&fst);

    /* The real thing, if this checkout has a disc extracted. */
    {
        FILE* f = fopen("discs/GGSPA4/disc1/sys/fst.bin", "rb");
        if (!f) {
            printf("(no extracted disc; skipped the real FST)\n");
        } else {
            long n;
            uint8_t* data;
            fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
            data = (uint8_t*)malloc((size_t)n);
            CHECK(fread(data, 1, (size_t)n, f) == (size_t)n);
            fclose(f);

            CHECK(mgs_fst_load(&fst, data, (size_t)n));
            printf("real FST: %u entries\n", fst.entry_count);
            CHECK(fst.entry_count == 1653u);

            /* The REL the whole port depends on. */
            CHECK(mgs_fst_file(&fst, "shared/mgso_pal.rel", &off, &len));
            printf("  shared/mgso_pal.rel  offset 0x%08X  length 0x%X\n", off, len);
            /* Cross-check: the same length config/GGSPA4.toml records from the
             * extracted file, recovered independently from the FST. */
            CHECK(len == 0x578CF4u);
            CHECK(len == 5737716u);

            /* Files the disc listing showed at the top level. */
            CHECK(mgs_fst_file(&fst, "demo.dat", &off, &len));
            CHECK(mgs_fst_file(&fst, "stage.dat", &off, &len));
            CHECK(mgs_fst_file(&fst, "shared/movie.dat", &off, &len));

            /* RELATIVE COMPONENTS, which this game's engine uses for every
             * file it loads: "./stage.dat", "./shared/codec.dat". The SDK's
             * DVDConvertPathToEntrynum accepts them, so this must too - and
             * a refused open is not reported as an error by the engine, it
             * simply retries for ever. This is the check that would have
             * caught that in a second rather than a morning. */
            CHECK(mgs_fst_find(&fst, "./stage.dat") ==
                  mgs_fst_find(&fst, "stage.dat"));
            CHECK(mgs_fst_find(&fst, "./shared/movie.dat") ==
                  mgs_fst_find(&fst, "shared/movie.dat"));
            CHECK(mgs_fst_find(&fst, "shared/./movie.dat") ==
                  mgs_fst_find(&fst, "shared/movie.dat"));
            /* `..` climbs out of a directory and back down. */
            CHECK(mgs_fst_find(&fst, "shared/../stage.dat") ==
                  mgs_fst_find(&fst, "stage.dat"));
            /* A leading slash is still the root, and `..` at the root stays
             * there rather than walking off the front of the table. */
            CHECK(mgs_fst_find(&fst, "/stage.dat") ==
                  mgs_fst_find(&fst, "stage.dat"));
            CHECK(mgs_fst_find(&fst, "../stage.dat") ==
                  mgs_fst_find(&fst, "stage.dat"));

            /* Something that is not there stays not there. */
            CHECK(mgs_fst_find(&fst, "shared/does_not_exist.dat") == 0u);
            CHECK(mgs_fst_find(&fst, "./shared/does_not_exist.dat") == 0u);

            mgs_fst_free(&fst);
            free(data);
        }
    }

    printf(failures ? "%d failure(s)\n" : "all FST checks passed\n", failures);
    return failures != 0;
}
