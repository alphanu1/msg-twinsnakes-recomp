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
#include "platform/mmio.h"

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

/* MSR and the exception save/restore pair, by offset in CPUState. */
#define CPU_MSR_OFFSET   664u
#define CPU_SRR0_OFFSET  668u
#define CPU_SRR1_OFFSET  672u

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


/* Some SPRs are not just remembered - the translated code READS them out of
 * the CPU state to decide what an instruction means, so a shadow copy here is
 * invisible to it.
 *
 *   HID2[PSE] gates every paired-single instruction. Without it the first
 *   psq_st in PSMTXIdentity raises an illegal-instruction program exception,
 *   which is exactly what the hardware does and exactly how this presented.
 *
 *   GQR0-7 carry the scale and type a psq_l/psq_st quantises with. A stale
 *   GQR does not fault; it silently returns wrongly-scaled numbers, which is
 *   worse.
 *
 *   SRR0/SRR1 are the pair `rfi` resumes from. OSLoadContext ends with
 *   mtsrr0/mtsrr1/rfi, so a shadow copy would have every context switch
 *   resume at whatever the host last wrote there instead of the thread the
 *   scheduler chose.
 *
 * So writes to those are mirrored into the fields the generated code reads.
 */
#define SPR_SRR0         26
#define SPR_SRR1         27
#define CPU_HID2_OFFSET  688u
#define CPU_GQR_OFFSET   768u
#define SPR_GQR0         912

static int spr_is_mirrored(unsigned spr)
{
    return spr == SPR_SRR0 || spr == SPR_SRR1 || spr == SPR_HID2 ||
           (spr >= SPR_GQR0 && spr < SPR_GQR0 + 8);
}

static uint32_t spr_offset(unsigned spr)
{
    if (spr == SPR_SRR0) return CPU_SRR0_OFFSET;
    if (spr == SPR_SRR1) return CPU_SRR1_OFFSET;
    if (spr == SPR_HID2) return CPU_HID2_OFFSET;
    return CPU_GQR_OFFSET + (spr - SPR_GQR0) * 4u;
}

static uint32_t spr_read_mirror(void* cpu, unsigned spr)
{
    uint32_t v;
    memcpy(&v, (uint8_t*)cpu + spr_offset(spr), 4);
    return v;
}

static void spr_mirror(void* cpu, unsigned spr, uint32_t value)
{
    if (spr == SPR_SRR0) { memcpy((uint8_t*)cpu + CPU_SRR0_OFFSET, &value, 4); return; }
    if (spr == SPR_SRR1) { memcpy((uint8_t*)cpu + CPU_SRR1_OFFSET, &value, 4); return; }
    if (spr == SPR_HID2) {
        memcpy((uint8_t*)cpu + CPU_HID2_OFFSET, &value, 4);
        return;
    }
    if (spr >= SPR_GQR0 && spr < SPR_GQR0 + 8) {
        memcpy((uint8_t*)cpu + CPU_GQR_OFFSET + (spr - SPR_GQR0) * 4u,
               &value, 4);
    }
}

static void host_instruction_fallback(void* cpu, uint32_t insn, uint32_t cia)
{
    unsigned opcode = (insn >> 26) & 0x3Fu;
    unsigned xo     = (insn >> 1) & 0x3FFu;
    unsigned rd     = (insn >> 21) & 0x1Fu;
    unsigned spr    = decode_spr(insn);
    uint32_t* gpr   = gpr_of(cpu);

    if (opcode == 31u && xo == 339u) {          /* mfspr rD, SPR */
        gpr[rd] = spr_is_mirrored(spr) ? spr_read_mirror(cpu, spr) : s_spr[spr];
        ++s_handled; complete(cpu, cia);
        return;
    }
    if (opcode == 31u && xo == 467u) {          /* mtspr SPR, rS */
        s_spr[spr] = gpr[rd];
        spr_mirror(cpu, spr, gpr[rd]);
        ++s_handled; complete(cpu, cia);
        return;
    }
    /* mtmsr / mfmsr. Not SPR access - MSR has its own instructions - but the
     * same story: DolRecomp does not translate them, so without this the
     * SDK's own attempt to enable floating point does nothing, and the next
     * floating-point instruction traps to the FP-unavailable vector far from
     * the cause.
     */
    if (opcode == 31u && xo == 146u) {          /* mtmsr rS */
        memcpy((uint8_t*)cpu + CPU_MSR_OFFSET, &gpr[rd], 4);
        ++s_handled; complete(cpu, cia);
        return;
    }
    if (opcode == 31u && xo == 83u) {           /* mfmsr rD */
        memcpy(&gpr[rd], (uint8_t*)cpu + CPU_MSR_OFFSET, 4);
        ++s_handled; complete(cpu, cia);
        return;
    }
    if (opcode == 19u && xo == 50u) {           /* rfi */
        /* Return from interrupt: resume at srr0 with MSR from srr1. Both
         * halves, or floating point stays disabled after the first
         * exception. */
        uint32_t srr0, srr1;
        memcpy(&srr0, (uint8_t*)cpu + CPU_SRR0_OFFSET, 4);
        memcpy(&srr1, (uint8_t*)cpu + CPU_SRR1_OFFSET, 4);
        memcpy((uint8_t*)cpu + CPU_MSR_OFFSET, &srr1, 4);
        mgs_module_set_pc(cpu, srr0);
        ++s_handled;
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
/* DolRecomp routes any access outside RAM through these. Without them every
 * hardware register reads as zero, and the SDK's boot is full of loops
 * waiting for a bit to change - some exit on zero by luck, the rest spin. */
/* The second addressable window; see runtime/memory/guest.h. */
#define CPU_PC_OFFSET_LOCAL      640u
#define MGS_VMEM_BASE            0x7E000000u
#define MGS_VMEM_SIZE            (32u * 1024u * 1024u)

#define CPU_EXTERNAL_READ        3400u
#define CPU_EXTERNAL_WRITE       3408u

static MgsMmio s_mmio;

MgsMmio* mgs_host_mmio(void);
MgsMmio* mgs_host_mmio(void) { return &s_mmio; }

/* Everything the recompiled code cannot reach through its own RAM pointer.
 *
 * Two quite different things arrive here. Hardware registers at 0xCC000000,
 * which is what this was written for - and the second memory window at
 * 0x7E000000, which is ordinary RAM that the generated code's fast path
 * simply does not know about: it recognises MEM1 at 0x80000000 and MEM2 at
 * 0x90000000, and nothing else. Routing the window through here is what
 * makes it real memory rather than a hole that reads as zero.
 *
 * The signatures are the runtime's, exactly: u64 return, u8 size. They were
 * u32/unsigned before, which worked only because nothing had yet done a
 * 64-bit access to a non-RAM address - a `lfd` into the window would have
 * returned half a value.
 */
/* The second window's backing store, handed over by the host once guest
 * memory exists. Held here rather than reached through the runtime so this
 * file keeps its single dependency: the CPU state's layout. */
static uint8_t* s_vmem;

void mgs_host_set_vmem(uint8_t* vmem);
void mgs_host_set_vmem(uint8_t* vmem) { s_vmem = vmem; }

/* Traffic into the window, so "nothing is there" can be told apart from
 * "something wrote there and we read it back wrong". */
static uint64_t s_vmem_reads, s_vmem_writes;
static uint32_t s_vmem_lo = 0xFFFFFFFFu, s_vmem_hi;

uint64_t mgs_host_vmem_reads(void);
uint64_t mgs_host_vmem_reads(void) { return s_vmem_reads; }
uint64_t mgs_host_vmem_writes(void);
uint64_t mgs_host_vmem_writes(void) { return s_vmem_writes; }
uint32_t mgs_host_vmem_lo(void);
uint32_t mgs_host_vmem_lo(void) { return s_vmem_lo; }
uint32_t mgs_host_vmem_hi(void);
uint32_t mgs_host_vmem_hi(void) { return s_vmem_hi; }

static uint8_t* vmem_ptr(uint32_t addr, unsigned size)
{
    uint32_t off = addr - MGS_VMEM_BASE;
    if (!s_vmem || off >= MGS_VMEM_SIZE || size > MGS_VMEM_SIZE - off)
        return NULL;
    return s_vmem + off;
}

static uint64_t host_external_read(void* cpu, uint32_t addr, uint8_t size)
{
    const uint8_t* p;
    uint64_t v = 0u;
    unsigned i;

    (void)cpu;

    p = vmem_ptr(addr, size);
    if (p) {
        ++s_vmem_reads;
        for (i = 0; i < size; ++i) v = (v << 8) | p[i];   /* big-endian */
        return v;
    }
    return mgs_mmio_read(&s_mmio, addr, size);
}

static void host_external_write(void* cpu, uint32_t addr, uint64_t value, uint8_t size)
{
    uint8_t* p;
    unsigned i;

    p = vmem_ptr(addr, size);
    if (p) {
        ++s_vmem_writes;
        if (addr < s_vmem_lo) s_vmem_lo = addr;
        if (addr > s_vmem_hi) s_vmem_hi = addr;
        for (i = 0; i < size; ++i)
            p[i] = (uint8_t)(value >> (8u * (size - 1u - i)));
        return;
    }
    mgs_mmio_write(&s_mmio, addr, (uint32_t)value, size);
}

void mgs_host_install_spr_handler(void* cpu)
{
    void (*fn)(void*, uint32_t, uint32_t) = host_instruction_fallback;
    uint64_t (*rd)(void*, uint32_t, uint8_t) = host_external_read;
    void (*wr)(void*, uint32_t, uint64_t, uint8_t) = host_external_write;

    memcpy((uint8_t*)cpu + CPU_INSTRUCTION_FALLBACK, &fn, sizeof fn);
    memcpy((uint8_t*)cpu + CPU_EXTERNAL_READ, &rd, sizeof rd);
    memcpy((uint8_t*)cpu + CPU_EXTERNAL_WRITE, &wr, sizeof wr);
    mgs_mmio_init(&s_mmio);

    /* HID0 and HID2 come out of reset with the cache and paired singles
     * already enabled on a GameCube; the SDK reads them, sets bits and writes
     * back. Starting at zero is not wrong, but starting where hardware starts
     * means the values the game reads back are the ones it expects. */
    s_spr[SPR_HID0] = 0x0011C464u;
    s_spr[SPR_HID2] = 0xA0000000u;   /* LSQE | PSE */
    spr_mirror(cpu, SPR_HID2, s_spr[SPR_HID2]);
}

unsigned long mgs_host_spr_handled(void) { return s_handled; }
unsigned long mgs_host_spr_unknown(void) { return s_unknown; }
