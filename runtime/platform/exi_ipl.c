/* See exi_ipl.h. */
#include "exi_ipl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* THE COMMAND CARRIES THE ADDRESS SHIFTED UP BY SIX, so the figures quoted
 * for these regions - 0x20000000 for the clock, 0x20000100 for SRAM - are
 * command values, not device addresses. Decoded, they land four apart, which
 * is why they are compared exactly rather than masked: masking off six bits
 * to find "the region" merges the clock and the settings into one.
 *
 * Getting this wrong is quiet. The first version compared the decoded
 * address against the undecoded constants, matched neither, and answered
 * every SRAM read with the zero reserved for the font ROM. */
#define IPL_RTC   (0x20000000u >> 6)   /* 0x00800000 */
#define IPL_SRAM  (0x20000100u >> 6)   /* 0x00800004 */
#define IPL_UART  (0x20010000u >> 6)   /* 0x00800400 */

/* SRAM's checksum covers the sixteen bytes after the two checksum fields -
 * the settings proper, not the extended area that follows them. A block that
 * fails it is not fatal: the SDK rewrites SRAM from its own defaults. Getting
 * it right anyway means the settings we choose are the ones that survive. */
static void sram_checksum(MgsExiIpl* p)
{
    uint16_t sum = 0u, inv = 0u;
    unsigned i;
    for (i = 4u; i < 20u; i += 2u) {
        uint16_t w = (uint16_t)((p->sram[i] << 8) | p->sram[i + 1u]);
        sum = (uint16_t)(sum + w);
        inv = (uint16_t)(inv + (uint16_t)~w);
    }
    p->sram[0] = (uint8_t)(sum >> 8); p->sram[1] = (uint8_t)sum;
    p->sram[2] = (uint8_t)(inv >> 8); p->sram[3] = (uint8_t)inv;
}

void mgs_exi_ipl_init(MgsExiIpl* p)
{
    memset(p, 0, sizeof *p);

    /* Defaults, chosen to be the least surprising thing a machine could hold:
     * no clock bias, no display offset, English, and no flags set. The video
     * mode the game uses comes from the disc, not from here. */
    p->sram[0x0Cu] = 0u; p->sram[0x0Du] = 0u;   /* counter bias   */
    p->sram[0x0Eu] = 0u; p->sram[0x0Fu] = 0u;
    p->sram[0x10u] = 0u;                        /* display offset */
    p->sram[0x11u] = 0u;                        /* NTD            */
    p->sram[0x12u] = 0u;                        /* language       */
    p->sram[0x13u] = 0u;                        /* flags          */
    sram_checksum(p);

    /* A FIXED CLOCK, deliberately. Reading the host's would make a headless
     * run unreproducible, and reproducibility is what that path is for - the
     * same reason OSGetTime counts guest ticks rather than wall clock. */
    p->rtc = 0u;
}

void mgs_exi_ipl_select(MgsExiIpl* p, int asserted)
{
    if (!p || asserted) return;
    p->command = 0u;
    p->command_bytes = 0u;
    p->offset = 0u;
    p->writing = 0;
}

void mgs_exi_ipl_byte(MgsExiIpl* p, uint8_t* byte)
{
    uint32_t region;

    if (!p || !byte) return;

    /* Four bytes of command first: the top bit says which direction, and the
     * address sits above the low six bits, which are ignored. */
    if (p->command_bytes < 4u) {
        p->command = (p->command << 8) | *byte;
        ++p->command_bytes;
        *byte = 0xFFu;
        if (p->command_bytes == 4u) {
            p->writing = (p->command >> 31) & 1u;
            p->address = (p->command >> 6) & 0x01FFFFFFu;
            p->offset  = 0u;
            if (getenv("MGS_TRACE_EXI"))
                fprintf(stderr, "[ipl] %s 0x%08X (command 0x%08X)\n",
                        p->writing ? "write" : "read ", p->address, p->command);
        }
        return;
    }

    region = p->address;

    if (region == IPL_SRAM) {
        unsigned i = p->offset & 0x3Fu;
        if (p->writing) p->sram[i] = *byte;
        else            *byte = p->sram[i];
    } else if (region == IPL_RTC) {
        /* The counter, most significant byte first. */
        unsigned i = p->offset & 3u;
        if (p->writing) {
            uint32_t sh = 24u - i * 8u;
            p->rtc = (p->rtc & ~(0xFFu << sh)) | ((uint32_t)*byte << sh);
        } else {
            *byte = (uint8_t)(p->rtc >> (24u - i * 8u));
        }
    } else {
        /* The font ROM and the UART. Nothing has asked for either, and the
         * fonts are not ours to ship. */
        if (!p->writing) *byte = 0x00u;
    }

    ++p->offset;
}
