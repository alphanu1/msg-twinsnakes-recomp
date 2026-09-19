/* memcpy, memset and __fill_mem, natively.
 *
 * WHY THESE, AHEAD OF ANY REMAINING SDK FUNCTION. A sampling profile of the
 * boot says 62.8% of the run is inside `memcpy` and another 23.5% inside
 * `__fill_mem` - 86% of everything, for two functions that between them
 * contain no logic at all. Attributing the callers says why: there are only
 * nineteen memcpy calls in the whole boot, and ONE of them moves 5,737,728
 * bytes, from rel_loader_LoadRel+0x94. That is the engine overlay being
 * copied into place.
 *
 * Translated, that copy is a two-instruction loop - `lbzu` then `stbu` - run
 * 5.7 million times, and every one of those stores lands in the second
 * address window, where it takes the slow external-write path back out to the
 * host. It costs roughly one host step per byte. Natively it is one call to
 * memmove.
 *
 * OVERLAP MATTERS, AND memcpy IS NOT memcpy HERE. The guest's implementation
 * compares src against dest and copies downwards or upwards accordingly:
 *
 *      cmplw  r4, r3          ; src vs dest
 *      blt    .L_800051C8     ; src < dest - copy BACKWARDS from the end
 *
 * so it is memmove semantics under a memcpy name, and the game is entitled to
 * rely on that. Calling the host's memcpy would be undefined exactly where
 * the game expects defined behaviour, so this calls memmove.
 *
 * __fill_mem takes its fill value as the LOW BYTE of r4 (`clrlwi r4, r4, 24`)
 * and does not preserve r3 - which is why the SDK's memset saves the
 * destination in r31 and restores it afterwards. This keeps r3 rather than
 * deliberately clobbering it: no caller can depend on a garbage value, and
 * leaving it defined is the safer of the two.
 */
#include "os_runtime.h"
#include "mem_shims.h"

#include <string.h>

/* A bounded host pointer for a guest range, or NULL if the range does not lie
 * wholly inside one window. Falling back to a byte loop in that case is not a
 * performance concern - it is the case that must stay CORRECT, because a
 * range straddling the end of a window is exactly where a host-side memmove
 * would run off the end of the allocation. */
void mgs_guest_memmove(GuestMemory* mem, uint32_t dst, uint32_t src,
                       uint32_t n)
{
    uint8_t* d = guest_ptr(mem, dst, n);
    uint8_t* s = guest_ptr(mem, src, n);

    if (d && s) { memmove(d, s, n); return; }

    /* Straddling, or unmapped. Copy in the direction the guest would, so
     * overlap behaves the same on this path as on the fast one. */
    if (src < dst) {
        uint32_t i = n;
        while (i--) guest_write8(mem, dst + i, guest_read8(mem, src + i));
    } else {
        uint32_t i;
        for (i = 0u; i < n; ++i)
            guest_write8(mem, dst + i, guest_read8(mem, src + i));
    }
}

void mgs_guest_memset(GuestMemory* mem, uint32_t dst, uint8_t v,
                      uint32_t n)
{
    uint8_t* d = guest_ptr(mem, dst, n);
    uint32_t i;

    if (d) { memset(d, v, n); return; }
    for (i = 0u; i < n; ++i) guest_write8(mem, dst + i, v);
}

void mgs_memcpy(CPUState* ctx);
void mgs_memcpy(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    uint32_t dst = mgs_guest_gpr(rt, 3);
    uint32_t src = mgs_guest_gpr(rt, 4);
    uint32_t n   = mgs_guest_gpr(rt, 5);

    if (n) mgs_guest_memmove(&rt->mem, dst, src, n);
    /* The guest's memcpy leaves r3 holding the destination: it walks with r6
     * and never writes r3. Callers do use the returned pointer. */
    mgs_set_guest_gpr(rt, 3, dst);
}

void mgs___fill_mem(CPUState* ctx);
void mgs___fill_mem(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    uint32_t dst = mgs_guest_gpr(rt, 3);
    uint32_t val = mgs_guest_gpr(rt, 4);
    uint32_t n   = mgs_guest_gpr(rt, 5);

    if (n) mgs_guest_memset(&rt->mem, dst, (uint8_t)(val & 0xFFu), n);
}

void mgs_memset(CPUState* ctx);
void mgs_memset(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    uint32_t dst = mgs_guest_gpr(rt, 3);

    mgs___fill_mem(ctx);
    mgs_set_guest_gpr(rt, 3, dst);
}
