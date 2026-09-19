#include "bp.h"

#include <string.h>

void mgs_bp_init(MgsGxBp* bp)
{
    memset(bp, 0, sizeof *bp);
}

void mgs_bp_write(MgsGxBp* bp, uint8_t reg, uint32_t value)
{
    bp->reg[reg] = value & 0x00FFFFFFu;
    bp->written[reg] = 1u;
}
