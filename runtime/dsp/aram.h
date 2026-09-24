/* Audio RAM, and the DMA engine that moves data in and out of it.
 *
 * The GameCube has 16 MB of ARAM that the CPU cannot address: everything
 * reaches it by DMA through the DSP interface. The SDK's `ARInit` probes it
 * before anything else can use it, and the boot stops there without it -
 * `__ARChecksize` spins on the ready bit at `__DSPRegs[11]`, which this
 * provides, and then writes and reads back test patterns to discover the
 * size, which needs the DMA to work for real.
 *
 * The register contract is the SDK's own (`ar.c`, `hw_regs.h`), with
 * `__DSPRegs` a `u16*` at 0xCC005000 so index n is at offset 2n:
 *
 *   [5]  0x0A  control/status; bit 0x200 set while a DMA is in flight
 *   [9]  0x12  ARAM size/info
 *   [11] 0x16  ARAM mode; BIT 0 IS THE READY BIT the boot waits on
 *   [13] 0x1A  refresh
 *   [16] 0x20  main-memory address, high 10 bits of a 26-bit address
 *   [17] 0x22  main-memory address, low 16 - the bottom 5 are ignored
 *   [18] 0x24  ARAM address, high
 *   [19] 0x26  ARAM address, low
 *   [20] 0x28  length, high; BIT 0x8000 IS THE DIRECTION - set reads from
 *              ARAM into main memory, clear writes into ARAM
 *   [21] 0x2A  length, low - writing it starts the transfer
 *
 * The transfer completes before the write returns. Nothing in the SDK's use
 * of it can tell the difference: `__ARWaitForDMA` polls the busy bit, which
 * is already clear, and the completion interrupt is raised straight after.
 * Modelling the latency would mean modelling a device nothing here observes.
 */
#ifndef MGS_DSP_ARAM_H
#define MGS_DSP_ARAM_H

#include <stdint.h>

#include "../memory/guest.h"

/* 16 MB, which is what a retail console has and what __ARChecksize expects
 * to find before it starts halving its guess. */
#define MGS_ARAM_SIZE (16u * 1024u * 1024u)

typedef struct MgsAram {
    uint8_t* data;
    GuestMemory* mem;

    uint64_t writes, reads;      /* DMA transfers, not register accesses */
    uint64_t bytes_in, bytes_out;

    /* WHERE the transfers land. A count of transfers says the game is using
     * ARAM; it does not say whether what a voice reads is what the game
     * wrote, which is the question when a mixer reads silence from an
     * address that is inside the store. */
    uint32_t lo_in, hi_in;
    /* Transfers whose main-memory side is in the second window. */
    uint64_t vmem_transfers;
} MgsAram;

int  mgs_aram_init(MgsAram* a, GuestMemory* mem);
void mgs_aram_free(MgsAram* a);

/* Run the transfer the registers describe. `regs` is the DSP register block
 * as bytes, big-endian, indexed from 0xCC005000. */
void mgs_aram_run_dma(MgsAram* a, const uint8_t* regs);

#endif
