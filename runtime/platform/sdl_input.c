/* SDL3 gamepad input.
 *
 * Deliberately thin: it reads SDL and hands normalised values to the pure
 * mapping layer in pad/pad.c. All the behaviour worth testing lives there,
 * where it can be tested without a controller plugged in - a test that needs
 * hardware present is a test that does not run.
 *
 * This is the first SDL3 in the tree. It is also the only file that knows
 * SDL exists, on the guest-input side.
 */
#include "../pad/pad.h"

#include <SDL3/SDL.h>
#include <string.h>

typedef struct MgsInput {
    SDL_Gamepad* pads[PAD_MAX_CONTROLLERS];
    int          started;
} MgsInput;

static MgsInput s_input;

int  mgs_input_init(void);
void mgs_input_shutdown(void);
void mgs_input_poll(void);
void mgs_input_read(unsigned port, MgsPadState* out);
unsigned mgs_input_connected_count(void);

int mgs_input_init(void)
{
    if (s_input.started) return 1;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) return 0;
    s_input.started = 1;
    mgs_input_poll();
    return 1;
}

void mgs_input_shutdown(void)
{
    unsigned i;
    for (i = 0; i < PAD_MAX_CONTROLLERS; ++i) {
        if (s_input.pads[i]) { SDL_CloseGamepad(s_input.pads[i]); s_input.pads[i] = NULL; }
    }
    if (s_input.started) { SDL_QuitSubSystem(SDL_INIT_GAMEPAD); s_input.started = 0; }
}

/* Open whatever is plugged in, in order, into ports 0..3. Hot-plug is handled
 * by re-scanning rather than by listening for events: the guest reads pads
 * once a frame anyway, so a rescan costs nothing measurable and avoids a
 * second source of truth about which port holds what.
 */
void mgs_input_poll(void)
{
    int count = 0;
    SDL_JoystickID* ids;
    unsigned slot = 0;
    unsigned i;

    if (!s_input.started) return;
    SDL_UpdateGamepads();

    ids = SDL_GetGamepads(&count);
    if (!ids) return;

    for (i = 0; i < (unsigned)count && slot < PAD_MAX_CONTROLLERS; ++i, ++slot) {
        if (!s_input.pads[slot])
            s_input.pads[slot] = SDL_OpenGamepad(ids[i]);
    }
    for (; slot < PAD_MAX_CONTROLLERS; ++slot) {
        if (s_input.pads[slot]) { SDL_CloseGamepad(s_input.pads[slot]); s_input.pads[slot] = NULL; }
    }
    SDL_free(ids);
}

static float axis_norm(SDL_Gamepad* pad, SDL_GamepadAxis axis)
{
    /* SDL axes are signed 16-bit. Dividing by 32767 rather than 32768 so a
     * full deflection reaches exactly 1.0 instead of 0.99997, which would
     * otherwise stop the stick one unit short of the SDK's range.
     */
    return (float)SDL_GetGamepadAxis(pad, axis) / 32767.0f;
}

static float trigger_norm(SDL_Gamepad* pad, SDL_GamepadAxis axis)
{
    /* Triggers report 0..32767 and never go negative. */
    float v = (float)SDL_GetGamepadAxis(pad, axis) / 32767.0f;
    return v < 0.0f ? 0.0f : v;
}

void mgs_input_read(unsigned port, MgsPadState* out)
{
    SDL_Gamepad* pad;
    uint16_t src = 0u;

    memset(out, 0, sizeof *out);
    out->err = PAD_ERR_NO_CONTROLLER;

    if (port >= PAD_MAX_CONTROLLERS) return;
    pad = s_input.pads[port];
    if (!pad) return;

    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH))          src |= MGS_SRC_A;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST))           src |= MGS_SRC_B;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST))           src |= MGS_SRC_X;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH))          src |= MGS_SRC_Y;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))          src |= MGS_SRC_START;
    /* Z has no natural home on a modern pad; the right shoulder is the least
     * surprising, since the GameCube's own R is the analog trigger. */
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) src |= MGS_SRC_Z;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP))        src |= MGS_SRC_DUP;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN))      src |= MGS_SRC_DDOWN;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))      src |= MGS_SRC_DLEFT;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))     src |= MGS_SRC_DRIGHT;

    mgs_pad_map(out,
                axis_norm(pad, SDL_GAMEPAD_AXIS_LEFTX),
                axis_norm(pad, SDL_GAMEPAD_AXIS_LEFTY),
                axis_norm(pad, SDL_GAMEPAD_AXIS_RIGHTX),
                axis_norm(pad, SDL_GAMEPAD_AXIS_RIGHTY),
                trigger_norm(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
                trigger_norm(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
                src);
}

/* THE CONTROLLER AS THE SERIAL INTERFACE CARRIES IT (F376): buttons, and
 * the analog axes as raw bytes - sticks centred on 0x80, triggers from 0.
 * The SDK takes the rest position from the controller's ORIGIN reply and
 * clamps the sticks itself (PADClamp), so what goes on the wire is the
 * hardware's range, not the clamped one mgs_pad_map produces: a full tilt
 * of a real stick reads about 100 either side of centre.
 *
 * Packed: buttons in bits 0-15, then main stick x, y, c-stick x, y,
 * trigger l, r a byte each. No controller: neutral, no buttons. */
static uint8_t raw_axis(float v, int invert)
{
    /* A small radial allowance for a stick that does not quite centre;
     * the SDK's own dead zone is larger and applies after. */
    int r;
    if (v > -0.04f && v < 0.04f) v = 0.0f;
    if (invert) v = -v;
    r = 128 + (int)(v * 100.0f + (v < 0.0f ? -0.5f : 0.5f));
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    return (uint8_t)r;
}

static uint8_t raw_trigger(float v)
{
    int r = (int)(v * 255.0f + 0.5f);
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    return (uint8_t)r;
}

uint64_t mgs_input_gc_raw(unsigned port);
uint64_t mgs_input_gc_raw(unsigned port)
{
    MgsPadState st;
    SDL_Gamepad* pad;
    uint64_t v;
    if (port >= PAD_MAX_CONTROLLERS || !(pad = s_input.pads[port]))
        return (uint64_t)0x80u << 16 | (uint64_t)0x80u << 24 |
               (uint64_t)0x80u << 32 | (uint64_t)0x80u << 40;
    mgs_input_read(port, &st);          /* buttons, trigger clicks */
    v  = st.button;
    v |= (uint64_t)raw_axis(axis_norm(pad, SDL_GAMEPAD_AXIS_LEFTX), 0) << 16;
    v |= (uint64_t)raw_axis(axis_norm(pad, SDL_GAMEPAD_AXIS_LEFTY), 1) << 24;
    v |= (uint64_t)raw_axis(axis_norm(pad, SDL_GAMEPAD_AXIS_RIGHTX), 0) << 32;
    v |= (uint64_t)raw_axis(axis_norm(pad, SDL_GAMEPAD_AXIS_RIGHTY), 1) << 40;
    v |= (uint64_t)raw_trigger(trigger_norm(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)) << 48;
    v |= (uint64_t)raw_trigger(trigger_norm(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) << 56;
    return v;
}

/* The first controller's name, for the start-up line. */
const char* mgs_input_name(unsigned port);
const char* mgs_input_name(unsigned port)
{
    return (port < PAD_MAX_CONTROLLERS && s_input.pads[port])
         ? SDL_GetGamepadName(s_input.pads[port]) : NULL;
}

unsigned mgs_input_connected_count(void)
{
    unsigned i, n = 0;
    for (i = 0; i < PAD_MAX_CONTROLLERS; ++i) if (s_input.pads[i]) ++n;
    return n;
}
