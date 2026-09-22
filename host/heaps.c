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
#include <stdlib.h>

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

/* ---- the engine's task table -------------------------------------------
 *
 * WHY THIS EXISTS. After the Konami logo the game keeps running a frame loop
 * - PADRead, VIWaitForRetrace, DVDGetDriveStatus all tick over - but issues
 * no further GX commands at all. Following the call graph outwards from the
 * main loop does not explain it, and cannot: the main loop is two calls, one
 * of which clears a flag, and the other of which is a SCHEDULER. Everything
 * the game does per frame is reached through a function pointer, so no static
 * analysis of `bl` targets will ever reach the renderer.
 *
 * The scheduler is fn_1_F394C, and its structure is all in its own code:
 *
 *   lis/addi r30, lbl_1_bss_23708    the table of level heads
 *   li r28, 0xc                      twelve priority levels
 *   addi r29, r29, 0x44              each level head is 0x44 bytes
 *   lwz r3, 0x40(r29) ; and. r0, r3, <mask at lbl_1_bss_23A38>
 *                                    a per-level enable mask, AND'd with a
 *                                    global one; non-zero SKIPS the level
 *   lwz r3, 0x0(r29)                 the level's first node
 *   lwz r27, 0x0(r3)                 the next node
 *   lwz r0, 0x8(r3) ; rlwinm. 0,12,15   flag bits 12..15; non-zero skips
 *   lwz r12, 0x4(r3) ; mtctr ; bctrl    the node's function - the work
 *
 * and fn_1_F3A20 links a node in, confirming the 0x44 stride and that +0x00
 * is the list pointer and +0x04 the function.
 *
 * So dumping this table says, directly, which tasks exist, which are gated
 * off, and what each one would call. That turns "the renderer stopped" into
 * a specific question about a specific node.
 */
#define TASK_TABLE_OFF   0x23708u
#define TASK_MASK_OFF    0x23A38u
#define TASK_LEVELS      12u
#define TASK_LEVEL_SIZE  0x44u
#define TASK_LEVEL_HEAD  0x00u
#define TASK_LEVEL_GATE  0x40u
#define TASK_NEXT        0x00u
#define TASK_FUNC        0x04u
#define TASK_FLAGS       0x08u

void mgs_dump_tasks(void* cpu, uint32_t rel_bss);
void mgs_dump_tasks(void* cpu, uint32_t rel_bss)
{
    const char* dump_env = getenv("MGS_TASK_DUMP");
    uint32_t dump_fn = dump_env ? (uint32_t)strtoul(dump_env, NULL, 0) : 0u;
    uint32_t table = rel_bss + TASK_TABLE_OFF;
    uint32_t mask  = mgs_module_guest_read32(cpu, rel_bss + TASK_MASK_OFF);
    unsigned lvl, total = 0u, gated = 0u, skipped = 0u;

    /* The mask's ADDRESS as well as its value: it is read in a hundred
     * places and written through its label in exactly one - the initialiser,
     * which writes zero - yet it takes three different non-zero values in a
     * run. Finding the writer means watching the address, so print it. */
    printf("engine tasks (table 0x%08X, global mask 0x%08X at 0x%08X):\n",
           table, mask, rel_bss + TASK_MASK_OFF);

    for (lvl = 0u; lvl < TASK_LEVELS; ++lvl) {
        uint32_t head = table + lvl * TASK_LEVEL_SIZE;
        uint32_t gate = mgs_module_guest_read32(cpu, head + TASK_LEVEL_GATE);
        uint32_t node = mgs_module_guest_read32(cpu, head + TASK_LEVEL_HEAD);
        unsigned n = 0u;
        int level_off = (gate & mask) != 0u;

        /* An empty level is the common case and says nothing; printing all
         * twelve every time buries the two that matter. */
        if (!node && !gate) continue;

        printf("  level %2u  gate 0x%08X%s\n", lvl, gate,
               level_off ? "   -- SKIPPED, gate & mask is set" : "");
        if (level_off) ++gated;

        /* The list is guest data and may be circular or corrupt; a bound
         * here is the difference between a diagnostic and a hang. */
        while (node && n < 256u) {
            uint32_t next  = mgs_module_guest_read32(cpu, node + TASK_NEXT);
            uint32_t func  = mgs_module_guest_read32(cpu, node + TASK_FUNC);
            uint32_t flags = mgs_module_guest_read32(cpu, node + TASK_FLAGS);
            int off = (flags & 0x000F0000u) != 0u;   /* rlwinm 0, 12, 15 */

            {   /* The name is what makes this table readable: twelve
                 * raw pointers had to be resolved by hand against the
                 * disassembly before, one run at a time. */
                const char* who = mgs_symbol_for(func);
                printf("      node 0x%08X  fn 0x%08X %-28s flags 0x%08X%s%s\n",
                       node, func, who ? who : "", flags,
                       off ? "  [flag-skipped]" : "",
                       func ? "" : "  [no function]");
            }
            /* MGS_TASK_DUMP=<function address>: also dump that task's node.
             *
             * THE NODE IS THE TASK'S OWN OBJECT. The dispatcher reaches
             * `bctrl` with r3 still holding the node, so a task is called
             * with its own list entry as its argument - which means the node
             * carries that task's state, and dumping it says why a task that
             * does run does nothing. The movie task is a state machine on
             * +0x44 and returns at once for states it does not handle; that
             * word reading 1 is how its stall was found (F196).
             *
             * Selected by function rather than dumped for every node,
             * because these fields mean different things to different tasks
             * and a column of them invites reading one task's layout onto
             * another. */
            if (dump_fn && func == dump_fn) {
                unsigned w;
                for (w = 0u; w < 0x60u; w += 0x10u)
                    printf("        +0x%02X %08X %08X %08X %08X\n", w,
                           mgs_module_guest_read32(cpu, node + w),
                           mgs_module_guest_read32(cpu, node + w + 4u),
                           mgs_module_guest_read32(cpu, node + w + 8u),
                           mgs_module_guest_read32(cpu, node + w + 12u));
            }
            if (off || !func) ++skipped;
            ++total;
            node = next;
            ++n;
        }
        if (n >= 256u) printf("      (list did not terminate - corrupt)\n");
    }

    printf("  %u task%s across the table; %u level%s gated off, "
           "%u node%s that would not run\n",
           total, total == 1u ? "" : "s", gated, gated == 1u ? "" : "s",
           skipped, skipped == 1u ? "" : "s");
}

/* ---- the stream's record ring ------------------------------------------
 *
 * WHY WALK IT RATHER THAN DUMP IT. The movie stalls because
 * `gcn_pool_acquire(ring, 2)` never finds a record tagged 2, and the ring
 * holds a quarter of a megabyte between its cursors. Dumping that 4 KB at a
 * time and reading tags out by eye is several runs of eight minutes; the
 * question - "which tags are actually in here" - is one pass over a linked
 * walk, and the walk is the same one `gcn_pool_acquire` does.
 *
 * LAYOUT, read out of `fn_1_1323C4` rather than assumed: +0x08 buffer base,
 * +0x0C buffer size, +0x14 the read cursor, +0x24 the write cursor. A record
 * is a tag word at +0x00, its total size at +0x04, and its payload at +0x10;
 * tag 0xFF is the wrap marker and sends the walk back to the buffer base.
 * Bit 7 of a tag means "already claimed", so it is reported separately
 * rather than as a different tag.
 */
#define RING_BUF_BASE   0x08u
#define RING_BUF_SIZE   0x0Cu
#define RING_READ       0x14u
#define RING_WRITE      0x24u
#define RING_STOP       0x34u

void mgs_dump_ring(void* cpu, uint32_t ring);
void mgs_dump_ring(void* cpu, uint32_t ring)
{
    uint32_t base  = mgs_module_guest_read32(cpu, ring + RING_BUF_BASE);
    uint32_t size  = mgs_module_guest_read32(cpu, ring + RING_BUF_SIZE);
    uint32_t read  = mgs_module_guest_read32(cpu, ring + RING_READ);
    uint32_t write = mgs_module_guest_read32(cpu, ring + RING_WRITE);
    uint32_t stop  = mgs_module_guest_read32(cpu, ring + RING_STOP);
    uint32_t tags[256], claimed[256];
    uint32_t p = read;
    unsigned n = 0u, wraps = 0u, i;

    printf("ring 0x%08X: buffer 0x%08X + 0x%X, read 0x%08X (+0x%X), "
           "write 0x%08X (+0x%X)%s\n",
           ring, base, size, read, read - base, write, write - base,
           stop ? "  STOPPED (+0x34 set)" : "");
    if (!base || !size || size > 0x400000u || read < base
        || read >= base + size) {
        printf("  cursors are not inside the buffer; not walked\n");
        return;
    }
    for (i = 0; i < 256u; ++i) tags[i] = claimed[i] = 0u;

    /* Bounded by the record count, not by trusting the cursors to meet: a
     * ring whose sizes are wrong walks forever otherwise, and this runs at
     * exit where a hang looks exactly like the stall being investigated. */
    while (p != write && n < 100000u) {
        uint32_t tag = mgs_module_guest_read32(cpu, p);
        uint32_t len;
        if ((tag & 0xFFu) == 0xFFu) {
            p = base;
            if (++wraps > 4u) break;
            continue;
        }
        len = mgs_module_guest_read32(cpu, p + 4u);
        if (tag & 0x80u) ++claimed[tag & 0x7Fu]; else ++tags[tag & 0x7Fu];
        ++n;
        if (!len || len > size) {
            printf("  record at 0x%08X has size 0x%X; walk stopped\n", p, len);
            break;
        }
        p += len;
        if (p >= base + size) p = base;
    }

    printf("  %u records between the cursors, %u wrap%s\n",
           n, wraps, wraps == 1u ? "" : "s");
    for (i = 0; i < 128u; ++i)
        if (tags[i] || claimed[i])
            printf("    tag %3u: %6u waiting, %6u already claimed%s\n",
                   i, tags[i], claimed[i],
                   i == 2u ? "   <- what the movie asks for" : "");
    if (!tags[2] && !claimed[2])
        printf("    tag 2 does not appear at all\n");
}
