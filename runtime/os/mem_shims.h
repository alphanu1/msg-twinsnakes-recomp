/* The guest-memory block moves, separated from the register unpacking.
 *
 * Exported because this is the half worth testing: the shims themselves only
 * read r3/r4/r5 and hand them on, while these two have to be right about
 * overlap, about which direction to copy, and about a range that straddles
 * the end of an address window - none of which is visible in a running game.
 * A copy that gets overlap wrong corrupts data and keeps going.
 */
#ifndef MGS_MEM_SHIMS_H
#define MGS_MEM_SHIMS_H

#include "../memory/guest.h"

/* memmove semantics, not memcpy: the guest's own implementation compares
 * source against destination and copies backwards when they overlap, and the
 * game is entitled to rely on it. */
void mgs_guest_memmove(GuestMemory* mem, uint32_t dst, uint32_t src,
                       uint32_t n);

void mgs_guest_memset(GuestMemory* mem, uint32_t dst, uint8_t v, uint32_t n);

#endif
