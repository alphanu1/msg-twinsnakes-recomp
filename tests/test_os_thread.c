/* Scheduler selection rules.
 *
 * The design document calls threading the high-risk item because the failure
 * mode is silent: pick the wrong thread and the game does not crash, it
 * corrupts state occasionally. So the selection rules are pinned here rather
 * than trusted to inspection.
 */
#include "os/os_thread.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

/* Guest OSThread blocks, spaced so they cannot overlap. */
#define T1 0x80100000u
#define T2 0x80101000u
#define T3 0x80102000u

static MgsRuntime rt;

static void resume(uint32_t t) { guest_write32(&rt.mem, t + OSTHREAD_SUSPEND, 0u); }

int main(void)
{
    CHECK(guest_memory_init(&rt.mem));
    mgs_sched_init(&rt);

    /* Threads start suspended, as OSCreateThread leaves them. */
    CHECK(mgs_sched_create(&rt, T1, 0x80006958u, 0u, 0x80200000u, 16));
    CHECK(mgs_sched_state(&rt, T1) == OS_THREAD_READY);
    CHECK(mgs_sched_pick(&rt) == 0u);           /* suspended: not runnable */

    resume(T1);
    CHECK(mgs_sched_pick(&rt) == T1);

    /* Lower number is higher priority, so T2 at 8 outranks T1 at 16. */
    CHECK(mgs_sched_create(&rt, T2, 0x80006958u, 0u, 0x80210000u, 8));
    resume(T2);
    CHECK(mgs_sched_pick(&rt) == T2);

    /* A tie must leave the incumbent alone. Without that, equal-priority
     * threads swap on every reschedule and the guarantee the game relies on -
     * that it keeps the CPU until it yields - breaks silently.
     */
    CHECK(mgs_sched_create(&rt, T3, 0x80006958u, 0u, 0x80220000u, 8));
    resume(T3);
    CHECK(mgs_sched_pick(&rt) == T2);           /* T2 was found first, T3 ties */

    /* A waiting thread is not runnable. */
    mgs_sched_set_state(&rt, T2, OS_THREAD_WAITING);
    CHECK(mgs_sched_pick(&rt) == T3);

    /* Nor is a moribund one. */
    mgs_sched_set_state(&rt, T3, OS_THREAD_MORIBUND);
    CHECK(mgs_sched_pick(&rt) == T1);

    /* RUNNING is runnable: the current thread is a candidate to continue,
     * which is what makes this cooperative rather than round-robin.
     */
    mgs_sched_set_state(&rt, T1, OS_THREAD_RUNNING);
    CHECK(mgs_sched_pick(&rt) == T1);

    /* Suspend counts, so a positive suspend blocks even a RUNNING thread. */
    guest_write32(&rt.mem, T1 + OSTHREAD_SUSPEND, 1u);
    CHECK(mgs_sched_pick(&rt) == 0u);

    /* Creating the same guest thread twice is refused rather than duplicated:
     * two slots for one OSThread* would mean two host contexts for one guest
     * thread.
     */
    CHECK(mgs_sched_create(&rt, T1, 0u, 0u, 0u, 4) == 0);

    /* Priority and state round-trip through GUEST memory, big-endian. */
    CHECK(rt.mem.ram[(T2 & 0x3FFFFFFFu) + OSTHREAD_PRIORITY + 3] == 8);

    guest_memory_free(&rt.mem);
    free(rt.sched);
    printf(failures ? "%d failure(s)\n" : "all scheduler checks passed\n", failures);
    return failures != 0;
}
