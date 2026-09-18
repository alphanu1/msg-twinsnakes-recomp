/* Shared declarations for the cross-module dispatch glue.
 *
 * Deliberately does NOT include either module's generated.h: both define
 * dolrecomp_find_original and dolrecomp_call_original as static inline over
 * their own chunk tables, so a file that saw both would get one table
 * silently shadowing the other.
 */
#ifndef MGS_MODULE_GLUE_H
#define MGS_MODULE_GLUE_H

#include "cpu/cpu.h"

/* Each defined in its own translation unit, over that module's table. */
int mgs_dol_call(CPUState* ctx, u32 address);
int mgs_rel_call(CPUState* ctx, u32 address);

/* DolRecomp's extension point, called from dolrecomp_call before the calling
 * module's own table. Live only when DOLRECOMP_ENABLE_REPLACEMENTS is defined.
 */
int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address);

#endif
