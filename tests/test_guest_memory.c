/* Byte-swap accessors.
 *
 * Worth testing first and properly: a wrong-endian access does not crash, it
 * returns a plausible number, so this class of bug surfaces hours later as a
 * divergence from Dolphin rather than as a failure here.
 */
#include "memory/guest.h"
#include <stdio.h>
#include <assert.h>

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

int main(void)
{
    GuestMemory m;
    CHECK(guest_memory_init(&m));

    /* A value whose bytes are all different, so a swap that half-works fails. */
    guest_write32(&m, 0x80001000u, 0x11223344u);
    CHECK(guest_read32(&m, 0x80001000u) == 0x11223344u);

    /* Stored big-endian in the block, regardless of host order. */
    CHECK(m.ram[0x1000] == 0x11);
    CHECK(m.ram[0x1001] == 0x22);
    CHECK(m.ram[0x1002] == 0x33);
    CHECK(m.ram[0x1003] == 0x44);

    /* 16 and 8 bit read back consistently from the same bytes. */
    CHECK(guest_read16(&m, 0x80001000u) == 0x1122u);
    CHECK(guest_read16(&m, 0x80001002u) == 0x3344u);
    CHECK(guest_read8(&m, 0x80001003u) == 0x44u);

    /* The uncached alias is the same memory. The game uses both for one
     * buffer, and an accessor that understood only the cached form would fail
     * on display lists and DMA specifically.
     */
    CHECK(guest_read32(&m, 0xC0001000u) == 0x11223344u);
    guest_write32(&m, 0xC0001000u, 0xAABBCCDDu);
    CHECK(guest_read32(&m, 0x80001000u) == 0xAABBCCDDu);

    /* Floats go through the integer path, so they swap too. */
    guest_write_f32(&m, 0x80002000u, 1.0f);
    CHECK(guest_read32(&m, 0x80002000u) == 0x3F800000u);
    CHECK(guest_read_f32(&m, 0x80002000u) == 1.0f);

    /* Out of range reads return 0 and writes are dropped, rather than walking
     * off the block. A guest pointer is not trusted.
     */
    CHECK(guest_read32(&m, 0x80000000u + GUEST_RAM_SIZE) == 0u);
    CHECK(guest_ptr(&m, 0x80000000u + GUEST_RAM_SIZE - 2u, 4) == NULL);

    guest_memory_free(&m);
    printf(failures ? "%d failure(s)\n" : "all guest memory checks passed\n", failures);
    return failures != 0;
}
