#include "mmio.h"

#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

/* Command processor FIFO registers, by byte offset from MMIO_CP. The SDK
 * addresses them as halfword indices, so GX_GET_CP_REG(24) is 0x30. */
#define CP_STATUS        0x00u
#define CP_STATUS_RD_IDLE  0x0004u
#define CP_STATUS_CMD_IDLE 0x0008u
#define CP_RW_DISTANCE   0x30u   /* bytes written but not yet consumed */
#define CP_WRITE_PTR     0x34u
#define CP_READ_PTR      0x38u


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
#define EXI_CSR            0x00u   /* status: chip select, and bit 12 EXT */
#define EXI_MAR            0x04u   /* DMA address */
#define EXI_LENGTH         0x08u   /* DMA length */
#define EXI_CR             0x0Cu   /* control: bit 0 = TSTART */
#define EXI_DATA           0x10u   /* immediate transfer data */
#define EXI_TSTART         0x01u
#define EXI_DMA            0x02u
#define EXI_EXT            0x1000u /* a device is present in this slot */
#define EXI_TCINT          0x08u   /* a transfer finished */
#define EXI_TCINTMSK       0x04u   /* ...and the guest wants to hear about it */
#define PI_EXI             (1u << 4)

/* CLEARING THE START BIT IS NOT HOW A TRANSFER FINISHES.
 *
 * The same lesson the serial interface already cost us (F153): hardware also
 * raises a transfer-complete interrupt, and a guest that waits for one rather
 * than polling waits forever without it. The memory card's mount is exactly
 * that kind of guest - it sets mountStep to 1 and then advances from the EXI
 * interrupt, so with no interrupt the mount stops dead after the status read
 * and eventually reports an I/O error. That is the shape this presented as:
 * nine transfers in a whole boot and then silence.
 *
 * TCINT is kept beside the register rather than in it, because the guest
 * clears it by writing a one and a plain register store cannot tell that from
 * any other write.
 */
static uint32_t exi_reg(const MgsMmio* m, uint32_t off)
{
    const uint8_t* p = &m->regs[off];
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void exi_set_reg(MgsMmio* m, uint32_t off, uint32_t v)
{
    uint8_t* p = &m->regs[off];
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* ONE TRANSFER, RUN TO COMPLETION.
 *
 * There is no bus on the other side, so the transfer finishes within the
 * store that started it - the same model the disc and serial interfaces use.
 * Immediate transfers carry up to four bytes in EXIxDATA, most significant
 * first; DMA transfers move EXIxLENGTH bytes through guest memory at EXIxMAR.
 * The direction is in the control register: 0 reads from the device, 1 writes
 * to it.
 */
/* One byte to whichever device chip select names. Channel 0 carries the
 * memory card on select 0 and the clock and settings on select 1; select 2
 * is the AD16 debug device, which nothing here provides. */
static void exi_byte(MgsMmio* m, uint8_t* b)
{
    if ((m->exi_cs & 1u) && m->card_ready) mgs_exi_card_byte(&m->card, b);
    else if (m->exi_cs & 2u)               mgs_exi_ipl_byte(&m->ipl, b);
}

static int exi_has_device(const MgsMmio* m)
{
    return ((m->exi_cs & 1u) && m->card_ready) || (m->exi_cs & 2u) != 0u;
}

static void exi_transfer(MgsMmio* m, unsigned chan, uint32_t cr)
{
    uint32_t base = (MMIO_EXI - MMIO_BASE) + chan * EXI_CHANNEL_STRIDE;
    unsigned rw   = (cr >> 2) & 3u;
    unsigned tlen = ((cr >> 4) & 3u) + 1u;
    unsigned i;

    ++m->exi_transfers;
    /* Only slot A carries a card, and only while it is the selected device. */
    if (chan != 0u || !exi_has_device(m)) {
        if (m->trace_exi && m->exi_traced < 400u) {
            ++m->exi_traced;
            fprintf(stderr, "[exi] DROPPED: chan %u cs %u rw %u len %u "
                            "(no device there)\n", chan, m->exi_cs, rw, tlen);
        }
        return;
    }
    ++m->exi_to_card;

    if (cr & EXI_DMA) {
        uint32_t mar = exi_reg(m, base + EXI_MAR) & 0x03FFFFFFu;
        uint32_t len = exi_reg(m, base + EXI_LENGTH);
        if (m->trace_exi && m->exi_traced < 400u) {
            ++m->exi_traced;
            fprintf(stderr, "[exi] dma rw=%u len=%u -> 0x%08X (cmd 0x%02X)\n",
                    rw, len, mar, m->card.command);
        }
        for (i = 0u; i < len; ++i) {
            uint8_t b = 0xFFu;
            uint32_t a = 0x80000000u | (mar + i);
            if (rw == 1u && m->exi_mem) b = guest_read8(m->exi_mem, a);
            exi_byte(m, &b);
            if (rw == 0u && m->exi_mem) guest_write8(m->exi_mem, a, b);
        }
        return;
    }

    {
        uint32_t data = exi_reg(m, base + EXI_DATA);
        uint32_t out  = 0u;
        for (i = 0u; i < tlen; ++i) {
            unsigned sh = 24u - i * 8u;
            uint8_t  b  = (rw == 0u) ? 0xFFu : (uint8_t)(data >> sh);
            exi_byte(m, &b);
            out |= (uint32_t)b << sh;
        }
        if (rw != 1u) exi_set_reg(m, base + EXI_DATA, out);
        if (m->trace_exi && m->exi_traced < 400u) {
            ++m->exi_traced;
            fprintf(stderr, "[exi] imm rw=%u len=%u  in 0x%08X -> out 0x%08X  "
                            "(pos now %u, cmd 0x%02X)\n",
                    rw, tlen, data, out, m->card.position, m->card.command);
        }
    }
}

/* Serial interface: SICOMCSR bit 0 is likewise a transfer-start the hardware
 * clears. */
#define SI_POLL            0x30u
#define SI_COMCSR          0x34u
#define SI_STATUS          0x38u
#define SI_IOBUF           0x80u   /* the 128-byte transfer buffer */

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
/* And 0x0400, which __OSInitAudioSystem polls immediately after ARINT on the
 * same DMA. On hardware these are separate completion flags for the same
 * transfer; here the transfer is a memcpy that has already happened, so both
 * are raised together. Raising only one leaves the second wait spinning, which
 * is how this presented. */
#define DSP_CR_ARDMA_DONE  0x0400u
#define AR_DMA_MMADDR      0x20u   /* main memory address */
#define AR_DMA_ARADDR      0x24u   /* ARAM address */
#define AR_DMA_CNT         0x28u   /* length, and writing it starts the DMA */

void mgs_mmio_init(MgsMmio* m)
{

    memset(m, 0, sizeof *m);

    /* Values hardware presents after reset, so the SDK's first reads see what
     * it expects rather than zeros it has to interpret. */
    m->regs[(MMIO_VI - MMIO_BASE) + VI_DISP_CFG + 1u] = 0x01u;  /* display enabled */

    /* ARAM IS READY. `__ARChecksize` opens with
     *   do {} while(!(__DSPRegs[11] & 1));
     * which is a halfword read of 0xCC005016 testing bit 0, and with the
     * register reading back zero the boot stops there for good - the last
     * thing before it is the graphics work, so it looks like a rendering
     * problem. The bit says the memory has finished coming up, which for a
     * buffer we allocated is true before the guest asks. */
    m->regs[(MMIO_DSP - MMIO_BASE) + 0x17u] = 0x01u;

    /* See the FIFO-register trace in mgs_mmio_write. */
    m->trace_fiforeg = getenv("MGS_TRACE_FIFOREG") != NULL;
    m->trace_pi      = getenv("MGS_TRACE_PI") != NULL;
    {   /* MGS_TRACE_SI=<n> logs n serial accesses; bare =1 keeps 200. */
        const char* e = getenv("MGS_TRACE_SI");
        const char* b = getenv("MGS_PAD_BUTTONS");

        m->trace_si = e != NULL;
        m->trace_vi = getenv("MGS_TRACE_VI") != NULL;
        m->trace_exi = getenv("MGS_TRACE_EXI") != NULL;
        m->si_trace_cap = 200u;
        if (e && *e) {
            unsigned long n = strtoul(e, NULL, 0);
            if (n > 1ul) m->si_trace_cap = (unsigned)n;
        }

        /* MGS_PAD_BUTTONS=<hex> holds a button down, to tell a guest that is
         * WAITING for input from one that is stalled. 0x1000 is Start. */
        m->pad_forced  = b ? (uint16_t)strtoul(b, NULL, 0) : 0u;
        m->pad_buttons = m->pad_forced;

        /* MGS_PAD_SCRIPT="frame:hex,frame:hex,..." holds each button word
         * from that frame onward, so a run can drive itself through a menu
         * without a human at the keyboard. Reaching the video needs Down
         * then A, which a single held word cannot express. */
        {
            const char* q = getenv("MGS_PAD_SCRIPT");
            m->pad_script_n = 0u;
            while (q && *q && m->pad_script_n < 16u) {
                char* end;
                unsigned long fr = strtoul(q, &end, 0);
                if (end == q || *end != ':') break;
                q = end + 1;
                m->pad_script_frame[m->pad_script_n] = (uint32_t)fr;
                m->pad_script_btn[m->pad_script_n] =
                    (uint16_t)strtoul(q, &end, 16);
                ++m->pad_script_n;
                q = (*end == ',') ? end + 1 : end;
            }
        }
    }

}

static uint8_t* at(MgsMmio* m, uint32_t addr)
{
    if (addr < MMIO_BASE || addr >= MMIO_END) return NULL;
    return m->regs + (addr - MMIO_BASE);
}

static void si_consume_read(MgsMmio* m, uint32_t addr);

uint32_t mgs_mmio_read(MgsMmio* m, uint32_t addr, unsigned size)
{
    if (addr >= MMIO_BASE && addr < MMIO_END)
        ++m->read_hist[(addr - MMIO_BASE) / 2u];
    uint8_t* p = at(m, addr);
    uint32_t v = 0u;
    unsigned i;

    ++m->reads;
    if (!p) return 0u;

    /* THE SERIAL INTERFACE IS A STUB AND THE CONTROLLER NEVER APPEARS.
     * Log the access pattern before modelling it: what the translated PAD
     * code reads, in what order, is the specification. Writes are logged in
     * mgs_mmio_write. Capped so a polling loop cannot fill the disc. */
    /* Reading a channel's input buffer consumes that poll; see
     * si_consume_read. */
    si_consume_read(m, addr);

    if (m->trace_si && addr >= MMIO_SI && addr < MMIO_EXI &&
        m->si_traced < m->si_trace_cap) {
        ++m->si_traced;
        fprintf(stderr, "[si] read  0x%08X size %u\n", addr, size);
    }

    /* DSPCR's completion flags are NO LONGER FORCED SET ON READ.
     *
     * They used to be, because nothing raised them: ARAM was not modelled and
     * the SDK's audio init waits on flags that hardware would set. ARAM is
     * real now and its DMA raises them where the transfer happens, which is
     * both accurate and enough.
     *
     * Forcing them had become actively harmful. These are the *status* bits
     * the operating system's dispatcher reads to decide WHICH of the DSP
     * line's three sources fired - audio-interface DMA, ARAM, or the DSP
     * itself. Held permanently set, the ARAM source always looked pending,
     * so the dispatcher never reached the DSP handler and a task's mail was
     * posted, delivered, and never read.
     */

    /* READING THE LOW HALF OF THE DSP's MAILBOX EMPTIES IT.
     *
     * `DSPReadMailFromDSP` reads both halves and the hardware clears the
     * "mail waiting" bit as it does, so the next `DSPCheckMailFromDSP`
     * returns zero. Leaving it set makes the same message readable forever,
     * and the SDK's boot handshake is a sequence of distinct messages - it
     * would take the first one for all of them. */
    if (addr >= MMIO_DSP + DSP_MAIL_FROM_LO &&
        addr < MMIO_DSP + DSP_MAIL_FROM_LO + 2u) {
        uint8_t* hi = at(m, MMIO_DSP + DSP_MAIL_FROM_HI);
        /* WHAT THE GUEST ACTUALLY RECEIVES, logged where it cannot be
         * missed. A trace on the reading function compares a pc the run loop
         * may never sample; this is the byte-level truth. */
        if (m->trace_fiforeg)
            fprintf(stderr, "[dsp] guest reads mail 0x%02X%02X%02X%02X\n",
                    hi[0], hi[1], hi[2], hi[3]);
        hi[0] = (uint8_t)(hi[0] & 0x7Fu);
        /* Reading a message means the guest is past the point of having a
         * current task, and resets the count of sends since. */
        m->dsp_booted = 1;
        m->dsp_mails_sent = 0u;
    }

    /* The half-line counter is the one register that must MOVE. The SDK's
     * retrace wait reads it until it passes a threshold, so a constant - any
     * constant, including a plausible one - spins forever. */
    if (addr >= MMIO_VI + VI_HALF_LINE && addr < MMIO_VI + VI_HALF_LINE + 4u) {
        uint8_t* hl = at(m, MMIO_VI + VI_HALF_LINE);
        hl[0] = (uint8_t)(m->vi_half_line >> 8);
        hl[1] = (uint8_t)m->vi_half_line;
    }

    /* The graphics processor, modelled as infinitely fast.
     *
     * There is no GP here, so nothing consumes the FIFO - and the game does
     * not merely submit and move on. It polls GXGetFifoPtrs in a yield loop
     * until the read pointer catches the write pointer, which with a static
     * read pointer never happens: 33 million reads of four registers and no
     * further drawing. That is not a stall in our code; it is the game
     * correctly waiting for hardware that is not there.
     *
     * Reporting the FIFO as already drained is the honest answer for a host
     * that executes graphics commands synchronously, and it is what phase 3
     * will genuinely be: the renderer consumes the stream during the write,
     * so by the time the guest can look, it IS empty.
     *
     * The read pointer is reported AS the write pointer rather than as a
     * constant, so the game's own arithmetic on the pair stays consistent
     * whatever it set the FIFO base to.
     */
    if (addr >= MMIO_CP + CP_RW_DISTANCE && addr < MMIO_CP + CP_RW_DISTANCE + 4u)
        return 0u;                                    /* nothing outstanding */

    if (addr >= MMIO_CP + CP_READ_PTR && addr < MMIO_CP + CP_READ_PTR + 4u) {
        uint32_t off = addr - (MMIO_CP + CP_READ_PTR);
        uint8_t* w = at(m, MMIO_CP + CP_WRITE_PTR + off);
        if (w) { p = w; }
    }

    if (addr >= MMIO_CP + CP_STATUS && addr < MMIO_CP + CP_STATUS + 2u) {
        /* Read idle and command idle: the GP has nothing left to do. The
         * overflow and underflow watermark bits stay as written - those are
         * the guest's own thresholds, not our state to invent. */
        for (i = 0; i < size; ++i) v = (v << 8) | p[i];
        return v | CP_STATUS_RD_IDLE | CP_STATUS_CMD_IDLE;
    }

    for (i = 0; i < size; ++i) v = (v << 8) | p[i];   /* big-endian, as the bus is */
    return v;
}



/* Video interface display interrupts. Four 32-bit registers; the INT bit is
 * the top bit of the upper halfword and the enable is bit 12 of it. DI0 and
 * DI1 are the retrace interrupts, DI2 and DI3 the position ones - the SDK's
 * handler returns early for the latter, so which of them is asserted decides
 * whether a frame is counted at all. */
#define VI_DI0          0x30u
#define VI_DI_COUNT     4u
#define VI_DI_INT       0x8000u
#define VI_DI_ENB       0x1000u

/* Pixel engine interrupt control, GX_GET_PE_REG(5). Bits 0 and 1 enable the
 * token and finish interrupts; bits 2 and 3 are write-one-to-clear
 * acknowledgements for them. */
#define PE_INT_CTRL     0x0Au
#define PE_ACK_TOKEN    0x0004u
#define PE_ACK_FINISH   0x0008u

/* Processor interface interrupt status, and the two bits this models. */
#define PI_INTSR_OFF    0x00u
#define PI_SI           (1u << 3)   /* serial: the controller ports */
#define PI_DSP          (1u << 6)   /* shared: audio interface, ARAM, DSP */
#define PI_VI           (1u << 8)
#define PI_PE_TOKEN     (1u << 9)
#define PI_PE_FINISH    (1u << 10)

static uint32_t pi_cause(const MgsMmio* m)
{
    const uint8_t* p = &m->regs[(MMIO_PI - MMIO_BASE) + PI_INTSR_OFF];
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void pi_set_cause(MgsMmio* m, uint32_t v)
{
    uint8_t* p = &m->regs[(MMIO_PI - MMIO_BASE) + PI_INTSR_OFF];
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint16_t vi_di(const MgsMmio* m, unsigned i)
{
    const uint8_t* p = &m->regs[(MMIO_VI - MMIO_BASE) + VI_DI0 + i * 4u];
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void vi_di_set(MgsMmio* m, unsigned i, uint16_t v)
{
    uint8_t* p = &m->regs[(MMIO_VI - MMIO_BASE) + VI_DI0 + i * 4u];
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

/* PI's VI bit follows the display interrupts rather than being latched: the
 * line is asserted while ANY display interrupt is pending, and drops when the
 * guest's handler has cleared them all. */
static void vi_refresh_line(MgsMmio* m)
{
    unsigned i;
    uint32_t cause = pi_cause(m);
    for (i = 0; i < VI_DI_COUNT; ++i) {
        if (vi_di(m, i) & VI_DI_INT) { pi_set_cause(m, cause | PI_VI); return; }
    }
    pi_set_cause(m, cause & ~PI_VI);
}

/* PI's DSP bit follows the DSP's three status bits, for the same reason PI's
 * VI bit follows the display interrupts: it is a shared line, not a latch.
 *
 * ONE LINE, THREE SOURCES - the audio interface (0x08), ARAM (0x20) and the
 * DSP itself (0x80). The SDK's __OSDispatchInterrupt reads this register to
 * decide WHICH of the three it is, and if none of the bits is set it builds
 * an empty cause, runs no handler and returns. PI's bit is then asserted with
 * nothing behind it: an interrupt nobody can claim, and nobody can clear.
 *
 * That is not merely untidy. Now that pending interrupts are re-offered while
 * the line is asserted (mgs_interrupt_pending), a line stuck high is re-taken
 * for ever - 871,157 times in a 40,000,000-step boot, each one entering the
 * dispatcher to do nothing. Before that change it was invisible, which is why
 * it survived so long.
 *
 * Mirrored on the STATUS bits alone, not gated by the mask bits beside them
 * (0x10, 0x40, 0x100). Hardware does gate, but the SDK's dispatcher tests
 * status only, so status is the condition that decides whether a handler can
 * run - and matching the test that matters beats matching a test nothing
 * reads. The distinction is moot in this boot anyway: the guest has all three
 * mask bits set.
 */
/* THE SERIAL INTERFACE: ONE CONTROLLER IN PORT 1, NOTHING IN THE OTHERS.
 *
 * Until now this was a stub that cleared the transfer-start bit and did
 * nothing else, and the trace shows exactly what that cost: the SDK starts
 * ONE transfer in a whole boot - SICOMCSR = 0xC0010301, channel 0, one byte
 * out and three back, which is SIGetType asking for a device id - and then
 * never reads the answer. It never reads it because clearing TSTART is not
 * how a transfer finishes. Hardware also sets TCINT and raises the serial
 * interrupt, and the SDK's completion handler is what reads the buffer. With
 * no completion the handler never runs, PAD concludes the port is empty, and
 * the boot stops at "No Memory Card" with nothing able to press a button.
 *
 * Bit positions here are the public register documentation, not a guess at
 * silicon: SICOMCSR bit 0 TSTART, bits 1-2 channel, bits 8-14 input length,
 * bits 16-22 output length, bit 30 TCINTMSK, bit 31 TCINT; SISR gives each
 * channel a byte, most significant first, with NOREP at bit 27 of channel 0.
 * A length field of zero means 128, not zero.
 */
#define SI_TSTART       0x00000001u
#define SI_TCINT        0x80000000u
#define SI_TCINTMSK     0x40000000u
#define SI_RDSTINT      0x10000000u
#define SI_RDSTINTMSK   0x08000000u
#define SI_NOREP(ch)    (1u << (27u - 8u * (unsigned)(ch)))
#define SI_RDST(ch)     (1u << (29u - 8u * (unsigned)(ch)))
#define SI_POLL_EN(ch)  (1u << (7u - (unsigned)(ch)))
#define SI_CHAN_STRIDE  0x0Cu
#define SI_CHAN_INBUFH  0x04u
#define SI_CHAN_INBUFL  0x08u

static uint32_t si_reg(const MgsMmio* m, uint32_t off)
{
    const uint8_t* p = &m->regs[(MMIO_SI - MMIO_BASE) + off];
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void si_set_reg(MgsMmio* m, uint32_t off, uint32_t v)
{
    uint8_t* p = &m->regs[(MMIO_SI - MMIO_BASE) + off];
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* The serial line, level-triggered like the rest (F126). */
static void si_refresh_line(MgsMmio* m)
{
    uint32_t csr = si_reg(m, SI_COMCSR);
    uint32_t cause = pi_cause(m);
    int asserted = ((csr & SI_TCINT)   && (csr & SI_TCINTMSK)) ||
                   ((csr & SI_RDSTINT) && (csr & SI_RDSTINTMSK));

    if (asserted) pi_set_cause(m, cause | PI_SI);
    else          pi_set_cause(m, cause & ~PI_SI);
}

/* Fill the transfer buffer with this channel's reply. Returns 0 when nothing
 * is attached, which is every port but the first. */
static int si_reply(MgsMmio* m, unsigned chan, uint8_t cmd, unsigned inlen)
{
    uint8_t* io = &m->regs[(MMIO_SI + SI_IOBUF) - MMIO_BASE];
    uint8_t buf[16];
    unsigned n = 0u, i;

    if (chan != 0u) return 0;          /* one controller, in port 1 */

    switch (cmd) {
        case 0x00u:                    /* type and status */
            /* 0x09 is a standard GameCube controller. */
            buf[0] = 0x09u; buf[1] = 0x00u; buf[2] = 0x00u; n = 3u;
            break;

        case 0x40u:                    /* poll: buttons and sticks */
        case 0x41u:                    /* origin */
        case 0x42u:                    /* recalibrate */
            /* Neutral: no buttons, sticks centred, triggers released. Real
             * input is not wired to this yet - what this commit establishes
             * is that a controller EXISTS, which is what PAD stopped on. */
            buf[0] = (uint8_t)(m->pad_buttons >> 8);
            buf[1] = (uint8_t)(m->pad_buttons);
            buf[2] = 0x80u; buf[3] = 0x80u;      /* main stick x, y */
            buf[4] = 0x80u; buf[5] = 0x80u;      /* c stick x, y */
            buf[6] = 0x00u; buf[7] = 0x00u;      /* analog l, r */
            buf[8] = 0x00u; buf[9] = 0x00u;      /* analog a, b */
            n = (cmd == 0x40u) ? 8u : 10u;
            break;

        default:
            return 0;
    }

    if (n > inlen) n = inlen;
    for (i = 0; i < n; ++i) io[i] = buf[i];
    return 1;
}

/* READING A CHANNEL'S INPUT BUFFER CONSUMES THE POLL.
 *
 * RDST means "there is data here you have not taken yet", and hardware clears
 * it when the high word is read. Leaving it set tells the SDK the data is
 * perpetually fresh, so a loop that waits for the NEXT poll never waits.
 * When the last channel is consumed the aggregate interrupt flag goes too.
 */
static void si_consume_read(MgsMmio* m, uint32_t addr)
{
    uint32_t off, st;
    unsigned ch, k;
    int more = 0;

    if (addr < MMIO_SI || addr >= MMIO_SI + 4u * SI_CHAN_STRIDE) return;
    off = addr - MMIO_SI;
    ch  = off / SI_CHAN_STRIDE;
    if (off % SI_CHAN_STRIDE != SI_CHAN_INBUFH) return;

    st = si_reg(m, SI_STATUS);
    if (!(st & SI_RDST(ch))) return;

    st &= ~SI_RDST(ch);
    si_set_reg(m, SI_STATUS, st);
    for (k = 0; k < 4u; ++k) if (st & SI_RDST(k)) more = 1;
    if (!more) {
        si_set_reg(m, SI_COMCSR, si_reg(m, SI_COMCSR) & ~SI_RDSTINT);
        si_refresh_line(m);
    }
}

static void si_transfer(MgsMmio* m)
{
    uint32_t csr = si_reg(m, SI_COMCSR);
    unsigned chan  = (csr >> 1) & 3u;
    unsigned inlen = (csr >> 8) & 0x7Fu;
    uint8_t  cmd   = m->regs[(MMIO_SI + SI_IOBUF) - MMIO_BASE];
    uint32_t st    = si_reg(m, SI_STATUS);

    if (!inlen) inlen = 128u;

    if (si_reply(m, chan, cmd, inlen)) st &= ~SI_NOREP(chan);
    else                               st |=  SI_NOREP(chan);
    si_set_reg(m, SI_STATUS, st);

    /* The transfer is over: TSTART clears and TCINT raises, exactly as the
     * set-a-bit-and-wait devices above. Completing instantly is honest here
     * too - there is no bus on the other side. */
    csr = (csr & ~SI_TSTART) | SI_TCINT;
    si_set_reg(m, SI_COMCSR, csr);
    ++m->si_transfers;
    si_refresh_line(m);
}

/* VBLANK POLLING - THE OTHER HALF OF THE SERIAL INTERFACE.
 *
 * Completing explicit transfers alone made this worse, not better, and the
 * measurement said so plainly: GX fell from 2,476,033 commands to 13,060 and
 * the guest sat in OSRestoreInterrupts. That is F128's shape exactly, so the
 * fix is to make the other half honest rather than to revert.
 *
 * What PAD actually does once it believes a controller exists is set
 * SIPOLL = 0x01280280 - the low byte 0x80 is EN0 - and then wait for the
 * hardware to poll that port every field on its own and raise RDSTINT. We
 * enumerated the ports and then never polled, so PAD waited forever for data
 * that was never going to arrive.
 *
 * Driving this from the frame tick rather than from reads is the same choice
 * the video interface makes above: a guest waiting on input must see it
 * arrive at the rate the host actually runs.
 */
static void si_poll_frame(MgsMmio* m)
{
    uint32_t poll = si_reg(m, SI_POLL);
    uint32_t st = si_reg(m, SI_STATUS);
    unsigned ch;
    int any = 0;

    for (ch = 0; ch < 4u; ++ch) {
        uint32_t base;
        uint8_t buf[16];

        if (!(poll & SI_POLL_EN(ch))) continue;      /* not being polled */
        if (!si_reply(m, ch, 0x40u, 8u)) {           /* nothing attached */
            st |= SI_NOREP(ch);
            continue;
        }

        /* si_reply left the answer in the shared transfer buffer; a polled
         * transfer delivers it to the channel's own input registers. */
        memcpy(buf, &m->regs[(MMIO_SI + SI_IOBUF) - MMIO_BASE], 8u);
        base = MMIO_SI + ch * SI_CHAN_STRIDE;
        si_set_reg(m, (base + SI_CHAN_INBUFH) - MMIO_SI,
                   ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                   ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3]);
        si_set_reg(m, (base + SI_CHAN_INBUFL) - MMIO_SI,
                   ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                   ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7]);

        st = (st & ~SI_NOREP(ch)) | SI_RDST(ch);
        any = 1;
    }

    si_set_reg(m, SI_STATUS, st);

    if (any) {
        uint32_t csr = si_reg(m, SI_COMCSR) | SI_RDSTINT;
        si_set_reg(m, SI_COMCSR, csr);
        ++m->si_polls;
        si_refresh_line(m);
    }
}

static void dsp_refresh_line(MgsMmio* m)
{
    const uint8_t* cr = &m->regs[(MMIO_DSP + DSP_CONTROL) - MMIO_BASE];
    uint16_t v = (uint16_t)((cr[0] << 8) | cr[1]);
    uint32_t cause = pi_cause(m);

    if (v & (0x0008u | 0x0020u | 0x0080u)) pi_set_cause(m, cause | PI_DSP);
    else                                   pi_set_cause(m, cause & ~PI_DSP);
}

void mgs_mmio_assert_retrace(MgsMmio* m)
{
    unsigned i, any = 0u;
    if (!m) return;
    /* Only DI0 and DI1: those are the retrace interrupts. Asserting DI2 or
     * DI3 would send the SDK's handler down its position-callback path,
     * which returns WITHOUT counting the frame or waking anything. */
    for (i = 0; i < 2u; ++i) {
        uint16_t di = vi_di(m, i);
        if (di & VI_DI_ENB) { vi_di_set(m, i, (uint16_t)(di | VI_DI_INT)); any = 1u; }
    }
    /* Before the guest arms a display interrupt there is no line to assert.
     * Raising PI's bit anyway would have the dispatcher call a handler with
     * nothing to service. */
    if (any) pi_set_cause(m, pi_cause(m) | PI_VI);
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
        /* Byte count only. What the bytes MEAN is the command parser's
         * business: this layer cannot see where a command starts, and
         * guessing from a lone opcode byte matches vertex data too - acting
         * on one of those writes a framebuffer over the game's memory. */
        m->wgpipe_bytes += size;
        if (size < 16u) ++m->wgpipe_by_size[size];
        if (m->fifo_sink) m->fifo_sink(m->fifo_user, value, size);
        return;
    }

    /* MGS_TRACE_FIFOREG shows writes to the two FIFO descriptions.
     *
     * The CPU-side one lives in PI at +0x0C/+0x10/+0x14 and the GP-side one
     * in CP at +0x20 onwards. When they name the same memory the guest is
     * drawing; when they differ it is RECORDING a display list, and every
     * write-gather-pipe write belongs in a buffer rather than in the parser.
     * Which registers carry that is worth confirming rather than assuming. */
    if (m->trace_fiforeg &&
        ((addr >= MMIO_PI + 0x0Cu && addr < MMIO_PI + 0x18u) ||
         (addr >= MMIO_BASE + 0x20u && addr < MMIO_BASE + 0x40u)))
        fprintf(stderr, "[mmio] fifo reg 0x%08X <- 0x%08X (%u bytes)\n",
                addr, value, size);

    p = at(m, addr);
    if (!p) return;
    for (i = 0; i < size; ++i)
        p[i] = (uint8_t)(value >> (8u * (size - 1u - i)));

    /* THE DSP CONSUMES A MESSAGE THE MOMENT IT IS SENT.
     *
     * `DSPSendMailToDSP` writes the high half - top bit set, which is the
     * "full" flag - then the low half, and the caller then spins on
     * `DSPCheckMailToDSP` until the DSP has taken it. Nothing here is going
     * to take it, so the flag is cleared as the send completes. The SDK's
     * boot handshake is a dozen of these in a row and stops at the first.
     */
    if (addr >= MMIO_DSP + DSP_MAIL_TO_LO &&
        addr < MMIO_DSP + DSP_MAIL_TO_LO + 2u) {
        uint8_t* hi = &m->regs[(MMIO_DSP + DSP_MAIL_TO_HI) - MMIO_BASE];
        hi[0] = (uint8_t)(hi[0] & 0x7Fu);
        ++m->dsp_mails_sent;
    }

    /* THE THREE STATUS BITS ARE WRITE-ONE-TO-CLEAR.
     *
     * `__DSPHandler` acknowledges its interrupt with
     *     tmp = (tmp & ~0x28) | 0x80;  __DSPRegs[5] = tmp;
     * which under a plain store would SET the DSP status bit and leave the
     * line asserted for ever. On hardware writing a one clears it, and that
     * difference is the whole meaning of the write.
     */
    if (addr >= MMIO_DSP + DSP_CONTROL && addr < MMIO_DSP + DSP_CONTROL + 2u) {
        const uint16_t status = (uint16_t)(0x0008u | 0x0020u | 0x0080u);
        uint8_t* cr = &m->regs[(MMIO_DSP + DSP_CONTROL) - MMIO_BASE];
        uint16_t written = (uint16_t)((cr[0] << 8) | cr[1]);
        uint16_t kept = (uint16_t)(m->dsp_status & ~written);
        uint16_t v = (uint16_t)((written & ~status) | (kept & status));
        cr[0] = (uint8_t)(v >> 8);
        cr[1] = (uint8_t)v;
        m->dsp_status = (uint16_t)(v & status);
        dsp_refresh_line(m);       /* the guest may have just dropped it */
    }

    /* UNHALTING THE DSP MAKES IT ANNOUNCE ITSELF.
     *
     * `DSPInit` sets bit 0x800 and then clears the halt bit, and the real
     * DSP runs its boot ROM and posts 0x8071FEED - which `__DSP_boot_task`
     * waits for and asserts on. Without it the boot spins reading
     * 0xCC005004 forever, which is where 90,000,000 steps ended up.
     *
     * This is the handshake and NOT a DSP. No microcode runs; the messages
     * that follow are accepted and dropped. That is enough to get past the
     * bring-up and no further, and phase 4 is where it becomes a real
     * coprocessor. */
    if (addr >= MMIO_DSP + DSP_CONTROL && addr < MMIO_DSP + DSP_CONTROL + 2u) {
        uint8_t* cr = &m->regs[(MMIO_DSP + DSP_CONTROL) - MMIO_BASE];
        uint32_t v = ((uint32_t)cr[0] << 8) | cr[1];

        /* ON THE RISING EDGE OF 0x800, NOT ON ITS PRESENCE.
         *
         * `DSPInit` sets that bit and it STAYS set, so arming on "the bit is
         * set" armed on every later write to this register - including
         * `__DSPHandler`'s own acknowledgement, which preserves it. Each one
         * re-posted the boot message over whatever was already in the
         * mailbox, so the guest read 0x8071FEED three times and never saw a
         * single task message. The DSP announces itself once per reset,
         * which is what an edge is. */
        if ((v & 0x800u) && !(m->dsp_control_prev & 0x800u)) m->dsp_booting = 1;
        m->dsp_control_prev = (uint16_t)v;

        if (m->dsp_booting && !(v & 0x4u)) {
            uint8_t* mb = &m->regs[(MMIO_DSP + DSP_MAIL_FROM_HI) - MMIO_BASE];
            mb[0] = 0x80u; mb[1] = 0x71u;   /* 0x8071, top bit = mail waiting */
            mb[2] = 0xFEu; mb[3] = 0xEDu;   /* 0xFEED */
            m->dsp_booting = 0;
        }
    }

    /* WRITING THE LOW HALF OF THE LENGTH STARTS AN ARAM TRANSFER. That is
     * the SDK's own sequence: address, address, length-high with the
     * direction bit, then length-low last. Everything before it is just
     * loading registers. */
    if (addr == MMIO_DSP + 0x2Au) {
        mgs_aram_run_dma(&m->aram, &m->regs[MMIO_DSP - MMIO_BASE]);
        /* The transfer is already done, so the busy bit is already clear.
         * `__ARWaitForDMA` spins on it and would never leave if it were
         * set and nothing cleared it. */
        {
            uint8_t* csr = &m->regs[(MMIO_DSP - MMIO_BASE) + 0x0Au];
            uint32_t v = ((uint32_t)csr[0] << 8) | csr[1];
            v &= ~0x200u;
            csr[0] = (uint8_t)(v >> 8); csr[1] = (uint8_t)v;
        }
    }

    /* Acknowledging an interrupt AT THE DEVICE is what drops its line into
     * PI. Both of these are the guest's own handlers doing exactly that. */
    if (addr >= MMIO_VI + VI_DI0 && addr < MMIO_VI + VI_DI0 + VI_DI_COUNT * 4u)
        vi_refresh_line(m);

    if (addr == MMIO_PE + PE_INT_CTRL && size == 2u) {
        uint32_t cause = pi_cause(m);
        /* COUNTED, because this is the guest telling us its handler ran.
         * The host's own delivery tally says only that the exception was
         * taken; an acknowledge here says the handler reached the end. The
         * difference between the two counts is the number of completions the
         * guest was told about and never processed, which is a thing no
         * host-side counter can see on its own. */
        uint32_t before = cause;
        if (value & PE_ACK_FINISH) { cause &= ~PI_PE_FINISH; ++m->pe_finish_acks; }
        if (value & PE_ACK_TOKEN)  { cause &= ~PI_PE_TOKEN;  ++m->pe_token_acks; }
        pi_set_cause(m, cause);
        if (m->trace_pi)
            fprintf(stderr, "[pi] PE ack #%llu wrote 0x%04X: cause 0x%08X -> 0x%08X\n",
                    (unsigned long long)m->pe_finish_acks,
                    (unsigned)value, before, cause);
    }

    /* Transfers that hardware would complete asynchronously complete here
     * immediately: clear the start bit the guest just set, so the poll that
     * follows sees a finished transfer rather than spinning forever. */
    {
        uint32_t off = addr - MMIO_BASE;
        uint32_t exi = MMIO_EXI - MMIO_BASE;
        if (off >= exi && off < exi + 3u * EXI_CHANNEL_STRIDE) {
            uint32_t within = (off - exi) % EXI_CHANNEL_STRIDE;
            unsigned chan = (off - exi) / EXI_CHANNEL_STRIDE;

            /* Chip select lives in the status register's bits 7-9. Letting go
             * of it is what ends a card command, so it has to be watched as
             * closely as asserting it. */
            if (within == EXI_CSR && chan == 0u) {
                uint8_t cs = (uint8_t)((value >> 7) & 7u);
                if (cs != m->exi_cs) {
                    m->exi_cs = cs;
                    if (m->card_ready) mgs_exi_card_select(&m->card, (cs & 1u) != 0u);
                    mgs_exi_ipl_select(&m->ipl, (cs & 2u) != 0u);
                }
                /* EXT is the slot's own answer about whether anything is
                 * plugged in - hardware status, not something software sets.
                 * Restoring it after every write keeps a guest that rewrites
                 * the whole register from accidentally unplugging the card. */
                if (m->card_ready) m->regs[off - within + EXI_CSR + 2u] |= 0x10u;

            }

            if (m->trace_exi && m->exi_traced < 400u && chan < 2u) {
                static const char* nm[5] = { "CSR", "MAR", "LEN", "CR ", "DATA" };
                ++m->exi_traced;
                fprintf(stderr, "[exi] w ch%u %s = 0x%08X (size %u)\n",
                        chan, within / 4u < 5u ? nm[within / 4u] : "???",
                        value, size);
            }
            if (within == EXI_CR && (value & EXI_TSTART)) {
                exi_transfer(m, chan, value);
                m->regs[off + size - 1u] &= (uint8_t)~EXI_TSTART;
                /* TCINT IS NOT RAISED HERE, THOUGH HARDWARE RAISES IT.
                 * Tried: it made the boot strictly worse - EXI transfers in
                 * a boot fell from 9 to 4, the card's geometry was never
                 * stored at all, and the mount's error moved from IOERROR to
                 * NOCARD. Delivering an interrupt from inside the store that
                 * started the transfer re-enters the guest at a point it did
                 * not choose; module.c's run loop already warns that raising
                 * one moves the pc. If this is revisited it needs queueing to
                 * a safe point the way DVD completions are, not asserting
                 * from here. */
            }
        }

        /* DSP reset: self-clearing, like EXI's transfer start. */
        if (addr == MMIO_DSP + DSP_CONTROL && (value & DSP_CR_RESET))
            m->regs[off + size - 1u] &= (uint8_t)~DSP_CR_RESET;

        /* Starting an ARAM DMA completes it: raise the completion flag the
         * guest is about to poll for. Writing the count register is what
         * starts a transfer on hardware. */
        if (addr == MMIO_DSP + AR_DMA_CNT) {
            uint8_t* cr = at(m, MMIO_DSP + DSP_CONTROL);
            if (cr) {
                uint16_t v = (uint16_t)((cr[0] << 8) | cr[1]);
                v |= (uint16_t)(DSP_CR_ARINT | DSP_CR_ARDMA_DONE);
                cr[0] = (uint8_t)(v >> 8);
                cr[1] = (uint8_t)v;
                m->dsp_status |= (uint16_t)DSP_CR_ARINT;
                dsp_refresh_line(m);
                /* A completed transfer also RAISES A LINE. Setting the
                 * status bit only says which source it was; without the
                 * interrupt the operating system's handler never runs, so
                 * the queue that owns the transfer never calls back and
                 * whoever is waiting on it waits for ever. Raised from the
                 * run loop, because that is where the guest can be
                 * interrupted. */
                ++m->aram_irq_pending;
            }
        }

        /* Serial transfer start: the same shape again. */
        /* VI GEOMETRY CHANGES. The external framebuffer is scanned out with
         * VI's registers, not with whatever GX last copied, so a movie that
         * uses a different width shows up here as a write to HSW (0x48:
         * high byte = width/16 pixels, low byte = stride/16 bytes) or to the
         * field base addresses. */
        if (m->trace_vi && addr >= MMIO_VI && addr < MMIO_VI + 0x80u) {
            uint32_t off_vi = addr - MMIO_VI;
            if (off_vi == 0x48u || off_vi == 0x1Cu || off_vi == 0x24u) {
                if (value != m->vi_last[off_vi == 0x48u ? 0u
                                      : off_vi == 0x1Cu ? 1u : 2u]) {
                    m->vi_last[off_vi == 0x48u ? 0u
                             : off_vi == 0x1Cu ? 1u : 2u] = value;
                    fprintf(stderr, "[vi] +0x%02X = 0x%08X%s\n",
                            off_vi, value,
                            off_vi == 0x48u ? "  (width/stride)" : "");
                }
            }
        }

        if (m->trace_si && addr >= MMIO_SI && addr < MMIO_EXI &&
            m->si_traced < m->si_trace_cap) {
            ++m->si_traced;
            fprintf(stderr, "[si] write 0x%08X size %u = 0x%08X\n",
                    addr, size, value);
        }

        /* SICOMCSR: TCINT and RDSTINT are write-one-to-clear, so the
         * value the guest just stored has to be undone for those bits
         * before anything else looks at the register. */
        if (addr == MMIO_SI + SI_COMCSR) {
            uint32_t v = si_reg(m, SI_COMCSR);
            if (value & SI_TCINT)   v &= ~SI_TCINT;
            if (value & SI_RDSTINT) v &= ~SI_RDSTINT;
            si_set_reg(m, SI_COMCSR, v);
            if (v & SI_TSTART) si_transfer(m);
            else               si_refresh_line(m);
        }
    }
}

/* What the controller in port 1 is holding down. Set from the host's input
 * layer once a frame; read by si_reply when the port is polled. */
void mgs_mmio_set_pad(MgsMmio* m, uint16_t buttons)
{
    if (!m) return;
    m->pad_buttons = (uint16_t)(m->pad_forced | buttons);
    if (m->pad_buttons && m->pad_reported != m->pad_buttons) {
        m->pad_reported = m->pad_buttons;
        fprintf(stderr, "[pad] buttons now 0x%04X\n", m->pad_buttons);
    }
}

void mgs_mmio_tick_frame(MgsMmio* m)
{
    /* The scripted pad, if one was given: the last entry whose frame has
     * arrived wins, so entries are held rather than pulsed. */
    if (m->pad_script_n) {
        unsigned i;
        uint16_t held = 0u;
        for (i = 0; i < m->pad_script_n; ++i)
            if (m->pad_frame >= m->pad_script_frame[i])
                held = m->pad_script_btn[i];
        m->pad_forced = held;
        ++m->pad_frame;
    }

    si_poll_frame(m);

    /* Advance a whole field per frame. Driving this from the frame loop
     * rather than from reads is deliberate: a guest that polls the beam
     * position must see time pass at the rate the host is actually running,
     * not as fast as it can spin. */
    m->vi_half_line = (uint16_t)((m->vi_half_line + VI_HALF_LINES_PER_FIELD)
                                 % VI_HALF_LINES_PER_FIELD);
    if (m->vi_half_line == 0u) m->vi_half_line = 1u;
}


/* Which registers is the guest reading? A stall shows up here as one address
 * with a count orders of magnitude above the rest. */
void mgs_mmio_report_hot(const MgsMmio* m, unsigned top)
{
    static const struct { uint32_t base, end; const char* name; } blocks[] = {
        { MMIO_CP,  MMIO_PE,     "CP"  }, { MMIO_PE,  MMIO_VI,  "PE" },
        { MMIO_VI,  MMIO_PI,     "VI"  }, { MMIO_PI,  MMIO_MI,  "PI" },
        { MMIO_MI,  MMIO_DSP,    "MI"  }, { MMIO_DSP, MMIO_DI,  "DSP" },
        { MMIO_DI,  MMIO_SI,     "DI"  }, { MMIO_SI,  MMIO_EXI, "SI" },
        { MMIO_EXI, MMIO_AI,     "EXI" }, { MMIO_AI,  MMIO_WGPIPE, "AI" },
    };
    unsigned n = (MMIO_END - MMIO_BASE) / 2u, i, k, shown = 0u;

    if (!m) return;
    /* The serial interface gets its own line whatever its rank.
     *
     * SI is the controller bus. Whether the game is POLLING it decides a
     * question the hottest-six list cannot answer: a screen that waits for a
     * button reads SI constantly, and one that has given up does not. The
     * memory-card warning offers Retry and Continue, so it should be
     * polling - and if it is, the boot is waiting for us rather than stuck. */
    {
        unsigned q; uint64_t si = 0u;
        for (q = 0; q < n; ++q) {
            uint32_t a = MMIO_BASE + q * 2u;
            if (a >= 0xCC006400u && a < 0xCC006800u) si += m->read_hist[q];
        }
        printf("serial interface (controller) reads: %llu\n",
               (unsigned long long)si);
    }
    printf("hottest MMIO reads:\n");
    for (k = 0; k < top; ++k) {
        unsigned best = 0u; uint32_t bestc = 0u; uint32_t addr; const char* nm = "?";
        for (i = 0; i < n; ++i) {
            uint32_t c = m->read_hist[i];
            if (c > bestc) { bestc = c; best = i; }
        }
        if (!bestc) break;
        addr = MMIO_BASE + best * 2u;
        for (i = 0; i < sizeof blocks / sizeof blocks[0]; ++i)
            if (addr >= blocks[i].base && addr < blocks[i].end) nm = blocks[i].name;
        printf("  0x%08X  %-4s +0x%03X  %10u reads\n",
               addr, nm, addr - (addr & 0xFFFFF000u), bestc);
        ((MgsMmio*)m)->read_hist[best] = 0u;   /* consumed for this report */
        ++shown;
    }
    if (!shown) printf("  (none)\n");
}


/* The video interface's top-field base, VI_TFBL at 0xCC00201C.
 *
 * Not simply the register's value. The SDK's setFbbRegs stores the address
 * in 32-byte units and sets bit 28 to say so whenever it does not fit in 24
 * bits - which for anything above 16 MB it never does. Reading the register
 * as a plain address gives a location 32 times too low, in the middle of the
 * game's own data. */
/* The video interface's register block, for callers that need to read the
 * scan-out geometry rather than infer it from the last copy. */
const uint8_t* mgs_mmio_vi_regs(const MgsMmio* m)
{
    return m ? &m->regs[MMIO_VI - MMIO_BASE] : NULL;
}

uint32_t mgs_mmio_xfb_address(const MgsMmio* m)
{
    const uint8_t* p;
    uint32_t reg, addr;

    if (!m) return 0u;
    p = &m->regs[(MMIO_VI - MMIO_BASE) + 0x1Cu];
    reg = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
          ((uint32_t)p[2] << 8) | (uint32_t)p[3];

    addr = reg & 0x00FFFFFFu;
    if (reg & 0x10000000u) addr <<= 5;
    if (!addr) return 0u;
    return 0x80000000u | (addr & 0x03FFFFFFu);   /* physical to cached */
}

void mgs_mmio_set_fifo_sink(MgsMmio* m, MgsFifoSink sink, void* user)
{
    if (!m) return;
    m->fifo_sink = sink;
    m->fifo_user = user;
}

/* ---- the two FIFO descriptions ---------------------------------------- */

static uint32_t reg32(const MgsMmio* m, uint32_t addr)
{
    const uint8_t* p = &m->regs[addr - MMIO_BASE];
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t reg16(const MgsMmio* m, uint32_t addr)
{
    const uint8_t* p = &m->regs[addr - MMIO_BASE];
    return (uint16_t)(((uint32_t)p[0] << 8) | (uint32_t)p[1]);
}

/* The addresses are physical - the top bit is not stored - so they come back
 * with the cached window's bit put back on, which is the form every other
 * part of this runtime uses. */
static uint32_t as_guest(uint32_t physical)
{
    return physical ? (0x80000000u | (physical & 0x03FFFFFFu)) : 0u;
}

uint32_t mgs_mmio_cpu_fifo_base(const MgsMmio* m)
{
    return as_guest(reg32(m, MMIO_PI + PI_FIFO_BASE));
}

uint32_t mgs_mmio_cpu_fifo_end(const MgsMmio* m)
{
    return as_guest(reg32(m, MMIO_PI + PI_FIFO_END));
}

uint32_t mgs_mmio_cpu_fifo_wrptr(const MgsMmio* m)
{
    return as_guest(reg32(m, MMIO_PI + PI_FIFO_WRPTR));
}

void mgs_mmio_set_cpu_fifo_wrptr(MgsMmio* m, uint32_t v)
{
    uint8_t* p = &m->regs[(MMIO_PI + PI_FIFO_WRPTR) - MMIO_BASE];
    uint32_t phys = v & 0x03FFFFFFu;
    p[0] = (uint8_t)(phys >> 24); p[1] = (uint8_t)(phys >> 16);
    p[2] = (uint8_t)(phys >> 8);  p[3] = (uint8_t)phys;
}

uint32_t mgs_mmio_gp_fifo_base(const MgsMmio* m)
{
    uint32_t lo = reg16(m, MMIO_BASE + CP_FIFO_BASE_L);
    uint32_t hi = reg16(m, MMIO_BASE + CP_FIFO_BASE_H);
    return as_guest((hi << 16) | lo);
}

int mgs_mmio_recording(const MgsMmio* m)
{
    uint32_t cpu = mgs_mmio_cpu_fifo_base(m);
    uint32_t gp  = mgs_mmio_gp_fifo_base(m);

    /* Before either has been programmed there is nothing to tell apart, and
     * the answer that keeps the boot working is "drawing" - the logo is
     * issued before the game ever records a list. */
    if (!cpu || !gp) return 0;
    return cpu != gp;
}

void mgs_mmio_attach_aram(MgsMmio* m, GuestMemory* mem)
{
    mgs_aram_init(&m->aram, mem);
}

void mgs_mmio_attach_card(MgsMmio* m, GuestMemory* mem, const char* path)
{
    uint32_t csr = (MMIO_EXI - MMIO_BASE) + EXI_CSR;
    if (!m) return;
    m->exi_mem = mem;
    mgs_exi_ipl_init(&m->ipl);
    /* SRAM first: the card's serial is generated from the flash id it holds,
     * which sits at offset 0x14 in the block, twelve bytes per channel. */
    m->card_ready = mgs_exi_card_init(&m->card, path, 16u, &m->ipl.sram[0x14]);
    /* Announce the slot as occupied from the outset: the SDK reads EXT before
     * it touches anything else, and a card that appears later looks like one
     * the player pushed in mid-boot. */
    if (m->card_ready) m->regs[csr + 2u] |= 0x10u;
}

void mgs_mmio_card_flush(MgsMmio* m)
{
    if (m && m->card_ready) mgs_exi_card_flush(&m->card);
}

/* ---- the audio interface's sample counter ------------------------------
 *
 * AICR (0xCC006C00) bit 0 runs the interface and bit 1 picks the rate:
 * clear is 32 kHz, set is 48 kHz. AISCNT (0xCC006C08) counts samples since
 * the interface was started, and `__AI_SRC_INIT` measures its rate against
 * OSGetTime to work out which clock it is on.
 *
 * So the counter is DERIVED from guest ticks rather than incremented by some
 * convenient amount per frame: the guest is timing it, and a counter that
 * moves at the wrong rate answers the question wrongly rather than failing.
 */
#define AI_CONTROL  0x00u
#define AI_SAMPLE_COUNT 0x08u
#define AI_PLAYING  0x01u
#define AI_48KHZ    0x02u

void mgs_mmio_advance_ticks(MgsMmio* m, uint32_t ticks)
{
    uint8_t* cr = &m->regs[(MMIO_AI - MMIO_BASE) + AI_CONTROL];
    uint32_t control = ((uint32_t)cr[0] << 24) | ((uint32_t)cr[1] << 16) |
                       ((uint32_t)cr[2] << 8) | (uint32_t)cr[3];
    uint64_t samples;
    uint8_t* sc;

    if (!(control & AI_PLAYING)) return;

    m->ai_ticks += ticks;

    /* The Gekko timebase is the 162 MHz bus divided by four. */
    samples = (m->ai_ticks * (uint64_t)((control & AI_48KHZ) ? 48000u : 32000u))
            / 40500000ull;

    sc = &m->regs[(MMIO_AI - MMIO_BASE) + AI_SAMPLE_COUNT];
    sc[0] = (uint8_t)(samples >> 24); sc[1] = (uint8_t)(samples >> 16);
    sc[2] = (uint8_t)(samples >> 8);  sc[3] = (uint8_t)samples;
}

int mgs_mmio_dsp_booted(const MgsMmio* m) { return m->dsp_booted; }

uint32_t mgs_mmio_dsp_mails_sent(const MgsMmio* m) { return m->dsp_mails_sent; }

int mgs_mmio_dsp_mail_pending(const MgsMmio* m)
{
    return (m->regs[(MMIO_DSP + DSP_MAIL_FROM_HI) - MMIO_BASE] & 0x80u) != 0;
}

void mgs_mmio_dsp_post_mail(MgsMmio* m, uint32_t mail)
{
    uint8_t* mb = &m->regs[(MMIO_DSP + DSP_MAIL_FROM_HI) - MMIO_BASE];
    /* Never over-write a message the guest has not taken. One mailbox, one
     * message; the caller checks first, and this is the backstop. */
    if (mail && (mb[0] & 0x80u)) return;
    /* Assert the DSP's own status bit. The line is shared with the audio
     * interface and ARAM, and this is what tells the dispatcher which of the
     * three it is - without it the message is delivered to nobody. */
    {
        uint8_t* cr = &m->regs[(MMIO_DSP + DSP_CONTROL) - MMIO_BASE];
        uint16_t v = (uint16_t)(((cr[0] << 8) | cr[1]) | 0x0080u);
        cr[0] = (uint8_t)(v >> 8); cr[1] = (uint8_t)v;
        m->dsp_status |= 0x0080u;
        dsp_refresh_line(m);
    }
    mb[0] = (uint8_t)((mail >> 24) | 0x80u);   /* top bit: mail waiting */
    mb[1] = (uint8_t)(mail >> 16);
    mb[2] = (uint8_t)(mail >> 8);
    mb[3] = (uint8_t)mail;
}

void mgs_mmio_dsp_clear_mail(MgsMmio* m)
{
    uint8_t* mb = &m->regs[(MMIO_DSP + DSP_MAIL_FROM_HI) - MMIO_BASE];
    {
        uint8_t* cr = &m->regs[(MMIO_DSP + DSP_CONTROL) - MMIO_BASE];
        uint16_t v = (uint16_t)(((cr[0] << 8) | cr[1]) & ~0x0080u);
        cr[0] = (uint8_t)(v >> 8); cr[1] = (uint8_t)v;
        m->dsp_status &= (uint16_t)~0x0080u;
        dsp_refresh_line(m);
    }
    mb[0] = mb[1] = mb[2] = mb[3] = 0u;
}

/* A completion is a DEVICE event before it is a line.
 *
 * ARAM's completion used to be announced by setting PI's DSP bit and nothing
 * else. The SDK's dispatcher reads the DSP's own status register to decide
 * which of the three sources on that shared line it is, finds none of them
 * set, builds an empty cause and returns - so the completion was delivered to
 * nobody, and the bit it set could never be cleared by anybody. That is the
 * line that was found stuck (PI cause 0x40, DSP control 0x0D50: all three
 * masks enabled, no status bit set at all).
 *
 * So the status bit is set here and PI's bit follows it, which is the order
 * hardware works in. */
void mgs_mmio_dsp_assert_aram(MgsMmio* m);
void mgs_mmio_dsp_assert_aram(MgsMmio* m)
{
    uint8_t* cr = &m->regs[(MMIO_DSP + DSP_CONTROL) - MMIO_BASE];
    uint16_t v = (uint16_t)(((cr[0] << 8) | cr[1]) | DSP_CR_ARINT);
    cr[0] = (uint8_t)(v >> 8); cr[1] = (uint8_t)v;
    m->dsp_status |= (uint16_t)DSP_CR_ARINT;
    dsp_refresh_line(m);
}

int mgs_mmio_take_aram_irq(MgsMmio* m)
{
    if (!m->aram_irq_pending) return 0;
    --m->aram_irq_pending;
    return 1;
}

void mgs_mmio_put_aram_irq(MgsMmio* m) { ++m->aram_irq_pending; }
