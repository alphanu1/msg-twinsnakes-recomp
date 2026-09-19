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

    /* See the FIFO-register trace in mgs_mmio_write. */
    m->trace_fiforeg = getenv("MGS_TRACE_FIFOREG") != NULL;

}

static uint8_t* at(MgsMmio* m, uint32_t addr)
{
    if (addr < MMIO_BASE || addr >= MMIO_END) return NULL;
    return m->regs + (addr - MMIO_BASE);
}

uint32_t mgs_mmio_read(MgsMmio* m, uint32_t addr, unsigned size)
{
    if (addr >= MMIO_BASE && addr < MMIO_END)
        ++m->read_hist[(addr - MMIO_BASE) / 2u];
    uint8_t* p = at(m, addr);
    uint32_t v = 0u;
    unsigned i;

    ++m->reads;
    if (!p) return 0u;

    /* DSPCR's completion flags read as set.
     *
     * A STAND-IN, and deliberately marked as one. This runtime has no DSP:
     * ARAM is host memory, so every transfer the guest starts has already
     * finished before it can look. The SDK's audio init clears these flags and
     * then waits for hardware to raise them asynchronously - which nothing
     * here will ever do, so the wait cannot complete however carefully the
     * write side is modelled.
     *
     * Reporting "already done" is accurate for the transfers and wrong for
     * anything that depends on DSP timing. That is an acceptable trade only
     * because audio is phase 4: nothing between here and a picture on screen
     * needs the DSP to behave like a coprocessor. Phase 4 replaces this with
     * a real voice mixer, and this comment is the reminder.
     */
    if (addr >= MMIO_DSP + DSP_CONTROL && addr < MMIO_DSP + DSP_CONTROL + 2u) {
        uint8_t* cr = at(m, MMIO_DSP + DSP_CONTROL);
        uint16_t v = (uint16_t)((cr[0] << 8) | cr[1]);
        v |= (uint16_t)(DSP_CR_ARINT | DSP_CR_ARDMA_DONE);
        cr[0] = (uint8_t)(v >> 8);
        cr[1] = (uint8_t)v;
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

    /* Acknowledging an interrupt AT THE DEVICE is what drops its line into
     * PI. Both of these are the guest's own handlers doing exactly that. */
    if (addr >= MMIO_VI + VI_DI0 && addr < MMIO_VI + VI_DI0 + VI_DI_COUNT * 4u)
        vi_refresh_line(m);

    if (addr == MMIO_PE + PE_INT_CTRL && size == 2u) {
        uint32_t cause = pi_cause(m);
        if (value & PE_ACK_FINISH) cause &= ~PI_PE_FINISH;
        if (value & PE_ACK_TOKEN)  cause &= ~PI_PE_TOKEN;
        pi_set_cause(m, cause);
    }

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
            if (cr) {
                uint16_t v = (uint16_t)((cr[0] << 8) | cr[1]);
                v |= (uint16_t)(DSP_CR_ARINT | DSP_CR_ARDMA_DONE);
                cr[0] = (uint8_t)(v >> 8);
                cr[1] = (uint8_t)v;
            }
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
