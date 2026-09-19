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
static uint64_t s_pe_seen, s_pe_sent;
uint64_t mgs_interrupt_pe_seen(void) { return s_pe_seen; }
uint64_t mgs_interrupt_pe_sent(void) { return s_pe_sent; }

int mgs_interrupt_pe_finish(const MgsModule* mod, void* cpu)
{
    if (!mgs_mmio_take_draw_done(mgs_host_mmio()))
        return 0;
    ++s_pe_seen;
    if (mgs_interrupt_raise(mod, cpu, PI_CAUSE_PE_FINISH)) {
        ++s_pe_sent;
        if (getenv("MGS_TRACE_PE"))
            fprintf(stderr, "[pe] finish delivered #%llu\n",
                    (unsigned long long)s_pe_sent);
        return 1;
    }
    /* Not delivered - the guest has interrupts off, or has not armed PE yet.
     * Put the token back so it is offered again rather than dropped. */
    mgs_host_mmio()->draw_done_tokens++;
    return 0;
}
