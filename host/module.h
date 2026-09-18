#ifndef MGS_HOST_MODULE_H
#define MGS_HOST_MODULE_H

#include <stdint.h>

typedef struct MgsModule {
    void*       handle;
    const void* desc;
    uint32_t    entry_point;
    uint32_t    cpu_state_size;
    char        game_id[8];
    uint32_t    code_ranges, chunk_ranges, rel_modules;
    int       (*dispatch)(void* state, uint32_t address);
    void      (*set_patch_hook)(int (*)(void*, uint32_t));
    char        error[256];
} MgsModule;

typedef enum {
    MGS_STOP_UNCOVERED = 0,   /* dispatch had no code for that address */
    MGS_STOP_SPINNING,        /* pc stopped moving: the guest is waiting */
    MGS_STOP_STEP_LIMIT,      /* ran out of host patience, not guest progress */
    MGS_STOP_EXCEPTION        /* a real fault, not a barrier */
} MgsStopReason;

typedef struct MgsRunResult {
    MgsStopReason stop;
    uint32_t      pc;
    uint64_t      steps;
    /* Filled when the stop was an exception: which one, why, and the address
     * that caused it - srr0 is the faulting instruction, not the vector. */
    uint32_t      exception;
    uint32_t      program_cause;
    uint32_t      srr0;
    uint32_t      msr;
    uint64_t      syscalls;   /* barriers serviced, not faults */
    uint64_t      frames;     /* retrace ticks raised */
} MgsRunResult;

int   mgs_module_load(MgsModule* mod, const char* path);
MgsRunResult mgs_module_run(const MgsModule* mod, void* cpu, uint64_t max_steps);
void  mgs_module_unload(MgsModule* mod);
void* mgs_module_new_cpu_state(const MgsModule* mod, uint8_t* ram, uint32_t ram_size);

/* The module's register file and program counter, so the CPU seam can be
 * bound to them and a stop can be reported by address. */
uint32_t* mgs_module_gpr(void* cpu_state);
uint32_t  mgs_module_pc(const void* cpu_state);
uint32_t  mgs_module_lr(const void* cpu_state);
void      mgs_module_set_pc(void* cpu_state, uint32_t pc);
void      mgs_module_set_lr(void* cpu_state, uint32_t lr);

/* Enter the guest, run a function to completion, and return with every other
 * register and the interrupted pc exactly as they were. Returns 1 if the
 * function returned, 0 if it ran out of steps or hit an address with no code. */
int       mgs_module_call_guest(const MgsModule* mod, void* cpu, uint32_t address,
                                const uint32_t* args, unsigned arg_count,
                                uint64_t max_steps);

#endif

/* Host-side handling of instructions DolRecomp defers - SPR access and cache
 * maintenance. Installed onto a CPU state before running it. */
struct MgsMmio;
struct MgsMmio* mgs_host_mmio(void);
void          mgs_host_install_spr_handler(void* cpu);
unsigned long mgs_host_spr_handled(void);
unsigned long mgs_host_spr_unknown(void);

/* Interrupt delivery. The host raises a cause and calls the guest's own
 * dispatcher; it does not reimplement the SDK's handling. */
void     mgs_interrupt_set_dispatch(uint32_t guest_address);
int      mgs_interrupt_raise(const MgsModule* mod, void* cpu, uint32_t cause_bit);
int      mgs_interrupt_vi(const MgsModule* mod, void* cpu);
int      mgs_interrupt_dsp(const MgsModule* mod, void* cpu);
uint64_t mgs_interrupt_delivered(void);
uint64_t mgs_interrupt_refused(void);
