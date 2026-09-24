#include "bp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mgs_bp_init(MgsGxBp* bp)
{
    memset(bp, 0, sizeof *bp);
}

/* An 11-bit TEV field, sign-extended. Dolphin declares all four as `s32`. */
static int32_t tev_field(uint32_t v)
{
    int32_t x = (int32_t)(v & 0x7FFu);
    return (x & 0x400) ? x - 0x800 : x;
}

void mgs_bp_write(MgsGxBp* bp, uint8_t reg, uint32_t value)
{
    bp->reg[reg] = value & 0x00FFFFFFu;
    bp->written[reg] = 1u;
    ++bp->rev;

    /* How many times the copy DESTINATION has been set since the last copy.
     * A copy that runs with zero writes since the previous one is reusing
     * that copy's address, which is how a display copy can land on the
     * texture a moment earlier wrote. */
    if (reg == 0x4Bu) ++bp->efb_addr_writes;

    /* Route 0xE0-0xE7 to the colour register or the konst register, by bit
     * 23. Doing it here rather than at read time is what makes both
     * survivable: a game that sets a konst and then a colour through the
     * same address would otherwise leave only the second. */
    if (reg >= 0xF6u && reg <= 0xFDu && getenv("MGS_TRACE_KSEL")) {
        static unsigned seen[8];
        unsigned i2 = reg - 0xF6u;
        if (seen[i2]++ < 3u)
            fprintf(stderr, "[ksel] 0x%02X first write 0x%06X  "
                    "swap_rb %u swap_ga %u  kcsel even %u odd %u\n",
                    reg, value & 0xFFFFFFu, value & 3u, (value >> 2) & 3u,
                    (value >> 4) & 0x1Fu, (value >> 14) & 0x1Fu);
    }
    if (reg >= 0xE0u && reg <= 0xE7u) {
        {   /* Which of these writes are konst and which are colour? The
             * routing above is only right if the game marks them the way
             * the hardware reads them, and that is a fact about the game,
             * not a fact about us. */
            static int on = -1; static unsigned n;
            if (on < 0) on = getenv("MGS_TRACE_TEVREG") != NULL;
            if (on && n < 400000u) {
                ++n;
                fprintf(stderr, "[tevreg] 0x%02X = 0x%06X  %s  "
                        "low 0x%03X high 0x%03X\n", reg, value & 0xFFFFFFu,
                        (value & (1u << 23)) ? "KONST" : "colour",
                        value & 0x7FFu, (value >> 12) & 0x7FFu);
            }
        }
        unsigned index = (unsigned)(reg - 0xE0u) >> 1;
        int is_bg = ((reg - 0xE0u) & 1u) != 0;
        int32_t (*dst)[4] = (value & (1u << 23)) ? bp->konst : bp->tevreg;
        if (is_bg) {
            dst[index][2] = tev_field(value);          /* blue  */
            dst[index][1] = tev_field(value >> 12);    /* green */
        } else {
            dst[index][0] = tev_field(value);          /* red   */
            dst[index][3] = tev_field(value >> 12);    /* alpha */
        }
    }
}
