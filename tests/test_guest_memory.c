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

    /* A 26-bit hardware address back into the guest's space. MEM1 comes
     * back cached; an address past MEM1 with bit 25 set can only be the
     * second window, where the engine's .bss is - the display list at
     * 0x7F4AD180 whose FIFO base the SDK wrote as 0x3F4AD180 is the case
     * that was being dropped. And the round trip is exact across the whole
     * window, so the display list reaches the memory it was recorded into. */
    CHECK(guest_from_phys26(0x00DC2600u) == 0x80DC2600u);
    CHECK(guest_from_phys26(0x08DC2600u) == 0x80DC2600u);   /* junk top bits */
    CHECK(guest_from_phys26(0x3F4AD180u) == 0x7F4AD180u);
    CHECK(guest_from_phys26(0x034AD180u) == 0x7F4AD180u);
    {
        uint32_t a;
        for (a = GUEST_VMEM_BASE; a < GUEST_VMEM_BASE + GUEST_VMEM_SIZE; a += 0x1000u)
            if (guest_from_phys26(a & 0x3FFFFFFFu) != a) {
                CHECK(!"the second window does not round-trip");
                break;
            }
    }
    guest_write8(&m, guest_from_phys26(0x034AD180u), 0x5Au);
    CHECK(guest_ptr(&m, 0x7F4AD180u, 1) && *guest_ptr(&m, 0x7F4AD180u, 1) == 0x5Au);

    guest_memory_free(&m);
    printf(failures ? "%d failure(s)\n" : "all guest memory checks passed\n", failures);
    return failures != 0;
}
