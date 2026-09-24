/* The SDK patch lookup, swept against the table it was built from.
 *
 * The lookup is not a search any more: a range check rejects most addresses
 * without touching memory, a bitmap settles the rest of the window, and only
 * then is the hash read. That is three layers of index arithmetic, each of
 * which can be wrong in a way that BOOTS - a dropped patch means the
 * translated original runs instead of our native one, which for most of
 * these is slower but correct, and would be found weeks later as a
 * divergence rather than as a crash.
 *
 * So the question asked here is not "does it find memcpy" but "does it agree
 * with the table, at every address in MEM1". A linear scan of 39 entries is
 * the oracle: obviously correct, and unusably slow in the dispatch path,
 * which is the whole reason the fast path exists.
 */
#include "os/patch_table.h"
#include <stdio.h>

int main(void)
{
    unsigned failures = 0;
    uint32_t i, n = mgs_patch_count();
    uint32_t a;
    unsigned long swept = 0, hits = 0;

    if (n == 0u) { printf("FAIL: the patch table is empty\n"); return 1; }

    /* Every entry must be found, and be found as itself. */
    for (i = 0; i < n; ++i) {
        uint32_t addr = mgs_patch_address_at(i);
        if (!mgs_patch_lookup(addr)) {
            printf("FAIL: patch %u at 0x%08X is in the table and was not "
                   "found\n", i, addr);
            ++failures;
        }
    }

    /* AND NOTHING ELSE IS FOUND. Every four-byte address in MEM1, compared
     * against the linear scan. 6.3 million lookups; it takes well under a
     * second and it is the only way to show the bitmap and the range check
     * do not answer for an address that is not theirs. */
    for (a = 0x80000000u; a < 0x81800000u; a += 4u) {
        int expect = 0;
        const void* got = (const void*)mgs_patch_lookup(a);
        for (i = 0; i < n; ++i)
            if (mgs_patch_address_at(i) == a) { expect = 1; break; }
        ++swept;
        if (expect) ++hits;
        if (expect != (got != 0)) {
            printf("FAIL: 0x%08X: the table says %s, the lookup says %s\n",
                   a, expect ? "patched" : "not patched",
                   got ? "patched" : "not patched");
            if (++failures > 8u) { printf("  (stopping)\n"); break; }
        }
    }

    /* The caller rejects anything outside [LO, LO+SPAN] without asking,
     * so a patch outside that window would silently never run. */
    for (i = 0; i < n; ++i) {
        uint32_t addr = mgs_patch_address_at(i);
        if (addr - MGS_PATCH_LO > MGS_PATCH_SPAN) {
            printf("FAIL: patch 0x%08X lies outside the emitted window "
                   "0x%08X+0x%X\n", addr, MGS_PATCH_LO, MGS_PATCH_SPAN);
            ++failures;
        }
    }

    printf("patch table: swept %lu addresses, %lu patched, %u entries\n",
           swept, hits, n);
    printf(failures ? "patch_table: FAILED\n" : "patch_table: ok\n");
    return failures ? 1 : 0;
}
