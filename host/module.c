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

    /* MGS_TRACE_STEPS=N prints the first N guest pcs. A stop address alone
     * says where execution ended, not how it got there, and for a boot the
     * path is the interesting part. */
    {
        const char* env = getenv("MGS_TRACE_STEPS");
        trace_steps = env ? (uint64_t)strtoull(env, NULL, 0) : 0u;
    }

    /* A ring of recent addresses. A stop address says where execution ended;
     * for a jump to a bad address the interesting part is what branched
     * there, and that is always a few steps back. */
    #define RECENT 12
    static uint32_t recent[RECENT];
    unsigned recent_n = 0u;

    memset(&r, 0, sizeof r);
    for (r.steps = 0; r.steps < max_steps; ++r.steps) {
        uint32_t pc = mgs_module_pc(cpu);

        recent[recent_n % RECENT] = pc;
        ++recent_n;

        /* Advance the video beam on a cadence, so a guest polling for retrace
         * sees time pass at the rate the host runs rather than as fast as it
         * can spin. Tied to steps rather than wall clock for now: a replayed
         * run must be reproducible, and wall clock is not. */
        if ((r.steps % 2000ull) == 0ull) mgs_mmio_tick_frame(mgs_host_mmio());

        if (r.steps < trace_steps)
            fprintf(stderr, "  step %4llu  pc = 0x%08X\n",
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
        if (pc < 0x3000u && (pc & 0xFFu) == 0u) {
            uint32_t srr0, srr1;
            memcpy(&srr0, (uint8_t*)cpu + CPU_SRR0, 4);
            memcpy(&srr1, (uint8_t*)cpu + CPU_SRR1, 4);

            if (pc == 0xC00u) {
                /* System call. The SDK issues `sc` as a completion barrier -
                 * DCFlushRange ends with one - and its handler returns
                 * immediately. srr0 already points past the sc. */
                ++r.syscalls;
                mgs_module_set_pc(cpu, srr0);
                memset((uint8_t*)cpu + CPU_EXCEPTION, 0, 4);
                continue;
            }
            if (pc == 0x700u || pc == 0x300u || pc == 0x600u || pc == 0x800u) {
                /* Program, DSI, alignment, FP-unavailable. These are real
                 * faults rather than barriers, so stopping is right: resuming
                 * would hide the cause and fail somewhere unrelated. */
                r.stop = MGS_STOP_EXCEPTION;
                r.pc = pc;
                memcpy(&r.exception, (uint8_t*)cpu + CPU_EXCEPTION, 4);
                memcpy(&r.program_cause, (uint8_t*)cpu + CPU_PROGRAM_EXC, 4);
                r.srr0 = srr0; r.msr = srr1;
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

void mgs_module_unload(MgsModule* mod)
{
    if (mod->handle) dlclose(mod->handle);
    memset(mod, 0, sizeof *mod);
}
