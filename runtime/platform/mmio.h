/* Memory-mapped hardware registers.
 *
 * Everything the GameCube's CPU talks to that is not RAM lives at 0xCC000000:
 * the command processor, the video interface, the processor interface, the
 * DSP, the disc interface, serial, external, and the write-gather pipe.
 * DolRecomp routes any access outside RAM to the host through
 * CPUState::external_read / external_write, so this is where they land.
 *
 * WHY THIS CANNOT JUST RETURN ZERO. With no handler installed, every register
 * reads as zero, and the SDK's boot is full of loops that wait for a bit to
 * change. Some of those exit on zero by luck; the ones that do not spin
 * forever, and a spin is indistinguishable from a hang. The two that matter
 * most are the video interface's line counter, which the SDK polls to wait
 * for retrace, and the DSP's mailbox handshake - both need a value that
 * MOVES, not a value that is merely plausible.
 *
 * Registers we do not model return their last written value. That is a better
 * default than zero: a great deal of SDK code writes a register and reads it
 * back to confirm, and honouring that costs nothing and removes a whole class
 * of false stalls.
 */
#ifndef MGS_MMIO_H
#define MGS_MMIO_H

#include <stdint.h>

/* Hardware register blocks, as the SDK's own headers name them. */
#define MMIO_BASE      0xCC000000u
#define MMIO_CP        0xCC000000u   /* command processor */
#define MMIO_PE        0xCC001000u   /* pixel engine */
#define MMIO_VI        0xCC002000u   /* video interface */
#define MMIO_PI        0xCC003000u   /* processor interface */
#define MMIO_MI        0xCC004000u   /* memory interface */
#define MMIO_DSP       0xCC005000u   /* DSP and ARAM */
#define MMIO_DI        0xCC006000u   /* disc interface */
#define MMIO_SI        0xCC006400u   /* serial: controllers */
#define MMIO_EXI       0xCC006800u   /* external: memory cards */
#define MMIO_AI        0xCC006C00u   /* audio streaming */
#define MMIO_WGPIPE    0xCC008000u   /* write-gather pipe: the GX FIFO */
#define MMIO_END       0xCC009000u

typedef struct MgsMmio {
    /* One flat store for the whole region. Modelled registers are special
     * cased on access; everything else reads back what was written. */
    uint8_t  regs[MMIO_END - MMIO_BASE];

    /* The video interface's beam position. Advanced by the frame loop, not by
     * reads, so a guest that polls it sees time pass at the rate the host is
     * actually running rather than as fast as it can spin. */
    uint16_t vi_half_line;

    /* GX command FIFO bytes written through the write-gather pipe. Counted
     * rather than stored: phase 3 will consume them, and until then knowing
     * the game is producing commands is the useful signal. */
    uint64_t wgpipe_bytes;

    uint64_t reads, writes;
} MgsMmio;

void     mgs_mmio_init(MgsMmio* m);
uint32_t mgs_mmio_read(MgsMmio* m, uint32_t addr, unsigned size);
void     mgs_mmio_write(MgsMmio* m, uint32_t addr, uint32_t value, unsigned size);

/* Called once per frame by the host, so polled hardware state advances with
 * real time rather than with how fast the guest spins. */
void     mgs_mmio_tick_frame(MgsMmio* m);

#endif
