/* MGS_PAD_SCRIPT entries: buttons plus optional stick bytes.
 *
 * The regression this guards: the per-frame selection loop once applied
 * the stick bytes of every entry unconditionally, so the stick always
 * took the LAST entry's values (usually the centred release) and scripted
 * stick deflection never reached the game at all.
 */
#include "platform/mmio.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    MgsMmio m;
    int i;

#ifdef _WIN32
    _putenv_s("MGS_PAD_SCRIPT", "2:0100:10:F0,5:0000:80:80");
#else
    setenv("MGS_PAD_SCRIPT", "2:0100:10:F0,5:0000:80:80", 1);
#endif
    mgs_mmio_init(&m);

    /* Both entries parsed, stick bytes included. */
    CHECK(m.pad_script_n == 2);
    CHECK(m.pad_script_frame[0] == 2 && m.pad_script_btn[0] == 0x0100);
    CHECK(m.pad_script_sx[0] == 0x10 && m.pad_script_sy[0] == 0xF0);
    CHECK(m.pad_script_frame[1] == 5 && m.pad_script_btn[1] == 0x0000);
    CHECK(m.pad_script_sx[1] == 0x80 && m.pad_script_sy[1] == 0x80);

    /* Before frame 2 nothing has arrived: stick centred, no buttons. */
    mgs_mmio_tick_frame(&m);
    CHECK(m.pad_stick_x == 0x80 && m.pad_stick_y == 0x80);
    CHECK(m.pad_forced == 0);

    /* Frames 2..4: the FIRST entry holds - buttons and stick together.
     * With the old unbraced loop the stick was already the last entry's
     * centred values here. */
    for (i = 0; i < 3; ++i) mgs_mmio_tick_frame(&m);
    CHECK(m.pad_forced == 0x0100);
    CHECK(m.pad_stick_x == 0x10 && m.pad_stick_y == 0xF0);

    /* From frame 5 the release entry wins. */
    for (i = 0; i < 3; ++i) mgs_mmio_tick_frame(&m);
    CHECK(m.pad_forced == 0);
    CHECK(m.pad_stick_x == 0x80 && m.pad_stick_y == 0x80);

    printf(failures ? "%d failure(s)\n" : "all pad_script checks passed\n", failures);
    return failures != 0;
}
