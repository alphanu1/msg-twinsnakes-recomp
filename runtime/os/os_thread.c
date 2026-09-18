#include "os_thread.h"

#include <stdlib.h>
#include <string.h>

static MgsScheduler* sched_of(MgsRuntime* rt) { return rt->sched; }

void mgs_sched_init(MgsRuntime* rt)
{
    if (!rt->sched)
        rt->sched = (MgsScheduler*)calloc(1, sizeof(MgsScheduler));
}

static MgsThread* slot_for(MgsScheduler* s, uint32_t guest_thread)
{
    unsigned i;
    for (i = 0; i < MGS_MAX_THREADS; ++i)
        if (s->threads[i].in_use && s->threads[i].guest_thread == guest_thread)
            return &s->threads[i];
    return NULL;
}

int mgs_sched_create(MgsRuntime* rt, uint32_t guest_thread,
                     uint32_t entry, uint32_t arg, uint32_t stack_top,
                     int priority)
{
    MgsScheduler* s = sched_of(rt);
    unsigned i;

    if (!s || !guest_thread) return 0;
    if (slot_for(s, guest_thread)) return 0;      /* already created */

    for (i = 0; i < MGS_MAX_THREADS; ++i) {
        MgsThread* t = &s->threads[i];
        if (t->in_use) continue;

        t->in_use = 1;
        t->guest_thread = guest_thread;
        t->entry = entry;
        t->arg = arg;
        t->stack_top = stack_top;

        /* The guest owns these fields and reads them back, so they are
         * written into guest memory rather than kept host-side. A thread
         * starts READY, not RUNNING: OSCreateThread does not run it.
         */
        guest_write16(&rt->mem, guest_thread + OSTHREAD_STATE, (uint16_t)OS_THREAD_READY);
        guest_write32(&rt->mem, guest_thread + OSTHREAD_PRIORITY, (uint32_t)priority);
        guest_write32(&rt->mem, guest_thread + OSTHREAD_BASE, (uint32_t)priority);
        guest_write32(&rt->mem, guest_thread + OSTHREAD_SUSPEND, 1u); /* suspended at birth */
        guest_write32(&rt->mem, guest_thread + OSTHREAD_ERROR, 0u);
        return 1;
    }
    return 0;   /* table full; the caller reports failure to the guest */
}

uint32_t mgs_sched_state(const MgsRuntime* rt, uint32_t guest_thread)
{
    if (!guest_thread) return 0u;
    return guest_read16(&rt->mem, guest_thread + OSTHREAD_STATE);
}

void mgs_sched_set_state(MgsRuntime* rt, uint32_t guest_thread, uint32_t state)
{
    if (!guest_thread) return;
    guest_write16(&rt->mem, guest_thread + OSTHREAD_STATE, (uint16_t)state);
}

uint32_t mgs_sched_pick(MgsRuntime* rt)
{
    MgsScheduler* s = sched_of(rt);
    uint32_t best = 0u;
    int best_priority = OS_PRIORITY_MAX + 1;
    unsigned i;

    if (!s) return 0u;

    for (i = 0; i < MGS_MAX_THREADS; ++i) {
        const MgsThread* t = &s->threads[i];
        uint32_t state;
        int priority;

        if (!t->in_use) continue;

        state = mgs_sched_state(rt, t->guest_thread);
        /* RUNNING counts as runnable: the current thread is a candidate to
         * continue, which is what makes this cooperative rather than
         * round-robin. A thread only loses the CPU by yielding or blocking.
         */
        if (!(state & (OS_THREAD_READY | OS_THREAD_RUNNING))) continue;
        if ((int32_t)guest_read32(&rt->mem, t->guest_thread + OSTHREAD_SUSPEND) > 0) continue;

        priority = (int)guest_read32(&rt->mem, t->guest_thread + OSTHREAD_PRIORITY);

        /* Lower number is higher priority. Strictly less-than, so ties leave
         * the incumbent in place - without that, equal-priority threads would
         * swap on every reschedule and the game's assumption that it keeps
         * the CPU until it yields would quietly break.
         */
        if (priority < best_priority) {
            best_priority = priority;
            best = t->guest_thread;
        }
    }
    return best;
}
