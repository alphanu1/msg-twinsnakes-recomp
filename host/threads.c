/* What is every guest thread doing?
 *
 * A stopped boot looks the same from the outside whatever the cause: the
 * scheduler idles, or one thread yields in a loop, and the pc tells you only
 * which of those. The question that actually matters - WHICH threads exist,
 * what state each is in, and what each is blocked on - is answered entirely
 * by guest memory, because the SDK keeps every thread on a linked list at a
 * fixed address.
 *
 * So this walks the guest's own structures rather than tracking threads
 * ourselves. Nothing here is a model of the scheduler; it is a reader of it,
 * and it stays correct however the game creates and destroys threads.
 */
#include "module.h"

#include <stdio.h>
#include <string.h>

/* Low memory, from the SDK's os.h. */
#define OS_ACTIVE_THREAD_QUEUE 0x800000DCu   /* OSThreadQueue: head, tail */
#define OS_CURRENT_THREAD      0x800000E4u

/* OSThread, from OSThread.h. The context is first, so every field is past it. */
#define TH_CONTEXT_SRR0   408u
#define TH_STATE          0x2C8u
#define TH_SUSPEND        0x2CCu
#define TH_PRIORITY       0x2D0u
#define TH_QUEUE          0x2DCu   /* the queue it is blocked on, or 0 */
#define TH_LINK_ACTIVE    0x2FCu   /* next, prev */

static const char* state_name(unsigned s)
{
    switch (s) {
        case 0: return "exited";
        case 1: return "ready";
        case 2: return "running";
        case 4: return "waiting";
        case 8: return "moribund";
        default: return "?";
    }
}

void mgs_dump_threads(void* cpu, const char* (*symbol)(uint32_t))
{
    uint32_t thread = mgs_module_guest_read32(cpu, OS_ACTIVE_THREAD_QUEUE);
    uint32_t current = mgs_module_guest_read32(cpu, OS_CURRENT_THREAD);
    unsigned n = 0;

    printf("guest threads (current 0x%08X):\n", current);
    if (!thread) { printf("  (the active thread list is empty)\n"); return; }

    /* Bounded: a corrupt link would otherwise walk forever, and a diagnostic
     * that can hang is worse than no diagnostic. */
    for (; thread && n < 64u; ++n) {
        uint32_t state = mgs_module_guest_read32(cpu, thread + TH_STATE) >> 16;
        uint32_t susp  = mgs_module_guest_read32(cpu, thread + TH_SUSPEND);
        uint32_t prio  = mgs_module_guest_read32(cpu, thread + TH_PRIORITY);
        uint32_t queue = mgs_module_guest_read32(cpu, thread + TH_QUEUE);
        uint32_t srr0  = mgs_module_guest_read32(cpu, thread + TH_CONTEXT_SRR0);
        const char* where = symbol ? symbol(srr0) : NULL;

        printf("  %s0x%08X  %-8s prio %2u%s  resumes at 0x%08X%s%s\n",
               thread == current ? "* " : "  ", thread,
               state_name(state), prio,
               susp ? " SUSPENDED" : "",
               srr0, where ? "  " : "", where ? where : "");
        if (state == 4u)
            printf("        blocked on queue 0x%08X\n", queue);

        thread = mgs_module_guest_read32(cpu, thread + TH_LINK_ACTIVE);
    }
    if (n >= 64u) printf("  ... list truncated at 64\n");
}
