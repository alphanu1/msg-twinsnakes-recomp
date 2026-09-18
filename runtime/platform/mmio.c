#include "mmio.h"
#include <string.h>

/* Video interface, by offset from MMIO_VI. Only the ones the SDK polls are
 * modelled; the rest read back what was written.
 */
#define VI_VTR         0x00u   /* vertical timing */
#define VI_DISP_CFG    0x02u   /* display configuration; bit 0 = enable */
#define VI_HALF_LINE   0x2Cu   /* current half-line: what VIWaitForRetrace polls */
#define VI_DI0         0x30u   /* display interrupt 0 */

/* The GameCube's video output is 525 half-lines per field at 60 Hz, and the
 * SDK waits for the counter to cross a threshold. The exact number matters
 * less than that it wraps at a sane rate; this is NTSC's.
 */
#define VI_HALF_LINES_PER_FIELD 525u

/* External interface, by offset from MMIO_EXI. Three channels, 0x14 apart.
 *
 * EXIxCR bit 0 is TSTART: software sets it to begin a transfer and HARDWARE
 * CLEARS IT when the transfer finishes. EXISync polls exactly that bit. A
 * register that merely remembers what was written therefore never completes a
 * transfer, and the SDK spins forever in EXISync.
 *
 * Completing instantly is the honest model, not a shortcut: there is no
 * physical device on the other end and no bus latency to reproduce. The same
 * reasoning applies to the disc and serial interfaces, which use the same
 * set-a-bit-and-wait shape.
 */
#define EXI_CHANNEL_STRIDE 0x14u
#define EXI_CR             0x0Cu   /* control: bit 0 = TSTART */
#define EXI_TSTART         0x01u

/* Serial interface: SICOMCSR bit 0 is likewise a transfer-start the hardware
 * clears. */
#define SI_COMCSR          0x34u

/* DSP, by offset from MMIO_DSP. */
#define DSP_MAILBOX_HI     0x00u
#define DSP_MAILBOX_LO     0x02u
#define DSP_CPU_MBOX_HI    0x04u
#define DSP_CPU_MBOX_LO    0x06u
#define DSP_CONTROL        0x0Au
/* DSPCR bit 0 is RES: software sets it to reset the DSP and hardware clears
 * it when the reset completes. Bit 2 is HALT. Same set-and-wait shape as EXI,
 * and __OSInitAudioSystem polls it immediately after setting it. */
#define DSP_CR_RESET       0x0001u
/* Bit 5 is ARINT, the ARAM DMA completion flag. __OSInitAudioSystem kicks a
 * DMA by writing the three AR registers below and then spins until this sets.
 * Nothing sets it unless the host does, because there is no DSP.
 *
 * ARAM itself is real in this runtime - a separate 16 MB store - so a DMA
 * here is a memcpy that has already happened by the time the guest looks.
 * Setting the flag on the write that starts the transfer is therefore
 * accurate rather than optimistic. */
#define DSP_CR_ARINT       0x0020u
#define AR_DMA_MMADDR      0x20u   /* main memory address */
#define AR_DMA_ARADDR      0x24u   /* ARAM address */
#define AR_DMA_CNT         0x28u   /* length, and writing it starts the DMA */

void mgs_mmio_init(MgsMmio* m)
{
    memset(m, 0, sizeof *m);

    /* Values hardware presents after reset, so the SDK's first reads see what
     * it expects rather than zeros it has to interpret. */
    m->regs[(MMIO_VI - MMIO_BASE) + VI_DISP_CFG + 1u] = 0x01u;  /* display enabled */
}

static uint8_t* at(MgsMmio* m, uint32_t addr)
{
    if (addr < MMIO_BASE || addr >= MMIO_END) return NULL;
    return m->regs + (addr - MMIO_BASE);
}

uint32_t mgs_mmio_read(MgsMmio* m, uint32_t addr, unsigned size)
{
    uint8_t* p = at(m, addr);
    uint32_t v = 0u;
    unsigned i;

    ++m->reads;
    if (!p) return 0u;

    /* The half-line counter is the one register that must MOVE. The SDK's
     * retrace wait reads it until it passes a threshold, so a constant - any
     * constant, including a plausible one - spins forever. */
    if (addr >= MMIO_VI + VI_HALF_LINE && addr < MMIO_VI + VI_HALF_LINE + 4u) {
        uint8_t* hl = at(m, MMIO_VI + VI_HALF_LINE);
        hl[0] = (uint8_t)(m->vi_half_line >> 8);
        hl[1] = (uint8_t)m->vi_half_line;
    }

    for (i = 0; i < size; ++i) v = (v << 8) | p[i];   /* big-endian, as the bus is */
    return v;
}

void mgs_mmio_write(MgsMmio* m, uint32_t addr, uint32_t value, unsigned size)
{
    uint8_t* p;
    unsigned i;

    ++m->writes;

    /* The write-gather pipe is not a register: every write to it is a byte of
     * GX command stream, and the address does not advance. Counting is enough
     * until phase 3 has something to parse it with. */
    if (addr >= MMIO_WGPIPE && addr < MMIO_WGPIPE + 0x20u) {
        m->wgpipe_bytes += size;
        return;
    }

    p = at(m, addr);
    if (!p) return;
    for (i = 0; i < size; ++i)
        p[i] = (uint8_t)(value >> (8u * (size - 1u - i)));

    /* Transfers that hardware would complete asynchronously complete here
     * immediately: clear the start bit the guest just set, so the poll that
     * follows sees a finished transfer rather than spinning forever. */
    {
        uint32_t off = addr - MMIO_BASE;
        uint32_t exi = MMIO_EXI - MMIO_BASE;
        if (off >= exi && off < exi + 3u * EXI_CHANNEL_STRIDE) {
            uint32_t within = (off - exi) % EXI_CHANNEL_STRIDE;
            if (within == EXI_CR && (value & EXI_TSTART))
                m->regs[off + size - 1u] &= (uint8_t)~EXI_TSTART;
        }

        /* DSP reset: self-clearing, like EXI's transfer start. */
        if (addr == MMIO_DSP + DSP_CONTROL && (value & DSP_CR_RESET))
            m->regs[off + size - 1u] &= (uint8_t)~DSP_CR_RESET;

        /* Starting an ARAM DMA completes it: raise the completion flag the
         * guest is about to poll for. Writing the count register is what
         * starts a transfer on hardware. */
        if (addr == MMIO_DSP + AR_DMA_CNT) {
            uint8_t* cr = at(m, MMIO_DSP + DSP_CONTROL);
            if (cr) cr[1] |= (uint8_t)DSP_CR_ARINT;
        }

        /* Serial transfer start: the same shape again. */
        if (addr == MMIO_SI + SI_COMCSR && (value & 1u))
            m->regs[off + size - 1u] &= (uint8_t)~1u;
    }
}

void mgs_mmio_tick_frame(MgsMmio* m)
{
    /* Advance a whole field per frame. Driving this from the frame loop
     * rather than from reads is deliberate: a guest that polls the beam
     * position must see time pass at the rate the host is actually running,
     * not as fast as it can spin. */
    m->vi_half_line = (uint16_t)((m->vi_half_line + VI_HALF_LINES_PER_FIELD)
                                 % VI_HALF_LINES_PER_FIELD);
    if (m->vi_half_line == 0u) m->vi_half_line = 1u;
}
