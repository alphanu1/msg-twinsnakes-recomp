/* Guest threads: cooperative, one at a time, priority-ordered.
 *
 * The design document flags this as the high-risk item, and the reason is
 * worth stating precisely. GameCube OS threads are cooperative on a single
 * core: a thread runs until it blocks or yields, and nothing preempts it. The
 * game is written against that guarantee, so any sequence between two
 * yield points is atomic from its point of view. Run two guest threads
 * genuinely in parallel on host threads and that atomicity vanishes - and it
 * vanishes silently, as rare corruption rather than as a crash.
 *
 * So: EXACTLY ONE guest thread executes at any instant. Host threads exist
 * only inside the platform layer - audio callback, file prefetch, GPU
 * submission - and never touch guest memory except through queued events.
 *
 * The OSThread structure itself lives in GUEST memory: the game allocates it,
 * and reads its fields. We therefore own only the execution context, and
 * every field the game can see is read and written through the byte-swapping
 * accessors at the offsets the SDK defines.
 */
#ifndef MGS_OS_THREAD_H
#define MGS_OS_THREAD_H

#include "os_runtime.h"

/* Field offsets within the guest OSThread, from the SDK's own header. Named
 * constants rather than a host struct: a host struct would carry host
 * endianness and host padding, and this one is laid out by a 2003 PowerPC
 * compiler.
 */
#define OSTHREAD_CONTEXT      0x000u
#define OSTHREAD_STATE        0x2C8u   /* u16 */
#define OSTHREAD_ATTR         0x2CAu   /* u16 */
#define OSTHREAD_SUSPEND      0x2CCu   /* s32 */
#define OSTHREAD_PRIORITY     0x2D0u   /* s32, 0 highest .. 31 lowest */
#define OSTHREAD_BASE         0x2D4u   /* s32 */
#define OSTHREAD_VAL          0x2D8u
#define OSTHREAD_QUEUE        0x2DCu
#define OSTHREAD_LINK_NEXT    0x2E0u
#define OSTHREAD_LINK_PREV    0x2E4u
#define OSTHREAD_QUEUEJOIN    0x2E8u
#define OSTHREAD_MUTEX        0x2F0u
#define OSTHREAD_STACKBASE    0x304u
#define OSTHREAD_STACKEND     0x308u
#define OSTHREAD_ERROR        0x30Cu
#define OSTHREAD_SPECIFIC     0x310u
#define OSTHREAD_SIZEOF       0x318u

/* OS_THREAD_STATE, from the SDK. A bitmask, not an enum sequence. */
#define OS_THREAD_READY       1u
#define OS_THREAD_RUNNING     2u
#define OS_THREAD_WAITING     4u
#define OS_THREAD_MORIBUND    8u

#define OS_PRIORITY_MIN       0    /* highest */
#define OS_PRIORITY_MAX       31   /* lowest */

#define MGS_MAX_THREADS       32

/* Our side of a guest thread: the host execution context, keyed by the guest
 * address of its OSThread. The guest address is the identity - the game
 * passes OSThread* around and we must map back.
 */
typedef struct MgsThread {
    uint32_t guest_thread;      /* guest OSThread*, 0 if the slot is free */
    uint32_t entry;             /* guest entry point */
    uint32_t arg;               /* r3 at entry */
    uint32_t stack_top;         /* guest stack pointer to start with */
    int      in_use;
} MgsThread;

typedef struct MgsScheduler {
    MgsThread threads[MGS_MAX_THREADS];
    uint32_t  current;          /* guest OSThread* of the running thread */
    int       needs_reschedule;
} MgsScheduler;

void     mgs_sched_init(MgsRuntime* rt);
int      mgs_sched_create(MgsRuntime* rt, uint32_t guest_thread,
                          uint32_t entry, uint32_t arg, uint32_t stack_top,
                          int priority);
/* The highest-priority runnable thread, or 0 if none. Lowest number wins,
 * and ties go to the thread that has waited longest, which is what the SDK's
 * round-robin within a priority level amounts to.
 */
uint32_t mgs_sched_pick(MgsRuntime* rt);
void     mgs_sched_set_state(MgsRuntime* rt, uint32_t guest_thread, uint32_t state);
uint32_t mgs_sched_state(const MgsRuntime* rt, uint32_t guest_thread);

#endif
