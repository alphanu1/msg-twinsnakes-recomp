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

/* --- finding the overlay at runtime ------------------------------------- */
/* DolRecomp emits recompiled REL code at a SYNTHETIC base. The game loads the
 * real overlay wherever OSAlloc happens to put it, so the two never coincide
 * and a dispatch into the REL never matches anything.
 *
 * OSLink(OSModuleInfo* module, void* bss) is where the overlay's final
 * addresses are decided, and every DOL dispatch already passes through this
 * router - so we can read r3 as it goes by, walk the module's section table
 * in guest RAM, and learn where .text actually landed. From then on a REL
 * address translates into the recompiled range by a simple delta.
 *
 * OSLink's address comes from our own symbol map (mkdd-align, cross-checked
 * against three reference decompilations).
 */
#define MGS_OSLINK_ADDR   0x80020AD8u

/* OSModuleInfo mirrors the REL header: section count at 0x0C, section table
 * offset at 0x10. Each section entry is 8 bytes - offset then size - and the
 * low bits of offset are flags, bit 0 marking an executable section.
 */
#define MGS_MODULE_NUM_SECTIONS   0x0Cu
#define MGS_MODULE_SECTION_INFO   0x10u
#define MGS_REL_TEXT_SECTION      1u

static u32 s_rel_runtime_base;   /* 0 until OSLink tells us */
static unsigned long s_translated;

/* --- the patch table hook ----------------------------------------------- */
/* The host installs this. It is consulted BEFORE either module's table, so a
 * patched SDK function's translated body never runs - which is the design
 * document's central decision, and the reason the dispatch hook exists at
 * all.
 *
 * A function pointer rather than a direct call, so the module stays
 * independent of the runtime: the module can be loaded by the phase 1 host,
 * which has no patch table, and by ours, which does.
 */
static int (*s_patch_hook)(CPUState* ctx, u32 address);
static unsigned long s_patched_calls;

void mgs_dispatch_set_patch_hook(int (*hook)(CPUState*, u32));
void mgs_dispatch_set_patch_hook(int (*hook)(CPUState*, u32)) { s_patch_hook = hook; }

unsigned long mgs_dispatch_patched_calls(void);
unsigned long mgs_dispatch_patched_calls(void) { return s_patched_calls; }

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
    fprintf(f, "# dispatched=%lu rel=%lu dol=%lu unclaimed=%lu "
               "translated=%lu rel_base=0x%08X\n",
            s_rel_calls + s_dol_calls, s_rel_calls, s_dol_calls, s_unclaimed,
            s_translated, s_rel_runtime_base);
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

/* Read where the overlay's .text landed, out of the module header the game
 * just handed to OSLink. Returns 0 if the header does not look right, so a
 * surprise leaves dispatch exactly as it was rather than corrupting it.
 */
static u32 rel_text_base_from_oslink(CPUState* ctx)
{
    u32 module = ctx->gpr[3];
    u32 count, info, entry, offset;

    if (module < 0x80000000u || module >= 0x81800000u)
        return 0;

    count = mem_read32(ctx, module + MGS_MODULE_NUM_SECTIONS);
    info  = mem_read32(ctx, module + MGS_MODULE_SECTION_INFO);
    if (count <= MGS_REL_TEXT_SECTION || info < 0x40u)
        return 0;

    entry  = info + MGS_REL_TEXT_SECTION * 8u;
    offset = mem_read32(ctx, entry);
    offset &= ~3u;                     /* strip the exec/flag bits */
    if (offset < 0x80000000u || offset >= 0x81800000u)
        return 0;
    return offset;
}

int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address)
{
    if (!s_trace_ready) trace_init();

    /* Native SDK implementations win over translated code, always. Checked
     * first so a patched function's original body is never reached.
     */
    if (s_patch_hook && s_patch_hook(ctx, address)) {
        s_patched_calls++;
        return 1;
    }

    if (address == MGS_OSLINK_ADDR && s_rel_runtime_base == 0u) {
        u32 base = rel_text_base_from_oslink(ctx);
        if (base) {
            s_rel_runtime_base = base;
            fprintf(stderr, "[mgs] OSLink: overlay .text at 0x%08X, "
                            "recompiled at 0x%08X (delta 0x%08X)\n",
                    base, MGS_REL_TEXT_BASE, MGS_REL_TEXT_BASE - base);
        } else {
            fprintf(stderr, "[mgs] OSLink: could not read the module header "
                            "(r3=0x%08X)\n", ctx->gpr[3]);
        }
    }

    /* An address inside the overlay as the game linked it: translate into the
     * recompiled range. Done before the DOL test because the overlay lives in
     * MEM1 and would otherwise look like a DOL address.
     */
    if (s_rel_runtime_base &&
        address - s_rel_runtime_base < MGS_REL_TEXT_SIZE) {
        address = MGS_REL_TEXT_BASE + (address - s_rel_runtime_base);
        s_translated++;
    }

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
