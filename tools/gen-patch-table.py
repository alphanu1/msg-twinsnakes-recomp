#!/usr/bin/env python3
"""Generate the SDK patch table from config/symbols/.

This is the design document's central mechanism: for each SDK function the
game calls, the runtime provides a NATIVE implementation and the translated
original is never executed. The boundary between translated and native is the
SDK's public API - documented, stable across GameCube titles, and for this
game entirely inside main.dol (see HANDOFF F9).

DolRecomp already ships the hook. With DOLRECOMP_ENABLE_REPLACEMENTS defined,
dolrecomp_call consults dolrecomp_dispatch_replacement BEFORE its own table,
so returning 1 from there means "handled natively, do not run the translated
code". Phase 1's cross-module router proved that path works.

Emits the address table plus a lookup built for the case that actually
dominates: the MISS. This is consulted once per dispatch - about 8.1 million
times a second - and almost every answer is "no", so the miss must be cheap
before the hit is.

Three layers, cheapest first:

  1. A range check. Every patch is an SDK OS, cache, DVD or time function and
     they all sit in one narrow window near the bottom of MEM1, while the
     game's own code is far above it. A subtract and an unsigned compare
     reject everything outside the window with no memory access at all.
  2. A bitmap, one bit per four-byte address across that window. Small enough
     to stay in L1, and exact rather than probabilistic.
  3. Only then the open-addressed hash.

An earlier version binary-searched the sorted table, which is eight
dependent loads for every miss; before that a linear scan. The hash version
was hand-written into the generated file and this generator was never
updated, so regenerating would have silently reverted it - which is exactly
what "generated code is never hand-edited" is there to prevent.
"""
import re, argparse, sys

# The lookup itself, emitted verbatim. Kept as one block rather than as
# string concatenation so it reads as C and can be edited as C.
LOOKUP_C = r"""
/* HOW THIS IS SEARCHED, AND WHY THE MISS IS WHAT MATTERS.
 *
 * mgs_patch_lookup is asked once per dispatch - around 8.1 million times a
 * second - and almost every answer is "no". Profiled over a heavy scene the
 * old binary search was 5.0% of ALL CPU samples, more than the entire render
 * path, because every miss walked a sorted table through several dependent
 * loads in memory nothing else keeps warm.
 *
 * So the miss is answered first and without touching memory. Every patch is
 * an SDK OS, cache, DVD or time function, and they all lie in one narrow
 * window near the bottom of MEM1 while the game's own code is far above it.
 * A subtract and an unsigned compare reject everything outside it.
 *
 * Inside the window a bitmap of one bit per four-byte address settles it
 * exactly - a few kilobytes, small enough to stay in L1, and exact rather
 * than probabilistic, so a hit is still a hit. Only then is the hash read.
 *
 * Open addressing, no allocation: address zero is not a valid patch target,
 * so it doubles as the empty marker. Guest addresses are four-byte aligned,
 * so the index takes the bits above that. */
#define PATCH_SLOT_BITS 8u
#define PATCH_SLOTS (1u << PATCH_SLOT_BITS)
#define PATCH_MASK (PATCH_SLOTS - 1u)

/* One bit per four-byte address. Sized generously against the window the
 * patches actually occupy; build_slots refuses the fast path rather than
 * miss a patch if they ever spread wider than this. */
#define PATCH_BITMAP_BYTES ((0x40000u >> 2) / 8u)

static uint32_t  s_slot_addr[PATCH_SLOTS];
static MgsSdkFn  s_slot_fn[PATCH_SLOTS];
static int       s_slots_built;
static uint32_t  s_lo, s_span;
static uint8_t   s_present[PATCH_BITMAP_BYTES];

static void build_slots(void)
{
    uint32_t i, hi;

    s_lo = k_patches[0].address;
    hi   = k_patches[0].address;
    for (i = 1u; i < MGS_PATCH_COUNT; ++i) {
        if (k_patches[i].address < s_lo) s_lo = k_patches[i].address;
        if (k_patches[i].address > hi)   hi   = k_patches[i].address;
    }
    s_span = hi - s_lo;

    if (s_span >= PATCH_BITMAP_BYTES * 8u * 4u) {
        /* Wider than the bitmap. Fall back to the hash for every call and
         * SAY SO: silently dropping a patch would be a correctness change
         * the next time someone adds one far from the rest. */
        fprintf(stderr, "[patch] the patch addresses span 0x%X bytes, wider "
                        "than the lookup bitmap; using the hash for every "
                        "call\n", (unsigned)s_span);
        s_span = 0xFFFFFFFFu;
    }

    for (i = 0u; i < MGS_PATCH_COUNT; ++i) {
        uint32_t h = (k_patches[i].address >> 2) & PATCH_MASK;
        while (s_slot_addr[h]) h = (h + 1u) & PATCH_MASK;
        s_slot_addr[h] = k_patches[i].address;
        s_slot_fn[h]   = k_patches[i].fn;
        if (s_span != 0xFFFFFFFFu) {
            uint32_t b = (k_patches[i].address - s_lo) >> 2;
            s_present[b >> 3] |= (uint8_t)(1u << (b & 7u));
        }
    }
    s_slots_built = 1;
}

MgsSdkFn mgs_patch_lookup(uint32_t address)
{
    uint32_t h, off;

    if (!s_slots_built) build_slots();

    off = address - s_lo;
    if (off > s_span) return 0;
    if (s_span != 0xFFFFFFFFu) {
        uint32_t b = off >> 2;
        if (!(s_present[b >> 3] & (1u << (b & 7u)))) return 0;
    }

    h = (address >> 2) & PATCH_MASK;
    for (;;) {
        uint32_t a = s_slot_addr[h];
        if (a == address) {
            /* Checked only on a HIT. A miss returns nothing either way, and
             * this walks a string when MGS_UNPATCH is set. */
            return unpatched(address) ? 0 : s_slot_fn[h];
        }
        if (!a) return 0;
        h = (h + 1u) & PATCH_MASK;
    }
}

/* FOR THE TEST, which sweeps every address in MEM1 against a linear scan of
 * the table above. The fast path is three layers of index arithmetic and
 * "it still boots" is not evidence that it agrees with the table. */
uint32_t mgs_patch_count(void) { return MGS_PATCH_COUNT; }

uint32_t mgs_patch_address_at(uint32_t i)
{
    return i < MGS_PATCH_COUNT ? k_patches[i].address : 0u;
}
"""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--symbols', required=True)
    ap.add_argument('--implemented', required=True,
                    help='file listing SDK function names the runtime implements, one per line')
    ap.add_argument('--out-c', required=True)
    ap.add_argument('--out-h', required=True)
    a = ap.parse_args()

    named = {}
    for line in open(a.symbols):
        m = re.match(r'^(\.\w+) 0x([0-9A-Fa-f]+) \S+ (\S+) (\S+)', line)
        # `.init` as well as `.text`. The CodeWarrior runtime's block moves -
        # memcpy, memset, __fill_mem - are linked into .init, and restricting
        # this to .text silently dropped them from the table: the name
        # resolved, the address did not, and they were reported as missing
        # implementations rather than as a section filter.
        if m and m.group(1) in ('.text', '.init'):
            named.setdefault(m.group(3), int(m.group(2), 16))

    want, missing = [], []
    for line in open(a.implemented):
        name = line.split('#')[0].strip()
        if not name:
            continue
        if name in named:
            want.append((named[name], name))
        else:
            missing.append(name)

    want.sort()

    lo = min(addr for addr, _ in want) if want else 0
    span = (max(addr for addr, _ in want) - lo) if want else 0
    with open(a.out_h, 'w') as f:
        f.write("/* Generated by tools/gen-patch-table.py - do not edit. */\n"
                "#ifndef MGS_PATCH_TABLE_H\n#define MGS_PATCH_TABLE_H\n\n"
                "#include <stdint.h>\n\n"
                "typedef struct CPUState CPUState;\n"
                "typedef void (*MgsSdkFn)(CPUState* ctx);\n\n"
                "/* Returns the native implementation for a guest address, or NULL. */\n"
                "MgsSdkFn mgs_patch_lookup(uint32_t address);\n\n"
                "/* The raw table, for the test that sweeps every address in\n"
                " * MEM1 against a linear scan of it. The lookup is three\n"
                " * layers of index arithmetic and \"it still boots\" is not\n"
                " * evidence that it agrees with the table it was built from. */\n"
                "uint32_t mgs_patch_count(void);\n"
                "uint32_t mgs_patch_address_at(uint32_t i);\n\n"
                "/* THE WINDOW EVERY PATCH LIES IN, as constants, so a caller\n"
                " * can reject a miss before paying for a call. The dispatch\n"
                " * hook runs on every guest function call - some twenty\n"
                " * million a second - and almost none of them is a patch. */\n"
                f"#define MGS_PATCH_LO   0x{lo:08X}u\n"
                f"#define MGS_PATCH_SPAN 0x{span:08X}u\n\n")
        for _, name in want:
            f.write(f"void mgs_{name}(CPUState* ctx);\n")
        f.write("\n#endif\n")

    with open(a.out_c, 'w') as f:
        f.write("/* Generated by tools/gen-patch-table.py - do not edit.\n"
                " *\n"
                " * Consulted on every guest call that reaches the dispatch hook, so the\n"
                " * MISS is what is optimised: see mgs_patch_lookup below.\n"
                " */\n"
                '#include "patch_table.h"\n#include <stdlib.h>\n#include <stdio.h>\n\n'
                "typedef struct { uint32_t address; MgsSdkFn fn; } MgsPatch;\n\n"
                "static const MgsPatch k_patches[] = {\n")
        for addr, name in want:
            f.write(f"    {{0x{addr:08X}u, mgs_{name}}},   /* {name} */\n")
        if not want:
            f.write("    {0u, 0},  /* C rejects a zero-sized array */\n")
        f.write("};\n\n")
        f.write(f"#define MGS_PATCH_COUNT {len(want)}u\n\n"
                "/* MGS_UNPATCH=<addr>[,<addr>...]: run the TRANSLATED code\n"
                " * for these instead of our shim.\n"
                " *\n"
                " * A shim is a claim that the real code cannot run here yet,\n"
                " * and such a claim goes stale: __OSInitAudioSystem was\n"
                " * stubbed because 'the flags it waits for will never be\n"
                " * raised however carefully the registers are modelled', and\n"
                " * a later test drove that whole sequence against the model\n"
                " * with every wait passing. Withdrawing a shim to check\n"
                " * should not need a rebuild, or it will not be done. */\n"
                "static int unpatched(uint32_t address)\n{\n"
                "    static const char* list;\n"
                "    static int looked;\n"
                "    const char* p;\n"
                "    if (!looked) { looked = 1; list = getenv(\"MGS_UNPATCH\"); }\n"
                "    if (!list) return 0;\n"
                "    for (p = list; *p; ) {\n"
                "        char* end = 0;\n"
                "        unsigned long v = strtoul(p, &end, 0);\n"
                "        if (end == p) break;\n"
                "        if ((uint32_t)v == address) return 1;\n"
                "        p = (*end == ',') ? end + 1 : end;\n"
                "    }\n"
                "    return 0;\n}\n\n"
                + LOOKUP_C)

    print(f"patch table: {len(want)} SDK functions patched")
    if missing:
        print(f"  NOT IN THE SYMBOL MAP ({len(missing)}), so not patchable yet:")
        for n in missing[:12]:
            print(f"    {n}")
        if len(missing) > 12:
            print(f"    ... and {len(missing)-12} more")
    return 0

if __name__ == '__main__':
    sys.exit(main())
