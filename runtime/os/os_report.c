/* OSReport: the game's own printf, and our first native SDK function.
 *
 * Implemented first because the design document's test strategy runs on it.
 * Every divergence from Dolphin is localised by diffing OSReport streams at
 * fixed frames, so until this is native and captured, there is nothing to
 * diff.
 *
 * The format string and its arguments live in GUEST memory, in guest byte
 * order, with the PowerPC EABI calling convention: r3 is the format, r4-r10
 * carry the remaining integer arguments, and further arguments are on the
 * guest stack. So this cannot hand anything to the host's vprintf - the
 * varargs have to be walked by hand.
 */
#include "os_runtime.h"

#include <stdio.h>
#include <string.h>

/* Where the next argument comes from. The EABI puts the first eight integer
 * arguments in r3..r10; r3 is the format itself, so data starts at r4.
 */
typedef struct {
    MgsRuntime* rt;
    unsigned    gpr_index;   /* next GPR to read, 4..10 */
    uint32_t    stack;       /* guest stack pointer for the overflow area */
} ArgCursor;

static uint32_t next_u32(ArgCursor* c)
{
    if (c->gpr_index <= 10u)
        return mgs_guest_gpr(c->rt, c->gpr_index++);
    /* Overflow area: 8 bytes into the caller's frame, growing up. */
    {
        uint32_t v = guest_read32(&c->rt->mem, c->stack);
        c->stack += 4u;
        return v;
    }
}

/* Copy a guest string into a host buffer, bounded. Guest strings are not
 * trusted to be terminated: a runaway read here would walk 24 MB.
 */
static void copy_guest_string(const MgsRuntime* rt, uint32_t addr,
                              char* out, size_t cap)
{
    size_t i = 0;
    if (!addr) { snprintf(out, cap, "(null)"); return; }
    for (; i + 1u < cap; ++i) {
        uint8_t ch = guest_read8(&rt->mem, addr + (uint32_t)i);
        if (!ch) break;
        out[i] = (char)ch;
    }
    out[i] = '\0';
}

void mgs_OSReport(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    char fmt[512];
    char line[1024];
    size_t out = 0;
    size_t i = 0;
    ArgCursor cur;

    copy_guest_string(rt, mgs_guest_gpr(rt, 3), fmt, sizeof fmt);

    cur.rt = rt;
    cur.gpr_index = 4u;
    cur.stack = mgs_guest_gpr(rt, 1) + 8u;

    while (fmt[i] && out + 1u < sizeof line) {
        if (fmt[i] != '%') { line[out++] = fmt[i++]; continue; }
        if (fmt[i + 1] == '%') { line[out++] = '%'; i += 2; continue; }

        /* Copy the whole conversion spec through, then apply it to one
         * argument. Rebuilding the spec rather than assuming %d keeps width,
         * flags and precision working, which matters because the SDK's own
         * messages use them.
         */
        {
            char spec[32];
            size_t s = 0;
            spec[s++] = fmt[i++];
            while (fmt[i] && s + 1u < sizeof spec && !strchr("diouxXcspfgeEG", fmt[i]))
                spec[s++] = fmt[i++];
            if (!fmt[i]) break;
            spec[s++] = fmt[i];
            spec[s] = '\0';

            switch (fmt[i++]) {
            case 's': {
                char str[256];
                copy_guest_string(rt, next_u32(&cur), str, sizeof str);
                out += (size_t)snprintf(line + out, sizeof line - out, "%s", str);
                break;
            }
            case 'c':
                out += (size_t)snprintf(line + out, sizeof line - out, "%c",
                                        (int)next_u32(&cur));
                break;
            case 'f': case 'g': case 'e': case 'E': case 'G':
                /* Floats are passed in f1.. under the EABI, not the GPRs. Not
                 * yet wired; printing the spec keeps the message readable and
                 * makes the gap obvious rather than silently wrong.
                 */
                out += (size_t)snprintf(line + out, sizeof line - out, "<f>");
                break;
            default:
                out += (size_t)snprintf(line + out, sizeof line - out,
                                        spec, (unsigned)next_u32(&cur));
                break;
            }
        }
    }
    line[out < sizeof line ? out : sizeof line - 1] = '\0';

    mgs_os_report_sink(rt, line);
}
