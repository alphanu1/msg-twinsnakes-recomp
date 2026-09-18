/* Disc path resolution order.
 *
 * Tested because the order is a policy decision, not an implementation
 * detail: it decides whether a user who names a path gets what they asked
 * for or something the program found instead.
 */
#include "dvd/disc_locate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    char out[1024];
    MgsDiscSource src;

    unsetenv("MGS_DISC1");
    unsetenv("MGS_DISC2");
    /* Point config at a scratch dir so the test never reads or writes the
     * developer's real configuration. */
    setenv("XDG_CONFIG_HOME", "build/test-config", 1);

    /* An explicit path wins, and is returned even if it does not exist: the
     * useful error is "that path did not mount", naming what the user asked
     * for, rather than silently using something else.
     */
    src = mgs_disc_locate(1u, "/tmp/some-disc.iso", NULL, "GGSPA4", out, sizeof out);
    CHECK(src == MGS_DISC_SOURCE_ARGUMENT);
    CHECK(strcmp(out, "/tmp/some-disc.iso") == 0);

    /* Environment beats everything below it. */
    setenv("MGS_DISC1", "/tmp/from-env.iso", 1);
    src = mgs_disc_locate(1u, NULL, NULL, "GGSPA4", out, sizeof out);
    CHECK(src == MGS_DISC_SOURCE_ENVIRONMENT);
    CHECK(strcmp(out, "/tmp/from-env.iso") == 0);

    /* But not the explicit argument. */
    src = mgs_disc_locate(1u, "/tmp/explicit.iso", NULL, "GGSPA4", out, sizeof out);
    CHECK(src == MGS_DISC_SOURCE_ARGUMENT);
    unsetenv("MGS_DISC1");

    /* The development layout is found when this repo has a disc extracted. */
    src = mgs_disc_locate(1u, NULL, NULL, "GGSPA4", out, sizeof out);
    if (src == MGS_DISC_SOURCE_DEV_LAYOUT) {
        CHECK(strcmp(out, "discs/GGSPA4/disc1") == 0);
        printf("development layout: %s\n", out);
    } else {
        printf("(no extracted disc; development layout not exercised)\n");
    }

    /* A remembered path is honoured only if it still exists - an image the
     * user has since moved must not silently resolve to a stale path.
     */
    CHECK(mgs_disc_remember(2u, "/tmp/definitely-not-here.iso"));
    src = mgs_disc_locate(2u, NULL, NULL, "NOSUCH", out, sizeof out);
    CHECK(src != MGS_DISC_SOURCE_REMEMBERED);

    CHECK(mgs_disc_remember(2u, "/tmp"));         /* exists */
    src = mgs_disc_locate(2u, NULL, NULL, "NOSUCH", out, sizeof out);
    CHECK(src == MGS_DISC_SOURCE_REMEMBERED);
    CHECK(strcmp(out, "/tmp") == 0);

    /* Nothing anywhere is reported as such, not guessed at. */
    src = mgs_disc_locate(1u, NULL, NULL, "NOSUCHGAME", out, sizeof out);
    if (src == MGS_DISC_SOURCE_NONE) CHECK(out[0] == '\0');

    /* Only discs 1 and 2 exist. */
    CHECK(mgs_disc_locate(3u, NULL, NULL, "GGSPA4", out, sizeof out)
          == MGS_DISC_SOURCE_NONE);

    printf("%s\n", mgs_disc_source_name(MGS_DISC_SOURCE_BESIDE_EXE));
    printf(failures ? "%d failure(s)\n" : "all disc location checks passed\n", failures);
    return failures != 0;
}
