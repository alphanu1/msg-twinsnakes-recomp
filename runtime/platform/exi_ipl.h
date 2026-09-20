/* The other device on channel 0: the clock and the machine's settings.
 *
 * Chip select 2 on channel 0 reaches what the SDK calls the IPL device - a
 * mask ROM with the fonts, a real-time clock, and 64 bytes of battery-backed
 * SRAM holding the machine's language, video mode and display offset.
 *
 * It is here because the memory card needs it. `DoMount` reads SRAM as part
 * of mounting, so a slot that answers nothing there fails the mount however
 * sound the card is - four of the nine EXI transfers in a boot were going to
 * devices that did not exist, and this is two of them (HANDOFF F167).
 *
 * The font ROM is NOT provided: it is Nintendo's, it is not ours to ship, and
 * nothing has asked for it. Reads of that region answer zero.
 */
#ifndef MGS_EXI_IPL_H
#define MGS_EXI_IPL_H

#include <stdint.h>

typedef struct MgsExiIpl {
    uint8_t  sram[64];
    uint32_t rtc;            /* seconds, as the clock counts them */
    uint32_t command;        /* the four bytes that open a transfer */
    unsigned command_bytes;  /* how many of them have arrived */
    uint32_t address;
    int      writing;
    unsigned offset;         /* how far into the addressed region we are */
} MgsExiIpl;

void mgs_exi_ipl_init(MgsExiIpl* p);
void mgs_exi_ipl_select(MgsExiIpl* p, int asserted);
void mgs_exi_ipl_byte(MgsExiIpl* p, uint8_t* byte);

#endif
