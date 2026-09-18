/* Cross-module dispatch: route a guest address to whichever recompiled
 * module owns it.
 *
 * DolRecomp gives each module its own address table and no knowledge of any
 * other. Twin Snakes needs two - main.dol and mgso_pal.rel - and they call
 * each other constantly: the engine lives in the REL and every SDK function
 * it uses lives in the DOL.
 *
 * dolrecomp_call consults dolrecomp_dispatch_replacement before its own
 * table, so this is where the two modules are joined.
 */
#include "module_glue.h"

/* Recompiled REL code is emitted at DolRecomp's REL_AUTO_BASE. The extent is
 * the REL's .text size, from `dtk rel info`: 0x456400 bytes at 0x805000EC.
 * Bounding it matters - an unbounded "anything high is the REL" test would
 * swallow addresses the REL does not own and report a false hit, and a false
 * hit returns 1, which tells the caller the call was handled.
 */
#define MGS_REL_TEXT_BASE 0x805000ECu
#define MGS_REL_TEXT_SIZE 0x00456400u

/* main.dol's .init starts at 0x80003100; .text ends at 0x80062050. */
#define MGS_DOL_TEXT_BASE 0x80003100u
#define MGS_DOL_TEXT_END  0x80062050u

int dolrecomp_dispatch_replacement(CPUState* ctx, u32 address)
{
    if (address - MGS_REL_TEXT_BASE < MGS_REL_TEXT_SIZE)
        return mgs_rel_call(ctx, address);

    if (address >= MGS_DOL_TEXT_BASE && address < MGS_DOL_TEXT_END)
        return mgs_dol_call(ctx, address);

    /* Not ours. Returning 0 lets the calling module try its own table, then
     * the host, then the interpreter - all of which are correct answers.
     */
    return 0;
}
