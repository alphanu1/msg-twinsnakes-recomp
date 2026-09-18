/* GameCube controllers, over SDL3.
 *
 * Two things make this more than a button-mapping exercise, and both are
 * places a naive mapping feels right and plays wrong.
 *
 * ANALOG TRIGGERS. The GameCube's L and R are analog with a digital click at
 * the end of travel. PADStatus carries BOTH: an 8-bit pressure value and a
 * separate PAD_TRIGGER_L/R bit that latches only at full press. A game can
 * read either, and Twin Snakes reads both - the analog value aims, the click
 * fires. Mapping the click from "axis > 0" would fire the moment you touch
 * the trigger; mapping pressure from the button would remove aiming entirely.
 *
 * RESTING VALUES. Zero is centre for a thumbstick, and a resting trigger
 * reads FULL NEGATIVE on the joystick interface rather than zero. So an
 * all-zero reading at rest is correct, and a trigger that reads -32767 is
 * released, not broken. Both look like faults if you go looking for one.
 *
 * STICK RANGE. The SDK's sticks are signed 8-bit but do NOT use the full
 * range: the hardware's usable travel is about +/-72 after the SDK's own
 * clamping, and games are calibrated against that. Feeding a full -128..127
 * makes everything oversensitive in a way that feels like a deadzone bug
 * rather than a scaling one.
 */
#ifndef MGS_PAD_H
#define MGS_PAD_H

#include <stdint.h>

/* PADStatus, 12 bytes, from the SDK's pad.h. */
#define PAD_STATUS_BUTTON     0x00u   /* u16 */
#define PAD_STATUS_STICK_X    0x02u   /* s8  */
#define PAD_STATUS_STICK_Y    0x03u
#define PAD_STATUS_SUBSTICK_X 0x04u
#define PAD_STATUS_SUBSTICK_Y 0x05u
#define PAD_STATUS_TRIGGER_L  0x06u   /* u8  */
#define PAD_STATUS_TRIGGER_R  0x07u
#define PAD_STATUS_ANALOG_A   0x08u
#define PAD_STATUS_ANALOG_B   0x09u
#define PAD_STATUS_ERR        0x0Au   /* s8  */
#define PAD_STATUS_SIZEOF     0x0Cu

#define PAD_BUTTON_LEFT   0x0001u
#define PAD_BUTTON_RIGHT  0x0002u
#define PAD_BUTTON_DOWN   0x0004u
#define PAD_BUTTON_UP     0x0008u
#define PAD_TRIGGER_Z     0x0010u
#define PAD_TRIGGER_R     0x0020u
#define PAD_TRIGGER_L     0x0040u
#define PAD_BUTTON_A      0x0100u
#define PAD_BUTTON_B      0x0200u
#define PAD_BUTTON_X      0x0400u
#define PAD_BUTTON_Y      0x0800u
#define PAD_BUTTON_START  0x1000u

/* PADStatus.err */
#define PAD_ERR_NONE          0
#define PAD_ERR_NO_CONTROLLER (-1)

#define PAD_MAX_CONTROLLERS 4

/* The SDK clamps sticks to roughly this before a game ever sees them. Games
 * are calibrated against the clamped range, not the raw one.
 */
#define PAD_STICK_RANGE    72
#define PAD_SUBSTICK_RANGE 60

/* Where a GameCube trigger's digital click latches, as a fraction of travel.
 * The real hardware clicks near the end; below it, only the analog value
 * moves.
 */
#define PAD_TRIGGER_CLICK_POINT 230   /* of 255 */

typedef struct MgsPadState {
    uint16_t button;
    int8_t   stick_x, stick_y;
    int8_t   substick_x, substick_y;
    uint8_t  trigger_l, trigger_r;
    int8_t   err;
    int      connected;
} MgsPadState;

/* Pure mapping, no SDL: takes normalised axes in [-1,1] and triggers in
 * [0,1] and produces what the guest will read. Separated from SDL so it can
 * be tested without a controller plugged in, which is the only way this gets
 * tested at all.
 */
void mgs_pad_map(MgsPadState* out,
                 float axis_lx, float axis_ly,
                 float axis_rx, float axis_ry,
                 float trigger_l, float trigger_r,
                 uint16_t sdl_buttons);

/* Bits in `sdl_buttons`, so the mapping layer need not know SDL's enums. */
#define MGS_SRC_A       0x0001u
#define MGS_SRC_B       0x0002u
#define MGS_SRC_X       0x0004u
#define MGS_SRC_Y       0x0008u
#define MGS_SRC_START   0x0010u
#define MGS_SRC_Z       0x0020u
#define MGS_SRC_DUP     0x0040u
#define MGS_SRC_DDOWN   0x0080u
#define MGS_SRC_DLEFT   0x0100u
#define MGS_SRC_DRIGHT  0x0200u

#endif
