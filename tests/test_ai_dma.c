/* Does the audio DMA ask for the next buffer, and at the right rate?
 *
 * This engine is what paces sound. The SDK's `AIInitDMA` programmes an
 * address and a block count, `AIStartDMA` sets the enable bit, and from then
 * on the hardware streams 32-byte blocks into the sample-rate converter and
 * raises AID - bit 3 of the control register - each time it latches a new
 * transfer. `__AIDHandler` acknowledges that and calls whatever was given to
 * `AIRegisterDMACallback`, which is how the audio manager is told to produce
 * more sound.
 *
 * None of it existed before: the registers were plain storage and AID was
 * raised by nothing. A game that streams does not then run slowly, it stops
 * - one buffer submitted, no second request, and no reason to read the disc
 * again.
 *
 * Driven here rather than in a boot for the reason test_dsp_init.c gives: a
 * guest loop that never ends is not a spin in this runtime, it is a
 * segfault, and that says nothing about which wait failed.
 *
 * Register numbers are the SDK's own __DSPRegs indices - 16-bit words from
 * 0xCC005000. [24] and [25] are the DMA address, [27] the control and block
 * count, [29] the blocks remaining, [5] the control and status register.
 */
#include "platform/mmio.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define DSP(i)     (MMIO_DSP + (i) * 2u)
#define CSR        DSP(5)
#define DMA_HI     DSP(24)
#define DMA_LO     DSP(25)
#define DMA_CTL    DSP(27)
#define DMA_LEFT   DSP(29)

#define AID        0x0008u
#define ENABLE     0x8000u
#define TICKS_PER_BLOCK 10125u

static void check(int cond, const char* what)
{
    if (!cond) { printf("FAIL %s: %s\n", __FILE__, what); ++failures; }
}

static void check_eq(unsigned long got, unsigned long want, const char* what)
{
    if (got != want) {
        printf("FAIL %s: %s: got %lu, want %lu\n", __FILE__, what, got, want);
        ++failures;
    }
}

/* What AIInitDMA does, in the SDK's own register writes. */
static void init_dma(MgsMmio* m, uint32_t addr, uint32_t blocks)
{
    mgs_mmio_write(m, DMA_HI, (addr >> 16) & 0x03FFu, 2u);
    mgs_mmio_write(m, DMA_LO, addr & 0xFFE0u, 2u);
    mgs_mmio_write(m, DMA_CTL,
                   (mgs_mmio_read(m, DMA_CTL, 2u) & 0x8000u) | (blocks & 0x7FFFu),
                   2u);
}

static void start_dma(MgsMmio* m)
{
    mgs_mmio_write(m, DMA_CTL, mgs_mmio_read(m, DMA_CTL, 2u) | ENABLE, 2u);
}

/* What __AIDHandler does to acknowledge: write the bit back. */
static void ack_aid(MgsMmio* m)
{
    uint32_t tmp = mgs_mmio_read(m, CSR, 2u);
    mgs_mmio_write(m, CSR, (tmp & ~0x00A0u) | AID, 2u);
}

/* Take every completion the engine has queued, the way the run loop does. */
static int drain(MgsMmio* m)
{
    int n = 0;
    while (mgs_mmio_take_aid_irq(m)) { mgs_mmio_dsp_assert_aid(m); ++n; }
    return n;
}

int main(void)
{
    MgsMmio m;
    int n;
    unsigned i;

    mgs_mmio_init(&m);

    /* 1. ENABLING ANNOUNCES ITSELF.
     *
     * The interrupt fires when the FIFO starts a transfer, not when it
     * finishes - that is what lets the handler point the registers at the
     * next buffer while the current one is still draining. */
    init_dma(&m, 0x80300000u, 4u);
    check_eq((unsigned long)drain(&m), 0ul, "programming alone starts nothing");
    start_dma(&m);
    check_eq((unsigned long)drain(&m), 1ul, "enabling announces the transfer");
    check((mgs_mmio_read(&m, CSR, 2u) & AID) != 0u, "AID is set in the CSR");
    ack_aid(&m);
    check((mgs_mmio_read(&m, CSR, 2u) & AID) == 0u, "acknowledging clears AID");

    /* 2. BLOCKS REMAINING READS ONE LOWER THAN THE TRUTH.
     *
     * The register is zero-based. Reported honestly, code that waits for it
     * to reach zero waits for ever on the last block. */
    check_eq(mgs_mmio_read(&m, DMA_LEFT, 2u) & 0x7FFFu, 3ul,
             "four blocks left reads as three");

    /* 3. IT DRAINS ON THE GUEST'S CLOCK, NOT AS FAST AS IT CAN.
     *
     * Three blocks of the four, one tick short of the fourth: nothing new
     * should have been latched. */
    for (i = 0; i < 3u; ++i) mgs_mmio_advance_ticks(&m, TICKS_PER_BLOCK);
    mgs_mmio_advance_ticks(&m, TICKS_PER_BLOCK - 1u);
    check_eq((unsigned long)drain(&m), 0ul, "no completion before the last block");
    check_eq(mgs_mmio_read(&m, DMA_LEFT, 2u) & 0x7FFFu, 0ul, "one block to go");

    /* 4. AND THEN IT RELATCHES AND ASKS AGAIN. */
    mgs_mmio_advance_ticks(&m, 1u);
    check_eq((unsigned long)drain(&m), 1ul, "the last block completes the transfer");
    ack_aid(&m);
    check_eq(mgs_mmio_read(&m, DMA_LEFT, 2u) & 0x7FFFu, 3ul, "relatched to four");

    /* 5. A BUFFER QUEUED MID-FLIGHT DOES NOT RESTART THE CURRENT ONE.
     *
     * `AIInitDMA` is called from inside the completion callback with the
     * enable bit still set. If that restarted the transfer the stream would
     * stutter on every buffer; on hardware the new values are simply what
     * the next relatch picks up. */
    init_dma(&m, 0x80400000u, 2u);
    check_eq((unsigned long)drain(&m), 0ul, "queueing mid-flight announces nothing");
    check_eq(mgs_mmio_read(&m, DMA_LEFT, 2u) & 0x7FFFu, 3ul, "and does not restart");
    for (i = 0; i < 4u; ++i) mgs_mmio_advance_ticks(&m, TICKS_PER_BLOCK);
    check_eq((unsigned long)drain(&m), 1ul, "the old transfer finishes");
    ack_aid(&m);
    check_eq(mgs_mmio_read(&m, DMA_LEFT, 2u) & 0x7FFFu, 1ul, "then the new one runs");

    /* 6. THE RATE IS THE ONE SOUND ACTUALLY PLAYS AT.
     *
     * 32-byte blocks of stereo 16-bit at 32 kHz is 4,000 blocks a second, so
     * a second of guest time is 4,000 completions of a one-block transfer.
     * This is the number that decides whether a movie's audio lasts as long
     * as its video; getting it wrong is not a stutter, it is a desync that
     * grows. */
    init_dma(&m, 0x80500000u, 1u);
    for (i = 0; i < 8u; ++i) mgs_mmio_advance_ticks(&m, TICKS_PER_BLOCK);
    drain(&m);
    {
        uint64_t before = mgs_mmio_aid_blocks(&m);
        mgs_mmio_advance_ticks(&m, 40500000u);     /* one second of guest time */
        n = drain(&m);
        check_eq((unsigned long)(mgs_mmio_aid_blocks(&m) - before), 4000ul,
                 "4,000 blocks in a second of guest time");
        check_eq((unsigned long)n, 4000ul, "and a completion for each");
    }

    /* 7. STOPPING STOPS IT.
     *
     * `AIStopDMA` clears the enable bit, and nothing should be asked for
     * after that - a completion delivered to a stopped stream is a callback
     * run against a buffer nobody owns. */
    mgs_mmio_write(&m, DMA_CTL, mgs_mmio_read(&m, DMA_CTL, 2u) & ~ENABLE, 2u);
    mgs_mmio_advance_ticks(&m, 40500000u);
    check_eq((unsigned long)drain(&m), 0ul, "a stopped engine asks for nothing");

    /* 8. AND STARTING AGAIN IS A NEW TRANSFER. */
    init_dma(&m, 0x80600000u, 3u);
    start_dma(&m);
    check_eq((unsigned long)drain(&m), 1ul, "restarting announces itself");

    printf("ai_dma: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
