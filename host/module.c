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
#include "platform/sdl_video.h"
#include "module.h"
#include "platform/mmio.h"
#include "os/os_runtime.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
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
 * depends on nothing but the CPUState offsets above.
 *
 * BOUNDS-CHECKED, and that is not defensiveness. These accessors read guest
 * POINTERS - a thread's link field, a context's address - and a guest pointer
 * can be garbage long before anything else notices: a thread list walked
 * during a crash dump is walking whatever the crash left behind. Folding an
 * address and indexing without a check turns the host into a second
 * casualty, and the resulting segfault is in the diagnostic rather than in
 * the bug, which is exactly where it is least useful. That is how this
 * presented: the host crashed inside its own thread dump while reporting a
 * guest panic.
 */
#define MGS_MEM1_SIZE (24u * 1024u * 1024u)
#define MGS_VMEM_BASE 0x7E000000u
#define MGS_VMEM_SIZE (32u * 1024u * 1024u)

/* The second addressable window, which these accessors have to know about
 * because the overlay lives in it. Bounded to MEM1 alone, a read of the
 * module header at 0x7F008000 returns zero - and zero is a valid-looking
 * header, so the failure is silent. */
static uint8_t* s_vmem;

void mgs_module_set_vmem(uint8_t* vmem);
void mgs_module_set_vmem(uint8_t* vmem) { s_vmem = vmem; }

static uint8_t* gptr(void* cpu, uint32_t addr, uint32_t size)
{
    uint32_t off;
    uint8_t* ram;

    if (addr - MGS_VMEM_BASE < MGS_VMEM_SIZE) {
        off = addr - MGS_VMEM_BASE;
        if (!s_vmem || size > MGS_VMEM_SIZE - off) return NULL;
        return s_vmem + off;
    }

    off = addr & 0x3FFFFFFFu;                 /* fold 0x8... and 0xC... */
    ram = cpu_ram(cpu);
    if (!ram || off > MGS_MEM1_SIZE || size > MGS_MEM1_SIZE - off) return NULL;
    return ram + off;
}

static uint32_t gread32(void* cpu, uint32_t addr)
{
    const uint8_t* p = gptr(cpu, addr, 4u);
    if (!p) return 0u;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void gwrite32(void* cpu, uint32_t addr, uint32_t v)
{
    uint8_t* p = gptr(cpu, addr, 4u);
    if (!p) return;
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint16_t gread16(void* cpu, uint32_t addr)
{
    const uint8_t* p = gptr(cpu, addr, 2u);
    if (!p) return 0u;
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void gwrite16(void* cpu, uint32_t addr, uint16_t v)
{
    uint8_t* p = gptr(cpu, addr, 2u);
    if (!p) return;
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
/* MEM1 ONLY, AND STRICTLY. Widening this to the overlay's BAT-mapped window
 * was tried and is WRONG (F237): it let `fpu_save`/`fpu_load` write 768 bytes
 * through pointers that were not contexts at all, and the run got worse, not
 * better - 53M steps to a wild jump against 113M to a clean report, with zero
 * voice advances and an uninitialised ring. The strictness is load-bearing:
 * this accessor writes a register file through a guest pointer, and refusing
 * to guess is the whole point of it. What the refusal costs is a real
 * exception reported instead of serviced, which is a diagnostic problem, not
 * a reason to write through an address that failed its own check. */
static int context_is_sane(uint32_t addr)
{
    uint32_t off = addr & 0x3FFFFFFFu;
    return addr != 0u && (addr >> 28) >= 0x8u && (off & 7u) == 0u &&
           off + 768u <= 24u * 1024u * 1024u;
}

/* THE OS VOUCHING FOR A CONTEXT, which a range test cannot do.
 *
 * `OSContext` is the first member of `OSThread` (dolsdk2004 OSThread.h), so a
 * genuine current context IS the current thread's address. That makes
 * `OSCurrentContext == OSCurrentThread` a structural check that a stretch of
 * uninitialised memory cannot pass by accident - unlike "is it in a window",
 * which is what `context_is_sane` tests and why it refuses the engine's own
 * worker threads, whose contexts live in the overlay's BAT-mapped region.
 *
 * Used ONLY by the lazy-FP path. `mgs_module_take_exception` keeps the strict
 * MEM1 test: widening THAT changes interrupt dispatch for the whole boot,
 * which is a much larger blast radius than one exception vector.
 */
#define OS_CURRENTTHREAD 0x800000E4u

static int context_vouched_for(void* cpu, uint32_t addr)
{
    if (context_is_sane(addr)) return 1;
    if ((addr & 7u) != 0u) return 0;
    if (addr - MGS_VMEM_BASE >= MGS_VMEM_SIZE) return 0;
    if (addr - MGS_VMEM_BASE + 768u > MGS_VMEM_SIZE) return 0;
    return addr == gread32(cpu, OS_CURRENTTHREAD);
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

    if (!context_vouched_for(cpu, ctx)) {
        /* Say WHICH address was refused, once. "Unhandled exception at
         * 0x800" with no context value is what made F236's crash look like
         * an audio bug for as long as it did. */
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "[fp] refused: OSCurrentContext = 0x%08X is not "
                            "a usable context\n", ctx);
        }
        return 0;                /* no current context: report, do not guess */
    }

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
/* gpr, fpr, ps1, pc, lr, ctr, cr, xer, fpscr - everything below msr at 664. */
#define CPU_VOLATILE_BYTES 664u

#define MSR_EE          0x8000u

uint32_t* mgs_module_msr_ptr(void* cpu)
{
    return (uint32_t*)((uint8_t*)cpu + CPU_MSR);
}

/* The link register, for shims that want to know WHO called them.
 *
 * A shim reads its arguments from the register file and that says what was
 * asked for; the link register says who asked. For a DVD read that is the
 * difference between "the movie was read eight times" and "the movie was
 * read eight times by this function", which is the whole question when
 * reads stop and nothing says why. */
uint32_t* mgs_module_lr_ptr(void* cpu)
{
    return (uint32_t*)((uint8_t*)cpu + CPU_LR_OFFSET);
}

uint32_t mgs_module_guest_read32(void* cpu, uint32_t addr)
{
    return gread32(cpu, addr);
}

void mgs_module_guest_write32(void* cpu, uint32_t addr, uint32_t v)
{
    gwrite32(cpu, addr, v);
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

/* Set by a signal handler, read by the run loop.
 *
 * A long run's numbers are printed when it finishes, which is no use when the
 * interesting question is "what is it doing right now" and the answer takes
 * ten minutes to arrive. Ctrl-C now ends the run the same way the step limit
 * does, so the report still happens. `volatile sig_atomic_t` because that is
 * the only thing a handler may portably touch. */
volatile sig_atomic_t mgs_module_interrupted;

/* MGS_WATCH: a guest word to report every change of, with the writer.
 *
 * NAMED AWAY FROM `s_watch_addr`, which is already taken further down for
 * the OSLink call watch - and taken in a way the compiler will not warn
 * about: two file-scope `static uint32_t s_watch_addr;` are tentative
 * definitions of ONE object in C, so declaring it again silently aliased it.
 * Setting it here from the environment then overwrote OSLink's address every
 * run, the overlay's .bss base was never captured, and the boot stopped
 * after loading the module - one file read, one frame drawn. */
static uint32_t s_memwatch_addr, s_memwatch_last;
/* MGS_WATCH=<addr>[:<length>] - a range reports WHICH WORD moved.
 *
 * A single address answers "did this change"; a range answers "which field
 * changed", which is the question that keeps coming up and that a single
 * address cannot. Walking the movie stall meant guessing the next field to
 * look at and paying a run for each guess. Capped, because this is compared
 * every step. */
#define MGS_WATCH_MAX 0x400u
static uint32_t s_memwatch_len;
static uint8_t  s_memwatch_prev[MGS_WATCH_MAX];

/* MGS_TRACE_FN=<addr>[,<addr>...]: guest functions to report calls to, with
 * their arguments and results. MGS_TRACE_FN_MAX caps the output. */
#define MGS_FNTRACE_MAX 8u
static uint32_t s_fntrace[MGS_FNTRACE_MAX];
static unsigned s_fntrace_n, s_fntrace_lines, s_fntrace_cap = 200u;
static struct { uint32_t ret, fn; } s_fnret[32];
static unsigned s_fnret_n;
/* Counted as well as printed, because the question is often "how many" and
 * a hot function answers it by drowning the log. MGS_TRACE_FN_MAX=0 counts
 * without printing a line. */
static uint64_t s_fntrace_hits[MGS_FNTRACE_MAX];

/* One step of the function trace, called from BOTH dispatch loops.
 *
 * The run loop is not the only place guest code is entered: a callback the
 * host invokes runs in mgs_module_call_guest's own loop, and tracing only
 * the first made `gcn_stream_read_done` read as 0 calls while the host's
 * counter said 406 callbacks had run (F203). Both loops now go through
 * here.
 *
 * WHAT THIS STILL CANNOT SEE, AND IT MATTERS (F217). Both loops observe `pc`
 * only at a DISPATCH BOUNDARY. Translated code calls other translated
 * functions directly, in C, without returning here - so a function is
 * counted only when it happens to be where a dispatch starts. Proven:
 * fn_80053B60 is reached by exactly one `bl`, a watch caught it writing
 * guest memory, and this reported 0 calls for it.
 *
 * So these numbers are LOWER BOUNDS, not counts. Use this to learn WHO
 * called something and WITH WHAT - the arguments and caller are sound - and
 * put a watch on a location the function writes when the question is HOW
 * MANY. */
static void fntrace_step(void* cpu, uint32_t pc)
{
    unsigned i;

    if (s_fntrace_n) {
        for (i = 0u; i < s_fntrace_n; ++i) {
            if (pc != s_fntrace[i]) continue;
            ++s_fntrace_hits[i];
            if (s_fntrace_lines < s_fntrace_cap) {
                const uint32_t* g = mgs_module_gpr(cpu);
                ++s_fntrace_lines;
                fprintf(stderr, "[fn] 0x%08X(0x%08X, 0x%08X, 0x%08X, 0x%08X)"
                                " from 0x%08X\n",
                        pc, g[3], g[4], g[5], g[6],
                        *mgs_module_lr_ptr(cpu));
            }
            if (s_fnret_n < (unsigned)(sizeof s_fnret / sizeof s_fnret[0])) {
                s_fnret[s_fnret_n].ret = *mgs_module_lr_ptr(cpu);
                s_fnret[s_fnret_n].fn  = pc;
                ++s_fnret_n;
            }
            break;
        }
    }
    if (s_fnret_n) {
        unsigned k = s_fnret_n;
        while (k--) {
            if (s_fnret[k].ret != pc) continue;
            if (s_fntrace_lines < s_fntrace_cap) {
                ++s_fntrace_lines;
                fprintf(stderr, "[fn] 0x%08X -> 0x%08X\n",
                        s_fnret[k].fn, mgs_module_gpr(cpu)[3]);
            }
            s_fnret_n = k;   /* and drop anything above: never came back */
            break;
        }
    }
}

/* The last pc the run loop saw, for a process that has to be killed.
 *
 * A wedge inside a single dispatch call cannot be reported by any of the
 * normal routes: the run loop never comes back, so the step limit, the spin
 * detector and the end-of-run report are all unreachable, and the only thing
 * that happens is that `timeout` eventually kills the process with nothing
 * printed at all. Two runs ended that way with no idea where.
 *
 * Updated every step and read from the signal handler. `volatile` and a
 * plain 32-bit store because that is what a handler may safely read; it is a
 * diagnostic, and a torn value would still name the right neighbourhood. */
volatile uint32_t mgs_module_last_pc;

/* WHICH PART OF THE HOST is running, for the same reason.
 *
 * Knowing the guest pc says where the GAME is; it does not say whether the
 * host is executing translated code, parsing a GX command, filling a
 * triangle or copying a framebuffer - and those want completely different
 * investigations. Letting the rasteriser abandon its work did not release a
 * wedged run, which ruled out the triangle fill and left everything else.
 *
 * A pointer store to a string literal: no allocation, nothing to free, and
 * safe to read from a signal handler. */
volatile const char* mgs_module_phase = "start";

/* Cycles the translated code may run per dispatch call. See MGS_BUDGET. */
static uint32_t s_budget = 100000u;

/* MGS_CYCLE_CENSUS: guest cycles run per dispatch call. */
static int s_cycle_census;
static uint64_t s_cycles_run, s_dispatches;
uint64_t mgs_module_cycles_run(void);
uint64_t mgs_module_cycles_run(void) { return s_cycles_run; }
uint64_t mgs_module_dispatches(void);
uint64_t mgs_module_dispatches(void) { return s_dispatches; }

static MgsPump s_pump;
static void*   s_pump_user;

/* Called from the run loop to drain the graphics command stream. Held as a
 * bare callback so this file needs no GX header. */
static void (*s_display)(void);

static void (*s_frame)(void);

/* STEPS BETWEEN RETRACES, DERIVED FROM THE CLOCK RATHER THAN GUESSED.
 *
 * A 60 Hz field is 675,000 ticks of the Gekko's 40.5 MHz time base. This was
 * a flat 2,000 steps, which at 32 ticks a step is 64,000 ticks a field -
 * so the screen advanced 10.5 times faster than guest time, and the two are
 * not independent: a run showed 200,000 retraces (3,333 s of video at 60 Hz)
 * against the audio DMA's own 314.66 s of sound, a ratio of 10.6.
 *
 * That matters here because this game's movie clock is SLAVED TO THE AUDIO:
 * `mpeg_movie_task` sets `stream->0x08` from the sound system's playback
 * position scaled by 300/1000, so with the screen running ten times ahead of
 * the audio, every decoded movie frame is held for about ten screen frames -
 * which is exactly "it plays the first frame or two of each chunk".
 *
 * Deriving it from the tick rate keeps the two in step whatever the rate is,
 * instead of leaving two constants that have to be changed together and
 * were not. MGS_RETRACE_STEPS overrides it for measurement.
 */
static unsigned mgs_tick_rate(void);

static unsigned long long mgs_retrace_period(void)
{
    static unsigned long long forced;
    static int asked;
    unsigned long long period;

    if (!asked) {
        const char* e = getenv("MGS_RETRACE_STEPS");
        asked = 1;
        forced = (e && *e) ? strtoull(e, NULL, 10) : 0ull;
    }
    if (forced) return forced;

    /* THE FIELD PERIOD COMES FROM THE GUEST'S OWN VI REGISTER, not a
     * constant here. It used to be 675000 - NTSC's - and this is the PAL
     * disc, so the screen advanced 60 times a second where the console
     * advances it 50. See the long note in runtime/platform/mmio.c.
     *
     * Read rather than cached because the guest programs the format in
     * VIConfigure, well after the first call here, and a value latched
     * before that would be the reset default for the whole run. */
    period = (unsigned long long)mgs_mmio_vi_field_ticks(mgs_host_mmio())
           / mgs_tick_rate();
    return period ? period : 1ull;
}

/* Ticks of guest time per interpreted step. See the note at its use.
 *
 * THIS IS THE RATE THE GUEST'S CLOCK RUNS AT RELATIVE TO ITS OWN WORK, and
 * at 32 it ran four times too fast. Everything timed hangs off it: the
 * retrace period is the field's tick count divided by this, and the audio
 * DMA drains on it too, so the two stay in step with each other whatever it
 * is - which is why the video/audio ratio looked healthy at 32 and hid the
 * problem.
 *
 * What it does NOT keep in step is the guest's clock against the guest's
 * WORK. Counting XFB copies against fields over 200M steps:
 *
 *     rate 32   7903 fields    920 frames   1 frame per 8.6 fields
 *     rate 16   3952 fields    918 frames   1 frame per 4.3
 *     rate  8   1977 fields   1095 frames   1 frame per 1.8
 *     rate  4    989 fields    750 frames   1 frame per 1.3
 *
 * A PAL game at 25 fps on 50 Hz fields is one frame per two fields. At 32
 * the game rendered one frame every 8.6 fields - about 6 fps - which is the
 * "it runs slowly" the user reported, and it is not a host performance
 * problem: the host simulates guest time several times faster than real.
 * The game was simply being told that far more time had passed than it had
 * had steps to act on.
 *
 * Eight is also what the hardware suggests rather than only what the
 * measurement prefers. The Gekko's timebase is the 162 MHz bus divided by
 * four, 40.5 MHz, against a 486 MHz core - twelve CPU cycles per tick. So
 * eight ticks per dispatch is about ninety-six guest instructions per
 * chunk, which is a plausible chunk; thirty-two would be nearly four
 * hundred, which is not.
 *
 * Measured over 400M steps, the audible difference is not subtle:
 *
 *     rate 32    285 of 62,731 AX frames not silent   (0.5%)
 *     rate  8  9,961 of 15,340                        (65%)
 *
 * because voices were being faded and retired on a clock running ahead of
 * the code that feeds them (F268).
 *
 * NOW 4, NOT 8, BECAUSE THE CPU NO LONGER NEEDS CAPPING (F281). This is a
 * step budget per unit of guest time, so a LOW number is a fast guest: at 4
 * the engine renders 24.1 fps against the console's own 13.1, because the
 * console is CPU-bound in this scene and a modern host is not. It used to
 * be held at 8 because everything below it collapsed the sound - and that
 * turned out to be two bugs in this file and the run loop, not a real
 * constraint. With them fixed, 8, 4 and 2 all stream the demo and the movie
 * and differ only in speed:
 *
 *     rate 8   11.5 fps   voice-mixes 72,274   demo 285   movie 63
 *     rate 4   24.1 fps   voice-mixes 68,862   demo 280   movie 62
 *     rate 2   26.7 fps   voice-mixes 73,650   demo 306   movie 62
 *
 * The game caps itself near one frame per two fields, so below 4 there is
 * little left to win and each halving doubles the host work spent spinning
 * in the guest's own retrace wait. Presentation is paced separately, at the
 * disc's field rate - see mgs_display_set_fps_cap in host/main.c.
 */
static unsigned mgs_tick_rate(void)
{
    static unsigned rate;
    if (!rate) {
        const char* e = getenv("MGS_TICK_RATE");
        rate = (e && *e) ? (unsigned)strtoul(e, NULL, 10) : 4u;
        if (!rate) rate = 1u;
    }
    return rate;
}

/* Whatever the host wants the heartbeat to report. Kept as a callback so
 * this file needs no GX or DVD header. */
static uint64_t (*s_progress)(unsigned which);

void mgs_module_set_progress(uint64_t (*fn)(unsigned which));
void mgs_module_set_progress(uint64_t (*fn)(unsigned which)) { s_progress = fn; }

/* Addresses the host wants to observe the guest reaching, and what it saw.
 *
 * Watching a call is not the same as replacing it. The guest runs OSLink
 * itself; the host only reads the arguments on the way past, because they
 * carry something nothing else reports - where the overlay's .bss was
 * allocated. Every engine global lives at an offset from that, so without it
 * the host can name a structure but cannot look at one.
 */
static uint32_t s_watch_addr;
static int      s_relink_bss = 1;
static uint32_t s_watch_r3, s_watch_r4;
static int      s_watch_seen;

/* A second, chattier watch: every call to one address, with its arguments.
 * The first watch answers "what was it called with"; this one answers "how
 * many times, and in what order", which is what a resource running out
 * needs. */
static uint32_t s_trace_addr, s_trace_addr2, s_trace_addr3, s_trace_addr4;
/* The OR of the four, so the per-instruction path tests one thing. */
static uint32_t s_trace_any;
static void (*s_trace_fn)(void* cpu, const uint32_t* gpr);
static void (*s_trace_fn2)(void* cpu, const uint32_t* gpr);
static void (*s_trace_fn3)(void* cpu, const uint32_t* gpr);
static void (*s_trace_fn4)(void* cpu, const uint32_t* gpr);

void mgs_module_trace_calls(uint32_t address,
                            void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls(uint32_t address,
                            void (*fn)(void* cpu, const uint32_t* gpr))
{
    s_trace_addr = address; s_trace_fn = fn;
    s_trace_any = s_trace_addr | s_trace_addr2 | s_trace_addr3 | s_trace_addr4;
}

void mgs_module_trace_calls2(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls2(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr))
{
    s_trace_addr2 = address; s_trace_fn2 = fn;
    s_trace_any = s_trace_addr | s_trace_addr2 | s_trace_addr3 | s_trace_addr4;
}

void mgs_module_trace_calls3(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls3(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr))
{
    s_trace_addr3 = address; s_trace_fn3 = fn;
    s_trace_any = s_trace_addr | s_trace_addr2 | s_trace_addr3 | s_trace_addr4;
}

void mgs_module_trace_calls4(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls4(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr))
{
    s_trace_addr4 = address; s_trace_fn4 = fn;
    s_trace_any = s_trace_addr | s_trace_addr2 | s_trace_addr3 | s_trace_addr4;
}

void mgs_module_watch(uint32_t address);
void mgs_module_watch(uint32_t address) { s_watch_addr = address; s_watch_seen = 0; }
void mgs_module_relink_bss(int on) { s_relink_bss = on; }

/* Called once, the first time the guest executes in the overlay AFTER the
 * watched call returned - which is to say, when linking is finished and the
 * module's own code is about to run. Anything that has to happen between
 * those two moments happens here, and nowhere else is that window visible. */
static void (*s_linked)(void* cpu, uint32_t module);
static int s_linked_done;

void mgs_module_on_linked(void (*fn)(void* cpu, uint32_t module));
void mgs_module_on_linked(void (*fn)(void* cpu, uint32_t module))
{
    s_linked = fn; s_linked_done = 0;
}

int mgs_module_watch_result(uint32_t* r3, uint32_t* r4);
int mgs_module_watch_result(uint32_t* r3, uint32_t* r4)
{
    if (!s_watch_seen) return 0;
    if (r3) *r3 = s_watch_r3;
    if (r4) *r4 = s_watch_r4;
    return 1;
}

void mgs_module_set_display(void (*fn)(void));
void mgs_module_set_display(void (*fn)(void)) { s_display = fn; }

/* Called once per retrace, to put the external framebuffer on the screen. */
void mgs_module_set_frame(void (*fn)(void));
void mgs_module_set_frame(void (*fn)(void)) { s_frame = fn; }

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
/* A SAMPLING PROFILER over guest addresses.
 *
 * The heartbeat prints one pc every N steps, and that turned out to be worse
 * than useless: the run loop raises a retrace interrupt every 2000 steps, so
 * any N whose remainder mod 2000 is small lands at nearly the same phase of
 * the tick on every sample. With N = 2,000,003 the sample drifted three
 * steps per beat and every beat landed inside __OSDispatchInterrupt - which
 * reads exactly like a hang in the interrupt handler, and is not.
 *
 * So the interval here is PRIME and coprime to every period in the run loop
 * (2000, 512, 256, 64). 1009 shares no factor with any of them, so the
 * sample walks the whole phase space and the histogram reflects where the
 * guest actually spends its time rather than where the host interrupts it.
 *
 * Open addressing, fixed capacity, no allocation: the profiler must not
 * change the behaviour it is measuring. When full it stops learning new
 * addresses and keeps counting known ones, which biases towards the hot
 * addresses - the ones worth seeing.
 */
#define PROF_INTERVAL 1009ull
#define PROF_SLOTS    16384u

static uint32_t s_prof_pc[PROF_SLOTS];
static uint32_t s_prof_hits[PROF_SLOTS];
static uint64_t s_prof_samples;
static unsigned s_prof_used;

static void prof_sample(uint32_t pc)
{
    unsigned i = (unsigned)((pc >> 2) * 2654435761u) & (PROF_SLOTS - 1u);
    unsigned probe;

    ++s_prof_samples;
    for (probe = 0u; probe < 64u; ++probe) {
        unsigned k = (i + probe) & (PROF_SLOTS - 1u);
        if (s_prof_hits[k] && s_prof_pc[k] != pc) continue;
        if (!s_prof_hits[k]) {
            if (s_prof_used >= PROF_SLOTS - (PROF_SLOTS / 8u)) return;
            s_prof_pc[k] = pc;
            ++s_prof_used;
        }
        ++s_prof_hits[k];
        return;
    }
}

/* CALLER profiling, for a single chosen address.
 *
 * The pc histogram says memcpy is 63% of the run; it cannot say who is
 * calling it, and that is the whole question. On entry to a function the
 * link register still holds the return address, so sampling lr the moment
 * the guest arrives at a chosen pc attributes the call to its caller.
 *
 * Every arrival is counted, not a sample of them: a caller that runs once
 * with a 4 MB copy matters as much as one that runs ten thousand times, and
 * sampling would hide exactly that asymmetry.
 */
static uint32_t s_caller_pc[PROF_SLOTS];
static uint32_t s_caller_hits[PROF_SLOTS];
static uint64_t s_caller_bytes[PROF_SLOTS];
static uint64_t s_caller_total;
static unsigned s_caller_used;

static void caller_sample(uint32_t lr, uint32_t bytes)
{
    unsigned i = (unsigned)((lr >> 2) * 2654435761u) & (PROF_SLOTS - 1u);
    unsigned probe;

    ++s_caller_total;
    for (probe = 0u; probe < 64u; ++probe) {
        unsigned k = (i + probe) & (PROF_SLOTS - 1u);
        if (s_caller_hits[k] && s_caller_pc[k] != lr) continue;
        if (!s_caller_hits[k]) {
            if (s_caller_used >= PROF_SLOTS - (PROF_SLOTS / 8u)) return;
            s_caller_pc[k] = lr;
            ++s_caller_used;
        }
        ++s_caller_hits[k];
        s_caller_bytes[k] += bytes;
        return;
    }
}

/* Dump the histogram, hottest first, as plain "count address" lines.
 *
 * Deliberately NOT resolved to names here: the host has no symbol table, and
 * giving it one would mean keeping two copies of config/symbols in step.
 * tools/resolve-addrs.py maps the output through both symbol files, which
 * also means the same dump can be re-resolved as naming improves. */
void mgs_module_profile_dump(FILE* out, unsigned top);
void mgs_module_profile_dump(FILE* out, unsigned top)
{
    unsigned i, n = 0u, shown;
    static unsigned order[PROF_SLOTS];

    if (!s_prof_samples) return;
    for (i = 0u; i < PROF_SLOTS; ++i)
        if (s_prof_hits[i]) order[n++] = i;

    /* Selection of the top `top` rather than a full sort: n is at most a
     * few thousand and this runs once, but a partial pass keeps the dump
     * from being the most expensive thing in a short run. */
    for (shown = 0u; shown < top && shown < n; ++shown) {
        unsigned best = shown, k;
        for (k = shown + 1u; k < n; ++k)
            if (s_prof_hits[order[k]] > s_prof_hits[order[best]]) best = k;
        { unsigned t = order[shown]; order[shown] = order[best]; order[best] = t; }
        fprintf(out, "[prof] %8u  %5.1f%%  0x%08X\n",
                s_prof_hits[order[shown]],
                100.0 * (double)s_prof_hits[order[shown]] / (double)s_prof_samples,
                s_prof_pc[order[shown]]);
    }
    if (s_caller_total) {
        unsigned m = 0u, sh;
        static unsigned corder[PROF_SLOTS];
        for (i = 0u; i < PROF_SLOTS; ++i)
            if (s_caller_hits[i]) corder[m++] = i;
        for (sh = 0u; sh < top && sh < m; ++sh) {
            unsigned best = sh, k;
            for (k = sh + 1u; k < m; ++k)
                if (s_caller_hits[corder[k]] > s_caller_hits[corder[best]]) best = k;
            { unsigned t = corder[sh]; corder[sh] = corder[best]; corder[best] = t; }
            fprintf(out, "[call] %8u calls  %12llu bytes  from 0x%08X\n",
                    s_caller_hits[corder[sh]],
                    (unsigned long long)s_caller_bytes[corder[sh]],
                    s_caller_pc[corder[sh]]);
        }
        fprintf(out, "[call] %llu arrivals from %u distinct callers\n",
                (unsigned long long)s_caller_total, m);
    }
    fprintf(out, "[prof] %llu samples over %u distinct addresses%s\n",
            (unsigned long long)s_prof_samples, n,
            s_prof_used >= PROF_SLOTS - (PROF_SLOTS / 8u) ? " (table full)" : "");
}

MgsRunResult mgs_module_run(const MgsModule* mod, void* cpu, uint64_t max_steps)
{
    MgsRunResult r;
    uint64_t gt = 0;   /* guest ticks, for the periodic hooks */
    /* NEXT-DEADLINE scheduling, not modulo.
     *
     * `gt` advances by MGS_TICK_RATE every step, so it only ever takes
     * multiples of that rate - and `gt % N == 0` can therefore only fire
     * when the rate DIVIDES N. Every period here is a multiple of 8, so at
     * rate 8 or 4 they divide and fire correctly, and at 7 they fire once
     * every lcm(7, N) ticks instead: seven times too rarely. Measured, the
     * DSP task offer went from 219,565 offers to 31,366, which starved
     * __AXOutDspReady, which stopped __AXServiceVPB (58,015 calls to 254),
     * which stopped every voice.
     *
     * A deadline fires at the right rate whatever the increment. */
    /* HOISTED OUT OF THE STEP LOOP.
     *
     * These were looked up per step, ten million times a second, and the
     * retrace check divided and then took a modulo - two 64-bit divisions
     * every instruction - to ask a question that changes fifty times a
     * second. The port runs about 9% slower than real time in this scene
     * and that is audible: guest time advances per step, so a field is
     * 810,000/4 = 202,500 steps and fifty fields a second needs 10.1M
     * steps/s against the 9.3M being managed. The audio device is fed from
     * that same clock, so the shortfall is heard directly as gaps.
     *
     * The field period is still READ rather than latched, for the reason
     * the note below gives - the guest programmes the format in
     * VIConfigure, long after the first call - but it is read once per
     * field instead of once per instruction. */
    MgsRuntime* const rt = mgs_runtime_from(NULL);
    MgsMmio* const mmio_p = mgs_host_mmio();
    const unsigned tick_rate = mgs_tick_rate();
    unsigned long long forced_retrace = 0ull;
    uint64_t due_vi = 0;
    uint64_t pending_ticks = 0;
    unsigned tick_batch = 0;
    uint64_t due_pe = 0, due_dsp = 0, due_pend = 0, due_aram = 0,
             due_aid = 0, due_pump = 0, due_disp = 0;
    uint32_t last_pc = 0u;
    uint64_t same_pc = 0u;
    uint64_t trace_steps = 0u;
    uint64_t trace_from = 0u;
    uint64_t trace_every = 0u;
    uint64_t heartbeat = 0u;
    int profile = 0;
    uint32_t caller_of = 0u;
    unsigned caller_size_reg = 5u;

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
        env = getenv("MGS_HEARTBEAT");
        heartbeat = env ? (uint64_t)strtoull(env, NULL, 0) : 0u;
        profile = getenv("MGS_PROFILE") != NULL;
        s_cycle_census = getenv("MGS_CYCLE_CENSUS") != NULL;
        env = getenv("MGS_BUDGET");
        if (env) {
            unsigned long v = strtoul(env, NULL, 0);
            if (v >= 64ul && v <= 10000000ul) s_budget = (uint32_t)v;
        }
        /* MGS_PROFILE_CALLERS=<guest address> attributes arrivals at that
         * address to the callers that got there. MGS_PROFILE_SIZE_REG names
         * the argument register holding a byte count, so the report can rank
         * by bytes moved as well as by call count - memcpy and __fill_mem
         * both take it in r5. */
        env = getenv("MGS_PROFILE_CALLERS");
        caller_of = env ? (uint32_t)strtoul(env, NULL, 0) : 0u;
        env = getenv("MGS_PROFILE_SIZE_REG");
        if (env) caller_size_reg = (unsigned)strtoul(env, NULL, 0);
    }

    /* A ring of recent addresses. A stop address says where execution ended;
     * for a jump to a bad address the interesting part is what branched
     * there, and that is always a few steps back. */
    /* A POWER OF TWO, so the ring index is a mask and not a DIVISION.
     *
     * This ring records the last few program counters for the report that
     * prints when dispatch finds no code for an address. It is written
     * every step - once per 13 guest instructions - and at 12 entries the
     * index cost a 32-bit division each time. Sixteen is the same
     * diagnostic without the divide. */
    #define RECENT 16
    static uint32_t recent[RECENT];
    unsigned recent_n = 0u;

    memset(&r, 0, sizeof r);
    {   /* MGS_RETRACE_STEPS still forces a STEP period, as it did. */
        const char* e = getenv("MGS_RETRACE_STEPS");
        forced_retrace = (e && *e) ? strtoull(e, NULL, 10) : 0ull;
    }
    {
        const char* env = getenv("MGS_WATCH");
        char* end = NULL;
        s_memwatch_addr = env ? (uint32_t)strtoul(env, &end, 0) : 0u;
        s_memwatch_len = 0u;
        if (env && end && *end == ':') {
            unsigned long n = strtoul(end + 1, NULL, 0);
            if (n > MGS_WATCH_MAX) n = MGS_WATCH_MAX;
            s_memwatch_len = (uint32_t)(n & ~3u);
        }
        if (s_memwatch_addr && s_memwatch_len) {
            uint32_t i;
            for (i = 0u; i < s_memwatch_len; i += 4u) {
                uint32_t v = gread32(cpu, s_memwatch_addr + i);
                s_memwatch_prev[i] = (uint8_t)(v >> 24);
                s_memwatch_prev[i + 1u] = (uint8_t)(v >> 16);
                s_memwatch_prev[i + 2u] = (uint8_t)(v >> 8);
                s_memwatch_prev[i + 3u] = (uint8_t)v;
            }
        }
        s_memwatch_last = s_memwatch_addr ? gread32(cpu, s_memwatch_addr) : 0u;
        env = getenv("MGS_TRACE_FN");
        s_fntrace_n = 0u;
        while (env && *env && s_fntrace_n < MGS_FNTRACE_MAX) {
            char* end = NULL;
            uint32_t a = (uint32_t)strtoul(env, &end, 0);
            if (end == env) break;
            if (a) s_fntrace[s_fntrace_n++] = a;
            env = (*end == ',') ? end + 1 : end;
        }
        env = getenv("MGS_TRACE_FN_MAX");
        if (env) s_fntrace_cap = (unsigned)strtoul(env, NULL, 0);
    }
    for (r.steps = 0; r.steps < max_steps && !mgs_module_interrupted; ++r.steps) {
        uint32_t pc;

        /* Guest time advances with the run loop. The Gekko timebase is
         * 40.5 MHz - the 162 MHz bus divided by four - so a 60 Hz field is
         * 675,000 ticks. Without this every timed wait in the SDK spins
         * forever, which is exactly what __OSInitAudioSystem was doing:
         * 13 million OSGetTick calls against a clock that never moved. */
        /* HOW FAST GUEST TIME RUNS, AND WHY 32 IS WRONG.
         *
         * The Gekko's time base counts at the bus clock over four - 40.5 MHz
         * - so one 60 Hz field is 675,000 ticks. Retrace here is every 2,000
         * steps, which at 32 ticks a step makes a field 64,000 ticks: guest
         * time runs about ten times too slowly against the frame rate it is
         * paired with. Anything that compares elapsed time to a frame number
         * - a movie player, most obviously - sees almost no time passing.
         *
         * MGS_TICK_RATE overrides it while that is being measured; the
         * calibrated figure for 60 Hz is 675000/2000 = 337 or 338. */
        /* BATCHED, because these are two out-of-line calls per guest
         * instruction and one of them assembles a register value from
         * bytes before it does anything.
         *
         * The guest's clock is 40.5 MHz and the SDK measures milliseconds
         * against it; flushing every 16 steps quantises it to 64 ticks,
         * which is 1.6 microseconds. Nothing in the SDK can see that. What
         * must stay exact is `gt`, the schedule the periodic hooks run on,
         * and that is accumulated every step as before. */
        pending_ticks += tick_rate;
        if (++tick_batch >= 16u) {
            mgs_runtime_advance_ticks(rt, pending_ticks);
            mgs_mmio_advance_ticks(mmio_p, (uint32_t)pending_ticks);
            pending_ticks = 0;
            tick_batch = 0;
        }
        /* THE PERIODIC HOOKS BELOW RUN ON GUEST TIME, NOT ON STEPS.
         *
         * They used to be `r.steps % N`, which makes every modelled device
         * faster or slower in GUEST time whenever the tick rate changes -
         * the poll that hands the guest its draw-done, the DSP task offer,
         * the DVD service. So the tick rate was not a performance knob, it
         * was a behaviour knob: at 8 the movie streams 65 chunks and the
         * engine renders 12 fps, and at 4 the engine renders 25 fps and the
         * movie stops at 8 chunks (F276).
         *
         * `gt` is that same schedule expressed in guest ticks. The periods
         * are the old step counts times 8, so at MGS_TICK_RATE=8 - what
         * they were tuned at - every hook fires on exactly the step it used
         * to, and at any other rate it fires at the same point in GUEST
         * TIME instead of the same step. */
        gt += tick_rate;
        /* The audio interface's sample counter comes off the same clock,
         * because __AI_SRC_INIT times one against the other. */


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
        if (forced_retrace ? (r.steps % forced_retrace) == 0ull
                           : gt >= due_vi) {
            /* Re-read the field period HERE, once a field, rather than in
             * the step above. A PAL field is 810,000 ticks and an NTSC one
             * 675,000, and the guest can change which. */
            if (!forced_retrace)
                due_vi = gt + (uint64_t)mgs_mmio_vi_field_ticks(mmio_p);
            mgs_mmio_set_pad(mmio_p, mgs_video_pad());
            mgs_mmio_tick_frame(mmio_p);
            mgs_interrupt_vi(mod, cpu);
            if (s_frame) s_frame();
            ++r.frames;
        }

        /* The graphics processor's completion signal, checked far more often
         * than retrace: the game blocks on it inside a frame, so answering it
         * only at the next retrace would halve the frame rate for no reason.
         * Raising it moves the pc, so it belongs here with the others. */
        else if ((gt >= due_pe ? (due_pe = gt + 512ull, 1) : 0))
            mgs_interrupt_pe_finish(mod, cpu);

        /* Submitted DSP tasks report themselves finished.
         *
         * NOT CHAINED ONTO THE ABOVE. The graphics check runs every 64 steps
         * and this one every 4,099; chaining them with `else if` made this
         * branch unreachable for every interval that shares a factor with
         * 64, which is most of them - written as `else if (steps % 4096)` it
         * never ran once, because 4,096 is a multiple of 64.
         *
         * The interval is prime for the same reason the profiler's is: the
         * run loop is full of periodic work, and anything sharing a factor
         * with it samples a fraction of the program. */
        if ((gt >= due_dsp ? (due_dsp = gt + 32792ull, 1) : 0))
            mgs_interrupt_dsp_task(mod, cpu);

        /* Anything left asserted is offered again.
         *
         * NOT CHAINED, and not conditional on an event. The processor
         * interface's line is level-triggered, and the SDK's dispatcher
         * services one source per entry - so a source that loses to a
         * higher-priority one stays pending and must be re-taken. See
         * mgs_interrupt_pending for why dropping it cost the whole boot.
         *
         * The interval is prime, for the reason every interval in this loop
         * is: 64, 2,000 and 4,099 are the periods interrupts are raised on,
         * and anything sharing a factor with them samples a fraction of the
         * program. This must not be one of them, because the case it exists
         * to catch is precisely the one where two of those coincide. */
        if ((gt >= due_pend ? (due_pend = gt + 1688ull, 1) : 0))
            mgs_interrupt_pending(mod, cpu);

        /* Far more often than a task: a transfer finishes as soon as it is
         * started here, and the audio manager waits on each one. */
        if ((gt >= due_aram ? (due_aram = gt + 1016ull, 1) : 0))
            mgs_interrupt_aram(mod, cpu);

        /* And the audio DMA's, which is what asks for the next buffer of
         * sound. Offered often, on a period sharing no factor with the
         * others in this loop: the engine queues a completion every 10,125
         * guest ticks, and one that waits is one the stream waits on. */
        if ((gt >= due_aid ? (due_aid = gt + 712ull, 1) : 0))
            mgs_interrupt_aid(mod, cpu);

        /* MGS_WATCH=<guest address>: who writes that word?
         *
         * A polled watch says a value changed and never says by whom, which
         * is the whole question for the engine's task mask: it is read in a
         * hundred places, written through a label in exactly ONE - the
         * initialiser, which writes zero - and yet it observably takes three
         * different non-zero values during a run. Something writes it
         * through a computed pointer, and forty candidate stores share the
         * 0x330 displacement it would use.
         *
         * Checked EVERY step, which is what makes the answer exact rather
         * than "somewhere in the last five hundred". Only when asked for:
         * the cost is a guest read per step and this is a diagnostic run,
         * not a normal one. */
        if (s_memwatch_addr && s_memwatch_len) {
            uint32_t i;
            for (i = 0u; i < s_memwatch_len; i += 4u) {
                uint32_t now = gread32(cpu, s_memwatch_addr + i);
                uint32_t was = ((uint32_t)s_memwatch_prev[i] << 24)
                             | ((uint32_t)s_memwatch_prev[i + 1u] << 16)
                             | ((uint32_t)s_memwatch_prev[i + 2u] << 8)
                             |  (uint32_t)s_memwatch_prev[i + 3u];
                if (now == was) continue;
                fprintf(stderr, "[watch] 0x%08X +0x%03X: 0x%08X -> 0x%08X  "
                                "at pc 0x%08X lr 0x%08X\n",
                        s_memwatch_addr, i, was, now,
                        mgs_module_last_pc, *mgs_module_lr_ptr(cpu));
                s_memwatch_prev[i] = (uint8_t)(now >> 24);
                s_memwatch_prev[i + 1u] = (uint8_t)(now >> 16);
                s_memwatch_prev[i + 2u] = (uint8_t)(now >> 8);
                s_memwatch_prev[i + 3u] = (uint8_t)now;
            }
        } else if (s_memwatch_addr) {
            uint32_t now = gread32(cpu, s_memwatch_addr);
            if (now != s_memwatch_last) {
                fprintf(stderr, "[watch] 0x%08X: 0x%08X -> 0x%08X  "
                                "at pc 0x%08X lr 0x%08X\n",
                        s_memwatch_addr, s_memwatch_last, now,
                        mgs_module_last_pc, *mgs_module_lr_ptr(cpu));
                s_memwatch_last = now;
            }
        }

        /* Host-driven work that must run on the guest thread. Like the
         * interrupt above, this can move the pc, so it comes BEFORE pc is
         * read. */
        if ((gt >= due_pump ? (due_pump = gt + 4096ull, 1) : 0) && s_pump)
            s_pump(mod, cpu, s_pump_user);

        /* Execute any framebuffer copy the game has put in the command
         * stream. Checked often: the copy is what makes a frame exist, and
         * deferring it to the next retrace would show every frame late. */
        if ((gt >= due_disp ? (due_disp = gt + 2048ull, 1) : 0) && s_display)
            s_display();

        pc = mgs_module_pc(cpu);

        /* Read the arguments of a watched call as the guest reaches it. The
         * first sighting is the one kept: OSLink is called once per module,
         * and a later call would be a different module's. */
        if (s_watch_addr && pc == s_watch_addr && !s_watch_seen) {
            uint32_t* g = mgs_module_gpr(cpu);
            s_watch_r3 = g[3];
            s_watch_r4 = g[4];
            s_watch_seen = 1;

            /* GIVE OSLink THE .bss THE RECOMPILED CODE ACTUALLY USES.
             *
             * `mgs_clear_overlay_bss` already records that the game and the
             * recompiled overlay disagree about where .bss lives, and zeroes
             * the recompiler's region so its globals start at zero. That
             * fixes INITIALISATION and nothing else, and the rest of the
             * disagreement is still live:
             *
             *   - a global the recompiled code addresses DIRECTLY lands in
             *     VMEM, at module + 0x491BA0 and up;
             *   - a global it reaches THROUGH A POINTER HELD IN .data lands
             *     wherever OSLink relocated that pointer to, which is the
             *     buffer the game allocated in MEM1.
             *
             * So the overlay has two .bss regions and the engine uses both.
             * Measured, in one run: the record-ring pair is live at
             * 0x7F4EF794 (VMEM) and all zeroes at 0x8059FD74 (MEM1), while
             * the buffer holding "r_open" and "demo50a" is live in MEM1 at
             * 0x80566590 and all zeroes at its VMEM counterpart 0x7F4B5FB0.
             * Each global is consistent with itself; any global reached BOTH
             * ways is split, and a write through one route is invisible to a
             * read through the other.
             *
             * r4 is OSLink's `bss` argument, and everything downstream - the
             * section table it fills in, every address `Relocate` writes into
             * .data and .text - follows from it. Rewriting it here, before
             * the guest executes a single instruction of OSLink, makes the
             * image agree with the recompiled code everywhere instead of in
             * half the cases.
             *
             * The region is the same one the post-link clear covers, so the
             * ordering still works: linking reads the relocation tables that
             * live there, writes only pointer VALUES into .data and .text,
             * and the clear then zeroes the region once they are dead.
             * Relocation never writes into .bss itself. */
            if (s_relink_bss && g[3]) {
                uint32_t want = g[3] + MGS_OVERLAY_BSS_OFFSET;
                if (want != g[4]) {
                    fprintf(stderr,
                            "[link] overlay .bss 0x%08X -> 0x%08X "
                            "(the recompiled overlay's own)\n", g[4], want);
                    g[4] = want;
                    s_watch_r4 = want;
                }
            }
        }

        /* The check, not the call: this is the per-step path and
         * the trace is off in every normal run. */
        if (s_fntrace_n) fntrace_step(cpu, pc);

        /* ONE BRANCH FOR ALL FOUR, because this is the per-instruction
         * path. Four independent tests cost four compares and four
         * predicted-not-taken branches every thirteen guest instructions,
         * to run something that is set in a diagnostic run and nowhere
         * else. `s_trace_any` is the OR of the four addresses, computed
         * where they are set. */
        if (s_trace_any) {
            if (s_trace_addr && pc == s_trace_addr && s_trace_fn)
                s_trace_fn(cpu, mgs_module_gpr(cpu));
            if (s_trace_addr2 && pc == s_trace_addr2 && s_trace_fn2)
                s_trace_fn2(cpu, mgs_module_gpr(cpu));
            if (s_trace_addr3 && pc == s_trace_addr3 && s_trace_fn3)
                s_trace_fn3(cpu, mgs_module_gpr(cpu));
            if (s_trace_addr4 && pc == s_trace_addr4 && s_trace_fn4)
                s_trace_fn4(cpu, mgs_module_gpr(cpu));
        }

        if (s_linked && s_watch_seen && !s_linked_done &&
            pc >= 0x7E000000u && pc < 0x80000000u) {
            s_linked_done = 1;
            s_linked(cpu, s_watch_r3);
        }

        recent[recent_n & (RECENT - 1u)] = pc;
        ++recent_n;
        mgs_module_last_pc = pc;
        mgs_module_phase = "run-loop";

        if (profile && (r.steps % PROF_INTERVAL) == 0ull) prof_sample(pc);

        if (caller_of && pc == caller_of) {
            const uint32_t* g = mgs_module_gpr(cpu);
            uint32_t lr;
            memcpy(&lr, (const uint8_t*)cpu + CPU_LR_OFFSET, sizeof lr);
            caller_sample(lr, caller_size_reg < 32u ? g[caller_size_reg] : 0u);
        }

        /* MGS_HEARTBEAT=N prints progress every N steps. A run that stops
         * producing output is either stuck in the guest or stuck in the
         * host, and those want completely different investigations; this is
         * the cheapest thing that tells them apart. */
        if (heartbeat && (r.steps % heartbeat) == 0ull) {
            /* Steps alone say the host is alive, which is rarely the
             * question. What matters is whether the GAME is getting
             * anywhere, so the counters that move when it does are here
             * too - frames copied out, and files read. */
            fprintf(stderr, "[beat] step %9llu  pc 0x%08X  frames %llu  "
                            "reads %llu\n",
                    (unsigned long long)r.steps, pc,
                    (unsigned long long)(s_progress ? s_progress(0) : 0),
                    (unsigned long long)(s_progress ? s_progress(1) : 0));
        }

        if (trace_every ? ((r.steps % trace_every) < trace_steps)
                        : (r.steps >= trace_from && r.steps < trace_from + trace_steps))
            fprintf(stderr, "  step %llu  pc = 0x%08X\n",
                    (unsigned long long)r.steps, pc);

        /* Refill the cycle budget. The translated code decrements it and
         * returns when it hits zero; leaving it empty would return
         * immediately every time and make no progress at all. */
        {
            /* MGS_BUDGET sets how many cycles the translated code may run
             * before it must come back.
             *
             * It is a diagnostic as much as a tuning knob. The run loop is
             * the only place the interrupt flag is read, so the budget is
             * also the longest the host can take to notice it - and a run
             * that will not stop is either spending a long time inside one
             * dispatch call or not decrementing the counter at all. Lowering
             * this tells the two apart: if a small budget makes the host
             * responsive, it was the former. */
            uint32_t budget = s_budget;
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
        /* The fast path INLINE. An exception vector is below 0x3000 and
         * page-aligned; every other address - which is every address, in a
         * normal step - can skip the call entirely. */
        if (pc < 0x3000u && (pc & 0xFFu) == 0u) {
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

        mgs_module_phase = "dispatch";
        /* HOW MUCH GUEST CODE DOES ONE DISPATCH CALL ACTUALLY RUN?
         *
         * The budget is 100,000 cycles, so in principle the run loop's
         * per-call overhead is amortised to nothing. In practice removing
         * two divisions and four pointer lookups from that loop moved the
         * port from 9.2% slower than real time to 3.5%, which it could not
         * have done if a call ran anything like 100,000 cycles. The
         * translated code decrements the downcount, so what it did not use
         * is what it did not run. */
        if (s_cycle_census) {
            uint32_t before = s_budget;
            uint32_t after;
            int ok = mod->dispatch(cpu, pc);
            memcpy(&after, (uint8_t*)cpu + CPU_DOWNCOUNT, sizeof after);
            s_cycles_run += (uint64_t)(before - after);
            ++s_dispatches;
            if (!ok) goto uncovered;
            continue;
        }
        if (!mod->dispatch(cpu, pc)) {
            uncovered:;
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
    /* A run that was ASKED to stop did not run out of patience, and saying
     * "step limit" when a signal arrived at step 446,813 of 1,200,000 is
     * simply untrue - it reads as though the budget was the constraint when
     * the budget was never reached. */
    if (s_fntrace_n) {
        unsigned i;
        fprintf(stderr, "traced guest functions:\n");
        for (i = 0u; i < s_fntrace_n; ++i)
            fprintf(stderr, "  0x%08X  %llu calls\n", s_fntrace[i],
                    (unsigned long long)s_fntrace_hits[i]);
    }

    r.stop = mgs_module_interrupted ? MGS_STOP_INTERRUPTED : MGS_STOP_STEP_LIMIT;
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
    /* THE WHOLE VOLATILE STATE, not just the registers an ABI call would
     * clobber.
     *
     * This is not a call the guest made. The host enters guest code at an
     * arbitrary instruction boundary in whatever the guest was doing, so from
     * the interrupted code's point of view it is an asynchronous interruption
     * and EVERYTHING it was relying on has to come back unchanged.
     *
     * Saving only gpr, pc and lr - which is what this did - left CTR, CR, XER
     * and the whole floating-point file to be trampled by the callback. CTR
     * is the one that bites: it holds both indirect-branch targets and
     * `bdnz` loop counts, so a callback that makes one indirect call destroys
     * the count of any counted loop it interrupted.
     *
     * That is exactly what stopped this boot. A disc-read callback landed
     * inside zlib's huft_build, in `while (a--)` compiled to bdnz, and left
     * CTR holding an overlay function address - 0x7EFFC638, about 2.13
     * billion. The loop then had 2.13 billion iterations to run instead of
     * seven, so inflate never returned, the engine never got its data, and
     * the boot sat there for ever building the same Huffman table. Sixty-four
     * callbacks in a boot and one of them has to land somewhere.
     *
     * Bytes 0..663 of CPUState are gpr, fpr, ps1, pc, lr, ctr, cr, xer and
     * fpscr - everything above msr, which is deliberately NOT restored
     * because the callback may legitimately have changed interrupt state. The
     * graphics quantisation registers sit further on and are saved
     * separately. */
    uint8_t  saved_core[CPU_VOLATILE_BYTES];
    uint8_t  saved_gqr[32];
    uint32_t saved_pc;
    uint32_t* gpr = mgs_module_gpr(cpu);
    uint64_t step;
    unsigned i;
    int returned = 0;

    memcpy(saved_core, cpu, sizeof saved_core);
    memcpy(saved_gqr, (const uint8_t*)cpu + CPU_GQR_OFFSET, sizeof saved_gqr);
    saved_pc = mgs_module_pc(cpu);

    for (i = 0; i < arg_count && i < 8u; ++i) gpr[3 + i] = args[i];
    mgs_module_set_lr(cpu, MGS_GUEST_RETURN_SENTINEL);
    mgs_module_set_pc(cpu, address);

    s_call_fail_pc = 0u;
    for (step = 0; step < max_steps; ++step) {
        uint32_t pc = mgs_module_pc(cpu);
        if (pc == MGS_GUEST_RETURN_SENTINEL) { returned = 1; break; }
        /* The check, not the call: this is the per-step path and
         * the trace is off in every normal run. */
        if (s_fntrace_n) fntrace_step(cpu, pc);
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

    memcpy(cpu, saved_core, sizeof saved_core);
    memcpy((uint8_t*)cpu + CPU_GQR_OFFSET, saved_gqr, sizeof saved_gqr);
    mgs_module_set_pc(cpu, saved_pc);   /* in saved_core too; explicit is clearer */
    return returned;
}

void mgs_module_unload(MgsModule* mod)
{
    if (mod->handle) dlclose(mod->handle);
    memset(mod, 0, sizeof *mod);
}
