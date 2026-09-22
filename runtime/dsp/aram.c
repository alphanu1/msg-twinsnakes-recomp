#include "aram.h"

#include <stdlib.h>
#include <string.h>

/* Register offsets, from the SDK's own indices. See aram.h. */
#define AR_MM_HI    0x20u
#define AR_MM_LO    0x22u
#define AR_AR_HI    0x24u
#define AR_AR_LO    0x26u
#define AR_LEN_HI   0x28u
#define AR_LEN_LO   0x2Au

#define AR_DIR_READ 0x8000u   /* in LEN_HI: ARAM -> main memory */

static uint16_t rd16(const uint8_t* regs, uint32_t off)
{
    return (uint16_t)(((uint32_t)regs[off] << 8) | regs[off + 1u]);
}

/* ONE PIECE OF HARDWARE, ONE BUFFER.
 *
 * This used to `calloc` its own 16 MB and leave `GuestMemory.aram`
 * untouched, so the console had two audio RAMs: the DMA filled one and
 * anything reading guest memory saw the other, permanently zero. Nothing
 * noticed while only the DMA used it - `__ARChecksize` probes through this
 * same path and passed either way - and it surfaced the moment the AX mixer
 * read a voice's samples and got silence from an address the DMA had
 * demonstrably written (HANDOFF F247).
 *
 * `GuestMemory` owns the allocation; this only borrows it, and must not
 * free it. */
int mgs_aram_init(MgsAram* a, GuestMemory* mem)
{
    memset(a, 0, sizeof *a);
    if (!mem || !mem->aram) return 0;
    a->data = mem->aram;
    a->mem = mem;
    return 1;
}

void mgs_aram_free(MgsAram* a)
{
    a->data = NULL;                 /* borrowed from GuestMemory, not ours */
}

void mgs_aram_run_dma(MgsAram* a, const uint8_t* regs)
{
    uint32_t mm, ar, len, i;
    int to_main;

    if (!a->data || !a->mem) return;

    /* THE LOW FIVE BITS ARE NOT PART OF THE ADDRESS. The SDK masks them off
     * when it writes these registers (`& ~0xffe0` on the low halves), because
     * the hardware transfers in 32-byte units. Using them would offset every
     * transfer by up to 31 bytes, which corrupts quietly rather than
     * failing. */
    mm  = ((uint32_t)(rd16(regs, AR_MM_HI)  & 0x03FFu) << 16) |
           (uint32_t)(rd16(regs, AR_MM_LO)  & 0xFFE0u);
    ar  = ((uint32_t)(rd16(regs, AR_AR_HI)  & 0x03FFu) << 16) |
           (uint32_t)(rd16(regs, AR_AR_LO)  & 0xFFE0u);
    len = ((uint32_t)(rd16(regs, AR_LEN_HI) & 0x03FFu) << 16) |
           (uint32_t)(rd16(regs, AR_LEN_LO) & 0xFFE0u);

    to_main = (rd16(regs, AR_LEN_HI) & AR_DIR_READ) != 0;

    if (!len) return;

    /* ARAM is 16 MB and the address is 26 bits, so a transfer can name memory
     * that is not there. `__ARChecksize` DOES THIS DELIBERATELY: it is
     * probing for the size, and what it learns from a write that lands
     * nowhere is that the memory is absent. Wrapping is what the hardware
     * does with the address lines it has, and is what makes the probe
     * terminate at 16 MB rather than run off the end of the allocation. */
    if (to_main) {
        for (i = 0; i < len; ++i)
            guest_write8(a->mem, mm + i,
                         a->data[(ar + i) & (MGS_ARAM_SIZE - 1u)]);
        ++a->reads;
        a->bytes_out += len;
    } else {
        for (i = 0; i < len; ++i)
            a->data[(ar + i) & (MGS_ARAM_SIZE - 1u)] =
                guest_read8(a->mem, mm + i);
        if (!a->writes || ar < a->lo_in) a->lo_in = ar;
        if (ar + len > a->hi_in) a->hi_in = ar + len;
        ++a->writes;
        a->bytes_in += len;
    }
}
