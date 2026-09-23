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
#include "memory/guest.h"

#include <stdio.h>
#include <string.h>

/* Low memory, from the SDK's os.h. */
#define OS_ACTIVE_THREAD_QUEUE 0x800000DCu   /* OSThreadQueue: head, tail */
#define OS_CURRENT_THREAD      0x800000E4u

/* OSThread, from OSThread.h. The context is first, so every field is past it. */
/* OSContext sits at the start of OSThread, so the saved registers are at
 * their own offsets within it: the general-purpose file first, then cr, lr
 * and the rest. 408 is 0x198, which is where srr0 lands - and that agreeing
 * with the SDK's layout is what makes the two below safe to use. */
#define TH_CONTEXT_GPR1     4u     /* gpr[1]: the stack pointer */
#define TH_CONTEXT_LR       0x84u
#define TH_CONTEXT_SRR0   408u
#define TH_STATE          0x2C8u
#define TH_SUSPEND        0x2CCu
#define TH_PRIORITY       0x2D0u
#define TH_QUEUE          0x2DCu   /* the queue it is blocked on, or 0 */
#define TH_LINK_ACTIVE    0x2FCu   /* next, prev */

/* Either address window, which a stack frame or a thread may live in. */
static int in_guest_ram(uint32_t a)
{
    if (a >= 0x80000000u && a < 0x81800000u) return 1;
    return a >= GUEST_VMEM_BASE && a < GUEST_VMEM_BASE + GUEST_VMEM_SIZE;
}

/* WHAT THE QUEUE IT IS BLOCKED ON ACTUALLY HOLDS.
 *
 * "blocked on queue 0x7F4A595C" says a thread is waiting and nothing about
 * whether it is waiting reasonably. A queue with messages in it and a thread
 * asleep on it is a scheduling fault; an EMPTY queue is a thread waiting for
 * something that was never sent, which is a completely different bug - and
 * the two are indistinguishable without reading the queue.
 *
 * The field a thread records is the thread-queue it is parked on, which is
 * INSIDE the OSMessageQueue rather than the queue itself: `&mq->queueReceive`
 * for a receiver, which is mq + 8, and `&mq->queueSend` for a sender blocked
 * on a full queue, which is mq + 0. Both are tried and the one whose counts
 * are self-consistent is reported; if neither is, nothing is claimed, because
 * an OSThreadQueue is also used on its own by OSSleepThread and that is not
 * a message queue at all.
 *
 *     OSMessageQueue: queueSend 0x00, queueReceive 0x08, msgArray 0x10,
 *                     msgCount 0x14, firstIndex 0x18, usedCount 0x1C
 */
static int describe_queue(void* cpu, uint32_t queue, uint32_t off)
{
    uint32_t mq = queue - off;
    uint32_t array, count, first, used;

    if (!in_guest_ram(mq) || (mq & 3u)) return 0;
    array = mgs_module_guest_read32(cpu, mq + 0x10u);
    count = mgs_module_guest_read32(cpu, mq + 0x14u);
    first = mgs_module_guest_read32(cpu, mq + 0x18u);
    used  = mgs_module_guest_read32(cpu, mq + 0x1Cu);

    /* A plausible message queue: a real buffer, a sane capacity, and an
     * occupancy and read cursor that fit inside it. Garbage fails all four. */
    if (!in_guest_ram(array) || (array & 3u)) return 0;
    if (count == 0u || count > 4096u) return 0;
    if (used > count || first >= count) return 0;

    /* "EMPTY" IS NOT "NEVER USED", and saying so cost a wrong root cause
     * once already: `usedCount` is the occupancy right now, so a queue that
     * carried thirty messages and was drained reads exactly the same zero as
     * one nothing was ever sent to. The reader cursor is printed alongside
     * it because THAT does distinguish them - a `first` that has moved is
     * proof the queue has been read from, whatever it holds at this
     * instant. */
    /* AND A ONE-SLOT QUEUE DEFEATS THAT TEST ENTIRELY. `first` advances
     * modulo the capacity, so for a capacity of one it is ALWAYS zero, and
     * the rule above then reports the busiest queue in the run as one
     * nothing was ever read from. That happened: a one-slot queue that had
     * carried a hundred and fifty messages was read as the head of a
     * deadlock and chased as such (F258). Say what can honestly be said
     * about it instead, which is nothing. */
    printf("        %s queue 0x%08X: %u of %u slots used, read cursor %u%s\n",
           off ? "receive on" : "send to", mq, used, count, first,
           used ? ""
                : count == 1u
                      ? "  <- empty; a one-slot queue's cursor cannot say more"
                : first ? "  <- drained (it has carried messages)"
                        : "  <- empty, and never read from");
    return 1;
}

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

    /* Bounded in two ways: a corrupt link would otherwise walk for ever, and
     * a pointer that is in neither address window is not a thread. A
     * diagnostic that hangs or crashes is worse than no diagnostic - and this
     * one runs precisely when the guest has already gone wrong, so garbage is
     * the expected input.
     *
     * THE SECOND WINDOW COUNTS. The engine overlay lives at 0x7E000000 and
     * upwards, and it creates threads whose structures are in its own memory.
     * Rejecting those as "not a thread" truncated the list at the first one -
     * so a dump that showed four threads was hiding however many came after,
     * and the three that were actually blocked were among the hidden ones.
     * An instrument that quietly stops early is worse than one that refuses
     * outright, because the short answer still looks like an answer. */
    for (; n < 64u; ++n) {
        int in_mem1 = thread >= 0x80000000u && thread < 0x81800000u;
        int in_vmem = thread >= GUEST_VMEM_BASE &&
                      thread <  GUEST_VMEM_BASE + GUEST_VMEM_SIZE;
        if ((!in_mem1 && !in_vmem) || (thread & 3u)) {
            if (thread) printf("  (link 0x%08X is not a thread; list ends)\n", thread);
            break;
        }
        uint32_t state = mgs_module_guest_read32(cpu, thread + TH_STATE) >> 16;
        uint32_t susp  = mgs_module_guest_read32(cpu, thread + TH_SUSPEND);
        uint32_t prio  = mgs_module_guest_read32(cpu, thread + TH_PRIORITY);
        uint32_t queue = mgs_module_guest_read32(cpu, thread + TH_QUEUE);
        uint32_t srr0  = mgs_module_guest_read32(cpu, thread + TH_CONTEXT_SRR0);
        const char* where = symbol ? symbol(srr0) : NULL;

        /* WHAT THE THREAD IS ACTUALLY DOING, which its resume address does
         * not say - every switched-out thread resumes inside the scheduler,
         * so that column reads the same for all of them and means nothing.
         *
         * The call chain does say. PowerPC's ABI has each frame point at the
         * one below it with the return address a word in, so walking the
         * saved stack pointer gives the path the thread took to block. That
         * is the difference between "waiting on a queue" and "waiting on a
         * queue, inside the texture loader, called from the task scheduler".
         */
        printf("  %s0x%08X  %-8s prio %2u%s  resumes at 0x%08X%s%s\n",
               thread == current ? "* " : "  ", thread,
               state_name(state), prio,
               susp ? " SUSPENDED" : "",
               srr0, where ? "  " : "", where ? where : "");
        if (state == 4u) {
            printf("        blocked on queue 0x%08X\n", queue);
            /* Receiver first: it is much the commoner case, and trying the
             * sender offset first would decode a receiver's queue against
             * the wrong fields. */
            if (!describe_queue(cpu, queue, 8u))
                (void)describe_queue(cpu, queue, 0u);
        }

        {
            uint32_t frame = mgs_module_guest_read32(cpu, thread + TH_CONTEXT_GPR1);
            uint32_t lr    = mgs_module_guest_read32(cpu, thread + TH_CONTEXT_LR);
            unsigned depth;

            if (lr) {
                const char* n = symbol ? symbol(lr) : NULL;
                printf("        called from 0x%08X%s%s\n",
                       lr, n ? "  " : "", n ? n : "");
            }
            /* Bounded, and every frame checked before it is followed: this
             * runs on a guest that has already gone wrong, so a stack of
             * garbage is an expected input rather than a surprise. A frame
             * must be word-aligned, inside an address window, and ABOVE the
             * one before it - stacks grow down, so a link that does not
             * increase is corrupt and following it would loop. */
            for (depth = 0u; depth < 12u; ++depth) {
                uint32_t next, ret;
                const char* n;

                if (!in_guest_ram(frame) || (frame & 3u)) break;
                next = mgs_module_guest_read32(cpu, frame);
                ret  = mgs_module_guest_read32(cpu, frame + 4u);
                if (!in_guest_ram(next) || next <= frame) break;
                if (ret) {
                    n = symbol ? symbol(ret) : NULL;
                    printf("          %2u  0x%08X%s%s\n",
                           depth, ret, n ? "  " : "", n ? n : "");
                }
                frame = next;
            }
        }

        thread = mgs_module_guest_read32(cpu, thread + TH_LINK_ACTIVE);
    }
    if (n >= 64u) printf("  ... list truncated at 64\n");
}
