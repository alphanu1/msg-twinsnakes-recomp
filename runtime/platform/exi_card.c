/* The memory card as an EXI device. See exi_card.h for why this layer.
 *
 * The protocol is the card's, not ours: a command byte while chip select is
 * asserted, then address bytes, then data, and the position within the
 * command decides what a byte means. The opcodes, the status bits and the
 * address packing were established from Dolphin's implementation of the same
 * device (THIRD_PARTY.md records the commit); the code here is our own.
 *
 * THE CARD IS CREATED ALREADY FORMATTED, deliberately. A blank card reads as
 * broken, and the game responds by offering to format it - which is another
 * screen needing a button press, and the whole reason this exists is to stop
 * needing one. The format is five reserved blocks: a header, two copies of
 * the directory and two of the block-allocation table, each protected by a
 * pair of checksums the SDK verifies on mount.
 */
#include "exi_card.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <direct.h>
#  define mgs_mkdir(p) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  define mgs_mkdir(p) mkdir((p), 0755)
#endif

/* Create the directory a card path sits in, so a first run does not fail
 * merely because nothing has written a save yet. */
static void ensure_dir_for(const char* path)
{
    char dir[512];
    size_t i, cut = 0u;
    for (i = 0u; path[i] && i < sizeof dir - 1u; ++i)
        if (path[i] == '/' || path[i] == '\\') cut = i;
    if (!cut) return;
    memcpy(dir, path, cut);
    dir[cut] = '\0';
    mgs_mkdir(dir);
}

/* Status bits the card reports. Only READY and UNLOCKED are ever set here:
 * there is no flash to be busy, and no error it can suffer. */
#define MC_STATUS_BUSY          0x80u
#define MC_STATUS_UNLOCKED      0x40u
#define MC_STATUS_SLEEP         0x20u
#define MC_STATUS_ERASE_ERROR   0x10u
#define MC_STATUS_PROGRAM_ERROR 0x08u
#define MC_STATUS_READY         0x01u

/* The command set. */
#define CMD_NINTENDO_ID    0x00u
#define CMD_READ_ARRAY     0x52u
#define CMD_ARRAY_TO_BUF   0x53u
#define CMD_SET_INTERRUPT  0x81u
#define CMD_WRITE_BUFFER   0x82u
#define CMD_READ_STATUS    0x83u
#define CMD_READ_ID        0x85u
#define CMD_READ_ERROR_BUF 0x86u
#define CMD_WAKE_UP        0x87u
#define CMD_SLEEP          0x88u
#define CMD_CLEAR_STATUS   0x89u
#define CMD_SECTOR_ERASE   0xF1u
#define CMD_PAGE_PROGRAM   0xF2u
#define CMD_EXTRA_BYTE     0xF3u
#define CMD_CHIP_ERASE     0xF4u

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

/* THE CARD'S CHECKSUM, WHICH THE SDK CHECKS ON EVERY MOUNT.
 *
 * A running sum of big-endian 16-bit words, and alongside it a sum of their
 * complements. A region whose two figures disagree with the stored pair is
 * treated as damaged - which is how a plausible-looking but wrong format
 * presents: not as garbage, but as a card the game politely offers to erase.
 * 0xFFFF is stored as 0 because the hardware's adder cannot produce it.
 */
static void card_checksum(const uint8_t* p, unsigned len,
                          uint16_t* sum, uint16_t* inv)
{
    unsigned i;
    uint16_t s = 0u, v = 0u;
    for (i = 0u; i + 1u < len; i += 2u) {
        uint16_t w = (uint16_t)((p[i] << 8) | p[i + 1u]);
        s = (uint16_t)(s + w);
        v = (uint16_t)(v + (uint16_t)~w);
    }
    if (s == 0xFFFFu) s = 0u;
    if (v == 0xFFFFu) v = 0u;
    *sum = s; *inv = v;
}

/* Offsets inside the reserved blocks. */
#define HDR_CHECKSUM      0x01FCu   /* covers 0x0000..0x01FB */
#define DIR_ENTRIES       (127u * 64u)
#define DIR_UPDATE        0x1FFAu
#define DIR_CHECKSUM      0x1FFCu   /* covers 0x0000..0x1FFB */
#define BAT_CHECKSUM      0x0000u   /* covers 0x0004..0x1FFF */
#define BAT_UPDATE        0x0004u
#define BAT_FREE          0x0006u
#define BAT_LAST_ALLOC    0x0008u

static void format_card(MgsExiCard* c, unsigned mbit, const uint8_t* flash_id)
{
    unsigned total_blocks = c->size / MGS_CARD_SECTOR;
    uint8_t* hdr = c->image;
    uint8_t* dir;
    uint8_t* bat;
    uint16_t sum, inv;
    unsigned i;

    memset(c->image, 0xFF, c->size);

    /* Block 0 - the header, and the reason a plausible one still reads as
     * damaged.
     *
     * The layout is serial[32], then deviceID, size and encode, then padding
     * out to a checksum pair in the last four bytes. Getting those right is
     * necessary and not sufficient: the SDK also demands that the serial be
     * DERIVED FROM THE MACHINE'S FLASH ID, which lives in SRAM. Each of the
     * first twelve bytes must equal the corresponding flash-ID byte plus a
     * value from a linear congruential generator seeded by bytes 12 to 19 of
     * the serial itself. An arbitrary serial passes every checksum and is
     * still refused, which is exactly what the game reported: "The Memory
     * Card in Slot A is damaged and cannot be used."
     *
     * So the serial is generated here the same way the SDK generates it when
     * it formats a card, against the flash ID this machine reports.
     */
    memset(hdr, 0x00, 0x200u);
    {
        int64_t rand = 0x1234;      /* the seed, stored in serial[12..19] */
        unsigned k;
        for (k = 0; k < 8u; ++k)
            hdr[12u + k] = (uint8_t)((uint64_t)rand >> (56u - k * 8u));
        for (k = 0; k < 12u; ++k) {
            uint8_t flash = flash_id ? flash_id[k] : 0u;
            rand = (rand * 1103515245 + 12345) >> 16;
            hdr[k] = (uint8_t)(flash + (uint8_t)rand);
            rand = ((rand * 1103515245 + 12345) >> 16) & 0x7FFF;
        }
    }
    put16(hdr + 0x20u, 0u);              /* device id: 0, or it reads broken */
    put16(hdr + 0x22u, (uint16_t)mbit);  /* size in megabits, checked against
                                          * the id the card itself reports   */
    put16(hdr + 0x24u, 0u);              /* encoding: ASCII                  */
    card_checksum(hdr, HDR_CHECKSUM, &sum, &inv);
    put16(hdr + HDR_CHECKSUM, sum);
    put16(hdr + HDR_CHECKSUM + 2u, inv);

    /* Blocks 1 and 2 - the directory, and its spare copy. An unused entry is
     * all-ones, which is what a freshly erased card holds. Both copies are
     * written: the SDK picks whichever has the higher update counter and
     * repairs the other from it, so leaving one blank invites it to decide
     * the card is damaged. */
    for (i = 1u; i <= 2u; ++i) {
        dir = c->image + i * MGS_CARD_SECTOR;
        memset(dir, 0xFF, MGS_CARD_SECTOR);
        memset(dir + DIR_ENTRIES, 0x00, MGS_CARD_SECTOR - DIR_ENTRIES);
        put16(dir + DIR_UPDATE, (uint16_t)(i - 1u));
        card_checksum(dir, DIR_CHECKSUM, &sum, &inv);
        put16(dir + DIR_CHECKSUM, sum);
        put16(dir + DIR_CHECKSUM + 2u, inv);
    }

    /* Blocks 3 and 4 - the block-allocation table, and its spare. Zero means
     * free, so the map is zeroed rather than erased to ones. Five blocks are
     * reserved: this table, its copy, the two directories and the header. */
    for (i = 3u; i <= 4u; ++i) {
        bat = c->image + i * MGS_CARD_SECTOR;
        memset(bat, 0x00, MGS_CARD_SECTOR);
        put16(bat + BAT_UPDATE, (uint16_t)(i - 3u));
        put16(bat + BAT_FREE, (uint16_t)(total_blocks - 5u));
        put16(bat + BAT_LAST_ALLOC, 4u);
        card_checksum(bat + 4u, MGS_CARD_SECTOR - 4u, &sum, &inv);
        put16(bat + BAT_CHECKSUM, sum);
        put16(bat + BAT_CHECKSUM + 2u, inv);
    }

    c->dirty = 1;
}

/* IS THIS IMAGE ONE THIS MACHINE CAN USE?
 *
 * A card file survives between runs, so a stale or foreign one is loaded in
 * preference to a good one - and an invalid image presents as the game
 * calling the card damaged, not as an error here. That cost an hour of
 * debugging a file while reading the code, so the load is checked rather
 * than trusted: the size, the header's checksum, and the serial against this
 * machine's flash id, which is what the SDK itself will check.
 */
static int card_image_valid(const MgsExiCard* c, unsigned mbit,
                            const uint8_t* flash_id)
{
    const uint8_t* hdr = c->image;
    uint16_t sum, inv;
    int64_t rand = 0;
    unsigned i;

    if (((hdr[0x20u] << 8) | hdr[0x21u]) != 0u) return 0;
    if (((hdr[0x22u] << 8) | hdr[0x23u]) != (int)mbit) return 0;

    card_checksum(hdr, HDR_CHECKSUM, &sum, &inv);
    if (((hdr[HDR_CHECKSUM] << 8) | hdr[HDR_CHECKSUM + 1u]) != sum) return 0;
    if (((hdr[HDR_CHECKSUM + 2u] << 8) | hdr[HDR_CHECKSUM + 3u]) != inv) return 0;

    for (i = 0u; i < 8u; ++i) rand = (rand << 8) | hdr[12u + i];
    for (i = 0u; i < 12u; ++i) {
        uint8_t flash = flash_id ? flash_id[i] : 0u;
        rand = (rand * 1103515245 + 12345) >> 16;
        if (hdr[i] != (uint8_t)(flash + (uint8_t)rand)) return 0;
        rand = ((rand * 1103515245 + 12345) >> 16) & 0x7FFF;
    }
    return 1;
}

int mgs_exi_card_init(MgsExiCard* c, const char* path, unsigned mbit,
                      const uint8_t* flash_id)
{
    FILE* f;
    long have = 0;

    memset(c, 0, sizeof *c);
    c->size    = mbit * 1024u * 1024u / 8u;
    c->card_id = mbit;
    /* The power-on status a real card reports, bit for bit: BUSY is set as
     * well as READY and UNLOCKED. Leaving BUSY out gave 0x41 where hardware
     * gives 0xC1, and 0x41 is what the SDK read immediately before it
     * abandoned the mount with an I/O error. */
    c->status  = MC_STATUS_BUSY | MC_STATUS_READY | MC_STATUS_UNLOCKED;
    c->image   = (uint8_t*)malloc(c->size);
    if (!c->image) return 0;

    if (path) { strncpy(c->path, path, sizeof c->path - 1u); }

    if (c->path[0]) ensure_dir_for(c->path);

    f = c->path[0] ? fopen(c->path, "rb") : NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        have = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (have == (long)c->size && fread(c->image, 1u, c->size, f) == c->size) {
            fclose(f);
            if (card_image_valid(c, mbit, flash_id)) {
                fprintf(stderr, "[card] loaded %s (%u Mbit)\n", c->path, mbit);
                return 1;
            }
            fprintf(stderr, "[card] %s is not a card this machine can read "
                            "- reformatting\n", c->path);
        } else {
            fclose(f);
            fprintf(stderr, "[card] %s is %ld bytes, not %u - reformatting\n",
                    c->path, have, c->size);
        }
    }

    format_card(c, mbit, flash_id);
    mgs_exi_card_flush(c);
    fprintf(stderr, "[card] formatted a new %u Mbit card at %s\n",
            mbit, c->path[0] ? c->path : "(memory only)");
    return 1;
}

void mgs_exi_card_free(MgsExiCard* c)
{
    if (!c) return;
    mgs_exi_card_flush(c);
    free(c->image);
    c->image = NULL;
}

void mgs_exi_card_flush(MgsExiCard* c)
{
    FILE* f;
    if (!c || !c->dirty || !c->image || !c->path[0]) return;
    f = fopen(c->path, "wb");
    if (!f) {
        fprintf(stderr, "[card] cannot write %s; saves will not persist\n", c->path);
        c->dirty = 0;
        return;
    }
    fwrite(c->image, 1u, c->size, f);
    fclose(f);
    c->dirty = 0;
}

void mgs_exi_card_select(MgsExiCard* c, int asserted)
{
    if (!c) return;
    if (!asserted) {
        /* Deassertion ends the command. A page program commits here, which is
         * what makes the length of the write the guest's business rather than
         * ours: it clocks out as many bytes as it likes and lets go. */
        if (c->command == CMD_PAGE_PROGRAM && c->position >= 5u) {
            unsigned n = c->position - 5u;
            if (n > sizeof c->program) n = sizeof c->program;
            if (c->address + n <= c->size) {
                memcpy(c->image + c->address, c->program, n);
                c->dirty = 1;
            }
        }
        c->position = 0u;
        c->command  = 0u;
    }
}

void mgs_exi_card_byte(MgsExiCard* c, uint8_t* byte)
{
    uint8_t in;

    if (!c || !c->image || !byte) return;
    in = *byte;

    if (c->position == 0u) {
        static unsigned traced;
        if (getenv("MGS_TRACE_CARD") && traced < 40u) {
            ++traced;
            fprintf(stderr, "[card] command 0x%02X\n", in);
        }
        ++c->commands;
        c->command = in;
        c->address = 0u;
        *byte = 0xFFu;
        if (c->command == CMD_CLEAR_STATUS) {
            c->status &= (uint8_t)~(MC_STATUS_PROGRAM_ERROR | MC_STATUS_ERASE_ERROR);
            c->status |= MC_STATUS_READY;
            ++c->position;
            return;
        }
        ++c->position;
        return;
    }

    switch (c->command) {
    case CMD_NINTENDO_ID:
        /* One dummy cycle, then the identity, repeating. The SDK divides it
         * to get the card's size in megabits. */
        *byte = (c->position == 1u)
              ? 0x80u
              : (uint8_t)(c->card_id >> (24u - (((c->position - 2u) & 3u) * 8u)));
        break;

    case CMD_READ_ID:
        *byte = (uint8_t)((c->position & 1u) ? c->card_id : (c->card_id >> 8));
        break;

    case CMD_READ_STATUS:
        *byte = c->status;
        break;

    case CMD_READ_ARRAY:
        /* Four address bytes, then four the card ignores, then data. The
         * address is packed oddly because the card is addressed by page and
         * byte-within-page rather than linearly. */
        switch (c->position) {
        case 1u: c->address  = (uint32_t)in << 17; *byte = 0xFFu; break;
        case 2u: c->address |= (uint32_t)in << 9;  break;
        case 3u: c->address |= (uint32_t)(in & 3u) << 7; break;
        case 4u: c->address |= (uint32_t)(in & 0x7Fu); break;
        default: break;
        }
        if (c->position == 4u && getenv("MGS_TRACE_CARD")) {
            /* WHICH BLOCK, AND IS IT THE ONE ASKED FOR?
             *
             * A mount reads the five system blocks, a 512-byte page at a
             * time. If the address packing here is wrong the SDK gets real
             * bytes from the wrong place, every checksum fails, and the game
             * reports a damaged card - which is indistinguishable from a
             * badly formatted one unless the addresses are visible. */
            static unsigned n;
            if (n++ < 24u)
                fprintf(stderr, "[card] read block %u page %u (address 0x%06X)\n",
                        c->address / MGS_CARD_SECTOR,
                        (c->address % MGS_CARD_SECTOR) / 512u, c->address);
        }
        if (c->position > 1u) {
            uint32_t a = c->address & (c->size - 1u);
            *byte = c->image[a];
            /* The byte address wraps inside its 512-byte page rather than
             * running on into the next one. */
            if (c->position >= 9u)
                c->address = (c->address & ~0x1FFu) | ((c->address + 1u) & 0x1FFu);
        }
        break;

    case CMD_PAGE_PROGRAM:
        switch (c->position) {
        case 1u: c->address  = (uint32_t)in << 17; break;
        case 2u: c->address |= (uint32_t)in << 9;  break;
        case 3u: c->address |= (uint32_t)(in & 3u) << 7; break;
        case 4u: c->address |= (uint32_t)(in & 0x7Fu); break;
        default: break;
        }
        if (c->position >= 5u)
            c->program[(c->position - 5u) & 0x7Fu] = in;
        *byte = 0xFFu;
        break;

    case CMD_SECTOR_ERASE:
        switch (c->position) {
        case 1u: c->address  = (uint32_t)in << 17; break;
        case 2u: c->address |= (uint32_t)in << 9;  break;
        default: break;
        }
        if (c->position == 2u) {
            uint32_t a = c->address & (c->size - 1u);
            uint32_t n = MGS_CARD_SECTOR;
            if (a + n <= c->size) { memset(c->image + a, 0xFF, n); c->dirty = 1; }
        }
        *byte = 0xFFu;
        break;

    case CMD_SET_INTERRUPT:
    case CMD_WAKE_UP:
    case CMD_SLEEP:
    case CMD_CHIP_ERASE:
    case CMD_EXTRA_BYTE:
    default:
        *byte = 0xFFu;
        break;
    }

    ++c->position;
}
