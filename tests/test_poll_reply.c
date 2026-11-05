/* SI poll replies must look like a real controller's.
 *
 * The regression this guards: the synthesized poll response left bit
 * 0x80 of the second reply byte clear. The SDK's PAD conversion treats
 * a poll response without that bit as "no analog data" and zeroes both
 * sticks (latching a recalibrate request instead), so every stick
 * input silently died between the SI registers and the game's pad
 * state while buttons kept working. A real controller sets the bit in
 * every poll response.
 */
#include "platform/mmio.h"

#include <stdio.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    MgsMmio m;
    uint32_t inbuf_h;

    mgs_mmio_init(&m);

    /* Port 1 held south-east with A down, as a gamepad would report. */
    mgs_mmio_set_pad(&m, 0x0100u);
    mgs_mmio_set_pad_analog(&m, 0xE8u, 0xE8u, 0x80u, 0x80u, 0u, 0u);

    /* Ask the serial interface to poll port 1 every vblank, the way
     * PAD does once it believes a controller exists. */
    mgs_mmio_write(&m, MMIO_SI + 0x30u, 0x01280280u, 4u);  /* SI_POLL: EN0 */
    mgs_mmio_tick_frame(&m);

    /* The channel input buffer now holds the poll response: buttons,
     * then the analog bytes as a controller sends them. */
    inbuf_h = mgs_mmio_read(&m, MMIO_SI + 0x04u, 4u);      /* INBUF0 high */
    CHECK((inbuf_h >> 24) == 0x01u);                       /* buttons hi */
    CHECK(((inbuf_h >> 16) & 0xFFu) == 0x80u);             /* the SDK bit */
    CHECK(((inbuf_h >> 8) & 0xFFu) == 0xE8u);              /* stick x */
    CHECK((inbuf_h & 0xFFu) == 0xE8u);                     /* stick y */

    printf(failures ? "%d failure(s)\n" : "all poll reply checks passed\n",
           failures);
    return failures != 0;
}
