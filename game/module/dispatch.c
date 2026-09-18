/* Cross-module dispatch: route a guest address to whichever recompiled
 * module owns it.
 *
 * DolRecomp gives each module its own address table and no knowledge of any
 * other. Twin Snakes needs two - main.dol and mgso_pal.rel - and they call
 * each other constantly: the engine lives in the REL and every SDK function
 * it uses lives in the DOL.
 *
 * dolrecomp_call consults dolrecomp_dispatch_replacement before its own
 * table, so this is where the two modules are joined.
 *
 * Set MGS_DISPATCH_TRACE=<path> to also record where execution actually goes:
 * a histogram of dispatched addresses, written at exit. tools/trace-report.py
 * resolves it against config/symbols/. Off unless the variable is set, so the
 * hot path stays a bounds check and a call.
 */
#include "module_glue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Recompiled REL code is emitted at DolRecomp's REL_AUTO_BASE. The extent is
 * the REL's .text size, from `dtk rel info`: 0x456400 bytes at 0x805000EC.
 * Bounding it matters - an unbounded "anything high is the REL" test would
 * swallow addresses the REL does not own and report a false hit, and a false
 * hit returns 1, which tells the caller the call was handled.
 */
#define MGS_REL_TEXT_BASE 0x805000ECu
#define MGS_REL_TEXT_SIZE 0x00456400u

/* main.dol's .init starts at 0x80003100; .text ends at 0x80062050. */
#define MGS_DOL_TEXT_BASE 0x80003100u
#define MGS_DOL_TEXT_END  0x80062050u

/* --- tracing ------------------------------------------------------------ */
/* An open-addressed histogram, fixed size and never resized: this runs inside
 * the dispatch path, so an allocation here would change the timing of the
 * thing being measured. Collisions simply share a bucket, which blurs cold
 * addresses and leaves the hot ones - the only ones we act on - intact.
 */
#define TRACE_SLOTS (1u << 16)

static struct { unsigned addr; unsigned long hits; } s_trace[TRACE_SLOTS];
static const char* s_trace_path;
static unsigned long s_rel_calls, s_dol_calls, s_unclaimed;
static int s_trace_ready;

static void trace_dump(void)
{
    FILE* f;
    unsigned i;
    if (!s_trace_path) return;
    f = fopen(s_trace_path, "w");
    if (!f) return;
    fprintf(f, "# dispatched=%lu rel=%lu dol=%lu unclaimed=%lu\n",
            s_rel_calls + s_dol_calls, s_rel_calls, s_dol_calls, s_unclaimed);
    for (i = 0; i < TRACE_SLOTS; ++i)
        if (s_trace[i].hits)
            fprintf(f, "%08X %lu\n", s_trace[i].addr, s_trace[i].hits);
    fclose(f);
}

static void trace_init(void)
{
    s_trace_ready = 1;
    s_trace_path = getenv("MGS_DISPATCH_TRACE");
    if (s_trace_path && *s_trace_path)
        atexit(trace_dump);
    else
        s_trace_path = NULL;
}

static void trace_hit(unsigned address)
{
    unsigned slot = (address >> 2) & (TRACE_SLOTS - 1u);
    s_trace[slot].addr = address;
    s_trace[slot].hits++;
}

int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address)
{
    if (!s_trace_ready) trace_init();

    if (address - MGS_REL_TEXT_BASE < MGS_REL_TEXT_SIZE) {
        s_rel_calls++;
        if (s_trace_path) trace_hit(address);
        return mgs_rel_call(ctx, address);
    }

    if (address >= MGS_DOL_TEXT_BASE && address < MGS_DOL_TEXT_END) {
        s_dol_calls++;
        if (s_trace_path) trace_hit(address);
        return mgs_dol_call(ctx, address);
    }

    /* Not ours. Returning 0 lets the calling module try its own table, then
     * the host, then the interpreter - all of which are correct answers.
     */
    s_unclaimed++;
    return 0;
}
