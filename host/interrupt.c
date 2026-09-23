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
        /* THE CAUSE BIT STAYS SET. It used to be taken back off here, to
         * avoid the guest "servicing a stale interrupt the moment it does
         * become ready" - but that reasoning is backwards, and it
         * contradicts the rule the next comment in this file spends a
         * paragraph establishing. An interrupt that was raised and never
         * delivered is not stale: it is OUTSTANDING. The device really did
         * complete, nothing has acknowledged it, and the line is still
         * asserted. Hardware holds it high until a handler writes the bit
         * back, and the CPU takes it as soon as it can.
         *
         * The window this lands in is a THREAD SWITCH. There is no current
         * OSContext for a few instructions between OSClearContext and the
         * next OSSetCurrentContext, and an interrupt arriving exactly there
         * found none and was destroyed. On hardware that window is covered
         * by MSR[EE] being clear; our delivery checks EE separately, so we
         * manufactured a hole the console does not have.
         *
         * Dropping it lost whole events. Four were destroyed in a single
         * boot, mid-run rather than during startup, and a sound stream that
         * had carried seventy messages then waited forever for the
         * seventy-first (F256).
         *
         * Leaving it set costs nothing: mgs_interrupt_pending re-offers it
         * on a later step, once a context exists, and does so only while
         * `cause & mask` says the guest still has that source armed. */
        ++s_failed;
        return 0;
    }

    ++s_delivered;
    return 1;
}

/* THE EXTERNAL INTERRUPT IS LEVEL-TRIGGERED, AND THAT IS NOT A DETAIL.
 *
 * The processor interface asserts its line while ANY armed cause bit is set.
 * The CPU does not take one interrupt per event - it takes one whenever that
 * line is high and MSR[EE] is on. So a source that is still pending after a
 * handler returns is taken AGAIN, immediately, as `rfi` restores EE.
 *
 * That matters because the SDK's __OSDispatchInterrupt services exactly ONE
 * source per entry: it builds the pending set, picks the highest priority
 * from InterruptPrioTable, calls that one handler and returns through
 * OSLoadContext. Everything else stays pending and relies on being re-taken.
 *
 * Raising only on new events, as this file did, quietly loses every
 * completion that arrives while a higher-priority source is pending. VI
 * outranks PE in that table, so a draw-done landing in the same window as a
 * retrace was serviced as the retrace and never seen again - the engine's
 * 62nd frame completion, the one whose signal never came, and the boot's
 * whole livelock (HANDOFF F126). The DSP line was being lost the same way.
 *
 * So pending interrupts are re-offered rather than dropped. This does not
 * invent an event: it only re-enters the dispatcher while the guest's own
 * cause and mask registers say the line is still asserted, which is what the
 * hardware does. A source the guest has masked is not re-offered, because the
 * SDK keeps PI's mask in step with its software mask (__OSMaskInterrupts
 * writes __PIRegs[1]), so `cause & mask` is the honest test for "still
 * asserted".
 */
static uint64_t s_redelivered;
uint64_t mgs_interrupt_redelivered(void);
uint64_t mgs_interrupt_redelivered(void) { return s_redelivered; }

int mgs_interrupt_pending(const MgsModule* mod, void* cpu);
int mgs_interrupt_pending(const MgsModule* mod, void* cpu)
{
    MgsMmio* mmio = mgs_host_mmio();
    uint32_t cause, mask;

    (void)mod;
    if (!s_dispatch_addr) return 0;
    if (!(mgs_module_msr(cpu) & 0x8000u)) return 0;   /* the guest has EE off */

    cause = mgs_mmio_read(mmio, MMIO_PI + PI_INTSR, 4);
    mask  = mgs_mmio_read(mmio, MMIO_PI + PI_INTMR, 4);
    if (!(cause & mask)) return 0;                    /* line is not asserted */

    /* No cause bit is set here. The line is already high; this is only the
     * CPU taking the exception it would have taken anyway. */
    if (!mgs_module_take_exception(cpu, s_dispatch_addr,
                                   OS_EXCEPTION_EXTERNAL_INTERRUPT))
        return 0;

    ++s_delivered;
    ++s_redelivered;
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
static uint64_t s_dsp_offers, s_dsp_not_booted, s_dsp_mail_pending;
static uint64_t s_dsp_no_task, s_dsp_no_frame, s_dsp_undelivered;
uint64_t mgs_dsp_task_stats(uint64_t* o, uint64_t* nb, uint64_t* mp,
                            uint64_t* nt, uint64_t* nf, uint64_t* ud);
uint64_t mgs_dsp_task_stats(uint64_t* o, uint64_t* nb, uint64_t* mp,
                            uint64_t* nt, uint64_t* nf, uint64_t* ud)
{
    *o = s_dsp_offers; *nb = s_dsp_not_booted; *mp = s_dsp_mail_pending;
    *nt = s_dsp_no_task; *nf = s_dsp_no_frame; *ud = s_dsp_undelivered;
    return s_dsp_tasks;
}
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
    /* Tell the DEVICE first. PI's DSP bit is shared by three sources and the
     * guest's dispatcher reads the DSP's status register to tell them apart;
     * raising the line without it delivers the completion to nobody. */
    mgs_mmio_dsp_assert_aram(m);
    if (mgs_interrupt_raise(mod, cpu, PI_CAUSE_DSP)) { ++s_aram_raised; return 1; }
    /* Not delivered - put it back rather than losing it. A transfer whose
     * completion is dropped is a callback that never runs. */
    mgs_mmio_put_aram_irq(m);
    ++s_aram_refused;
    return 0;
}

/* THE AUDIO DMA'S COMPLETION.
 *
 * The same shape as the ARAM one above and for the same reason, but it is
 * asking a different question. An ARAM completion says a copy finished; this
 * says a buffer of sound has started playing and the next one is wanted. The
 * SDK's `__AIDHandler` acknowledges it and calls whatever was handed to
 * `AIRegisterDMACallback`, which is how the audio manager - and above it, a
 * movie player - is told to produce more.
 *
 * Nothing raised this before, because the DMA engine that raises it was not
 * modelled at all. A game that streams is then not slow but stopped: it
 * submits one buffer, waits to be asked for another, and is never asked. It
 * stops reading the disc, because it has nothing to read for.
 */
int mgs_interrupt_aid(const MgsModule* mod, void* cpu);
static uint64_t s_aid_raised, s_aid_refused;
uint64_t mgs_interrupt_aid_raised(void);
uint64_t mgs_interrupt_aid_raised(void) { return s_aid_raised; }
uint64_t mgs_interrupt_aid_refused(void);
uint64_t mgs_interrupt_aid_refused(void) { return s_aid_refused; }

int mgs_interrupt_aid(const MgsModule* mod, void* cpu)
{
    MgsMmio* m = mgs_host_mmio();
    if (!mgs_mmio_take_aid_irq(m)) return 0;
    mgs_mmio_dsp_assert_aid(m);
    if (mgs_interrupt_raise(mod, cpu, PI_CAUSE_DSP)) { ++s_aid_raised; return 1; }
    /* Put it back rather than lose it: a dropped completion is a callback
     * that never runs, and the stream stops for good. */
    mgs_mmio_put_aid_irq(m);
    ++s_aid_refused;
    return 0;
}

/* `__DSP_curr_task` in main.dol: the SDK's current DSP task, or null when
 * there is none. Read before posting a task mail, because the handler
 * dereferences it without checking (see below). */
#define GUEST_DSP_CURR_TASK 0x8027DF94u

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
    /* WHY A TASK MAIL DID NOT GO OUT, counted.
     *
     * Six resumes were delivered in a run where the audio DMA fired 18,686
     * times, and "six" says nothing about which of the four gates below
     * stopped the rest. Counting each is how that becomes answerable
     * without another guess. Reported at exit. */
    ++s_dsp_offers;
    if (!mgs_mmio_dsp_booted(m)) { ++s_dsp_not_booted; return 0; }
    /* The guest has not read what is already there. */
    if (mgs_mmio_dsp_mail_pending(m)) { ++s_dsp_mail_pending; return 0; }

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
        /* WHICH MAIL, AND A KNOWN DEFECT IN THE DEFAULT (F219).
         *
         * These are the DSP's task mails, and the SDK's handler treats them
         * very differently (dolsdk2004 dsp_task.c):
         *
         *   0xDCD10000  init    -> the task's init_cb
         *   0xDCD10001  resume  -> the task's res_cb
         *   0xDCD10003  done    -> the task's done_cb, and then
         *                          __DSP_remove_task(): the task is UNLINKED
         *
         * We post init, then DONE, then init again - and `done` takes the
         * task off the list. The task removed is AX's, and AX is what pulls
         * PCM out of the Vorbis decoder: `__AXOutAiCallback` runs a mixing
         * frame only when `__AXOutDspReady` is 1, and the one place that
         * sets it is `__AXDSPResumeCallback`, reached only by a RESUME.
         * That is the far end of the movie stall (F218).
         *
         * So `done` is wrong and `resume` is what a persistent AX task
         * expects.
         *
         * IT IS NOW THE DEFAULT, AND THE REASON IT WAS NOT IS GONE (F263).
         * The note here used to read "switching it lets AX actually run and
         * the boot then reaches audio paths this runtime does not model -
         * one run ended in an unhandled exception at 2.5M steps", and that
         * was the right call at the time. Those paths are now modelled: the
         * voice mixer, the SDL device, the ARAM store the voices read from
         * and the audio-DMA clock that paces them all exist.
         *
         * Re-measured before changing it, rather than assumed: with the
         * resume mail, runs reach the 200M and 400M step limits cleanly
         * (pc 0x80061584 and 0x8005BF18), where the recorded failure was at
         * 2.5M. The mixer then runs 31,160 frames in 200M steps and reaches
         * 87.4% of full scale.
         *
         * WITH THE `done` MAIL THERE IS NO SOUND AT ALL, and that is worth
         * stating plainly because it is not obvious from the outside: `done`
         * runs the task's done_cb and `__DSP_remove_task` UNLINKS the task,
         * so `__AXOutDspReady` is never set, `__AXOutAiCallback` never runs
         * a mixing frame, and the mixer reports zero frames. Every "is there
         * sound yet" measurement taken against the default was measuring a
         * mixer that was never called.
         *
         * MGS_DSP_RESUME=0 restores the old mail, for comparison.
         */
        static int resume_mail = -1;
        static uint64_t seen_sends;   /* AX frames produced, not the last AID seen */
        uint32_t mail;
        if (resume_mail < 0) {
            const char* e = getenv("MGS_DSP_RESUME");
            resume_mail = !(e && e[0] == '0');
        }
        mail = phase == 0 ? 0xDCD10000u
             : (resume_mail ? 0xDCD10001u : 0xDCD10003u);

        /* A RESUME ANSWERS AN ASSERT. IT IS NOT A TIMER.
         *
         * The run loop offers this every 4,099 steps, which for a resume is
         * wrong twice over: the DSP resumes because the guest ASSERTED the
         * task, and a resume posted when nothing asked for one is a message
         * the line has to carry for nothing. PI's DSP bit is shared by the
         * mailbox, ARAM and the audio DMA, and the dispatcher services one
         * source per entry - so a flood here starves the others.
         *
         * It measurably did. Posting on the step interval put roughly
         * 29,000 resumes on that line in a run, and AX's frame callback then
         * ran 45 times against 18,686 audio-DMA interrupts: 0.2% of them.
         * The AI callback is what asserts the task, so answering only a
         * genuine assert paces this at exactly the rate AX asks for.
         *
         * PACED TO THE AUDIO DMA, which is the rate AX actually works at.
         *
         * The first attempt gated on guest->DSP mails, reasoning that
         * DSPAssertTask sends one. It does not, here: a whole run sends TWO
         * mails to the DSP, and gating on them cut AX's frames from 45 to 3.
         *
         * The audio DMA is the right clock. On hardware `__AXOutAiCallback`
         * runs on each AID interrupt, asserts the task, and the DSP answers
         * with a resume - so one resume per AID is exactly one AX frame, and
         * our engine already raises AID every 20 blocks, which is 5 ms of
         * guest time (F187). That is the period AX is written around.
         */
        int ax_frame_due = 0;
        if (resume_mail && phase != 0) {
            uint64_t aid = mgs_interrupt_aid_raised();

            /* AND ONLY WHILE A TASK ACTUALLY EXISTS.
             *
             * `__DSP_curr_task` is the SDK's pointer to the running task,
             * and its handler dereferences it the moment a mail arrives:
             * `lwz r0, 0x8(r5)` then, for a resume, `stw r0, 0x0(r5)`. The
             * assertion that would have caught a null is compiled out of a
             * release build, so a mail posted when no task is current writes
             * 1 to guest address ZERO - into the OS's low memory.
             *
             * It does exactly that. With resumes posted on the audio-DMA
             * clock alone, a run ends with the guest thread list destroyed:
             * one entry, priority 1081872, and a link into nothing, against
             * nine clean threads by default. This runtime's own comment a
             * few lines up warned about the same null for the INIT mail;
             * the resume needed the same care and did not have it.
             */
            if (!mgs_module_guest_read32(cpu, GUEST_DSP_CURR_TASK)) {
                ++s_dsp_no_task; return 0;
            }
            /* A COUNT OF FRAMES PRODUCED, NOT THE LAST INTERRUPT SEEN.
             *
             * One AX frame per audio-DMA interrupt. Comparing against the
             * latest `aid` means that whenever a frame is missed - the
             * resume refused, the guest busy - the arrears are FORGIVEN:
             * the next success sets the mark to wherever `aid` has reached
             * and the skipped blocks are never mixed.
             *
             * Measured, they add up. The DMA delivered 35,102 interrupts
             * and the mixer produced 31,965 frames: 8.9% of the audio was
             * simply never made, which is 5 ms lost every time and a
             * soundtrack that falls further behind the longer it plays -
             * exactly the "voices get slower, with popping" reported.
             *
             * Counting instead lets it catch up: if two interrupts pass
             * while one frame could not be delivered, the next two offers
             * each produce one. */
            if (seen_sends >= aid) { ++s_dsp_no_frame; return 0; }

            /* THE CREDIT IS SPENT WHEN THE RESUME IS DELIVERED, NOT WHEN IT
             * IS OFFERED - and this used to spend it here.
             *
             * One resume is one AX frame, gated on one per audio-DMA
             * interrupt. Consuming `seen_sends` before the raise meant that
             * a resume the guest could not take - interrupts masked, which
             * is ordinary and frequent - was gone for good: the next offer
             * saw `aid == seen_sends` and declined, so that audio block
             * never got its frame. The mail was withdrawn and the block
             * silently skipped.
             *
             * How much that costs depends on how much of its time the guest
             * spends with MSR[EE] clear, which is why it looked like a tick
             * rate problem. At rate 7, 4,079 of 219,546 offers went
             * undelivered and the sound ran; at rate 4, 21,052 did and it
             * collapsed - 550 calls to `__AXServiceVPB` against 57,286,
             * because `__AXOutDspReady` is set only by a resume that
             * actually arrives (F281).
             *
             * So the credit is spent, and the frame mixed, only after the
             * raise succeeds. A refused offer changes nothing and is simply
             * made again. */
            ax_frame_due = 1;
        }

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
            ++s_dsp_undelivered;
            return 0;
        }
        /* Delivered. NOW the audio block is spoken for, and now the DSP
         * has mixed it - the guest is about to enter its handler and read
         * the voice positions this moves. */
        if (ax_frame_due) {
            static int on = -1;
            if (on < 0) {
                const char* e = getenv("MGS_AX_MODEL");
                on = !(e && e[0] == '0');
            }
            ++seen_sends;            /* one frame produced, arrears kept */
            if (on) mgs_ax_dsp_frame(cpu);
        }
        if (mail == 0xDCD10000u) { phase = 1; return 1; }
        /* A resume leaves the task alive, so the cycle stays in phase 1 and
         * keeps resuming. A `done` unlinks it, and the old behaviour went
         * back to phase 0 to post another init. */
        if (!resume_mail) phase = 0;
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

/* (s_pe_seen and s_pe_sent are declared above and shared with the trace
 * there - deliberately the same counters, and declaring them twice made them
 * the same object by accident rather than by intent. See
 * tools/check-duplicate-statics.py.) */

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
