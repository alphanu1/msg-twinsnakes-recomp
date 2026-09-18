/* Controller mapping.
 *
 * Tested through the pure mapping layer rather than SDL, because a test that
 * needs a controller plugged in is a test that never runs. The cases chosen
 * are the ones that feel like control bugs rather than code bugs: inverted
 * aiming, triggers that fire on touch, sticks that are oversensitive.
 */
#include "pad/pad.h"
#include <stdio.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    MgsPadState p;

    /* Neutral is neutral: no drift, no phantom buttons. */
    mgs_pad_map(&p, 0,0, 0,0, 0,0, 0);
    CHECK(p.stick_x == 0 && p.stick_y == 0);
    CHECK(p.substick_x == 0 && p.substick_y == 0);
    CHECK(p.button == 0 && p.trigger_l == 0 && p.trigger_r == 0);
    CHECK(p.err == PAD_ERR_NONE && p.connected);

    /* Full deflection reaches the SDK's range exactly - not 127, which would
     * be oversensitive against a game calibrated for the clamped range. */
    mgs_pad_map(&p, 1.0f,0, 0,0, 0,0, 0);
    CHECK(p.stick_x == PAD_STICK_RANGE);
    mgs_pad_map(&p, -1.0f,0, 0,0, 0,0, 0);
    CHECK(p.stick_x == -PAD_STICK_RANGE);

    /* Y is inverted: SDL says down is positive, the GameCube says up is.
     * Getting this wrong inverts aiming and reads as a control bug. */
    mgs_pad_map(&p, 0,-1.0f, 0,0, 0,0, 0);
    CHECK(p.stick_y == PAD_STICK_RANGE);          /* SDL up -> GC up */
    mgs_pad_map(&p, 0,1.0f, 0,0, 0,0, 0);
    CHECK(p.stick_y == -PAD_STICK_RANGE);

    /* The C-stick has its own, smaller range. */
    mgs_pad_map(&p, 0,0, 1.0f,-1.0f, 0,0, 0);
    CHECK(p.substick_x == PAD_SUBSTICK_RANGE);
    CHECK(p.substick_y == PAD_SUBSTICK_RANGE);

    /* Out-of-range input is clamped, not wrapped: a wrap would send a full
     * right deflection hard left. */
    mgs_pad_map(&p, 5.0f,0, 0,0, 0,0, 0);
    CHECK(p.stick_x == PAD_STICK_RANGE);
    mgs_pad_map(&p, -5.0f,0, 0,0, 0,0, 0);
    CHECK(p.stick_x == -PAD_STICK_RANGE);

    /* Triggers: analog value moves well before the digital click latches.
     * This is the case that separates aiming from firing. */
    mgs_pad_map(&p, 0,0, 0,0, 0.25f,0, 0);
    CHECK(p.trigger_l > 0);
    CHECK((p.button & PAD_TRIGGER_L) == 0);       /* pressed, not clicked */

    mgs_pad_map(&p, 0,0, 0,0, 0.5f,0, 0);
    CHECK(p.trigger_l > 100 && p.trigger_l < 160);
    CHECK((p.button & PAD_TRIGGER_L) == 0);

    mgs_pad_map(&p, 0,0, 0,0, 1.0f,1.0f, 0);
    CHECK(p.trigger_l == 255 && p.trigger_r == 255);
    CHECK(p.button & PAD_TRIGGER_L);
    CHECK(p.button & PAD_TRIGGER_R);

    /* Every button maps to the bit the SDK defines, and to no other. */
    mgs_pad_map(&p, 0,0, 0,0, 0,0, MGS_SRC_A);
    CHECK(p.button == PAD_BUTTON_A);
    mgs_pad_map(&p, 0,0, 0,0, 0,0, MGS_SRC_START);
    CHECK(p.button == PAD_BUTTON_START);
    mgs_pad_map(&p, 0,0, 0,0, 0,0, MGS_SRC_Z);
    CHECK(p.button == PAD_TRIGGER_Z);

    mgs_pad_map(&p, 0,0, 0,0, 0,0,
                MGS_SRC_DUP | MGS_SRC_DDOWN | MGS_SRC_DLEFT | MGS_SRC_DRIGHT);
    CHECK(p.button == (PAD_BUTTON_UP | PAD_BUTTON_DOWN |
                       PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT));

    /* Everything at once still composes correctly. */
    mgs_pad_map(&p, 0,0, 0,0, 1.0f,1.0f, MGS_SRC_A | MGS_SRC_Z);
    CHECK(p.button == (PAD_BUTTON_A | PAD_TRIGGER_Z |
                       PAD_TRIGGER_L | PAD_TRIGGER_R));

    printf(failures ? "%d failure(s)\n" : "all pad checks passed\n", failures);
    return failures != 0;
}
