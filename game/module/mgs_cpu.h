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

#define mem_read8   mgs_mem_read8
#define mem_read16  mgs_mem_read16
#define mem_read32  mgs_mem_read32
#define mem_read64  mgs_mem_read64
#define mem_write8  mgs_mem_write8
#define mem_write16 mgs_mem_write16
#define mem_write32 mgs_mem_write32
#define mem_write64 mgs_mem_write64

#endif
