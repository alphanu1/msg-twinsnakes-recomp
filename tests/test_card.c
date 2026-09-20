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

    mgs_exi_card_free(&card);
    printf(failures ? "card: FAILED\n" : "card: ok\n");
    return failures ? 1 : 0;
}
