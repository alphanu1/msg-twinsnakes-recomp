/* Guest time, and the interrupt flag.
 *
 * Time is a tick count the frame loop advances, not a reading of the host
 * clock. That is a correctness requirement rather than a simplification: the
 * design document's test strategy replays a recorded input sequence and
 * compares guest memory at fixed frames, which only works if the guest's
 * notion of time is a function of frames rather than of how fast the host ran.
 */
#include "os_runtime.h"

void mgs_OSGetTime(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    /* OSTime is 64-bit, returned in r3:r4 high word first. */
    mgs_set_guest_gpr(rt, 3, (uint32_t)(rt->ticks >> 32));
    mgs_set_guest_gpr(rt, 4, (uint32_t)rt->ticks);
}

void mgs_OSGetTick(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    mgs_set_guest_gpr(rt, 3, (uint32_t)rt->ticks);
}

/* The three interrupt calls return the PREVIOUS state, which the game relies
 * on to restore correctly through nesting.
 */
void mgs_OSDisableInterrupts(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int was = rt->interrupts_enabled;
    rt->interrupts_enabled = 0;
    mgs_set_guest_gpr(rt, 3, (uint32_t)was);
}

void mgs_OSEnableInterrupts(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int was = rt->interrupts_enabled;
    rt->interrupts_enabled = 1;
    mgs_set_guest_gpr(rt, 3, (uint32_t)was);
}

void mgs_OSRestoreInterrupts(CPUState* ctx)
{
    MgsRuntime* rt = mgs_runtime_from(ctx);
    int was = rt->interrupts_enabled;
    rt->interrupts_enabled = (int)mgs_guest_gpr(rt, 3) ? 1 : 0;
    mgs_set_guest_gpr(rt, 3, (uint32_t)was);
}

void mgs_os_report_sink(MgsRuntime* rt, const char* line)
{
    if (rt->report_sink) rt->report_sink(rt->report_user, line);
}
