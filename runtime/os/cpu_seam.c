/* The seam between the SDK shims and the recompiled code's CPU state.
 *
 * Every shim reads its arguments from guest registers and writes its result
 * back, so something has to say where those registers live. That is the only
 * thing the runtime needs to know about the recompiled code, and keeping it
 * to one file is deliberate: it is the whole of the coupling.
 *
 * Two modes, and the split matters:
 *
 *   BOUND    a recompiled module is loaded and the runtime points at ITS
 *            register file, so a shim reads exactly what the translated code
 *            just passed.
 *
 *   FREE     no module: the runtime owns a register file of its own. That is
 *            what lets the shims be tested, and the host be run, without a
 *            recompiled module present - which is the difference between a
 *            runtime that can be developed and one that can only be
 *            integrated.
 *
 * The bound case takes a pointer and a stride rather than a struct, so this
 * file never includes DolRecomp's generated headers. See HANDOFF F2: the
 * generated code's CPU header is ours to choose, and choosing it here rather
 * than inheriting it is what keeps the runtime independent.
 */
#include "os_runtime.h"

#include <string.h>

#define MGS_GPR_COUNT 32u

typedef struct CpuSeam {
    uint32_t  own[MGS_GPR_COUNT];   /* FREE mode */
    uint32_t* bound;                /* BOUND mode: into the module's state */
    uint32_t  own_msr;              /* FREE mode */
    uint32_t* bound_msr;            /* BOUND mode */
    uint32_t* bound_lr;             /* BOUND mode; FREE mode has no caller */
} CpuSeam;

static CpuSeam s_seam;
static MgsRuntime* s_current;

void mgs_cpu_bind_registers(uint32_t* gpr_array);
void mgs_cpu_bind_msr(uint32_t* msr);
void mgs_cpu_bind_lr(uint32_t* lr);
void mgs_cpu_unbind(void);
void mgs_runtime_set_current(MgsRuntime* rt);

/* Point the seam at a loaded module's register file. */
void mgs_cpu_bind_registers(uint32_t* gpr_array) { s_seam.bound = gpr_array; }

/* MSR is bound separately because it is not part of the register file, and
 * the HOST knows where it lives - the runtime deliberately does not. Without
 * this the interrupt shims would keep a flag of their own, and the host's
 * "may I deliver an interrupt?" gate would be reading a different variable
 * from the one the guest just wrote. They must be the same bit. */
void mgs_cpu_bind_msr(uint32_t* msr) { s_seam.bound_msr = msr; }

/* The link register, bound like the MSR and for the same reason: the HOST
 * knows where it lives and the runtime deliberately does not. Reads as zero
 * in FREE mode, where there is no translated caller to name. */
void mgs_cpu_bind_lr(uint32_t* lr) { s_seam.bound_lr = lr; }

uint32_t mgs_guest_lr(void)
{
    return s_seam.bound_lr ? *s_seam.bound_lr : 0u;
}

void mgs_cpu_unbind(void)
{
    s_seam.bound = NULL; s_seam.bound_msr = NULL; s_seam.bound_lr = NULL;
}

uint32_t mgs_cpu_msr(void)
{
    return s_seam.bound_msr ? *s_seam.bound_msr : s_seam.own_msr;
}

void mgs_cpu_set_msr(uint32_t value)
{
    if (s_seam.bound_msr) *s_seam.bound_msr = value;
    else                  s_seam.own_msr = value;
}

/* The runtime the shims act on. Set once at startup rather than threaded
 * through every shim, so each shim's signature can stay the SDK's - which is
 * what makes the patch table checkable against the real API by eye.
 */
void mgs_runtime_set_current(MgsRuntime* rt) { s_current = rt; }

MgsRuntime* mgs_runtime_from(CPUState* ctx)
{
    (void)ctx;      /* the current runtime is global; ctx is the caller's */
    return s_current;
}

uint32_t mgs_guest_gpr(const MgsRuntime* rt, unsigned index)
{
    (void)rt;
    if (index >= MGS_GPR_COUNT) return 0u;
    return s_seam.bound ? s_seam.bound[index] : s_seam.own[index];
}

void mgs_set_guest_gpr(MgsRuntime* rt, unsigned index, uint32_t value)
{
    (void)rt;
    if (index >= MGS_GPR_COUNT) return;
    if (s_seam.bound) s_seam.bound[index] = value;
    else              s_seam.own[index] = value;
}
