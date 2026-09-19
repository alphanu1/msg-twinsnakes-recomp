/* The native runtime's own state, and the seam to the recompiled code.
 *
 * Deliberately independent of ModernGekko: phase 1 borrowed a Dolphin-derived
 * runtime to prove the translated CPU code, and phases 2 onward replace it.
 * Nothing here includes a ModernGekko or Dolphin header.
 */
#ifndef MGS_OS_RUNTIME_H
#define MGS_OS_RUNTIME_H

#include <stdint.h>
#include "../memory/guest.h"

typedef struct CPUState CPUState;

struct MgsScheduler;

typedef struct MgsRuntime {
    GuestMemory mem;

    /* Guest threads. One runs at a time; see os_thread.h for why that is a
     * correctness requirement and not a simplification.
     */
    struct MgsScheduler* sched;

    /* Guest time. The Gekko's timebase runs at 40.5 MHz - the 162 MHz bus
     * divided by four - and everything the game schedules derives from it.
     * Held as a tick count rather than read from the host clock so a replayed
     * input sequence produces identical timing, which the frame-by-frame
     * comparison against Dolphin depends on.
     */
    uint64_t ticks;

    /* "Interrupts disabled" is NOT a flag of our own. It is MSR[EE], the
     * processor bit the real OSDisableInterrupts clears with mtmsr, and the
     * host reads that same bit to decide whether an interrupt may be
     * delivered. Keeping a private copy here instead meant the guest could
     * enable interrupts and the host would never notice - which is exactly
     * how the scheduler came to idle forever with 1355 retraces refused.
     *
     * See mgs_cpu_msr / mgs_cpu_set_msr in cpu_seam.c.
     */

    void (*report_sink)(void* user, const char* line);
    void* report_user;
} MgsRuntime;

#define MGS_TIMEBASE_HZ 40500000u

/* The recompiled code carries its own CPUState; these are the two directions
 * across that seam. Implemented per host runtime so the SDK shims never
 * include a runtime header.
 */
MgsRuntime* mgs_runtime_from(CPUState* ctx);
uint32_t    mgs_guest_gpr(const MgsRuntime* rt, unsigned index);
void        mgs_set_guest_gpr(MgsRuntime* rt, unsigned index, uint32_t value);

/* Fill the low-memory globals the boot ROM would have left. Must run BEFORE
 * the guest's entry point: OSInit reads every one of them. */
struct MgsDisc;
void mgs_boot_info_init(GuestMemory* mem, const struct MgsDisc* disc);

void mgs_os_report_sink(MgsRuntime* rt, const char* line);
void mgs_sched_init(MgsRuntime* rt);

/* The CPU seam. See cpu_seam.c: bound to a loaded module's register file, or
 * free-standing so the runtime can be run and tested without one. */
void mgs_cpu_bind_registers(uint32_t* gpr_array);
void mgs_cpu_bind_msr(uint32_t* msr);
uint32_t mgs_cpu_msr(void);
void mgs_cpu_set_msr(uint32_t value);
void mgs_cpu_unbind(void);
void mgs_runtime_set_current(MgsRuntime* rt);

/* Advance guest time. Nothing else moves it: OSGetTime and OSGetTick read
 * rt->ticks, and a timebase that never advances turns every timed wait in the
 * SDK into an infinite loop. Driven from the run loop rather than the host
 * clock so a replayed run is reproducible. */
void mgs_runtime_advance_ticks(MgsRuntime* rt, uint64_t ticks);
uint64_t mgs_runtime_ticks(const MgsRuntime* rt);

#endif
