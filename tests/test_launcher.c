/* The launcher's core: SHA-1 against published vectors, the hashes it
 * carries against config/GGSPA4.toml, and the disc check against whatever
 * images happen to be on this machine (skipped when there are none - a
 * clone without the game still passes).
 *
 * MGS_TEST_DISC1 / MGS_TEST_DISC2 name images to check; otherwise the
 * development layout's extracted folders are used if present. */
#include "../host/sha1.h"
#include "../host/launcher_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static int toml_has(const char* hash)
{
    char line[512];
    FILE* f = fopen("config/GGSPA4.toml", "r");
    int found = 0;
    if (!f) return -1;
    while (fgets(line, sizeof line, f))
        if (strstr(line, "sha1") && strstr(line, hash)) found = 1;
    fclose(f);
    return found;
}

static void check_disc(const char* path, unsigned number)
{
    char msg[256];
    MgsDiscStatus st;
    if (!path) return;
    st = mgs_launcher_check_disc(path, number, msg, sizeof msg);
    printf("disc %u: %s -> %s\n", number, path, msg);
    if (st != MGS_DISC_MISSING) CHECK(st == MGS_DISC_OK);
    /* The same image offered as the other disc must be refused. */
    if (st == MGS_DISC_OK)
        CHECK(mgs_launcher_check_disc(path, number == 1u ? 2u : 1u, msg, sizeof msg)
              == MGS_DISC_WRONG_NUMBER);
}

int main(void)
{
    char hex[41];
    const char* d1 = getenv("MGS_TEST_DISC1");
    const char* d2 = getenv("MGS_TEST_DISC2");
    char msg[256];

    mgs_sha1_hex("abc", 3, hex);
    CHECK(!strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    mgs_sha1_hex("", 0, hex);
    CHECK(!strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    mgs_sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(!strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));

    /* The hashes the launcher carries are the config's. */
    if (toml_has("124bca49886033df5053d3be1f8748a218ddf60f") >= 0) {
        CHECK(toml_has("124bca49886033df5053d3be1f8748a218ddf60f") == 1);
        CHECK(toml_has("99910b99144cf1c1cf7f8a2183b6ca5e595f65d4") == 1);
    }

    CHECK(mgs_launcher_check_disc("/nonexistent/disc.iso", 1u, msg, sizeof msg)
          == MGS_DISC_MISSING);

    if (!d1) d1 = "discs/GGSPA4/disc1";
    if (!d2) d2 = "discs/GGSPA4/disc2";
    check_disc(d1, 1u);
    check_disc(d2, 2u);

    if (failures) printf("%d failure(s)\n", failures);
    else printf("launcher: ok\n");
    return failures ? 1 : 0;
}
