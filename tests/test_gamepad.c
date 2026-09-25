/* The controller path end to end, with SDL3's VIRTUAL gamepad (F376).
 *
 * test_pad.c covers the mapping layer on its own; this covers what reaches
 * the game: SDL opens the pad, our code reads it, and the serial interface
 * gets raw bytes - sticks centred on 0x80, about 100 either side at full
 * tilt, triggers 0-255, buttons in the SDK's bit layout. A virtual pad
 * needs no hardware, so this runs on any machine and any platform SDL
 * supports, which is the point: nobody has to own a controller for the
 * controller to stay working.
 */
#include "pad/pad.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>

int  mgs_input_init(void);
void mgs_input_poll(void);
void mgs_input_shutdown(void);
uint64_t mgs_input_gc_raw(unsigned port);
const char* mgs_input_name(unsigned port);

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define BUTTONS(v) ((unsigned)((v) & 0xFFFFu))
#define SX(v)  ((unsigned)(((v) >> 16) & 0xFFu))
#define SY(v)  ((unsigned)(((v) >> 24) & 0xFFu))
#define CX(v)  ((unsigned)(((v) >> 32) & 0xFFu))
#define CY(v)  ((unsigned)(((v) >> 40) & 0xFFu))
#define TL(v)  ((unsigned)(((v) >> 48) & 0xFFu))
#define TR(v)  ((unsigned)(((v) >> 56) & 0xFFu))

int main(void)
{
    SDL_VirtualJoystickDesc desc;
    SDL_JoystickID id;
    SDL_Joystick* joy;
    uint64_t v;
    unsigned port = 0u, p;

    /* No controller at all: neutral, no buttons - never a stick pushed
     * hard left because an unset byte read as 0. */
    CHECK(mgs_input_init());
    v = mgs_input_gc_raw(0u);
    if (mgs_input_name(0u) == NULL) {
        CHECK(BUTTONS(v) == 0u);
        CHECK(SX(v) == 0x80u && SY(v) == 0x80u);
        CHECK(CX(v) == 0x80u && CY(v) == 0x80u);
        CHECK(TL(v) == 0u && TR(v) == 0u);
    }

    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "mgs virtual pad";
    id = SDL_AttachVirtualJoystick(&desc);
    if (!id) {
        printf("SKIP: no virtual joystick (%s)\n", SDL_GetError());
        mgs_input_shutdown();
        return failures ? 1 : 0;
    }
    joy = SDL_OpenJoystick(id);
    CHECK(joy != NULL);
    /* A virtual pad's trigger axis spans -32768..32767 for 0..full, so
     * at rest it is -32768 (0 would be half pressed). */
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768);
    mgs_input_poll();                       /* opens it into a port */
    /* A real controller plugged into this machine takes port 1 first;
     * the virtual one is found by its name. */
    for (p = 0; p < PAD_MAX_CONTROLLERS; ++p) {
        const char* n = mgs_input_name(p);
        if (n && !SDL_strcmp(n, "mgs virtual pad")) { port = p; break; }
    }
    CHECK(p < PAD_MAX_CONTROLLERS);

    /* At rest. */
    v = mgs_input_gc_raw(port);
    CHECK(BUTTONS(v) == 0u);
    CHECK(SX(v) == 0x80u && SY(v) == 0x80u);
    CHECK(CX(v) == 0x80u && CY(v) == 0x80u);
    CHECK(TL(v) == 0u && TR(v) == 0u);

    /* Main stick full right and full UP. SDL's Y grows downwards, the
     * GameCube's upwards: full up must read HIGH, or walking forward walks
     * backward. */
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFTY, -32768);
    /* C-stick full left and full down. */
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHTX, -32768);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHTY, 32767);
    /* Right trigger all the way (analog 255 and the digital click), left
     * trigger a quarter (analog only, no click). */
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    SDL_SetJoystickVirtualAxis(joy, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -16384);
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_SOUTH, true);
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_START, true);
    SDL_SetJoystickVirtualButton(joy, SDL_GAMEPAD_BUTTON_DPAD_UP, true);
    mgs_input_poll();                       /* SDL applies the new state */
    v = mgs_input_gc_raw(port);

    CHECK(SX(v) >= 0xE0u && SX(v) <= 0xE8u);        /* ~128 + 100 */
    CHECK(SY(v) >= 0xE0u && SY(v) <= 0xE8u);        /* up reads high */
    CHECK(CX(v) >= 0x18u && CX(v) <= 0x20u);        /* ~128 - 100 */
    CHECK(CY(v) >= 0x18u && CY(v) <= 0x20u);        /* down reads low */
    CHECK(TR(v) == 255u);
    CHECK(TL(v) >= 60u && TL(v) <= 68u);            /* a quarter */
    CHECK((BUTTONS(v) & PAD_TRIGGER_R) != 0u);       /* fully pressed clicks */
    CHECK((BUTTONS(v) & PAD_TRIGGER_L) == 0u);       /* a quarter does not */
    CHECK((BUTTONS(v) & PAD_BUTTON_A) != 0u);        /* south is A */
    CHECK((BUTTONS(v) & 0x1000u) != 0u);             /* Start */
    CHECK((BUTTONS(v) & PAD_BUTTON_UP) != 0u);
    CHECK((BUTTONS(v) & PAD_BUTTON_B) == 0u);        /* nothing else */

    /* Unplugged: back to neutral, not the last thing it held. */
    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(id);
    mgs_input_poll();
    for (p = 0; p < PAD_MAX_CONTROLLERS; ++p) {
        const char* n = mgs_input_name(p);
        CHECK(!n || SDL_strcmp(n, "mgs virtual pad") != 0);
    }
    if (!mgs_input_name(port)) {
        v = mgs_input_gc_raw(port);
        CHECK(BUTTONS(v) == 0u && SX(v) == 0x80u && SY(v) == 0x80u);
    }

    mgs_input_shutdown();
    if (failures) printf("%d failure(s)\n", failures);
    else printf("gamepad: ok\n");
    return failures ? 1 : 0;
}
