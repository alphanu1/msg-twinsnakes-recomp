/* Finishing DVD reads, and telling the game.
 *
 * DVDReadAsyncPrio is serviced natively: the read is handed to the worker
 * pool and the shim returns immediately, exactly as the SDK's does. What the
 * SDK does NEXT is the part that was missing here - when the drive finishes,
 * the DI interrupt runs the caller's DVDCallback, and THAT is what unblocks
 * the game's loader thread.
 *
 * Without it the boot ran perfectly and did nothing: 400 million steps, every
 * retrace delivered, and the scheduler idle the whole time because every
 * thread was waiting on a read that had already finished on a worker but had
 * never been reported.
 *
 * WHY THE SCHEDULER IS HELD OFF AROUND THE CALLBACK. On hardware the callback
 * runs inside __OSDispatchInterrupt, which brackets its handler with
 * OSDisableScheduler / OSEnableScheduler. That is not tidiness: a callback
 * routinely calls OSWakeupThread, OSWakeupThread ends in __OSReschedule, and
 * __OSReschedule switches contexts with OSLoadContext - an rfi that never
 * returns. Calling the callback without that bracket means the switch happens
 * halfway through a host-initiated call and the host restores registers over
 * a thread that has already moved on.
 *
 * So this reproduces the bracket using the guest's OWN scheduler calls rather
 * than reaching into its data. The thread the callback wakes is then picked
 * up by the next retrace interrupt, which reschedules anyway.
 */
#include "module.h"
#include "os/os_runtime.h"
#include "dvd/dvd.h"

#include <stdio.h>
#include <string.h>

/* Guest addresses from config/symbols/main.dol.symbols.txt. These are the
 * scheduler's public brackets, not internals. */
#define GUEST_OSDisableScheduler 0x80022FF4u
#define GUEST_OSEnableScheduler  0x80023034u

#define MSR_EE 0x8000u

static uint64_t s_completed, s_callbacks, s_deferred;
static uint64_t s_bytes, s_reads_late;
static uint64_t s_read_errors;

/* Reads the disc refused. A read that fails returns a negative length and is
 * otherwise indistinguishable, in a log, from one the game never issued -
 * and "the game is not loading anything" is how that presents. */
uint64_t mgs_dvd_errors(void);
uint64_t mgs_dvd_errors(void) { return s_read_errors; }
uint64_t mgs_dvd_bytes(void);
uint64_t mgs_dvd_bytes(void) { return s_bytes; }

uint64_t mgs_dvd_completed(void)  { return s_completed; }
uint64_t mgs_dvd_callbacks(void)  { return s_callbacks; }
uint64_t mgs_dvd_deferred(void)   { return s_deferred; }

void mgs_dvd_service(const MgsModule* mod, void* cpu, MgsDvd* dvd)
{
    MgsDvdRequest* done[MGS_DVD_MAX_PENDING];
    unsigned n, i;

    if (!dvd) return;

    /* The same gate the interrupt path uses, and for the same reason: the
     * guest clears MSR[EE] around its critical sections, and a callback that
     * runs there breaks exactly the atomicity it is protecting. The read
     * stays completed and is reported on a later pass. */
    if (!(mgs_module_msr(cpu) & MSR_EE)) {
        if (mgs_dvd_pending_count(dvd)) ++s_deferred;
        return;
    }

    /* The guest's own clock, which is what decides whether a read has
     * finished. See MgsDvdRequest::ready_tick. */
    {
        uint64_t now = mgs_runtime_ticks(mgs_runtime_from(NULL));
        mgs_dvd_set_clock(dvd, now);
        n = mgs_dvd_drain(dvd, done, MGS_DVD_MAX_PENDING, now);
    }
    if (!n) return;

    for (i = 0; i < n; ++i) {
        uint32_t callback = done[i]->guest_callback;
        ++s_completed;
        if (done[i]->result < 0) {
            ++s_read_errors;
            fprintf(stderr, "[dvd] READ FAILED: %u bytes at 0x%08X -> 0x%08X\n",
                    done[i]->length, done[i]->offset, done[i]->guest_dest);
        }
        s_bytes += (done[i]->result > 0) ? (uint64_t)done[i]->result : 0u;

        if (callback) {
            uint32_t args[2];
            /* DVDCallback(s32 result, DVDFileInfo* fileInfo). result is the
             * byte count transferred, negative on error - the game branches
             * on it, so passing the request's own result matters. */
            args[0] = (uint32_t)(int32_t)done[i]->result;
            args[1] = done[i]->guest_block;

            mgs_module_call_guest(mod, cpu, GUEST_OSDisableScheduler, NULL, 0u, 100000ull);
            if (mgs_module_call_guest(mod, cpu, callback, args, 2u, 4000000ull))
                ++s_callbacks;
            else
                fprintf(stderr, "[dvd] callback 0x%08X did not return; "
                                "gave up at 0x%08X\n",
                        callback, mgs_module_call_fail_pc());
            mgs_module_call_guest(mod, cpu, GUEST_OSEnableScheduler, NULL, 0u, 100000ull);
        }
        mgs_dvd_release(done[i]);
    }
}

/* THE MEMORY CARD'S COMPLETION, DELIVERED THE SAME WAY A DVD READ'S IS.
 *
 * CARDMountAsync reports READY immediately - there is no bus and nothing to
 * wait for - but the SDK's contract is that the caller learns of it through
 * the attach callback, and the game waits for exactly that. The shim cannot
 * call it: it runs inside a guest call, so calling back would nest the guest
 * stack and run the callback under the caller's interrupt state.
 *
 * So the same discipline as every other completion here: queued on the
 * runtime side, run from the pump, on the guest thread, with the scheduler
 * held off across it and MSR[EE] checked first.
 */
uint32_t mgs_card_take_callback(int32_t* chan, int32_t* result);

void mgs_card_service(const MgsModule* mod, void* cpu);
void mgs_card_service(const MgsModule* mod, void* cpu)
{
    int32_t  chan = 0, result = 0;
    uint32_t cb;

    /* The same gate the DVD path uses: a callback that runs with the guest's
     * interrupts off breaks the atomicity it is protecting. */
    if (!(mgs_module_msr(cpu) & MSR_EE)) return;

    cb = mgs_card_take_callback(&chan, &result);
    if (!cb) return;

    {
        uint32_t args[2];
        args[0] = (uint32_t)chan;
        args[1] = (uint32_t)result;
        mgs_module_call_guest(mod, cpu, GUEST_OSDisableScheduler, NULL, 0u, 100000ull);
        if (!mgs_module_call_guest(mod, cpu, cb, args, 2u, 4000000ull))
            fprintf(stderr, "[card] attach callback 0x%08X did not return; "
                            "gave up at 0x%08X\n", cb, mgs_module_call_fail_pc());
        mgs_module_call_guest(mod, cpu, GUEST_OSEnableScheduler, NULL, 0u, 100000ull);
    }
}
