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

    /* Cooperative scheduling means "interrupts disabled" is a flag the game
     * sets and reads back, not a real mask: only one guest thread runs at a
     * time, so there is nothing to mask against.
     */
    int interrupts_enabled;

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

void mgs_os_report_sink(MgsRuntime* rt, const char* line);
void mgs_sched_init(MgsRuntime* rt);

/* The CPU seam. See cpu_seam.c: bound to a loaded module's register file, or
 * free-standing so the runtime can be run and tested without one. */
void mgs_cpu_bind_registers(uint32_t* gpr_array);
void mgs_cpu_unbind(void);
void mgs_runtime_set_current(MgsRuntime* rt);

#endif
