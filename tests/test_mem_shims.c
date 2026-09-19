/* The native block moves, against the behaviour the guest's own code has.
 *
 * Worth testing directly rather than only in a running game, because every
 * way these can be wrong is quiet. A copy that mishandles overlap does not
 * crash - it writes plausible bytes and the game carries on with corrupt
 * data, and the symptom arrives somewhere else entirely. A copy that walks
 * off the end of an address window does crash, but only for the one input
 * that straddles it, which a boot may not produce for hours.
 *
 * The reference is the guest's implementation at 0x8000519C, which compares
 * source against destination and copies backwards when the source is lower -
 * memmove semantics under a memcpy name.
 */
#include "memory/guest.h"
#include "os/mem_shims.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static void fill_pattern(GuestMemory* m, uint32_t at, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; ++i) guest_write8(m, at + i, (uint8_t)(i & 0xFFu));
}

static int pattern_ok(GuestMemory* m, uint32_t at, uint32_t n, uint32_t first)
{
    uint32_t i;
    for (i = 0; i < n; ++i)
        if (guest_read8(m, at + i) != (uint8_t)((first + i) & 0xFFu)) return 0;
    return 1;
}

int main(void)
{
    GuestMemory m;
    uint32_t i;

    CHECK(guest_memory_init(&m));

    /* A plain, non-overlapping move. */
    fill_pattern(&m, 0x80100000u, 4096u);
    mgs_guest_memmove(&m, 0x80200000u, 0x80100000u, 4096u);
    CHECK(pattern_ok(&m, 0x80200000u, 4096u, 0u));

    /* OVERLAP, DESTINATION ABOVE SOURCE. This is the case the guest handles
     * by copying backwards from the end; a forward copy would smear the
     * first bytes over the whole range. */
    fill_pattern(&m, 0x80300000u, 1024u);
    mgs_guest_memmove(&m, 0x80300000u + 16u, 0x80300000u, 1024u);
    CHECK(pattern_ok(&m, 0x80300000u + 16u, 1024u, 0u));

    /* OVERLAP, DESTINATION BELOW SOURCE - copied forwards. */
    fill_pattern(&m, 0x80400000u, 1024u);
    mgs_guest_memmove(&m, 0x80400000u, 0x80400000u + 16u, 1008u);
    CHECK(pattern_ok(&m, 0x80400000u, 1008u, 16u));

    /* Zero length must touch nothing. */
    guest_write8(&m, 0x80500000u, 0xAB);
    mgs_guest_memmove(&m, 0x80500000u, 0x80100000u, 0u);
    CHECK(guest_read8(&m, 0x80500000u) == 0xAB);

    /* The fill takes the LOW BYTE only - the guest's __fill_mem does
     * `clrlwi r4, r4, 24` before using it. A shim that stored the whole word
     * would be wrong for every caller passing a sign-extended value. */
    mgs_guest_memset(&m, 0x80600000u, 0x5A, 256u);
    for (i = 0; i < 256u; ++i) CHECK(guest_read8(&m, 0x80600000u + i) == 0x5A);
    CHECK(guest_read8(&m, 0x80600000u + 256u) != 0x5A ||
          guest_read8(&m, 0x80600000u + 256u) == 0x00);

    /* THE SECOND WINDOW, which is where the overlay lives and where the copy
     * that made this worth doing at all lands. */
    fill_pattern(&m, GUEST_VMEM_BASE + 0x1000u, 4096u);
    mgs_guest_memmove(&m, GUEST_VMEM_BASE + 0x8000u,
                      GUEST_VMEM_BASE + 0x1000u, 4096u);
    CHECK(pattern_ok(&m, GUEST_VMEM_BASE + 0x8000u, 4096u, 0u));

    /* ACROSS the two windows - MEM1 to the second window and back. Each side
     * resolves to a different allocation, so a host-pointer fast path can
     * only be taken for one of them at a time. */
    fill_pattern(&m, 0x80700000u, 2048u);
    mgs_guest_memmove(&m, GUEST_VMEM_BASE + 0x20000u, 0x80700000u, 2048u);
    CHECK(pattern_ok(&m, GUEST_VMEM_BASE + 0x20000u, 2048u, 0u));
    mgs_guest_memmove(&m, 0x80800000u, GUEST_VMEM_BASE + 0x20000u, 2048u);
    CHECK(pattern_ok(&m, 0x80800000u, 2048u, 0u));

    /* STRADDLING THE END OF A WINDOW. guest_ptr refuses a range that does not
     * fit, so this must take the byte-at-a-time path rather than running off
     * the end of the allocation. Nothing is checked about the contents
     * outside the window - the point is that it returns at all. */
    mgs_guest_memset(&m, GUEST_VMEM_BASE + GUEST_VMEM_SIZE - 64u, 0x11, 128u);
    for (i = 0; i < 64u; ++i)
        CHECK(guest_read8(&m, GUEST_VMEM_BASE + GUEST_VMEM_SIZE - 64u + i) == 0x11);

    guest_memory_free(&m);
    printf("%s: %d failure%s\n", failures ? "FAIL" : "ok",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
