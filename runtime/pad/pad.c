#include "pad.h"

#include <math.h>
#include <string.h>

static int8_t scale_axis(float v, int range)
{
    float scaled;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    scaled = v * (float)range;
    /* Round away from zero, so a full deflection reaches the range rather
     * than stopping one short of it. */
    scaled = scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f;
    return (int8_t)scaled;
}

static uint8_t scale_trigger(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < 0.0f) v = 0.0f;
    return (uint8_t)(v * 255.0f + 0.5f);
}

void mgs_pad_map(MgsPadState* out,
                 float axis_lx, float axis_ly,
                 float axis_rx, float axis_ry,
                 float trigger_l, float trigger_r,
                 uint16_t src)
{
    memset(out, 0, sizeof *out);
    out->connected = 1;
    out->err = PAD_ERR_NONE;

    /* Y is inverted between the two worlds: SDL reports down as positive,
     * the GameCube reports up as positive. Getting this wrong inverts aiming
     * and reads as a control bug rather than a sign error.
     */
    out->stick_x    = scale_axis(axis_lx, PAD_STICK_RANGE);
    out->stick_y    = scale_axis(-axis_ly, PAD_STICK_RANGE);
    out->substick_x = scale_axis(axis_rx, PAD_SUBSTICK_RANGE);
    out->substick_y = scale_axis(-axis_ry, PAD_SUBSTICK_RANGE);

    out->trigger_l = scale_trigger(trigger_l);
    out->trigger_r = scale_trigger(trigger_r);

    if (src & MGS_SRC_A)      out->button |= PAD_BUTTON_A;
    if (src & MGS_SRC_B)      out->button |= PAD_BUTTON_B;
    if (src & MGS_SRC_X)      out->button |= PAD_BUTTON_X;
    if (src & MGS_SRC_Y)      out->button |= PAD_BUTTON_Y;
    if (src & MGS_SRC_START)  out->button |= PAD_BUTTON_START;
    if (src & MGS_SRC_Z)      out->button |= PAD_TRIGGER_Z;
    if (src & MGS_SRC_DUP)    out->button |= PAD_BUTTON_UP;
    if (src & MGS_SRC_DDOWN)  out->button |= PAD_BUTTON_DOWN;
    if (src & MGS_SRC_DLEFT)  out->button |= PAD_BUTTON_LEFT;
    if (src & MGS_SRC_DRIGHT) out->button |= PAD_BUTTON_RIGHT;

    /* The digital click latches near the end of travel, not on first touch.
     * Deriving it from "trigger moved at all" would fire every time the
     * player begins to aim.
     */
    if (out->trigger_l >= PAD_TRIGGER_CLICK_POINT) out->button |= PAD_TRIGGER_L;
    if (out->trigger_r >= PAD_TRIGGER_CLICK_POINT) out->button |= PAD_TRIGGER_R;
}
