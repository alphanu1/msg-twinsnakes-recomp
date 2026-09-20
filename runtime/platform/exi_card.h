/* A memory card on the external interface.
 *
 * EMULATED AS A DEVICE, NOT AS AN API. Stubbing CARDMount and friends was
 * tried and cannot work (HANDOFF F166): the game calls the SDK's blocking
 * wrappers, and the state machine underneath reads the card's header,
 * directory and block-allocation table straight out of memory rather than
 * asking anyone. Answering the calls above it leaves the SDK looking at
 * bytes that are not there.
 *
 * Answering as the device instead means the SDK's own code runs unmodified
 * and is right about the format by construction - which also keeps the
 * translated/native boundary where the design document puts it, at the SDK's
 * public API, instead of reaching underneath it.
 */
#ifndef MGS_EXI_CARD_H
#define MGS_EXI_CARD_H

#include <stdint.h>

#define MGS_CARD_SECTOR 8192u

typedef struct MgsExiCard {
    uint8_t*  image;
    uint32_t  size;          /* bytes */
    uint32_t  card_id;       /* what the NintendoID command answers: size in Mbit */

    /* Transfer state. A command runs while chip select is asserted; the byte
     * index within it decides what each byte means. */
    unsigned  position;
    uint8_t   command;
    uint32_t  address;
    uint8_t   status;
    uint8_t   program[128];  /* a page, staged before it is committed */

    uint64_t  commands;      /* how many the device was ever given */
    int       dirty;
    char      path[512];
} MgsExiCard;

/* Loads `path` if it holds a card of the right size, and otherwise creates a
 * formatted one there. Returns 0 if no card could be provided, in which case
 * the interface reports an empty slot. */
/* `flash_id` is the machine's twelve-byte flash id, from SRAM. The card's
 * serial is generated from it, because the SDK refuses a card whose serial
 * does not match - see format_card. */
int  mgs_exi_card_init(MgsExiCard* c, const char* path, unsigned mbit,
                       const uint8_t* flash_id);
void mgs_exi_card_free(MgsExiCard* c);

/* Chip select. Deasserting ends whatever command was running, which is how
 * the protocol delimits them. */
void mgs_exi_card_select(MgsExiCard* c, int asserted);

/* One byte in each direction, as the bus does it. */
void mgs_exi_card_byte(MgsExiCard* c, uint8_t* byte);

/* Write the image back if anything changed. Cheap when nothing has. */
void mgs_exi_card_flush(MgsExiCard* c);

#endif
