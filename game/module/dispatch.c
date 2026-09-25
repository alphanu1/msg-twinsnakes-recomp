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
 * the REL's .text size, from `dtk rel info`: 0x456400 bytes at 0x7F0080EC.
 *
 * That base is not a choice any more. DolRecomp's --rel-base fixes every
 * absolute address in the generated overlay code, and the game loads its
 * overlay at 0x7F008000 - a hard-coded constant in its own loader. Recompiling
 * at a DIFFERENT base, which is what 0x80500000 was, leaves the code branching
 * correctly (dispatch can translate a call) and reading its DATA from an
 * address 0x014F8000 away from where the data actually is. Nothing translates
 * a load.
 * Bounding it matters - an unbounded "anything high is the REL" test would
 * swallow addresses the REL does not own and report a false hit, and a false
 * hit returns 1, which tells the caller the call was handled.
 */
#define MGS_REL_TEXT_BASE 0x7F0080ECu
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

/* The second window's host buffer, for mgs_cpu.h's inlined accessors. Left
 * NULL, the chunks take the old external_read route for that window. */
u8* g_mgs_vmem;
void mgs_dispatch_set_vmem(u8* base);
void mgs_dispatch_set_vmem(u8* base) { g_mgs_vmem = base; }

/* THE FPU SWITCH, DONE WHERE THE TRAP WAS (HANDOFF F370).
 *
 * The SDK switches floating point lazily. A thread switch leaves MSR[FP]
 * clear, the thread's next float instruction raises FP-unavailable, and the
 * handler - OSSwitchFPUContext - saves the previous owner's registers into
 * its OSContext, loads the current thread's, marks it the owner, sets
 * MSR[FP] and returns with rfi to the instruction that trapped.
 *
 * That return lands in the middle of a block. Upstream interprets forward
 * from there; a native build cannot, so every instruction had to be an
 * entry point, which keeps the generated code from holding anything in a
 * host register across an instruction: PSMTXMultVec, 21 instructions,
 * compiled to about 40 KB.
 *
 * So the trap is not taken. The generated code already expects this helper
 * to be able to change state - it writes its registers back before the
 * call and reloads them after - so the switch is done here, in place, and
 * the code carries on. It is the handler's own work: the game's own
 * recompiled __OSSaveFPUContext and __OSLoadFPUContext do the saving and
 * loading, so the float semantics are exactly what ran before. What the
 * handler restores before its rfi (r3-r5, CR, LR, CTR, XER) is restored
 * here, and SRR0/SRR1 are left as its rfi leaves them.
 *
 * Not reproduced: the exception prologue's saves of r0-r5 and the special
 * registers into the current OSContext, and the EXC state bit it sets and
 * the handler clears. Nothing reads a running thread's saved registers
 * before OSSaveContext overwrites them.
 *
 * MGS_FPU_TRAP=1 takes the real exception instead. */
#define MGS_OS_CURRENT_CONTEXT 0x000000D4u   /* __OSCurrentContext */
#define MGS_OS_FPU_CONTEXT     0x000000D8u   /* __OSFPUContext */
#define MGS_OS_SAVE_FPU        0x8001D7C8u   /* __OSSaveFPUContext, dtk-sig */
#define MGS_OS_LOAD_FPU        0x8001D6A4u   /* __OSLoadFPUContext, dtk-sig */

bool __real_ppc_fp_available(CPUState* cpu, u32 cia);
bool __wrap_ppc_fp_available(CPUState* cpu, u32 cia);
bool __real_ppc_fp_raise_unavailable(CPUState* cpu, u32 cia);
bool __wrap_ppc_fp_raise_unavailable(CPUState* cpu, u32 cia);
extern bool g_ppc_lazy_fp_enabled;

static int s_fpu_in_place = -1;
unsigned long long mgs_dispatch_fpu_switches;

static u32 low_be32(const CPUState* cpu, u32 off)
{
    const u8* p = cpu->ram + off;
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void low_wbe32(CPUState* cpu, u32 off, u32 v)
{
    u8* p = cpu->ram + off;
    p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v;
}

static bool fpu_switch_in_place(CPUState* cpu, u32 cia)
{
    u32 cur, owner, r3, r4, r5, cr, lr, ctr, xer, pc, msr;
    if (s_fpu_in_place < 0) s_fpu_in_place = getenv("MGS_FPU_TRAP") == NULL;
    if (!s_fpu_in_place || !cpu->ram) return false;
    cur = low_be32(cpu, MGS_OS_CURRENT_CONTEXT);
    owner = low_be32(cpu, MGS_OS_FPU_CONTEXT);
    if (!cur) return false;          /* before OSInit: the real exception */
    r3 = cpu->gpr[3]; r4 = cpu->gpr[4]; r5 = cpu->gpr[5];
    cr = cpu->cr; lr = cpu->lr; ctr = cpu->ctr; xer = cpu->xer; pc = cpu->pc;
    msr = cpu->msr;
    cpu->msr |= PPC_MSR_FP;
    low_wbe32(cpu, MGS_OS_FPU_CONTEXT, cur);
    if (owner != cur) {
        if (owner) {
            cpu->gpr[5] = owner;
            if (!mgs_dol_call(cpu, MGS_OS_SAVE_FPU)) goto lost;
        }
        cpu->gpr[4] = cur;
        if (!mgs_dol_call(cpu, MGS_OS_LOAD_FPU)) goto lost;
    }
    cpu->gpr[3] = r3; cpu->gpr[4] = r4; cpu->gpr[5] = r5;
    cpu->cr = cr; cpu->lr = lr; cpu->ctr = ctr; cpu->xer = xer; cpu->pc = pc;
    cpu->srr0 = cia;
    cpu->srr1 = (msr & PPC_MSR_RFI_MASK) | PPC_MSR_FP;
    ++mgs_dispatch_fpu_switches;
    return true;
lost:
    fprintf(stderr, "[fpu] the SDK's FPU save/load at 0x%08X/0x%08X is not "
                    "in this module; cannot switch in place\n",
            MGS_OS_SAVE_FPU, MGS_OS_LOAD_FPU);
    abort();
}

/* The LLVM backend calls this one; the C backend's inline test calls the
 * next. Both reach here through the link's --wrap. */
bool __wrap_ppc_fp_available(CPUState* cpu, u32 cia)
{
    if (!g_ppc_lazy_fp_enabled || (cpu->msr & PPC_MSR_FP)) return true;
    if (fpu_switch_in_place(cpu, cia)) return true;
    return __real_ppc_fp_available(cpu, cia);
}

bool __wrap_ppc_fp_raise_unavailable(CPUState* cpu, u32 cia)
{
    if (fpu_switch_in_place(cpu, cia)) return true;
    return __real_ppc_fp_raise_unavailable(cpu, cia);
}

/* THE NATIVE-REGION GUARD, for code from DolRecomp's LLVM backend.
 *
 * A native function calls the next one directly, so the dispatch path where
 * the patch table used to be consulted is never taken between them. Instead
 * each native function asks, on entry, whether it may run: a function whose
 * entry is one of our native SDK implementations must not, and exits with
 * its state written back so the host's dispatch reaches the patch exactly
 * as before. Everything else runs on. The patch hook answers the question
 * with ppc_host_call's query convention unset, so this asks it directly:
 * the host installs a query-only lookup alongside the hook. */
static int (*s_patch_query)(u32 address);

/* Read by native code on every MSR[EE] 0 -> 1 when it was generated with
 * DOLRECOMP_EE_EXIT_WHEN_PENDING=1 (local DolRecomp patch): it leaves for
 * the run loop only while this is set. The host sets it when it has had to
 * refuse an interrupt because EE was clear, and clears it on delivery. One
 * until the host takes it over, which is the upstream behaviour. */
u32 dolrecomp_msr_ee_exit_wanted = 1u;

void mgs_dispatch_set_patch_query(int (*query)(u32));
void mgs_dispatch_set_patch_query(int (*query)(u32)) { s_patch_query = query; }

bool ppc_native_region_available(CPUState* cpu, u32 start, u32 end);
bool ppc_native_region_available(CPUState* cpu, u32 start, u32 end)
{
    (void)cpu; (void)end;
    return !(s_patch_query && s_patch_query(start));
}

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
static int module_address_is_sane(u32 addr)
{
    if (addr >= 0x80000000u && addr < 0x81800000u) return 1;   /* MEM1 */
    if (addr >= 0x7E000000u && addr < 0x80000000u) return 1;   /* the window */
    return 0;
}

static u32 rel_text_base_from_oslink(CPUState* ctx)
{
    u32 module = ctx->gpr[3];
    u32 count, info, entry, offset;

    /* Two windows are valid: MEM1, and the second addressable region at
     * 0x7E000000. Twin Snakes links its module in the SECOND one, so a check
     * that only knew about MEM1 rejected the real module pointer and left
     * every overlay address untranslated. */
    if (!module_address_is_sane(module))
        return 0;

    count = mem_read32(ctx, module + MGS_MODULE_NUM_SECTIONS);
    info  = mem_read32(ctx, module + MGS_MODULE_SECTION_INFO);
    if (count <= MGS_REL_TEXT_SECTION || info < 0x40u)
        return 0;

    /* sectionInfoOffset is an offset INTO THE FILE, so the table lives at
     * module + info - not at `info`, which would read from whatever happens
     * to sit at 0x4C in low memory. Likewise each entry's offset field is
     * relative to the module, not absolute: a REL is relocatable and has no
     * link address to be absolute about. */
    entry  = module + info + MGS_REL_TEXT_SECTION * 8u;
    offset = mem_read32(ctx, entry);
    offset &= ~3u;                     /* strip the exec/flag bits */
    if (offset == 0u)                  /* an empty section is not an error */
        return 0;
    if (!module_address_is_sane(module + offset))
        return 0;
    return module + offset;
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
            u32 k;
            fprintf(stderr, "[mgs] OSLink: could not read the module header "
                            "(r3=0x%08X)\n", ctx->gpr[3]);
            /* The header itself, so "could not read" says WHICH field was
             * wrong rather than only that something was. */
            static const u32 at[] = { 0x7F000000u, 0x7F008000u };
            u32 j;
            for (j = 0; j < 2u; ++j) {
                fprintf(stderr, "[mgs]   %08X:", at[j]);
                for (k = 0; k < 8u; ++k)
                    fprintf(stderr, " %08X", mem_read32(ctx, at[j] + k * 4u));
                fprintf(stderr, "\n");
            }
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
