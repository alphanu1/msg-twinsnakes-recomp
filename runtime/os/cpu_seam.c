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
} CpuSeam;

static CpuSeam s_seam;
static MgsRuntime* s_current;

void mgs_cpu_bind_registers(uint32_t* gpr_array);
void mgs_cpu_unbind(void);
void mgs_runtime_set_current(MgsRuntime* rt);

/* Point the seam at a loaded module's register file. */
void mgs_cpu_bind_registers(uint32_t* gpr_array) { s_seam.bound = gpr_array; }
void mgs_cpu_unbind(void) { s_seam.bound = NULL; }

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
