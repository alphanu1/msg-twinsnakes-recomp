/* Remappable controls: presets, the controls.ini text, and the rules that
 * keep a hand-edited file from breaking the game.
 *
 * Tested at the data layer (runtime/platform/controls.c) because that is
 * where a wrong binding would come from; SDL is only the name lookup.
 * The cases are the ones that would feel like control bugs: WASD not
 * moving, a remap lost on save, an unknown key name killing the table.
 */
#include "platform/controls.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static int key_is(const MgsControls* c, int btn, int slot, const char* name)
{
    return c->key[btn][slot] == (int32_t)SDL_GetKeyFromName(name);
}

int main(void)
{
    MgsControls c, back;
    char text[8192];

    /* The WASD preset moves with WASD and the arrows both, and the
     * buttons it displaced land where the preset says. */
    mgs_controls_defaults(&c, MGS_PRESET_WASD);
    CHECK(c.preset == MGS_PRESET_WASD);
    CHECK(key_is(&c, MGS_BTN_UP, 0, "W") && key_is(&c, MGS_BTN_UP, 1, "Up"));
    CHECK(key_is(&c, MGS_BTN_DOWN, 0, "S") && key_is(&c, MGS_BTN_LEFT, 0, "A"));
    CHECK(key_is(&c, MGS_BTN_RIGHT, 0, "D"));
    CHECK(key_is(&c, MGS_BTN_A, 0, "X") && key_is(&c, MGS_BTN_B, 0, "Z"));
    CHECK(key_is(&c, MGS_BTN_X, 0, "F") && key_is(&c, MGS_BTN_Y, 0, "C"));
    CHECK(key_is(&c, MGS_BTN_R, 0, "R") && key_is(&c, MGS_BTN_L, 0, "Q"));
    CHECK(key_is(&c, MGS_BTN_START, 0, "Return"));

    /* The classic preset is the mapping the game shipped with. */
    mgs_controls_defaults(&c, MGS_PRESET_CLASSIC);
    CHECK(key_is(&c, MGS_BTN_Y, 0, "A") && key_is(&c, MGS_BTN_X, 0, "S"));
    CHECK(key_is(&c, MGS_BTN_R, 0, "W"));
    CHECK(key_is(&c, MGS_BTN_UP, 0, "Up") && c.key[MGS_BTN_UP][1] == 0);

    /* Gamepad defaults: face buttons in place, Z on the right shoulder,
     * L/R left to the analog triggers. */
    CHECK(c.pad[MGS_BTN_A] == MGS_PADBTN_SOUTH);
    CHECK(c.pad[MGS_BTN_B] == MGS_PADBTN_EAST);
    CHECK(c.pad[MGS_BTN_Z] == MGS_PADBTN_RIGHT_SHOULDER);
    CHECK(c.pad[MGS_BTN_L] == MGS_PADBTN_NONE && c.pad[MGS_BTN_R] == MGS_PADBTN_NONE);
    CHECK(mgs_controls_button_bit(MGS_BTN_A) == 0x0100u);
    CHECK(mgs_controls_button_bit(MGS_BTN_UP) == 0x0008u);

    /* A remap survives being written out and read back. */
    mgs_controls_defaults(&c, MGS_PRESET_WASD);
    c.key[MGS_BTN_A][0] = (int32_t)SDL_GetKeyFromName("J");
    c.key[MGS_BTN_A][1] = 0;
    c.pad[MGS_BTN_A] = MGS_PADBTN_WEST;
    c.deadzone = 0.30f;
    c.mouse.enabled = 0;
    c.mouse.sensitivity = 2.5f;
    CHECK(mgs_controls_write_text(&c, text, (int)sizeof text) > 0);
    mgs_controls_defaults(&back, MGS_PRESET_CLASSIC);
    mgs_controls_parse_text(&back, text);
    CHECK(key_is(&back, MGS_BTN_A, 0, "J") && back.key[MGS_BTN_A][1] == 0);
    CHECK(key_is(&back, MGS_BTN_UP, 0, "W"));       /* the rest came along */
    CHECK(back.pad[MGS_BTN_A] == MGS_PADBTN_WEST);
    CHECK(back.deadzone > 0.29f && back.deadzone < 0.31f);
    CHECK(back.mouse.enabled == 0);
    CHECK(back.mouse.sensitivity > 2.4f && back.mouse.sensitivity < 2.6f);
    CHECK(back.preset == MGS_PRESET_CUSTOM);        /* a hand edit says so */

    /* An untouched file still detects as its preset, not Custom. */
    mgs_controls_defaults(&c, MGS_PRESET_CLASSIC);
    CHECK(mgs_controls_write_text(&c, text, (int)sizeof text) > 0);
    mgs_controls_defaults(&back, MGS_PRESET_WASD);
    mgs_controls_parse_text(&back, text);
    CHECK(back.preset == MGS_PRESET_CLASSIC);

    /* Unknown names and junk lines are ignored, never fatal, and do not
     * clear what was already bound. */
    mgs_controls_defaults(&c, MGS_PRESET_WASD);
    mgs_controls_parse_text(&c,
        "nonsense line\n[keyboard]\na=NoSuchKeyName\nup=W, AlsoNotAKey\n"
        "[mouse]\nsensitivity=99\n");
    CHECK(c.key[MGS_BTN_A][0] == 0);                       /* cleared, no crash */
    CHECK(key_is(&c, MGS_BTN_UP, 0, "W") && c.key[MGS_BTN_UP][1] == 0);
    CHECK(c.mouse.sensitivity == 3.0f);                    /* clamped */

    /* Out-of-range dead zone clamps instead of inverting the stick. */
    mgs_controls_parse_text(&c, "[gamepad]\ndeadzone=-2\n");
    CHECK(c.deadzone == 0.0f);
    mgs_controls_parse_text(&c, "[gamepad]\ndeadzone=9\n");
    CHECK(c.deadzone == 0.5f);

    if (failures) printf("test_controls: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
