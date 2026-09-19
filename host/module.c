/* Loading a recompiled module and running it.
 *
 * The module is a shared object DolRecomp's output was compiled into. It
 * exports one function - a descriptor carrying the entry point, the dispatch
 * routine, and the coverage tables - and expects a CPUState whose `ram`
 * points at guest memory.
 *
 * The host owns guest memory and the patch table; the module owns the
 * translated code. The dispatch hook is where they meet, and it is installed
 * rather than linked so the same module can also be run by a host that has no
 * patch table.
 */
#include "module.h"
#include "platform/mmio.h"
#include "os/os_runtime.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirrors ModernGekkoModuleDesc. Declared here rather than included so the
 * host never compiles against a ModernGekko header - phases 2 onward replace
 * that runtime, and an include would quietly re-couple us to it.
 */
typedef struct { uint32_t start, end; } ModRange;
typedef struct {
    uint32_t abi_version, cpu_abi_version, cpu_state_size;
    char     game_id[8];
    uint32_t entry_point;
    int    (*dispatch)(void* state, uint32_t address);
    void   (*on_state_loaded)(void* state);
    const ModRange* code_ranges;  uint32_t num_code_ranges;
    const ModRange* smc_ranges;   uint32_t num_smc_ranges;
    const ModRange* chunk_ranges; uint32_t num_chunk_ranges;
    const uint64_t* chunk_hashes;
    const void* rel_modules;      uint32_t num_rel_modules;
} ModDesc;

/* Offsets into CPUState, taken from GXRuntime's core/cpu.h with offsetof
 * rather than guessed. The host must fill these without including that
 * header, because including it would re-couple the host to the runtime
 * phases 2 onward replace.
 *
 * MGS_CPU_STATE_SIZE is the guard: the module reports its own
 * cpu_state_size, and if it does not match the size these offsets were
 * derived from, the layout has moved and every one of them is wrong. Refusing
 * to load is the only safe answer - writing `ram` to a stale offset would
 * corrupt whatever now lives there and fail somewhere unrelated.
 */
#define MGS_CPU_STATE_SIZE 3536u
#define CPU_GPR_OFFSET      0u
#define CPU_PC_OFFSET     640u
#define CPU_HOST_CALL    3440u
#define CPU_RAM_OFFSET   3456u
#define CPU_RAM_SIZE     3464u
#define CPU_DOWNCOUNT    3480u
#define CPU_MSR           664u
#define CPU_SRR0          668u
#define CPU_SRR1          672u
#define CPU_EXCEPTION     800u
#define CPU_PROGRAM_EXC   804u

int mgs_module_load(MgsModule* mod, const char* path)
{
    const ModDesc* (*get)(void);

    memset(mod, 0, sizeof *mod);
    mod->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!mod->handle) {
        snprintf(mod->error, sizeof mod->error, "%s", dlerror());
        return 0;
    }

    get = (const ModDesc* (*)(void))dlsym(mod->handle, "staticrecomp_get_module");
    if (!get) {
        snprintf(mod->error, sizeof mod->error,
                 "no staticrecomp_get_module: %s", dlerror());
        dlclose(mod->handle); mod->handle = NULL;
        return 0;
    }

    {
        const ModDesc* d = get();
        mod->desc = d;
        mod->entry_point = d->entry_point;
        mod->cpu_state_size = d->cpu_state_size;
        memcpy(mod->game_id, d->game_id, sizeof mod->game_id);
        mod->code_ranges = d->num_code_ranges;
        mod->chunk_ranges = d->num_chunk_ranges;
        mod->rel_modules = d->num_rel_modules;
        mod->dispatch = d->dispatch;
    }

    /* Install the patch table hook if the module offers the seam. A module
     * without it still loads and runs - it simply executes the translated SDK
     * instead of ours, which is exactly what phase 1 did.
     */
    mod->set_patch_hook = (void (*)(int (*)(void*, uint32_t)))
                          dlsym(mod->handle, "mgs_dispatch_set_patch_hook");
    return 1;
}

void* mgs_module_new_cpu_state(const MgsModule* mod, uint8_t* ram, uint32_t ram_size)
{
    uint8_t* state;

    if (mod->cpu_state_size != MGS_CPU_STATE_SIZE)
        return NULL;                  /* layout moved; see the guard above */

    state = (uint8_t*)calloc(1, mod->cpu_state_size);
    if (!state) return NULL;

    memcpy(state + CPU_RAM_OFFSET, &ram, sizeof ram);
    memcpy(state + CPU_RAM_SIZE, &ram_size, sizeof ram_size);
    memcpy(state + CPU_PC_OFFSET, &mod->entry_point, sizeof mod->entry_point);

    /* MSR with floating point and both address translation bits enabled.
     *
     * The GameCube's boot leaves FP on, and the SDK assumes it: the first
     * floating-point instruction after OSInit traps to the FP-unavailable
     * vector otherwise. Starting at zero is not a neutral choice, it is the
     * one setting guaranteed to fault.
     *
     * MSR_FP 0x2000, MSR_IR 0x0020, MSR_DR 0x0010, MSR_EE 0x8000 - external
     * interrupts enabled, since the guest masks them itself when it needs to.
     */
    {
        uint32_t msr = 0x00002030u;
        memcpy(state + CPU_MSR, &msr, sizeof msr);
    }

    /* A generous cycle budget: the translated code decrements this and
     * returns to the host when it runs out, which is how a frame boundary is
     * reached. Zero would return immediately and look like a hang.
     */
    {
        uint32_t budget = 1000000u;
        memcpy(state + CPU_DOWNCOUNT, &budget, sizeof budget);
    }
    return state;
}

uint32_t* mgs_module_gpr(void* cpu_state)
{
    return (uint32_t*)((uint8_t*)cpu_state + CPU_GPR_OFFSET);
}

#define CPU_LR_OFFSET 644u

uint32_t mgs_module_lr(const void* cpu_state)
{
    uint32_t lr;
    memcpy(&lr, (const uint8_t*)cpu_state + CPU_LR_OFFSET, sizeof lr);
    return lr;
}

void mgs_module_set_lr(void* cpu_state, uint32_t lr)
{
    memcpy((uint8_t*)cpu_state + CPU_LR_OFFSET, &lr, sizeof lr);
}

void mgs_module_set_pc(void* cpu_state, uint32_t pc)
{
    memcpy((uint8_t*)cpu_state + CPU_PC_OFFSET, &pc, sizeof pc);
}

uint32_t mgs_module_pc(const void* cpu_state)
{
    uint32_t pc;
    memcpy(&pc, (const uint8_t*)cpu_state + CPU_PC_OFFSET, sizeof pc);
    return pc;
}


/* ---- Lazy floating-point context switching ---------------------------
 *
 * The Dolphin SDK does not save 32 floating-point registers on every thread
 * switch. It leaves MSR_FP CLEAR when it restores a context and installs
 * OSSwitchFPUContext on the FP-unavailable vector, so the first floating
 * point instruction a thread executes traps to 0x800. The handler there
 * saves the previous owner's FPU state into that owner's OSContext, loads
 * the new owner's, records the new owner in __OSFPUContext, sets MSR_FP and
 * resumes. A thread that never touches floating point never pays for it.
 *
 * So an exception at 0x800 is not a fault here. It is the SDK's design
 * working exactly as intended, and stopping on it - which is what this host
 * did - stops the boot at the first float after OSInit.
 *
 * It is serviced here rather than dispatched to for the same reason the
 * external-interrupt vector is: __OSExceptionInit COPIES the vector stubs
 * into low memory at runtime, so no translated chunk contains them and
 * dispatch legitimately has no code at 0x800.
 *
 * What follows is OSSwitchFPUContext, __OSSaveFPUContext and
 * __OSLoadFPUContext performed against our register file instead of the
 * Gekko's. The DATA is identical - same OSContext offsets, same ownership
 * word at 0x800000D8, same OS_CONTEXT_STATE_FPSAVED gate on the load - so
 * the guest cannot tell the difference by inspecting memory.
 *
 * One deliberate difference: the hardware vector stub saves the interrupted
 * register file into OS_CURRENTCONTEXT and sets OS_CONTEXT_STATE_EXC, and
 * the handler clears that bit again before its rfi. We run no stub, so our
 * registers were never spilled and there is nothing to restore - we resume
 * from srr0 with the live register file. Since we never SET that bit we do
 * not clear it either; touching it would be mimicking half of a pair.
 */
#define CPU_FPR_OFFSET    128u   /* f64 fpr[32] - ps0 as a double */
#define CPU_PS1_OFFSET    384u   /* f64 ps1[32] - the paired-single half */
#define CPU_FPSCR_OFFSET  660u
#define CPU_HID2_OFFSET   688u

/* Fixed low-memory words the OS keeps its context bookkeeping in. */
#define OS_CURRENTCONTEXT 0x800000D4u
#define OS_FPUCONTEXT     0x800000D8u

/* OSContext offsets, from the SDK's OSContext.h. */
#define OS_CTX_FPR0       144u   /* 32 doubles */
#define OS_CTX_FPSCR      400u   /* stfd of mffs: the value is the low word */
#define OS_CTX_STATE      418u   /* u16 */
#define OS_CTX_PSF0       456u   /* 32 pairs of singles */
#define OS_CTX_STATE_FPSAVED 0x0001u

#define MSR_FP            0x2000u
#define HID2_PSE          0x20000000u   /* paired singles enabled */

static uint8_t* cpu_ram(void* cpu)
{
    uint8_t* ram;
    memcpy(&ram, (uint8_t*)cpu + CPU_RAM_OFFSET, sizeof ram);
    return ram;
}

/* Guest memory is big-endian; the host is not. Every access below swaps.
 * These are local rather than the runtime's accessors so that this file
 * depends on nothing but the CPUState offsets above. */
static uint32_t gread32(void* cpu, uint32_t addr)
{
    const uint8_t* p = cpu_ram(cpu) + (addr & 0x3FFFFFFFu);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void gwrite32(void* cpu, uint32_t addr, uint32_t v)
{
    uint8_t* p = cpu_ram(cpu) + (addr & 0x3FFFFFFFu);
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint16_t gread16(void* cpu, uint32_t addr)
{
    const uint8_t* p = cpu_ram(cpu) + (addr & 0x3FFFFFFFu);
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void gwrite16(void* cpu, uint32_t addr, uint16_t v)
{
    uint8_t* p = cpu_ram(cpu) + (addr & 0x3FFFFFFFu);
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static double gread_f64(void* cpu, uint32_t addr)
{
    uint64_t bits = ((uint64_t)gread32(cpu, addr) << 32) | gread32(cpu, addr + 4u);
    double d; memcpy(&d, &bits, sizeof d); return d;
}

static void gwrite_f64(void* cpu, uint32_t addr, double d)
{
    uint64_t bits; memcpy(&bits, &d, sizeof bits);
    gwrite32(cpu, addr, (uint32_t)(bits >> 32));
    gwrite32(cpu, addr + 4u, (uint32_t)bits);
}

static double gread_f32(void* cpu, uint32_t addr)
{
    uint32_t bits = gread32(cpu, addr); float f;
    memcpy(&f, &bits, sizeof f); return (double)f;
}

static void gwrite_f32(void* cpu, uint32_t addr, double d)
{
    float f = (float)d; uint32_t bits;
    memcpy(&bits, &f, sizeof bits); gwrite32(cpu, addr, bits);
}

/* Is the address a plausible OSContext? A context is 768 bytes, 8-byte
 * aligned, and lives in MEM1. Checking beats trusting a word the guest could
 * still be initialising - a bad pointer here would scribble 768 bytes over
 * whatever it points at, and that corruption would surface somewhere else
 * entirely. */
static int context_is_sane(uint32_t addr)
{
    uint32_t off = addr & 0x3FFFFFFFu;
    return addr != 0u && (addr >> 28) >= 0x8u && (off & 7u) == 0u &&
           off + 768u <= 24u * 1024u * 1024u;
}

static void fpu_save(void* cpu, uint32_t ctx)
{
    uint8_t* st = (uint8_t*)cpu;
    uint32_t hid2; unsigned i;
    double v;

    gwrite16(cpu, ctx + OS_CTX_STATE,
             (uint16_t)(gread16(cpu, ctx + OS_CTX_STATE) | OS_CTX_STATE_FPSAVED));

    for (i = 0; i < 32u; ++i) {
        memcpy(&v, st + CPU_FPR_OFFSET + i * 8u, sizeof v);
        gwrite_f64(cpu, ctx + OS_CTX_FPR0 + i * 8u, v);
    }

    /* mffs/stfd writes the FPSCR into the LOW word of an 8-byte slot; the
     * high word is architecturally undefined. Writing the value into the
     * high word instead would load back as garbage. */
    {
        uint32_t fpscr;
        memcpy(&fpscr, st + CPU_FPSCR_OFFSET, sizeof fpscr);
        gwrite32(cpu, ctx + OS_CTX_FPSCR, 0u);
        gwrite32(cpu, ctx + OS_CTX_FPSCR + 4u, fpscr);
    }

    memcpy(&hid2, st + CPU_HID2_OFFSET, sizeof hid2);
    if (!(hid2 & HID2_PSE))
        return;                  /* psq_st is skipped on hardware too */

    /* psq_st with W=0 and type 0 stores BOTH halves as singles: ps0 at +0,
     * ps1 at +4. Only the ps1 half matters on reload - the load path
     * overwrites ps0 from the full double - but storing both is what the
     * hardware does and costs nothing. */
    for (i = 0; i < 32u; ++i) {
        memcpy(&v, st + CPU_FPR_OFFSET + i * 8u, sizeof v);
        gwrite_f32(cpu, ctx + OS_CTX_PSF0 + i * 8u, v);
        memcpy(&v, st + CPU_PS1_OFFSET + i * 8u, sizeof v);
        gwrite_f32(cpu, ctx + OS_CTX_PSF0 + i * 8u + 4u, v);
    }
}

static void fpu_load(void* cpu, uint32_t ctx)
{
    uint8_t* st = (uint8_t*)cpu;
    uint32_t hid2; unsigned i;
    double v;

    /* A context whose FP state was never saved has nothing to load, and the
     * SDK returns immediately. Loading zeros instead would wipe the FPU of a
     * thread that legitimately still owns its values. */
    if (!(gread16(cpu, ctx + OS_CTX_STATE) & OS_CTX_STATE_FPSAVED))
        return;

    {
        uint32_t fpscr = gread32(cpu, ctx + OS_CTX_FPSCR + 4u);
        memcpy(st + CPU_FPSCR_OFFSET, &fpscr, sizeof fpscr);
    }

    memcpy(&hid2, st + CPU_HID2_OFFSET, sizeof hid2);
    if (hid2 & HID2_PSE) {
        for (i = 0; i < 32u; ++i) {
            v = gread_f32(cpu, ctx + OS_CTX_PSF0 + i * 8u + 4u);
            memcpy(st + CPU_PS1_OFFSET + i * 8u, &v, sizeof v);
        }
    }

    /* The doubles come last, exactly as in __OSLoadFPUContext: the psq_l
     * pass above set ps0 from a single-precision copy, and this overwrites
     * it with the full-precision one. */
    for (i = 0; i < 32u; ++i) {
        v = gread_f64(cpu, ctx + OS_CTX_FPR0 + i * 8u);
        memcpy(st + CPU_FPR_OFFSET + i * 8u, &v, sizeof v);
    }
}

/* Returns 1 when the exception was serviced and the guest may resume. */
static int mgs_fp_unavailable(void* cpu)
{
    uint32_t ctx   = gread32(cpu, OS_CURRENTCONTEXT);
    uint32_t owner = gread32(cpu, OS_FPUCONTEXT);
    uint32_t srr0, srr1;

    if (!context_is_sane(ctx))
        return 0;                /* no current context: report, do not guess */

    if (owner != ctx) {
        gwrite32(cpu, OS_FPUCONTEXT, ctx);
        if (owner != 0u && context_is_sane(owner))
            fpu_save(cpu, owner);
        fpu_load(cpu, ctx);
    }

    memcpy(&srr0, (uint8_t*)cpu + CPU_SRR0, 4);
    memcpy(&srr1, (uint8_t*)cpu + CPU_SRR1, 4);
    srr1 |= MSR_FP;
    memcpy((uint8_t*)cpu + CPU_SRR1, &srr1, 4);
    memcpy((uint8_t*)cpu + CPU_MSR, &srr1, 4);
    memset((uint8_t*)cpu + CPU_EXCEPTION, 0, 4);
    mgs_module_set_pc(cpu, srr0);
    return 1;
}

/* ---- Taking an exception the way the hardware does -------------------
 *
 * __OSDispatchInterrupt DOES NOT RETURN. It ends in OSLoadContext, which
 * restores a register file and executes `rfi` - so the thread it resumes may
 * not even be the one that was interrupted. Calling it like a function and
 * waiting for a return address is therefore not a shortcut that mostly
 * works; it cannot work at all, and it presented as 1000 interrupts that
 * "failed" after burning 200,000 steps each.
 *
 * What the hardware does instead is a state transition, and the SDK's vector
 * stub completes it: spill the interrupted register file into the current
 * thread's OSContext, mark the context as holding a full exception frame,
 * put the exception number and that context in r3/r4, and branch to the
 * handler. The handler owns the CPU from then on. Resuming is the guest's
 * business, via its own rfi, which spr.c already services.
 *
 * So this does exactly that and then RETURNS TO THE MAIN LOOP, which keeps
 * stepping from the handler's first instruction like any other guest code.
 */
#define OS_CTX_GPR0    0u
#define OS_CTX_CR      128u
#define OS_CTX_LR      132u
#define OS_CTX_CTR     136u
#define OS_CTX_XER     140u
#define OS_CTX_SRR0    408u
#define OS_CTX_SRR1    412u
#define OS_CTX_GQR0    420u
#define OS_CTX_STATE_EXC 0x0002u

#define CPU_LR_OFFSET   644u
#define CPU_CTR_OFFSET  648u
#define CPU_CR_OFFSET   652u
#define CPU_XER_OFFSET  656u
#define CPU_GQR_OFFSET  768u

#define MSR_EE          0x8000u

uint32_t* mgs_module_msr_ptr(void* cpu)
{
    return (uint32_t*)((uint8_t*)cpu + CPU_MSR);
}

uint32_t mgs_module_guest_read32(void* cpu, uint32_t addr)
{
    return gread32(cpu, addr);
}

uint32_t mgs_module_msr(const void* cpu)
{
    uint32_t msr;
    memcpy(&msr, (const uint8_t*)cpu + CPU_MSR, sizeof msr);
    return msr;
}

uint32_t mgs_module_current_context(void* cpu)
{
    return gread32(cpu, OS_CURRENTCONTEXT);
}

int mgs_module_take_exception(void* cpu, uint32_t handler, uint32_t number)
{
    uint32_t ctx = gread32(cpu, OS_CURRENTCONTEXT);
    uint32_t* gpr = mgs_module_gpr(cpu);
    uint8_t* st = (uint8_t*)cpu;
    uint32_t v, msr, pc;
    unsigned i;

    /* No current context means the OS has not finished standing up its
     * scheduler. There is nowhere to spill to, and inventing a location
     * would corrupt whatever is there. */
    if (!context_is_sane(ctx)) return 0;

    for (i = 0; i < 32u; ++i)
        gwrite32(cpu, ctx + OS_CTX_GPR0 + i * 4u, gpr[i]);

    memcpy(&v, st + CPU_CR_OFFSET, 4);  gwrite32(cpu, ctx + OS_CTX_CR, v);
    memcpy(&v, st + CPU_LR_OFFSET, 4);  gwrite32(cpu, ctx + OS_CTX_LR, v);
    memcpy(&v, st + CPU_CTR_OFFSET, 4); gwrite32(cpu, ctx + OS_CTX_CTR, v);
    memcpy(&v, st + CPU_XER_OFFSET, 4); gwrite32(cpu, ctx + OS_CTX_XER, v);
    for (i = 0; i < 8u; ++i) {
        memcpy(&v, st + CPU_GQR_OFFSET + i * 4u, 4);
        gwrite32(cpu, ctx + OS_CTX_GQR0 + i * 4u, v);
    }

    /* srr0/srr1 are the resume point and the MSR to resume with - the pair
     * `rfi` consumes. Writing the handler's address here instead of the
     * interrupted one is the classic way to resume in the wrong place. */
    pc  = mgs_module_pc(cpu);
    msr = mgs_module_msr(cpu);
    gwrite32(cpu, ctx + OS_CTX_SRR0, pc);
    gwrite32(cpu, ctx + OS_CTX_SRR1, msr);
    memcpy(st + CPU_SRR0, &pc, 4);
    memcpy(st + CPU_SRR1, &msr, 4);

    /* OS_CONTEXT_STATE_EXC tells OSLoadContext this frame holds ALL the
     * volatile registers, not just the callee-saved ones a voluntary switch
     * would store. Without it the restore uses `lmw r13` and r5-r12 come
     * back as whatever the handler left in them. */
    gwrite16(cpu, ctx + OS_CTX_STATE,
             (uint16_t)(gread16(cpu, ctx + OS_CTX_STATE) | OS_CTX_STATE_EXC));

    /* The hardware clears MSR[EE] on entry so the handler is not itself
     * interrupted, and clears MSR[FP] so lazy FP still works. Address
     * translation stays on: the SDK's stub turns it straight back on, and
     * our translated code has no untranslated mode to run in. */
    v = (msr & ~(MSR_EE | MSR_FP));
    memcpy(st + CPU_MSR, &v, 4);

    gpr[3] = number;
    gpr[4] = ctx;
    mgs_module_set_pc(cpu, handler);
    return 1;
}

/* ---- Exception vectors, serviced in the host ------------------------
 *
 * __OSExceptionInit COPIES the vector stubs into low memory at runtime, so
 * no translated chunk contains them and dispatch legitimately has no code
 * at 0x300, 0x800 or 0xC00. The host services the ones that are barriers
 * rather than faults.
 *
 * This runs for BOTH the main loop and mgs_module_call_guest, because an
 * interrupt handler is guest code like any other: it takes syscall barriers
 * and it touches floating point. Servicing vectors in one loop and not the
 * other meant every VI interrupt died the moment its handler reached a
 * float - silently, since call_guest only reports that it did not return.
 *
 * Returns 1 when pc was serviced and the guest may be re-entered, -1 when
 * pc is a vector this host will not service (a genuine fault), 0 when pc is
 * not a vector at all.
 */
static int service_vector(void* cpu, uint32_t pc,
                          uint64_t* syscalls, uint64_t* fp_switches)
{
    uint32_t srr0, srr1;

    if (pc >= 0x3000u || (pc & 0xFFu) != 0u)
        return 0;

    memcpy(&srr0, (uint8_t*)cpu + CPU_SRR0, 4);
    memcpy(&srr1, (uint8_t*)cpu + CPU_SRR1, 4);

    if (pc == 0xC00u) {
        /* System call. The SDK issues `sc` as a completion barrier -
         * DCFlushRange ends with one - and its handler returns immediately.
         * srr0 already points past the sc.
         *
         * Returning means emulating `rfi`, and that is BOTH halves: resume
         * at srr0 AND restore MSR from srr1. Taking an exception clears MSR,
         * floating point included, so resuming without restoring it leaves
         * FP disabled - and the next floating-point instruction traps to the
         * FP-unavailable vector, somewhere entirely unrelated to the sc that
         * caused it. That is exactly how this presented.
         */
        if (syscalls) ++*syscalls;
        mgs_module_set_pc(cpu, srr0);
        memcpy((uint8_t*)cpu + CPU_MSR, &srr1, 4);
        memset((uint8_t*)cpu + CPU_EXCEPTION, 0, 4);
        return 1;
    }
    if (pc == 0x800u && mgs_fp_unavailable(cpu)) {
        if (fp_switches) ++*fp_switches;
        return 1;
    }
    if (pc == 0x700u || pc == 0x300u || pc == 0x600u || pc == 0x800u)
        return -1;               /* program, DSI, alignment: a real fault */
    return 0;
}

static MgsPump s_pump;
static void*   s_pump_user;

void mgs_module_set_pump(MgsPump pump, void* user)
{
    s_pump = pump; s_pump_user = user;
}

/* Run the guest until it stops.
 *
 * Translated code does not run to completion: it returns to the host at chunk
 * boundaries, when its cycle budget expires, and whenever it reaches an
 * address its own tables do not cover. So the host must re-enter at the
 * current pc, repeatedly. Calling dispatch once executes a few instructions
 * and returns, which looks exactly like "the game stopped here" and is why
 * the first run appeared to stall at __OSPSInit's entry.
 *
 * Stops when dispatch reports it could not handle an address - that is a
 * genuine gap, and the pc names it - or when the step ceiling is reached,
 * which catches a guest spinning rather than letting it hang the host.
 */
MgsRunResult mgs_module_run(const MgsModule* mod, void* cpu, uint64_t max_steps)
{
    MgsRunResult r;
    uint32_t last_pc = 0u;
    uint64_t same_pc = 0u;
    uint64_t trace_steps = 0u;
    uint64_t trace_from = 0u;
    uint64_t trace_every = 0u;

    /* MGS_TRACE_STEPS=N prints the first N guest pcs. A stop address alone
     * says where execution ended, not how it got there, and for a boot the
     * path is the interesting part. */
    {
        const char* env = getenv("MGS_TRACE_STEPS");
        trace_steps = env ? (uint64_t)strtoull(env, NULL, 0) : 0u;
        env = getenv("MGS_TRACE_FROM");
        trace_from = env ? (uint64_t)strtoull(env, NULL, 0) : 0u;
        /* MGS_TRACE_EVERY samples a window periodically instead of once, so
         * a long run can be asked "what is it doing NOW" at intervals
         * without producing 40 million lines. */
        env = getenv("MGS_TRACE_EVERY");
        trace_every = env ? (uint64_t)strtoull(env, NULL, 0) : 0u;
    }

    /* A ring of recent addresses. A stop address says where execution ended;
     * for a jump to a bad address the interesting part is what branched
     * there, and that is always a few steps back. */
    #define RECENT 12
    static uint32_t recent[RECENT];
    unsigned recent_n = 0u;

    memset(&r, 0, sizeof r);
    for (r.steps = 0; r.steps < max_steps; ++r.steps) {
        uint32_t pc;

        /* Guest time advances with the run loop. The Gekko timebase is
         * 40.5 MHz - the 162 MHz bus divided by four - so a 60 Hz field is
         * 675,000 ticks. Without this every timed wait in the SDK spins
         * forever, which is exactly what __OSInitAudioSystem was doing:
         * 13 million OSGetTick calls against a clock that never moved. */
        mgs_runtime_advance_ticks(mgs_runtime_from(NULL), 32u);

        /* Advance the video beam on a cadence, so a guest polling for retrace
         * sees time pass at the rate the host runs rather than as fast as it
         * can spin. Tied to steps rather than wall clock for now: a replayed
         * run must be reproducible, and wall clock is not.
         *
         * A retrace interrupt goes with it. The beam moving is what a polling
         * loop sees; the interrupt is what a waiting THREAD needs, and the
         * SDK's boot waits rather than polls. Raising one without the other
         * leaves half the guest satisfied.
         *
         * THIS MUST COME BEFORE pc IS READ. Raising an interrupt moves the
         * pc to the guest's dispatcher; reading pc first and dispatching that
         * stale value re-enters the interrupted code instead of the handler,
         * with the exception's MSR still in force - so interrupts are
         * permanently disabled from then on and exactly one is ever
         * delivered. That is precisely how this presented. */
        if ((r.steps % 2000ull) == 0ull) {
            mgs_mmio_tick_frame(mgs_host_mmio());
            mgs_interrupt_vi(mod, cpu);
            ++r.frames;
        }

        /* The graphics processor's completion signal, checked far more often
         * than retrace: the game blocks on it inside a frame, so answering it
         * only at the next retrace would halve the frame rate for no reason.
         * Raising it moves the pc, so it belongs here with the others. */
        else if ((r.steps % 64ull) == 0ull)
            mgs_interrupt_pe_finish(mod, cpu);

        /* Host-driven work that must run on the guest thread. Like the
         * interrupt above, this can move the pc, so it comes BEFORE pc is
         * read. */
        if ((r.steps % 512ull) == 0ull && s_pump)
            s_pump(mod, cpu, s_pump_user);

        pc = mgs_module_pc(cpu);
        recent[recent_n % RECENT] = pc;
        ++recent_n;

        if (trace_every ? ((r.steps % trace_every) < trace_steps)
                        : (r.steps >= trace_from && r.steps < trace_from + trace_steps))
            fprintf(stderr, "  step %llu  pc = 0x%08X\n",
                    (unsigned long long)r.steps, pc);

        /* Refill the cycle budget. The translated code decrements it and
         * returns when it hits zero; leaving it empty would return
         * immediately every time and make no progress at all. */
        {
            uint32_t budget = 100000u;
            memcpy((uint8_t*)cpu + CPU_DOWNCOUNT, &budget, sizeof budget);
        }

        /* An exception vector is not a gap in the translation. The guest
         * took an exception, and the handler that would service it is copied
         * into low memory by the OS at runtime - so it exists in no generated
         * chunk and dispatch legitimately has no code for it.
         *
         * Servicing them here is honest rather than a shortcut: this host has
         * no supervisor mode and no real interrupts, so the state transition
         * an exception performs on hardware has no counterpart. What the
         * guest needs is to resume at srr0, which is exactly what its own
         * handler would do.
         */
        {
            int v = service_vector(cpu, pc, &r.syscalls, &r.fp_switches);
            if (v > 0) continue;
            if (v < 0) {
                r.stop = MGS_STOP_EXCEPTION;
                r.pc = pc;
                memcpy(&r.exception, (uint8_t*)cpu + CPU_EXCEPTION, 4);
                memcpy(&r.program_cause, (uint8_t*)cpu + CPU_PROGRAM_EXC, 4);
                memcpy(&r.srr0, (uint8_t*)cpu + CPU_SRR0, 4);
                memcpy(&r.msr, (uint8_t*)cpu + CPU_SRR1, 4);
                return r;
            }
        }

        if (!mod->dispatch(cpu, pc)) {
            r.stop = MGS_STOP_UNCOVERED;
            r.pc = pc;
            /* An uncovered address at an exception vector is not a gap in the
             * translation - it is the guest taking an exception the OS has
             * not installed a handler for yet. Reporting the cause turns
             * "stopped at 0x700" into something actionable. */
            memcpy(&r.exception, (uint8_t*)cpu + CPU_EXCEPTION, 4);
            memcpy(&r.program_cause, (uint8_t*)cpu + CPU_PROGRAM_EXC, 4);
            memcpy(&r.srr0, (uint8_t*)cpu + CPU_SRR0, 4);
            memcpy(&r.msr, (uint8_t*)cpu + CPU_MSR, 4);
            {
                unsigned k, start = recent_n > RECENT ? recent_n - RECENT : 0u;
                fprintf(stderr, "  path in:");
                for (k = start; k < recent_n; ++k)
                    fprintf(stderr, " 0x%08X", recent[k % RECENT]);
                fprintf(stderr, "\n");
            }
            return r;
        }

        /* A repeated pc is NOT evidence of spinning. Translated code returns
         * to the host whenever its cycle budget expires, so a long loop -
         * __fill_mem clearing 615 KB of .bss, say - re-enters at the same
         * loop head thousands of times while making perfect progress. A
         * threshold low enough to catch a genuine spin quickly would report
         * every large memset as a hang, which is how this first presented.
         *
         * So the bar is high, and it only bounds how long the host waits
         * before saying something; it is not a correctness check. */
        if (pc == last_pc) {
            if (++same_pc > 2000000u) {
                r.stop = MGS_STOP_SPINNING;
                r.pc = pc;
                /* The pc alone says where, never why. The path in and the
                 * MSR say whether the guest is waiting for an interrupt it
                 * has disabled - which is a host bug - or for data that is
                 * genuinely not arriving, which is not. */
                {
                    unsigned k, start = recent_n > RECENT ? recent_n - RECENT : 0u;
                    fprintf(stderr, "  path in:");
                    for (k = start; k < recent_n; ++k)
                        fprintf(stderr, " 0x%08X", recent[k % RECENT]);
                    fprintf(stderr, "\n");
                }
                memcpy(&r.msr, (uint8_t*)cpu + CPU_MSR, 4);
                return r;
            }
        } else {
            same_pc = 0u;
            last_pc = pc;
        }
    }
    r.stop = MGS_STOP_STEP_LIMIT;
    r.pc = mgs_module_pc(cpu);
    return r;
}

/* Call a guest function from the host, and return when it does.
 *
 * Needed because an interrupt is not something the host can simulate from the
 * outside: the SDK's handler runs GUEST code, touches guest structures, and
 * wakes guest threads. So the host has to be able to enter the guest, run a
 * function to completion, and come back with everything else undisturbed.
 *
 * The return is detected with a SENTINEL link register. A real `blr` jumps to
 * whatever lr held, so setting lr to an address outside every code range
 * makes the function's own return land somewhere recognisable - and lets the
 * run loop stop without needing to understand the callee at all.
 *
 * Registers are saved and restored around the call because the interrupted
 * guest code is entitled to find them exactly as it left them. Not doing that
 * corrupts whatever was running, intermittently and far from the cause.
 */
/* The address a host-initiated guest call "returns" to.
 *
 * MUST BE 4-BYTE ALIGNED. `blr` branches to lr with the low two bits
 * ignored, so an odd sentinel is never the pc the guest arrives at: the
 * call runs to completion, returns correctly, and the host then waits for
 * an address that cannot occur. It presented as "callback did not return;
 * gave up at 0x0DEADBEC" - two less than the sentinel, which is exactly the
 * masking.
 *
 * It must also be an address no chunk covers, so a genuine branch there is
 * impossible. Low memory below the exception vectors is not usable for this:
 * the guest puts real code there. */
#define MGS_GUEST_RETURN_SENTINEL 0x0DEADBECu

/* Where the last nested guest call gave up, or 0. A call that does not reach
 * its return sentinel is reported by its caller as "not delivered", which says
 * nothing about why; this does. */
static uint32_t s_call_fail_pc;

uint32_t mgs_module_call_fail_pc(void) { return s_call_fail_pc; }

int mgs_module_call_guest(const MgsModule* mod, void* cpu, uint32_t address,
                          const uint32_t* args, unsigned arg_count,
                          uint64_t max_steps)
{
    uint32_t saved_gpr[32];
    uint32_t saved_pc, saved_lr;
    uint32_t* gpr = mgs_module_gpr(cpu);
    uint64_t step;
    unsigned i;
    int returned = 0;

    memcpy(saved_gpr, gpr, sizeof saved_gpr);
    saved_pc = mgs_module_pc(cpu);
    saved_lr = mgs_module_lr(cpu);

    for (i = 0; i < arg_count && i < 8u; ++i) gpr[3 + i] = args[i];
    mgs_module_set_lr(cpu, MGS_GUEST_RETURN_SENTINEL);
    mgs_module_set_pc(cpu, address);

    s_call_fail_pc = 0u;
    for (step = 0; step < max_steps; ++step) {
        uint32_t pc = mgs_module_pc(cpu);
        if (pc == MGS_GUEST_RETURN_SENTINEL) { returned = 1; break; }
        {
            uint32_t budget = 100000u;
            memcpy((uint8_t*)cpu + CPU_DOWNCOUNT, &budget, sizeof budget);
        }
        {
            int v = service_vector(cpu, pc, NULL, NULL);
            if (v > 0) continue;
            if (v < 0) { s_call_fail_pc = pc; break; }
        }
        if (!mod->dispatch(cpu, pc)) {
            s_call_fail_pc = pc;              /* leave the reason visible */
            break;
        }
    }

    memcpy(gpr, saved_gpr, sizeof saved_gpr);
    mgs_module_set_pc(cpu, saved_pc);
    mgs_module_set_lr(cpu, saved_lr);
    return returned;
}

void mgs_module_unload(MgsModule* mod)
{
    if (mod->handle) dlclose(mod->handle);
    memset(mod, 0, sizeof *mod);
}
