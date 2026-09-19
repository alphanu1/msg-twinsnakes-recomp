/* Delivering interrupts to the guest.
 *
 * The SDK's boot does not merely wait on hardware registers - it sets hardware
 * in motion and waits for the COMPLETION INTERRUPT to run a handler that
 * updates memory. Servicing the register is not enough, which is why the boot
 * ran 40 million steps polling a RAM location with only 32 MMIO reads: it was
 * waiting for a handler that could never run.
 *
 * An interrupt cannot be simulated from outside the guest. The SDK's handler
 * is guest code, touches guest structures and wakes guest threads, so the host
 * has to enter the guest and run it - which is what mgs_module_call_guest is
 * for.
 *
 * WHAT IS DELIVERED, AND WHAT IS NOT. __OSDispatchInterrupt reads the
 * processor interface's cause and mask registers and calls whichever handler
 * the guest registered. So the host raises an interrupt by setting a cause bit
 * and calling that one function - it does NOT reimplement the SDK's dispatch,
 * reach into the handler table, or know which handler is registered. The guest
 * decides all of that, exactly as it would on hardware.
 */
#include "module.h"
#include "platform/mmio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Processor interface, by offset from MMIO_PI. */
#define PI_INTSR   0x00u   /* interrupt cause: write 1 to acknowledge */
#define PI_INTMR   0x04u   /* interrupt mask */

/* Interrupt sources, from the SDK's OSInterrupt.h. The mask is built from the
 * TOP down - OS_INTERRUPTMASK(n) is 0x80000000 >> n - so source 0 is the most
 * significant bit, not the least. Building it the other way round silently
 * raises the wrong interrupt. */
#define OS_INTERRUPT_MASK(n) (0x80000000u >> (n))
#define INT_DSP_AI      5
#define INT_DSP_ARAM    6
#define INT_DSP_DSP     7
#define INT_AI_AI       8
#define INT_PI_SI      20
#define INT_PI_DI      21
#define INT_PI_VI      24

/* The PI cause register uses its own bit order, low bit first, unlike the
 * OS mask above. Keeping both straight is the whole difficulty here. */
#define PI_CAUSE_ERROR   (1u << 0)
#define PI_CAUSE_RSW     (1u << 1)
#define PI_CAUSE_DI      (1u << 2)
#define PI_CAUSE_SI      (1u << 3)
#define PI_CAUSE_EXI     (1u << 4)
#define PI_CAUSE_AI      (1u << 5)
#define PI_CAUSE_DSP     (1u << 6)
#define PI_CAUSE_MEM     (1u << 7)
#define PI_CAUSE_VI      (1u << 8)
#define PI_CAUSE_PE_TOKEN  (1u << 9)
#define PI_CAUSE_PE_FINISH (1u << 10)
#define PI_CAUSE_CP      (1u << 11)

/* From the SDK's OSException.h. */
#define OS_EXCEPTION_EXTERNAL_INTERRUPT 4u

static uint32_t s_dispatch_addr;      /* guest __OSDispatchInterrupt */
static uint64_t s_delivered, s_refused, s_failed;

void mgs_interrupt_set_dispatch(uint32_t guest_address);
void mgs_interrupt_set_dispatch(uint32_t guest_address) { s_dispatch_addr = guest_address; }

uint64_t mgs_interrupt_delivered(void);
uint64_t mgs_interrupt_delivered(void) { return s_delivered; }
uint64_t mgs_interrupt_refused(void);
uint64_t mgs_interrupt_refused(void) { return s_refused; }
uint64_t mgs_interrupt_failed(void);
uint64_t mgs_interrupt_failed(void) { return s_failed; }

/* Raise a PI interrupt and let the guest dispatch it.
 *
 * Returns 0 if nothing was delivered - which is normal and not a failure: the
 * guest masks interrupts around its own critical sections, and delivering one
 * anyway would break exactly the atomicity the cooperative scheduler exists to
 * preserve.
 */
int mgs_interrupt_raise(const MgsModule* mod, void* cpu, uint32_t cause_bit)
{
    MgsMmio* mmio = mgs_host_mmio();
    uint32_t mask;
    uint32_t args[2];

    if (!s_dispatch_addr) return 0;

    /* Two independent gates, and both matter.
     *
     * MSR[EE] is the processor's. The guest clears it around its own
     * critical sections - OSDisableInterrupts is exactly that - and
     * delivering an interrupt anyway would break the atomicity the
     * cooperative scheduler is built on. This host honours it for the same
     * reason the hardware does.
     *
     * PI's mask is the interrupt controller's, and says which SOURCES the
     * guest has armed. */
    if (!(mgs_module_msr(cpu) & 0x8000u)) { ++s_refused; return 0; }

    mask = mgs_mmio_read(mmio, MMIO_PI + PI_INTMR, 4);
    if (!(mask & cause_bit)) { ++s_refused; return 0; }   /* guest is not listening */

    /* Set the cause, then enter the guest's dispatcher. The handler
     * acknowledges by writing the bit back, so it is not cleared here. */
    {
        uint32_t cur = mgs_mmio_read(mmio, MMIO_PI + PI_INTSR, 4);
        mgs_mmio_write(mmio, MMIO_PI + PI_INTSR, cur | cause_bit, 4);
    }

    /* __OSDispatchInterrupt(exception, context) never returns - it ends in
     * OSLoadContext's rfi. So this is not a call: it is the state transition
     * the hardware performs on an external interrupt, after which the main
     * loop simply keeps stepping, now inside the handler. */
    if (!mgs_module_take_exception(cpu, s_dispatch_addr,
                                   OS_EXCEPTION_EXTERNAL_INTERRUPT)) {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "[interrupt] no current OSContext yet; "
                            "interrupt not delivered\n");
        }
        /* Take the cause bit back off. Leaving it set would have the guest
         * service a stale interrupt the moment it does become ready. */
        {
            uint32_t cur = mgs_mmio_read(mmio, MMIO_PI + PI_INTSR, 4);
            mgs_mmio_write(mmio, MMIO_PI + PI_INTSR, cur & ~cause_bit, 4);
        }
        ++s_failed;
        return 0;
    }

    ++s_delivered;
    return 1;
}

int mgs_interrupt_vi(const MgsModule* mod, void* cpu)
{
    /* Assert the display interrupts first: PI's VI bit is a mirror of them,
     * and the SDK's handler reads them to decide which path to take. */
    mgs_mmio_assert_retrace(mgs_host_mmio());
    return mgs_interrupt_raise(mod, cpu, PI_CAUSE_VI);
}

int mgs_interrupt_dsp(const MgsModule* mod, void* cpu)
{
    return mgs_interrupt_raise(mod, cpu, PI_CAUSE_DSP);
}

/* Completing a DSP task, without a DSP.
 *
 * `__DSP_boot_task` uploads microcode and then the real coprocessor runs it
 * and reports back through the mailbox. The SDK's `__DSPHandler` reads
 * exactly one message per interrupt and dispatches on it:
 *
 *   0xDCD10000  the task has started   -> init_cb
 *   0xDCD10001  it has resumed         -> res_cb
 *   0xDCD10002  it has yielded
 *   0xDCD10003  it has finished        -> done_cb
 *
 * The boot waits on a `done_cb` that sets one flag, so those two messages are
 * the whole of what is needed to get past it.
 *
 * THIS IS NOT A DSP AND DOES NOT PRETEND TO BE. No microcode runs, nothing is
 * mixed, and no sound comes out. What it models is the one fact the SDK is
 * waiting to learn - that the task it submitted is over - which is true here
 * the moment it is submitted, because nothing is going to run it.
 *
 * Two conditions guard it, and both matter. The message is posted only after
 * the guest has READ the boot message, because `__DSPHandler` asserts that a
 * current task exists and reading that message is what proves one does. And
 * only once the guest has stopped sending, because the upload is a dozen
 * sends in a row and interrupting it half way would have the handler consume
 * a message the boot sequence was waiting to send.
 */
static uint64_t s_dsp_tasks;
uint64_t mgs_interrupt_dsp_tasks(void);
uint64_t mgs_interrupt_dsp_tasks(void) { return s_dsp_tasks; }

/* An ARAM transfer has finished; tell the guest.
 *
 * The transfer itself is a memcpy that completed before the guest could
 * look, but the operating system does not poll - `ARQ` hands out completion
 * callbacks and the audio manager waits on what they set. Without the line
 * being raised the callback never runs, and the wait is indistinguishable
 * from a hang: `__AMPushBuffered` sat on one for 90.2% of a run.
 *
 * The source bit is already set where the transfer happened, which is what
 * tells the dispatcher this is ARAM rather than the DSP or the audio
 * interface sharing the same line. */
int mgs_interrupt_aram(const MgsModule* mod, void* cpu);
static uint64_t s_aram_raised, s_aram_refused;
uint64_t mgs_interrupt_aram_raised(void);
uint64_t mgs_interrupt_aram_raised(void) { return s_aram_raised; }
uint64_t mgs_interrupt_aram_refused(void);
uint64_t mgs_interrupt_aram_refused(void) { return s_aram_refused; }

int mgs_interrupt_aram(const MgsModule* mod, void* cpu)
{
    MgsMmio* m = mgs_host_mmio();
    if (!mgs_mmio_take_aram_irq(m)) return 0;
    if (mgs_interrupt_raise(mod, cpu, PI_CAUSE_DSP)) { ++s_aram_raised; return 1; }
    /* Not delivered - put it back rather than losing it. A transfer whose
     * completion is dropped is a callback that never runs. */
    mgs_mmio_put_aram_irq(m);
    ++s_aram_refused;
    return 0;
}

int mgs_interrupt_dsp_task(const MgsModule* mod, void* cpu);
int mgs_interrupt_dsp_task(const MgsModule* mod, void* cpu)
{
    static int phase;            /* 0 idle, 1 started posted, 2 done posted */
    MgsMmio* m = mgs_host_mmio();

    /* ON CHANGE, not on a cadence. Sampling every 4,096th call printed one
     * line, taken at the first call - long before the DSP had come up - and
     * the state it reported was true and useless. */
    if (getenv("MGS_TRACE_DSP")) {
        static int last = -1;
        int now = (mgs_mmio_dsp_booted(m) ? 1 : 0)
                | (mgs_mmio_dsp_mails_sent(m) ? 2 : 0)
                | (mgs_mmio_dsp_mail_pending(m) ? 4 : 0)
                | (phase << 3);
        if (now != last) {
            fprintf(stderr, "[dsp] booted=%d sent=%u pending=%d phase=%d\n",
                    mgs_mmio_dsp_booted(m), mgs_mmio_dsp_mails_sent(m),
                    mgs_mmio_dsp_mail_pending(m), phase);
            last = now;
        }
    }
    if (!mgs_mmio_dsp_booted(m)) return 0;
    /* The guest has not read what is already there. */
    if (mgs_mmio_dsp_mail_pending(m)) return 0;

    /* AND THE UPLOAD HAS TO HAVE FINISHED.
     *
     * `__DSP_boot_task` sends a dozen messages in a row and only afterwards
     * does the task it is booting become the current one. A task message
     * posted in the middle of that arrives at a handler whose
     * `__DSP_curr_task` is still NULL - which in a release build, with the
     * assertions compiled out, is a store through a null pointer followed by
     * a call through whatever is at offset 0x28 of it.
     *
     * Posting at `sent=6` did exactly that. Waiting for the sends to stop is
     * the signal that the upload is over, and needs no count of how many
     * messages the sequence happens to contain. */
    /* ONLY WHEN STARTING A TASK. Once the start has been posted and read,
     * the finish must follow regardless - and reading a message resets the
     * count of sends, so requiring sends here left the sequence stuck half
     * way through, with the start delivered and the finish never posted. */
    if (phase == 0) {
        static uint32_t last_sent;
        static unsigned quiet;
        uint32_t sent = mgs_mmio_dsp_mails_sent(m);
        if (sent == 0u) return 0;
        if (sent != last_sent) { last_sent = sent; quiet = 0u; return 0; }
        if (++quiet < 8u) return 0;
    }

    {
        uint32_t mail = phase == 0 ? 0xDCD10000u : 0xDCD10003u;

        mgs_mmio_dsp_post_mail(m, mail);
        if (!mgs_interrupt_raise(mod, cpu, PI_CAUSE_DSP)) {
            /* NOT DELIVERED - the guest has interrupts off, or has not armed
             * the DSP line yet. TAKE THE MESSAGE BACK OUT. Leaving it there
             * means the next attempt sees a message already pending and
             * declines, while the guest never reads it because no interrupt
             * ever arrived: a deadlock of this code's own making, and one
             * that presents as the boot never getting its callback. */
            mgs_mmio_dsp_post_mail(m, 0u);
            mgs_mmio_dsp_clear_mail(m);
            return 0;
        }
        if (mail == 0xDCD10000u) { phase = 1; return 1; }
        phase = 0;                                    /* ready for the next */
        ++s_dsp_tasks;
        if (getenv("MGS_TRACE_DSP"))
            fprintf(stderr, "[dsp] task %llu completed\n",
                    (unsigned long long)s_dsp_tasks);
        return 1;
    }
}

/* The graphics processor's "I have retired everything you sent me" signal.
 *
 * GXDrawDone writes a draw-done token into the FIFO and then SLEEPS until
 * the PE finish interrupt wakes it. There is no graphics processor here, so
 * nothing retires anything - but the token is in the command stream, the
 * host can see it, and raising the interrupt when it appears is the honest
 * answer: a GP that completes instantly is still a GP that completes.
 *
 * Until this existed the game rendered its first frame, called GXDrawDone,
 * and every thread slept forever while the retrace handler kept running.
 * That looked like a healthy idle loop and was not.
 */
/* The engine's frame ring, watched from the one place that always sees a
 * completion: the moment we deliver it.
 *
 * The draw-done callback at 0x8004D170 signals the main loop's semaphore ONLY
 * when the flag at +0x64 of a slot is set, and it picks that slot with the
 * index at +0x16F8 - while the submitting side at 0x8004C318 arms the slot at
 * a DIFFERENT index, +0x16F0. Two indices into one ring is a producer and a
 * consumer, and whether they agree is the whole question: 62 submissions
 * produced 62 completions but only 60 signals, and a consumer that is running
 * ahead of the producer reads a slot nobody armed, whose flag is the zero
 * .bss was initialised with.
 *
 * A pc hook cannot answer this. The callback is reached from inside the SDK's
 * interrupt dispatch, DOL-internal, and the run loop only observes pc at
 * dispatch boundaries - so absence from a pc trace would prove nothing. This
 * runs in our own delivery path instead, which is the one place that cannot
 * miss.
 *
 * The addresses are the engine's and were established in F112/F115. They are
 * a diagnostic, not an interface: nothing here changes guest state.
 */
#define ENGINE_FRAME_BASE   0x8020B918u
#define ENGINE_RING_PRODUCER  (ENGINE_FRAME_BASE + 0x16F0u)
#define ENGINE_RING_CONSUMER  (ENGINE_FRAME_BASE + 0x16F8u)
#define ENGINE_SLOT_STRIDE  0x18u
#define ENGINE_SLOT_FLAG    0x64u

static uint64_t s_pe_seen, s_pe_sent;

/* SAMPLED BEFORE THE HANDLER RUNS - AND THE HANDLER DOES NOT RUN HERE.
 *
 * mgs_interrupt_raise does not call the handler. It performs the state
 * transition the hardware performs on an external interrupt and returns; the
 * guest executes the handler on subsequent steps of the run loop. Two
 * versions of this trace got that wrong in opposite directions:
 *
 *   - the first sampled the ring AFTER the raise and reported it as the
 *     state the callback acted on. It is not - no guest instruction has run.
 *   - the second printed the PI cause after the raise and read the PE bit
 *     being set as evidence of a stuck interrupt. We had just set it.
 *
 * So everything here is sampled BEFORE the raise, and the cause is printed
 * for context only. What is still pending when the run ENDS is the honest
 * measure of an unserviced interrupt, and that is reported separately.
 */
static void trace_frame_ring(void* cpu)
{
    uint32_t prod, cons, flag;

    prod = mgs_module_guest_read32(cpu, ENGINE_RING_PRODUCER);
    cons = mgs_module_guest_read32(cpu, ENGINE_RING_CONSUMER);
    flag = mgs_module_guest_read32(
        cpu, ENGINE_FRAME_BASE + cons * ENGINE_SLOT_STRIDE + ENGINE_SLOT_FLAG);

    fprintf(stderr,
            "[ring] pe #%llu  at interrupt %llu  cause 0x%08X  "
            "prod %u cons %u flag %u%s\n",
            (unsigned long long)(s_pe_sent + 1ull),
            (unsigned long long)s_delivered,
            mgs_mmio_read(mgs_host_mmio(), MMIO_PI + PI_INTSR, 4),
            prod, cons, flag,
            flag ? "" : "   <-- flag clear, this one will NOT signal");
}

static uint64_t s_pe_seen, s_pe_sent;

/* SAMPLED BEFORE THE HANDLER RUNS, NOT AFTER.
 *
 * mgs_interrupt_raise enters the guest and runs __OSDispatchInterrupt to
 * completion, so by the time it returns the callback has already read the
 * flag AND advanced the consumer index. A trace placed after it reports the
 * state the callback left behind, which is not the state it acted on - the
 * first version of this did exactly that and made one decline look like the
 * only one. Both samples are taken, and the pair is what is printed. */
uint64_t mgs_interrupt_pe_seen(void) { return s_pe_seen; }
uint64_t mgs_interrupt_pe_sent(void) { return s_pe_sent; }

int mgs_interrupt_pe_finish(const MgsModule* mod, void* cpu)
{
    if (!mgs_display_take_draw_done())
        return 0;
    ++s_pe_seen;
    if (getenv("MGS_TRACE_RING")) trace_frame_ring(cpu);
    if (mgs_interrupt_raise(mod, cpu, PI_CAUSE_PE_FINISH)) {
        ++s_pe_sent;
        if (getenv("MGS_TRACE_PE"))
            fprintf(stderr, "[pe] finish delivered #%llu\n",
                    (unsigned long long)s_pe_sent);
        return 1;
    }
    /* Not delivered - the guest has interrupts off, or has not armed PE yet.
     * Put the token back so it is offered again rather than dropped. */
    mgs_display_put_draw_done();
    return 0;
}
