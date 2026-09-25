/* The CPU header the generated chunks include (DOLRECOMP_CPU_HEADER).
 *
 * GXRuntime's own accessors, plus one thing they lack: an inlined path for
 * the second window, 0x7E000000-0x7FFFFFFF.
 *
 * WHY. The engine overlay lives there - its code (which is recompiled and
 * never read as data), its .data and its constants. GXRuntime's inline
 * accessors map MEM1 only, so every engine data access fell through to
 * `cpu->external_read`: an indirect call into the host, a range check, and a
 * byte loop. Ben's run shows 1.59 billion reads and 552 million writes taking
 * that route. The engine's .bss moved into MEM1 with --rel-bss (HANDOFF F355),
 * which takes most of it off that path; this takes the rest.
 *
 * The window is one flat 32 MB host buffer (runtime/memory/guest.h), so the
 * fast path is a subtract and a compare. MEM1 is still tried first, so
 * nothing about ordinary memory changes. The host installs the buffer through
 * `mgs_dispatch_set_vmem`; until it does - or when it leaves it unset because
 * a diagnostic wants to see every window access - the old route is taken.
 *
 * Generated code is not edited: this header is selected by DolRecomp's own
 * override hook, and the accessors below are substituted by name for the
 * chunks alone. GXRuntime's internal helpers keep calling its own versions.
 */
#ifndef MGS_CPU_H
#define MGS_CPU_H

#include "core/cpu.h"

extern u8* g_mgs_vmem;

#define MGS_VMEM_BASE 0x7E000000u
#define MGS_VMEM_SIZE 0x02000000u

#if defined(__GNUC__) || defined(__clang__)
#define MGS_INLINE static inline __attribute__((always_inline))
#else
#define MGS_INLINE static inline
#endif

MGS_INLINE u8* mgs_vmem_ptr(u32 addr, u32 size)
{
    u32 off = addr - MGS_VMEM_BASE;
    return (g_mgs_vmem && off <= MGS_VMEM_SIZE - size) ? g_mgs_vmem + off : NULL;
}

#define MGS_READ(bits, T, rd)                                               \
MGS_INLINE T mgs_mem_read##bits(CPUState* cpu, u32 addr)                    \
{                                                                           \
    u8* p = get_ram_ptr(cpu, addr, (bits) / 8u, NULL);                      \
    if (p) return rd;                                                       \
    p = mgs_vmem_ptr(addr, (bits) / 8u);                                    \
    if (p) return rd;                                                       \
    if (cpu->external_read)                                                 \
        return (T)cpu->external_read(cpu, addr, (bits) / 8u);               \
    return 0;                                                               \
}

MGS_READ(8,  u8,  *p)
MGS_READ(16, u16, read_be16(p))
MGS_READ(32, u32, read_be32(p))
MGS_READ(64, u64, read_be64(p))

#define MGS_WRITE(bits, T, wr)                                              \
MGS_INLINE void mgs_mem_write##bits(CPUState* cpu, u32 addr, T value)       \
{                                                                           \
    u32 offset;                                                             \
    u8* p = get_ram_ptr(cpu, addr, (bits) / 8u, &offset);                   \
    if (p) {                                                                \
        clear_matching_reservation(cpu, addr);                              \
        if (g_mem_write_journal && offset != (u32)-1)                       \
            g_mem_write_journal(offset, (bits) / 8u,                        \
                                g_mem_write_journal_user);                  \
        wr;                                                                 \
        return;                                                             \
    }                                                                       \
    p = mgs_vmem_ptr(addr, (bits) / 8u);                                    \
    if (p) {                                                                \
        clear_matching_reservation(cpu, addr);                              \
        wr;                                                                 \
        return;                                                             \
    }                                                                       \
    if (cpu->external_write)                                                \
        cpu->external_write(cpu, addr, value, (bits) / 8u);                 \
}

MGS_WRITE(8,  u8,  *p = value)
MGS_WRITE(16, u16, write_be16(p, value))
MGS_WRITE(32, u32, write_be32(p, value))
MGS_WRITE(64, u64, write_be64(p, value))

#undef MGS_READ
#undef MGS_WRITE

/* GXRuntime's inlined paired-single load and store call ITS accessors, not
 * the names below - they are defined inside its header, before this file can
 * rename anything - so an engine constant loaded with psq_l still took the
 * host route. The same fast path, over these accessors; quantised types and
 * the illegal-instruction check still go to GXRuntime's out-of-line version,
 * exactly as its own inline does. */
MGS_INLINE bool mgs_psq_load_inline(CPUState* cpu, u8 frD, u32 ea, bool w,
                                    u8 gqr_index, bool indexed, u32 cia)
{
    const u32 gqr = cpu->gqr[gqr_index & 7u];
    if (((gqr >> 16) & 7u) == 0u &&
        (indexed || (cpu->hid2 & PPC_HID2_LSQE) != 0u)) {
        cpu->fpr[frD] = f64_value(convert_to_double(mgs_mem_read32(cpu, ea)));
        cpu->ps1[frD] = w ? 1.0
                          : f64_value(convert_to_double(mgs_mem_read32(cpu, ea + 4u)));
        return true;
    }
    return ppc_psq_load(cpu, frD, ea, w, gqr_index, indexed, cia);
}

MGS_INLINE bool mgs_psq_store_inline(CPUState* cpu, u8 frS, u32 ea, bool w,
                                     u8 gqr_index, bool indexed, u32 cia)
{
    const u32 gqr = cpu->gqr[gqr_index & 7u];
    if ((gqr & 7u) == 0u && (indexed || (cpu->hid2 & PPC_HID2_LSQE) != 0u)) {
        mgs_mem_write32(cpu, ea, convert_to_single_ftz(f64_bits(cpu->fpr[frS])));
        if (!w)
            mgs_mem_write32(cpu, ea + 4u,
                            convert_to_single_ftz(f64_bits(cpu->ps1[frS])));
        return true;
    }
    return ppc_psq_store(cpu, frS, ea, w, gqr_index, indexed, cia);
}

/* The same for the float-available check that precedes EVERY guest float
 * instruction. GXRuntime marks it `static inline`, and GCC still emitted it
 * out of line in the largest chunks - their size exhausts its inlining
 * budget - so a one-bit test cost a call per float instruction. */
MGS_INLINE bool mgs_fp_available_inline(CPUState* cpu, u32 cia)
{
    if (!g_ppc_lazy_fp_enabled || (cpu->msr & PPC_MSR_FP))
        return true;
    return ppc_fp_raise_unavailable(cpu, cia);
}

#define ppc_fp_available_inline mgs_fp_available_inline
#define ppc_psq_load_inline  mgs_psq_load_inline
#define ppc_psq_store_inline mgs_psq_store_inline

#define mem_read8   mgs_mem_read8
#define mem_read16  mgs_mem_read16
#define mem_read32  mgs_mem_read32
#define mem_read64  mgs_mem_read64
#define mem_write8  mgs_mem_write8
#define mem_write16 mgs_mem_write16
#define mem_write32 mgs_mem_write32
#define mem_write64 mgs_mem_write64

#endif
