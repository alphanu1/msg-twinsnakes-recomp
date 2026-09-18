/* Expose the dol module's address table to the dispatch router.
 *
 * One translation unit per module, because each generated.h defines
 * dolrecomp_find_original and dolrecomp_call_original as static inline over
 * its own chunk table.
 *
 * Calls dolrecomp_call_original, the table lookup - NOT dolrecomp_call, which
 * would re-enter dolrecomp_dispatch_replacement and recurse.
 */
/* Set per target: each module has its own generated.h. */
#include MGS_GENERATED_HEADER
#include "module_glue.h"

int mgs_dol_call(CPUState* ctx, u32 address)
{
    return dolrecomp_call_original(ctx, address);
}
