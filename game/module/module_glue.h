/* Shared declarations for the cross-module dispatch glue.
 *
 * Deliberately does NOT include either module's generated.h: both define
 * dolrecomp_find_original and dolrecomp_call_original as static inline over
 * their own chunk tables, so a file that saw both would get one table
 * silently shadowing the other.
 */
#ifndef MGS_MODULE_GLUE_H
#define MGS_MODULE_GLUE_H

#include "cpu/cpu.h"

/* Each defined in its own translation unit, over that module's table. */
int mgs_dol_call(CPUState* ctx, u32 address);
int mgs_rel_call(CPUState* ctx, u32 address);

/* DolRecomp's extension point, called from dolrecomp_call before the calling
 * module's own table. Live only when DOLRECOMP_ENABLE_REPLACEMENTS is defined.
 */
int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address);

/* THE ORIGINAL-CODE LOOKUP, for either backend.
 *
 * With the C backend, generated.h's own dolrecomp_call_original is used: a
 * few hundred chunks, a short chain of range tests. With the LLVM backend
 * there is one region per function and that chain is some 13,000 tests long
 * - 62% of the intro movie's time went on walking it (HANDOFF F362) - so the
 * bridge includes an index of the same mapping (gen_native_index.py,
 * MGS_NATIVE_INDEX) and searches it: a small cache of recent addresses in
 * front of a binary search over the sorted ranges. It resolves every address
 * to exactly the function the generated lookup would. */
#if defined(DOLRECOMP_BACKEND_LLVM) && defined(MGS_NATIVE_INDEX)
#include MGS_NATIVE_INDEX
static int mgs_call_original(CPUState* ctx, u32 address)
{
    static struct { u32 addr; void (*fn)(CPUState*); } cache[4096];
    unsigned slot = (address >> 2) & 4095u;
    void (*fn)(CPUState*) = NULL;
    if (cache[slot].addr == address && cache[slot].fn) {
        fn = cache[slot].fn;
    } else {
        u32 lo = 0u, hi = MGS_IDX_RANGES;
        while (lo < hi) {
            u32 mid = lo + (hi - lo) / 2u;
            if (MGS_IDX_range[mid].start <= address) lo = mid + 1u;
            else hi = mid;
        }
        if (lo) {
            u32 off = address - MGS_IDX_range[lo - 1u].start;
            if (off < MGS_IDX_range[lo - 1u].size && (off & 3u) == 0u)
                fn = MGS_IDX_fn[MGS_IDX_range[lo - 1u].first +
                                off / MGS_IDX_range[lo - 1u].stride];
        }
        if (!fn) return 0;
        cache[slot].addr = address;
        cache[slot].fn = fn;
    }
    ctx->pc = address;
    fn(ctx);
    return 1;
}
#else
#define mgs_call_original dolrecomp_call_original
#endif

#endif
