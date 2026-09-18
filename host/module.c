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

uint32_t mgs_module_pc(const void* cpu_state)
{
    uint32_t pc;
    memcpy(&pc, (const uint8_t*)cpu_state + CPU_PC_OFFSET, sizeof pc);
    return pc;
}

void mgs_module_unload(MgsModule* mod)
{
    if (mod->handle) dlclose(mod->handle);
    memset(mod, 0, sizeof *mod);
}
