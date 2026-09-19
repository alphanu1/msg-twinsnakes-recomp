#ifndef MGS_HOST_MODULE_H
#define MGS_HOST_MODULE_H

#include <signal.h>
#include <stdint.h>
#include <stdio.h>

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
    MGS_STOP_EXCEPTION,       /* a real fault, not a barrier */
    MGS_STOP_INTERRUPTED      /* asked to stop: Ctrl-C, SIGTERM, a timeout */
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
    uint64_t      fp_switches; /* lazy FP context switches serviced */
} MgsRunResult;

/* A periodic hook the run loop calls between steps, for work that has to
 * happen on the guest thread but is driven by the host: finished DVD reads,
 * and whatever else phases 3 and 4 add. The run loop deliberately knows
 * nothing about what it pumps. */
typedef void (*MgsPump)(const MgsModule* mod, void* cpu, void* user);
void mgs_module_set_pump(MgsPump pump, void* user);

uint32_t mgs_module_call_fail_pc(void);
void mgs_host_set_vmem(uint8_t* vmem);

/* The display path: GX's copy out, and the video interface's scan-out.
 * Declared here so the run loop can drive them without including the GX
 * headers. */
struct MgsMmio;
struct GuestMemory;
void mgs_display_init(struct GuestMemory* mem);
void mgs_module_set_display(void (*fn)(void));
void mgs_module_set_frame(void (*fn)(void));
void mgs_module_set_progress(uint64_t (*fn)(unsigned which));

/* Dump the sampling profiler's histogram, hottest first, as
 * "[prof] <count> <percent> <address>" lines. Enabled by MGS_PROFILE.
 * Addresses are raw; tools/resolve-addrs.py puts names to them. */
void mgs_module_profile_dump(FILE* out, unsigned top);

/* Observe a guest call's arguments without replacing the call. */
void mgs_module_watch(uint32_t address);
int  mgs_module_watch_result(uint32_t* r3, uint32_t* r4);
void mgs_module_guest_write32(void* cpu, uint32_t addr, uint32_t v);
void mgs_module_set_vmem(uint8_t* vmem);

/* Ctrl-C ends the run cleanly, so its report is still printed. */
extern volatile sig_atomic_t mgs_module_interrupted;

/* The last pc the run loop saw. Read from the signal handler so a process
 * wedged inside one dispatch call can still say where it was. */
extern volatile uint32_t mgs_module_last_pc;

/* Which part of the host is running. A string literal, set at the few places
 * that can take a long time; read from the signal handler. */
extern volatile const char* mgs_module_phase;
void mgs_module_on_linked(void (*fn)(void* cpu, uint32_t module));
void mgs_module_trace_calls(uint32_t address,
                            void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls2(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls3(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_module_trace_calls4(uint32_t address,
                             void (*fn)(void* cpu, const uint32_t* gpr));
void mgs_dump_heaps(void* cpu, uint32_t rel_bss);
void mgs_dump_tasks(void* cpu, uint32_t rel_bss);
void mgs_clear_overlay_bss(void* cpu, uint32_t module);
void mgs_display_service(struct MgsMmio* mmio, struct GuestMemory* mem, unsigned height);
int  mgs_display_present(struct MgsMmio* mmio, const struct GuestMemory* mem);
uint64_t mgs_display_frames(void);
uint64_t mgs_dvd_deferred(void);
int  mgs_display_take_draw_done(void);
void mgs_display_put_draw_done(void);
int  mgs_display_save_ppm(const char* path, const struct GuestMemory* mem);
void mgs_display_set_best_path(const char* p);
unsigned mgs_display_best_lit(void);
unsigned mgs_display_best_w(void);
unsigned mgs_display_best_h(void);
uint64_t mgs_host_vmem_reads(void);
uint64_t mgs_host_vmem_writes(void);
void mgs_host_set_write_watch(uint32_t addr);
uint32_t mgs_host_vmem_lo(void);
uint32_t mgs_host_vmem_hi(void);
uint32_t mgs_module_msr(const void* cpu);
uint32_t mgs_module_guest_read32(void* cpu, uint32_t addr);
void mgs_dump_threads(void* cpu, const char* (*symbol)(uint32_t));
uint32_t* mgs_module_msr_ptr(void* cpu);
uint32_t mgs_module_current_context(void* cpu);
int      mgs_module_take_exception(void* cpu, uint32_t handler, uint32_t number);
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
uint64_t mgs_interrupt_failed(void);
int mgs_interrupt_pe_finish(const MgsModule* mod, void* cpu);
int mgs_interrupt_pending(const MgsModule* mod, void* cpu);
uint64_t mgs_interrupt_redelivered(void);
int mgs_display_peek_draw_done(void);

/* Report a submitted DSP task as started and then finished, which is what
 * the boot is waiting for. See host/interrupt.c - it is the handshake, not a
 * coprocessor, and produces no sound. */
int      mgs_interrupt_aram(const MgsModule* mod, void* cpu);
uint64_t mgs_interrupt_aram_raised(void);
uint64_t mgs_interrupt_aram_refused(void);
int      mgs_interrupt_dsp_task(const MgsModule* mod, void* cpu);
uint64_t mgs_interrupt_dsp_tasks(void);
uint64_t mgs_interrupt_pe_seen(void);
uint64_t mgs_interrupt_pe_sent(void);
