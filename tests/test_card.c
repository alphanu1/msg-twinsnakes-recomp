/* Does a card we formatted survive the SDK's own inspection?
 *
 * The game answered that question with "The Memory Card in Slot A is damaged
 * and cannot be used", which is a slow way to find out. The checks the SDK
 * makes on a card's header are few and exactly specified, so they are made
 * here instead, against a freshly formatted image and with no game running.
 *
 * The serial is the interesting one. Every checksum can be right and the card
 * still be refused, because the SDK requires the serial to be derived from
 * the machine's flash id - which means a format is only valid for the machine
 * whose SRAM it was made against.
 */
#include "platform/exi_card.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static uint16_t rd16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* The SDK's checksum: a running sum of big-endian 16-bit words and a running
 * sum of their complements, with 0xFFFF stored as zero. */
static void checksum(const uint8_t* p, unsigned len, uint16_t* sum, uint16_t* inv)
{
    uint16_t s = 0u, v = 0u;
    unsigned i;
    for (i = 0u; i + 1u < len; i += 2u) {
        uint16_t w = rd16(p + i);
        s = (uint16_t)(s + w);
        v = (uint16_t)(v + (uint16_t)~w);
    }
    if (s == 0xFFFFu) s = 0u;
    if (v == 0xFFFFu) v = 0u;
    *sum = s; *inv = v;
}

int main(void)
{
    static const uint8_t flash_id[12] = { 0 };   /* what our SRAM reports */
    MgsExiCard card;
    const uint8_t* hdr;
    uint16_t sum, inv;

    if (!mgs_exi_card_init(&card, NULL, 16u, flash_id)) {
        printf("FAIL: no card\n");
        return 1;
    }
    hdr = card.image;

    /* Refused outright unless the device id is zero and the size in the
     * header matches the size the card itself reports over the bus. */
    CHECK(rd16(hdr + 0x20u) == 0u);
    CHECK(rd16(hdr + 0x22u) == 16u);

    /* The header's own checksum, over everything but the trailing pair. */
    checksum(hdr, 0x1FCu, &sum, &inv);
    CHECK(rd16(hdr + 0x1FCu) == sum);
    CHECK(rd16(hdr + 0x1FEu) == inv);

    /* THE SERIAL. Bytes 12 to 19 seed a generator; each of the first twelve
     * bytes must then equal the matching flash-id byte plus the next value it
     * produces. Written out here from the rule rather than from the code that
     * generates it, so that an arithmetic slip in one does not hide in both. */
    {
        int64_t rand = 0;
        unsigned i;
        int bad = 0;
        for (i = 0u; i < 8u; ++i)
            rand = (rand << 8) | hdr[12u + i];
        for (i = 0u; i < 12u; ++i) {
            rand = (rand * 1103515245 + 12345) >> 16;
            if (hdr[i] != (uint8_t)(flash_id[i] + (uint8_t)rand)) ++bad;
            rand = ((rand * 1103515245 + 12345) >> 16) & 0x7FFF;
        }
        if (bad) {
            printf("FAIL %s:%d: %d of 12 serial bytes do not match the "
                   "flash id; the SDK reads this card as damaged\n",
                   __FILE__, __LINE__, bad);
            ++failures;
        }
    }

    /* Both directory copies and both allocation tables carry a checksum pair
     * in their last four bytes, over everything before it. */
    {
        unsigned b;
        for (b = 1u; b <= 2u; ++b) {
            const uint8_t* d = card.image + b * MGS_CARD_SECTOR;
            checksum(d, MGS_CARD_SECTOR - 4u, &sum, &inv);
            CHECK(rd16(d + MGS_CARD_SECTOR - 4u) == sum);
            CHECK(rd16(d + MGS_CARD_SECTOR - 2u) == inv);
        }
        for (b = 3u; b <= 4u; ++b) {
            const uint8_t* f = card.image + b * MGS_CARD_SECTOR;
            checksum(f + 4u, MGS_CARD_SECTOR - 4u, &sum, &inv);
            CHECK(rd16(f) == sum);
            CHECK(rd16(f + 2u) == inv);
        }
    }

    /* DOES A READ RETURN THE BYTES WE WROTE?
     *
     * Every checksum above can be right and the card still read as damaged,
     * because what the SDK verifies is what comes back over the bus, not
     * what is in the file. The address a read command carries is packed
     * across four bytes - seven bits of offset, two more, then the page -
     * and a mistake there returns real bytes from the wrong place, which
     * fails every checksum exactly as a bad format would.
     *
     * So a read is driven here the way the SDK drives one: the command, four
     * address bytes, four the card ignores, then the data.
     */
    {
        static const uint32_t probes[] = { 0u, 0x200u, 8192u, 3u * 8192u, 0x1FC00u };
        unsigned k;
        for (k = 0; k < sizeof probes / sizeof probes[0]; ++k) {
            uint32_t addr = probes[k];
            uint8_t b;
            unsigned j;
            int wrong = 0;

            mgs_exi_card_select(&card, 1);
            b = 0x52u;                          mgs_exi_card_byte(&card, &b);
            b = (uint8_t)(addr >> 17);          mgs_exi_card_byte(&card, &b);
            b = (uint8_t)(addr >> 9);           mgs_exi_card_byte(&card, &b);
            b = (uint8_t)((addr >> 7) & 3u);    mgs_exi_card_byte(&card, &b);
            b = (uint8_t)(addr & 0x7Fu);        mgs_exi_card_byte(&card, &b);
            for (j = 0; j < 4u; ++j) { b = 0xFFu; mgs_exi_card_byte(&card, &b); }

            for (j = 0; j < 64u; ++j) {
                b = 0xFFu;
                mgs_exi_card_byte(&card, &b);
                if (b != card.image[addr + j]) ++wrong;
            }
            mgs_exi_card_select(&card, 0);

            if (wrong) {
                printf("FAIL %s:%d: reading 0x%06X returned %u of 64 bytes "
                       "from the wrong place\n", __FILE__, __LINE__, addr, wrong);
                ++failures;
            }
        }
    }

    mgs_exi_card_free(&card);
    {   /* THE "DONE" INTERRUPT (F380). The CARD library waits for it
         * after every erase and every page write; without it a save erased
         * the block map and never wrote it back. Command bytes are clocked
         * one at a time between select and deselect, as EXI does. */
        MgsExiCard c2;
        static const uint8_t on[2]    = { 0x81u, 0x01u };
        static const uint8_t off[2]   = { 0x81u, 0x00u };
        static const uint8_t erase[3] = { 0xF1u, 0x00u, 0x10u };   /* block 1 */
        uint8_t prog[5 + 128];
        unsigned k;
        #define SEND(buf, n) do { mgs_exi_card_select(&c2, 1); \
            for (k = 0; k < (n); ++k) { uint8_t b = (buf)[k]; mgs_exi_card_byte(&c2, &b); } \
            mgs_exi_card_select(&c2, 0); } while (0)
        CHECK(mgs_exi_card_init(&c2, NULL, 16u, flash_id));
        SEND(erase, 3u);
        CHECK(!c2.irq_pending);                 /* not asked for: silent */
        SEND(on, 2u);
        CHECK(c2.irq_enabled);
        SEND(erase, 3u);
        CHECK(c2.irq_pending);
        CHECK(c2.image[0x2000] == 0xFFu && c2.image[0x3FFF] == 0xFFu);
        c2.irq_pending = 0u;
        memset(prog, 0x5Au, sizeof prog);
        prog[0] = 0xF2u; prog[1] = 0x00u; prog[2] = 0x10u; prog[3] = 0x00u; prog[4] = 0x00u;
        SEND(prog, (unsigned)sizeof prog);
        CHECK(c2.irq_pending);
        CHECK(c2.image[0x2000] == 0x5Au && c2.image[0x207F] == 0x5Au);
        c2.irq_pending = 0u;
        SEND(off, 2u);
        SEND(erase, 3u);
        CHECK(!c2.irq_pending);
        #undef SEND
        mgs_exi_card_free(&c2);
    }

    printf(failures ? "card: FAILED\n" : "card: ok\n");
    return failures ? 1 : 0;
}
