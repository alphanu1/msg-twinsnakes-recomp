/* Special-purpose registers, handled by the host.
 *
 * DolRecomp does not translate SPR access. It emits ppc_fallback_instruction
 * and leaves the instruction to whoever is hosting, which is the right
 * division: an SPR is processor state, and what it means depends entirely on
 * the machine underneath.
 *
 * Without this a boot stops two instructions in, inside ICFlashInvalidate,
 * with an illegal-instruction exception - the fallback's default when no host
 * handler is installed.
 *
 * Note this catches what the PATCH TABLE cannot. A patched SDK function is
 * only intercepted when the guest reaches it through dispatch; a call inside
 * the same generated chunk compiles to a plain goto and never consults the
 * hook. ICFlashInvalidate is called that way from __OSPSInit, which is why
 * patching it alone did not help. Handling the instruction works wherever the
 * call came from.
 */
#include "module.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Gekko SPR numbers. */
#define SPR_LR     8
#define SPR_CTR    9
#define SPR_GQR0 912
#define SPR_GQR7 919
#define SPR_HID2 920
#define SPR_WPAR 921
#define SPR_HID0 1008
#define SPR_HID1 1009
#define SPR_L2CR 1017

/* Processor state with no host equivalent. Remembered rather than discarded,
 * because the SDK reads several of these back to confirm a write took - HID2
 * especially, which is how it checks paired singles are enabled.
 */
static uint32_t s_spr[1024];
static unsigned long s_handled, s_unknown;

static uint32_t* gpr_of(void* cpu) { return mgs_module_gpr(cpu); }

/* The SPR field in mfspr/mtspr is 10 bits, stored with its halves SWAPPED:
 * bits 11-15 hold the low five and bits 16-20 the high five. Decoding it as a
 * plain 10-bit number reads HID0 (1008) as 63 and silently touches the wrong
 * register.
 */
static unsigned decode_spr(uint32_t insn)
{
    unsigned field = (insn >> 11) & 0x3FFu;
    return ((field & 0x1Fu) << 5) | ((field >> 5) & 0x1Fu);
}

/* Completing an instruction means ADVANCING PAST IT. The translated code
 * calls the fallback and returns to the host with pc still pointing at the
 * instruction it could not handle, so a handler that only performs the effect
 * leaves the host re-dispatching the same address forever. That looks
 * identical to the guest spinning, which is exactly how it presented.
 */
static void complete(void* cpu, uint32_t cia)
{
    mgs_module_set_pc(cpu, cia + 4u);
}

static void host_instruction_fallback(void* cpu, uint32_t insn, uint32_t cia)
{
    unsigned opcode = (insn >> 26) & 0x3Fu;
    unsigned xo     = (insn >> 1) & 0x3FFu;
    unsigned rd     = (insn >> 21) & 0x1Fu;
    unsigned spr    = decode_spr(insn);
    uint32_t* gpr   = gpr_of(cpu);

    if (opcode == 31u && xo == 339u) {          /* mfspr rD, SPR */
        gpr[rd] = s_spr[spr];
        ++s_handled; complete(cpu, cia);
        return;
    }
    if (opcode == 31u && xo == 467u) {          /* mtspr SPR, rS */
        s_spr[spr] = gpr[rd];
        ++s_handled; complete(cpu, cia);
        return;
    }
    if (opcode == 31u && xo == 598u) {          /* sync */
        ++s_handled; complete(cpu, cia);
        return;
    }
    if (opcode == 31u && (xo == 470u || xo == 54u || xo == 86u ||
                          xo == 1014u || xo == 982u || xo == 246u)) {
        /* dcbi, dcbst, dcbf, dcbz, icbi, dcbtst: cache maintenance. The host
         * has one coherent memory and no guest instruction cache, so the
         * coherence these buy is already guaranteed. */
        ++s_handled; complete(cpu, cia);
        return;
    }

    /* Anything else is a genuine gap. Report it once with enough detail to
     * act on - the raw encoding and where it was - rather than silently
     * doing nothing, which would corrupt guest state invisibly.
     */
    ++s_unknown;
    complete(cpu, cia);   /* step over it, so one gap does not become a hang */
    if (s_unknown <= 8ul)
        fprintf(stderr, "  unhandled instruction 0x%08X at 0x%08X "
                        "(opcode %u, xo %u)\n", insn, cia, opcode, xo);
}

#define CPU_INSTRUCTION_FALLBACK 3432u

void mgs_host_install_spr_handler(void* cpu)
{
    void (*fn)(void*, uint32_t, uint32_t) = host_instruction_fallback;
    memcpy((uint8_t*)cpu + CPU_INSTRUCTION_FALLBACK, &fn, sizeof fn);

    /* HID0 and HID2 come out of reset with the cache and paired singles
     * already enabled on a GameCube; the SDK reads them, sets bits and writes
     * back. Starting at zero is not wrong, but starting where hardware starts
     * means the values the game reads back are the ones it expects. */
    s_spr[SPR_HID0] = 0x0011C464u;
    s_spr[SPR_HID2] = 0xA0000000u;   /* LSQE | PSE */
}

unsigned long mgs_host_spr_handled(void) { return s_handled; }
unsigned long mgs_host_spr_unknown(void) { return s_unknown; }
