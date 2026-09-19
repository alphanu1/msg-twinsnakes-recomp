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

#include "../dsp/aram.h"

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

typedef void (*MgsFifoSink)(void* user, uint32_t value, unsigned size);

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

    MgsFifoSink fifo_sink;
    void*       fifo_user;

    uint64_t reads, writes;

    /* MGS_TRACE_FIFOREG: writes to the CPU- and GP-side FIFO descriptions. */
    int      trace_fiforeg;

    /* Audio RAM. The boot does not get past ARInit without it. */
    MgsAram  aram;

    /* Guest ticks seen, for the audio interface's sample counter. Kept as
     * ticks rather than samples so the counter can be derived at whatever
     * rate the control register currently selects, which is the thing
     * __AI_SRC_INIT is trying to measure. */
    uint64_t ai_ticks;

    /* Per-register read counts, for finding a poll that never ends. A guest
     * waiting on hardware is indistinguishable from a guest doing work when
     * all you have is a total - and "33 million reads" was the only signal
     * that the boot had stalled at all. Indexed by halfword so the table is
     * small enough to scan. */
    uint32_t read_hist[(MMIO_END - MMIO_BASE) / 2u];
} MgsMmio;

void     mgs_mmio_init(MgsMmio* m);
uint32_t mgs_mmio_read(MgsMmio* m, uint32_t addr, unsigned size);
void     mgs_mmio_write(MgsMmio* m, uint32_t addr, uint32_t value, unsigned size);

/* Called once per frame by the host, so polled hardware state advances with
 * real time rather than with how fast the guest spins. */
void     mgs_mmio_tick_frame(MgsMmio* m);

/* A device's interrupt line, not a latch the host owns.
 *
 * PI's status register mirrors which devices are ASSERTING an interrupt
 * right now. A device drops its line when the guest acknowledges it at the
 * device - the VI handler clears the display-interrupt INT bits, the PE
 * finish handler writes bit 3 of the pixel engine's control register. Until
 * that modelling existed here the host set PI's VI bit once and never
 * cleared it, so every dispatch found VI asserted, VI outranks the pixel
 * engine in the SDK's priority table, and the PE finish interrupt was never
 * the highest-priority pending source. The game slept on GXDrawDone with the
 * interrupt that would have woken it permanently queued behind retrace.
 *
 * mgs_mmio_assert_retrace raises the display interrupts a retrace asserts,
 * which is what makes the SDK's handler take its retrace path rather than
 * its position-callback path. */
void     mgs_mmio_assert_retrace(MgsMmio* m);

/* Where the command stream goes. The MMIO layer recognises the handful of
 * registers it must ACT on and forwards every byte to this, which is the
 * graphics side's business. Set it before the guest starts drawing; left
 * unset, the stream is counted and discarded, which is what happened before
 * there was a renderer. */
void     mgs_mmio_set_fifo_sink(MgsMmio* m, MgsFifoSink sink, void* user);

/* Where the video interface is scanning from, as a guest address, or 0 if
 * the game has not programmed it yet. */
uint32_t mgs_mmio_xfb_address(const MgsMmio* m);

/* ---- the two FIFO descriptions ----------------------------------------
 *
 * There are two, and which one the write-gather pipe is feeding decides
 * whether the guest is DRAWING or RECORDING a display list.
 *
 *   CPU side, in PI:  +0x0C base, +0x10 end, +0x14 write pointer.
 *   GP side,  in CP:  +0x3C and +0x3E, the halves of the base address.
 *
 * `GXBeginDisplayList` points the CPU side at a buffer in main memory and
 * leaves the GP side alone, so the same stores to 0xCC008000 land in that
 * buffer instead of being executed. A host that sends every pipe write to
 * the command parser executes the recording - with whatever vertex format
 * happens to be live rather than the one the list will be called under.
 *
 * Observed directly: base 0x81791C60, end 0x8179E45C (the 51,200 bytes the
 * game asked for), then restored to 0x80450160, which is exactly what the
 * CP side holds. */
#define PI_FIFO_BASE   0x0Cu
#define PI_FIFO_END    0x10u
#define PI_FIFO_WRPTR  0x14u
#define CP_FIFO_BASE_L 0x3Cu
#define CP_FIFO_BASE_H 0x3Eu

uint32_t mgs_mmio_cpu_fifo_base(const MgsMmio* m);
uint32_t mgs_mmio_cpu_fifo_end(const MgsMmio* m);
uint32_t mgs_mmio_cpu_fifo_wrptr(const MgsMmio* m);
void     mgs_mmio_set_cpu_fifo_wrptr(MgsMmio* m, uint32_t v);
uint32_t mgs_mmio_gp_fifo_base(const MgsMmio* m);

/* Non-zero when the pipe is feeding a display-list buffer rather than the
 * graphics processor - that is, when the two descriptions disagree. */
int      mgs_mmio_recording(const MgsMmio* m);

/* Give the DSP interface the memory its DMA moves data to and from. Until
 * this is called the ARAM registers still read back sensibly, but a transfer
 * does nothing - which is the right behaviour for a runtime test that has no
 * guest memory to speak of. */
void     mgs_mmio_attach_aram(MgsMmio* m, GuestMemory* mem);

/* Tell the audio interface how much guest time has passed.
 *
 * `__AI_SRC_INIT` starts the interface, waits for the sample counter at
 * 0xCC006C08 to change, and times how long that took with OSGetTime - it is
 * CALIBRATING, so the counter has to advance in the right proportion to the
 * guest's own clock, not merely advance. Driving it from the same tick
 * source the timebase uses is what makes the ratio come out right; driving
 * it from the frame tick would make the boot measure an audio clock about
 * ten times too fast. */
void     mgs_mmio_advance_ticks(MgsMmio* m, uint32_t ticks);

/* Print the registers the guest read most, most-read first. */
void     mgs_mmio_report_hot(const MgsMmio* m, unsigned top);

#endif
