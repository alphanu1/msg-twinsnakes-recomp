/* Reading the engine's own heaps.
 *
 * The game panics at `memory.c:1197` because an allocation from its heap 2
 * returned NULL. Everything needed to see why is in guest memory: the engine
 * keeps a table of heap descriptors, and each one records where its memory
 * starts, how large it is, how much is free, and the head of its free list.
 *
 * The addresses come from reading the engine's own code, not from guessing:
 *
 *   fn_1_F4988   mulli r7, r3, 0x2c          heap index * 44
 *                lis/addi r3, lbl_1_bss_24AD8   the table's base
 *                lwz r7, 0x8(r5)             freeList, at +0x08
 *   fn_1_F48B8   stw r29, 0x24(r31)          size,     at +0x24
 *                stw r29, 0x28(r31)          free,     at +0x28
 *
 * The table is in the overlay's .bss, which is allocated at runtime - so its
 * address is whatever OSLink was handed, which the host watches for. Without
 * that, these offsets name a structure the host cannot find.
 */
#include "module.h"

#include <stdio.h>

/* From the engine's disassembly; see the comment above. */
#define HEAP_TABLE_OFF   0x24AD8u
#define HEAP_STRIDE      0x2Cu
#define HEAP_FREELIST    0x08u
#define HEAP_SIZE        0x24u
#define HEAP_FREE        0x28u
#define HEAP_COUNT       8u

/* A free-list block's header, from the allocator's own walk: it reads the
 * block size at +0x1C and the next pointer at +0x00, and the payload starts
 * 0x20 bytes in. */
#define BLOCK_NEXT       0x00u
#define BLOCK_SIZE       0x1Cu
#define BLOCK_HEADER     0x20u

/* Where the RECOMPILED overlay keeps the table.
 *
 * Not the game's allocated .bss: the recompiled code resolves its own global
 * addresses at recompile time, so the engine's heaps live in the image at
 * 0x7F4BE678 - read straight out of the generated code for fn_1_F4988, which
 * forms it as `lis 32588` then `addi -6536`. Expressed relative to the module
 * base so it follows the overlay wherever it is loaded. */
#define HEAP_TABLE_IN_IMAGE 0x4B6678u

void mgs_dump_heaps(void* cpu, uint32_t rel_bss);
void mgs_dump_heaps(void* cpu, uint32_t rel_bss)
{
    uint32_t table = rel_bss + HEAP_TABLE_OFF;
    unsigned i;

    printf("engine heaps (table at 0x%08X, from overlay .bss 0x%08X):\n",
           table, rel_bss);

    for (i = 0; i < HEAP_COUNT; ++i) {
        uint32_t base = table + i * HEAP_STRIDE;
        uint32_t list = mgs_module_guest_read32(cpu, base + HEAP_FREELIST);
        uint32_t size = mgs_module_guest_read32(cpu, base + HEAP_SIZE);
        uint32_t free = mgs_module_guest_read32(cpu, base + HEAP_FREE);
        uint32_t walk = list, largest = 0, total = 0;
        unsigned blocks = 0;

        if (!size && !list) continue;        /* never created */

        /* Walk the free list. The largest single block is what actually
         * decides whether an allocation can be satisfied - a heap with 8 MB
         * free in 4,000 fragments cannot answer a 64 KB request, and
         * "free" alone would say it could. */
        while (walk && blocks < 100000u) {
            uint32_t bsz = mgs_module_guest_read32(cpu, walk + BLOCK_SIZE) & 0x07FFFFFFu;
            if (bsz > largest) largest = bsz;
            total += bsz;
            ++blocks;
            walk = mgs_module_guest_read32(cpu, walk + BLOCK_NEXT);
        }

        printf("  heap %u: size %8u  free %8u  list 0x%08X  "
               "%u block%s, largest %u\n",
               i, size, free, list, blocks, blocks == 1u ? "" : "s", largest);
        if (blocks >= 100000u)
            printf("           (free list did not terminate - corrupt)\n");
    }
}

/* ---- the overlay's .bss ------------------------------------------------
 *
 * THE RECOMPILED OVERLAY AND THE GAME DISAGREE ABOUT WHERE .bss IS, and both
 * are right about their own world.
 *
 * The game loads a REL *file*. Its loaded sections end at 0x491B7C, and the
 * rest of the file - another 925 KB - is relocation tables. `.bss` is not in
 * the file at all: the game allocates it separately and hands the pointer to
 * `OSLink`, which relocates every reference in ITS copy to point there.
 *
 * DolRecomp compiles the overlay at a fixed base and resolves those same
 * references itself, placing `.bss` immediately after `.data` - which is where
 * a statically linked module's bss would be, and which here is **on top of the
 * relocation tables**. So every engine global the recompiled code touches
 * lands at 0x7F499BA0 and up, reading relocation data instead of zero.
 *
 * That is why the engine's heap table was empty: `fn_1_F43E0` did run and did
 * write the descriptors, into memory that was never zero to begin with and
 * that the allocator then read as a corrupt free list.
 *
 * The fix is what `.bss` means. Once `OSLink` has consumed the relocation
 * tables they are dead, and zeroing them gives the recompiled overlay the
 * zero-initialised globals it is entitled to. The range is read from the
 * module's own section table in guest memory rather than assumed, so it
 * follows the disc rather than a constant in this file.
 */
#define REL_NUM_SECTIONS   0x0Cu
#define REL_SECTION_INFO   0x10u
#define REL_BSS_SIZE       0x20u

void mgs_clear_overlay_bss(void* cpu, uint32_t module);
void mgs_clear_overlay_bss(void* cpu, uint32_t module)
{
    uint32_t count = mgs_module_guest_read32(cpu, module + REL_NUM_SECTIONS);
    uint32_t info  = mgs_module_guest_read32(cpu, module + REL_SECTION_INFO);
    uint32_t bss   = mgs_module_guest_read32(cpu, module + REL_BSS_SIZE);
    uint32_t last = 0, i;

    /* AFTER LINKING, THE HEADER'S OFFSETS ARE ADDRESSES. `OSLink` rewrites
     * `sectionInfoOffset` and every section's offset in place, turning each
     * into an absolute pointer. This runs after linking - that is the whole
     * point of it - so adding the module base a second time overflows, and
     * the reads that follow return zero from nowhere in particular.
     *
     * Both forms are accepted rather than one assumed, because which one is
     * present depends on exactly when this is called. */
    if (info < 0x1000000u) info += module;
    if (!count || count > 64u || !bss) {
        printf("overlay .bss: module header not readable; not cleared\n");
        return;
    }

    for (i = 0; i < count; ++i) {
        uint32_t off = mgs_module_guest_read32(cpu, info + i * 8u) & ~3u;
        uint32_t size = mgs_module_guest_read32(cpu, info + i * 8u + 4u);
        if (off && off < 0x1000000u) off += module;      /* not yet relocated */

        /* ONLY SECTIONS INSIDE THE IMAGE. After linking, the .bss section's
         * entry points at the memory the GAME allocated for it - somewhere
         * else entirely - and taking the maximum over every section then
         * picks that instead of the image's end. The region wanted here is
         * the image's own tail, where the recompiled overlay put its bss on
         * top of the relocation tables. */
        if (off < module || off >= module + 0x01000000u) continue;
        if (size && off + size > last) last = off + size;
    }
    if (!last) {
        printf("overlay .bss: no loaded sections found; not cleared\n");
        return;
    }

    /* From the end of the loaded sections, far enough to cover .bss wherever
     * the recompiler aligned it. Everything in this span is relocation data
     * the linker has already used. */
    {
        uint32_t from = last;
        uint32_t span = bss + 0x8000u;
        uint32_t k;
        for (k = 0; k < span; k += 4u)
            mgs_module_guest_write32(cpu, from + k, 0u);
        printf("overlay .bss: cleared 0x%08X + 0x%X "
               "(relocation tables, dead after linking)\n", from, span);
    }
}
