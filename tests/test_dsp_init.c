/* Can the audio system's start-up sequence complete against our hardware?
 *
 * `__OSInitAudioSystem` is stubbed out, and every attempt to withdraw that
 * stub has ended in a segfault after a very long boot - which says nothing
 * about WHICH of its waits never ends, because a guest loop that never exits
 * is not a spin in this runtime, it is 200,000 host stack frames (F184).
 *
 * So the sequence is driven here directly instead. The SDK sets a register
 * and then spins on a bit; each of those spins is one case below, bounded,
 * and named. A failure says exactly which wait our model never satisfies,
 * in a second, with no game and no module.
 *
 * The register numbers are the SDK's own __DSPRegs indices, which are 16-bit
 * words from 0xCC005000: [5] is the control and status register, [2] and [3]
 * the mailbox from the DSP, [16] [18] [20] the ARAM DMA address, source and
 * count.
 */
#include "platform/mmio.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define DSP(i)  (MMIO_DSP + (i) * 2u)
#define CSR     DSP(5)

/* Spin the way the SDK spins, but give up rather than hang. Returns the
 * number of reads it took, or -1 if the condition never came true. */
static long spin_until(MgsMmio* m, uint32_t addr, unsigned size,
                       uint32_t mask, int want_set, const char* what)
{
    long i;
    for (i = 0; i < 100000; ++i) {
        uint32_t v = mgs_mmio_read(m, addr, size);
        if (want_set ? (v & mask) : !(v & mask)) return i;
    }
    printf("FAIL %s:%d: %s never came true (0x%04X after %ld reads)\n",
           __FILE__, __LINE__, what,
           (unsigned)mgs_mmio_read(m, addr, size), i);
    ++failures;
    return -1;
}

static void aram_dma(MgsMmio* m)
{
    mgs_mmio_write(m, DSP(16), 0x01000000u, 4u);   /* main-memory address */
    mgs_mmio_write(m, DSP(18), 0x00000000u, 4u);   /* ARAM address        */
    mgs_mmio_write(m, DSP(20), 0x00000020u, 4u);   /* count starts it     */
}

int main(void)
{
    static MgsMmio m;
    uint32_t csr;

    mgs_mmio_init(&m);

    /* 1. Reset the coprocessor. Hardware clears the bit when it is done. */
    mgs_mmio_write(&m, CSR, 0x08ACu, 2u);
    mgs_mmio_write(&m, CSR, 0x08ACu | 1u, 2u);
    spin_until(&m, CSR, 2u, 0x0001u, 0, "reset bit clears");

    /* 2. No mail may be waiting from a DSP that has not run yet. */
    mgs_mmio_write(&m, DSP(0), 0u, 2u);
    spin_until(&m, DSP(2), 2u, 0x8000u, 0, "mailbox starts empty");

    /* 3. The first ARAM transfer, and the completion flag it raises. */
    aram_dma(&m);
    spin_until(&m, CSR, 2u, 0x0020u, 1, "ARAM transfer raises its flag");
    csr = mgs_mmio_read(&m, CSR, 2u);
    mgs_mmio_write(&m, CSR, csr, 2u);              /* write-one-to-clear */

    /* 4. The second transfer, after the SDK's timed delay. */
    aram_dma(&m);
    spin_until(&m, CSR, 2u, 0x0020u, 1, "second ARAM transfer raises its flag");
    csr = mgs_mmio_read(&m, CSR, 2u);
    mgs_mmio_write(&m, CSR, csr, 2u);

    /* 5. "Still running" must go clear. Setting it on COMPLETION was a
     *    contradiction that spun here for ever (HANDOFF F177). */
    csr = mgs_mmio_read(&m, CSR, 2u) & ~0x0800u;
    mgs_mmio_write(&m, CSR, csr, 2u);
    spin_until(&m, CSR, 2u, 0x0400u, 0, "ARAM DMA reports itself finished");

    /* 6. Let the coprocessor run, and wait for it to report in. */
    csr = mgs_mmio_read(&m, CSR, 2u) & ~0x0004u;
    mgs_mmio_write(&m, CSR, csr, 2u);
    if (spin_until(&m, DSP(2), 2u, 0x8000u, 1, "the DSP sends its boot mail") >= 0) {
        uint32_t hi = mgs_mmio_read(&m, DSP(2), 2u);
        uint32_t lo = mgs_mmio_read(&m, DSP(3), 2u);
        uint32_t mail = (hi << 16) | lo;
        /* THE VALUE IS NOT CHECKED, THE ARRIVAL IS.
         *
         * The SDK does compare this mail arithmetically, but the body of that
         * comparison is empty - it acts on nothing. What it genuinely waits
         * for is the top bit, which says a message is there at all. The real
         * coprocessor's boot ROM posts 0x8071FEED and `__DSP_boot_task`
         * asserts on exactly that, so that is what our model posts. */
        if (mail != 0x8071FEEDu) {
            printf("FAIL %s:%d: boot mail is 0x%08X, the DSP boot ROM posts "
                   "0x8071FEED\n", __FILE__, __LINE__, mail);
            ++failures;
        }
    }

    /* 7. Reset again to finish. */
    mgs_mmio_write(&m, CSR, 0x08ACu, 2u);
    mgs_mmio_write(&m, CSR, 0x08ACu | 1u, 2u);
    spin_until(&m, CSR, 2u, 0x0001u, 0, "final reset bit clears");

    printf(failures ? "dsp_init: FAILED\n" : "dsp_init: ok\n");
    return failures ? 1 : 0;
}
