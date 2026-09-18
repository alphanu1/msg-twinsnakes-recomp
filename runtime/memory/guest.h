/* Guest memory: one flat block, every access byte-swapped.
 *
 * The Gekko is big-endian and both hosts are little-endian, so EVERY load and
 * store swaps. That is not a detail to optimise away later - it is the single
 * most common source of silent corruption in a port like this, because a
 * wrong-endian read usually produces a plausible number rather than a crash.
 *
 * Guest pointers are 32-bit offsets into this block, never host pointers.
 * Structures the SDK shims read out of guest memory - GX display lists, OS
 * thread blocks, DVD file info - are read through the accessors below and
 * NEVER cast to a host struct. A host struct has host endianness and host
 * padding; the guest's does not.
 */
#ifndef MGS_GUEST_H
#define MGS_GUEST_H

#include <stdint.h>
#include <string.h>

/* MEM1. The GameCube has 24 MB, mirrored uncached at 0xC0000000. */
#define GUEST_RAM_BASE      0x80000000u
#define GUEST_RAM_UNCACHED  0xC0000000u
#define GUEST_RAM_SIZE      (24u * 1024u * 1024u)

/* ARAM is a separate 16 MB store the DSP can reach; it is not addressable by
 * the CPU, so it is deliberately not part of this block.
 */
#define GUEST_ARAM_SIZE     (16u * 1024u * 1024u)

typedef struct GuestMemory {
    uint8_t* ram;       /* GUEST_RAM_SIZE bytes */
    uint8_t* aram;      /* GUEST_ARAM_SIZE bytes */
} GuestMemory;

int  guest_memory_init(GuestMemory* m);
void guest_memory_free(GuestMemory* m);

/* Address to host pointer. Accepts cached and uncached aliases, because the
 * game uses both for the same memory and an accessor that only understood one
 * would fail on DMA and display-list addresses specifically.
 */
static inline uint8_t* guest_ptr(const GuestMemory* m, uint32_t addr, uint32_t size)
{
    uint32_t off = addr & 0x3FFFFFFFu;          /* fold 0x8... and 0xC... */
    if (off > GUEST_RAM_SIZE || size > GUEST_RAM_SIZE - off)
        return NULL;
    return m->ram + off;
}

static inline uint32_t guest_read32(const GuestMemory* m, uint32_t addr)
{
    const uint8_t* p = guest_ptr(m, addr, 4);
    if (!p) return 0u;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline uint16_t guest_read16(const GuestMemory* m, uint32_t addr)
{
    const uint8_t* p = guest_ptr(m, addr, 2);
    if (!p) return 0u;
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint8_t guest_read8(const GuestMemory* m, uint32_t addr)
{
    const uint8_t* p = guest_ptr(m, addr, 1);
    return p ? *p : 0u;
}

static inline void guest_write32(GuestMemory* m, uint32_t addr, uint32_t v)
{
    uint8_t* p = guest_ptr(m, addr, 4);
    if (!p) return;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static inline void guest_write16(GuestMemory* m, uint32_t addr, uint16_t v)
{
    uint8_t* p = guest_ptr(m, addr, 2);
    if (!p) return;
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static inline void guest_write8(GuestMemory* m, uint32_t addr, uint8_t v)
{
    uint8_t* p = guest_ptr(m, addr, 1);
    if (p) *p = v;
}

/* Floats go through the integer path so the swap is explicit and there is no
 * chance of the compiler reading a host-endian float out of guest memory.
 */
static inline float guest_read_f32(const GuestMemory* m, uint32_t addr)
{
    uint32_t bits = guest_read32(m, addr);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

static inline void guest_write_f32(GuestMemory* m, uint32_t addr, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    guest_write32(m, addr, bits);
}

#endif
