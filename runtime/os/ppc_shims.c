/* Processor and cache control.
 *
 * These are the SDK's thin wrappers over Gekko special-purpose registers -
 * HID0, HID2, MSR - and over the cache instructions. DolRecomp does not
 * translate SPR access: it emits ppc_fallback_instruction and defers to an
 * interpreter. A host without one stops there, which is what a boot does two
 * instructions in, inside ICFlashInvalidate.
 *
 * Patching them is not a workaround. It is the design document's central
 * decision applied to the exact case it was made for: the boundary between
 * translated and native is the SDK's public API, and these are SDK functions
 * whose bodies touch hardware that does not exist here.
 *
 * WHY THE CACHE OPERATIONS ARE HONEST NO-OPS. The GameCube has separate
 * instruction and data caches that software must keep coherent by hand: the
 * game flushes the data cache before the GP reads a display list, and
 * invalidates the instruction cache after loading a REL. The host has one
 * coherent memory and no guest instruction cache to invalidate, so the
 * coherence those calls buy is already guaranteed. Doing nothing is the
 * correct emulation, not a shortcut.
 *
 * The one that is NOT a no-op is HID2: the game reads it back to confirm
 * paired singles are enabled, so the value it wrote has to still be there.
 */
#include "os_runtime.h"

/* Gekko HID2 bits, from the SDK's header. */
#define PPC_HID2_LSQE  0x80000000u   /* load/store quantised enable */
#define PPC_HID2_WPE   0x40000000u   /* write pipe enable */
#define PPC_HID2_PSE   0x20000000u   /* paired singles enable */
#define PPC_HID2_LCE   0x10000000u   /* locked cache enable */

static uint32_t s_hid0;
static uint32_t s_hid2;
static uint32_t s_msr;

/* --- HID and MSR: remembered, because the game reads them back ----------- */

void mgs_PPCMfhid0(CPUState* ctx) { mgs_set_guest_gpr(mgs_runtime_from(ctx), 3, s_hid0); }
void mgs_PPCMthid0(CPUState* ctx) { s_hid0 = mgs_guest_gpr(mgs_runtime_from(ctx), 3); }
void mgs_PPCMfhid2(CPUState* ctx) { mgs_set_guest_gpr(mgs_runtime_from(ctx), 3, s_hid2); }
void mgs_PPCMthid2(CPUState* ctx) { s_hid2 = mgs_guest_gpr(mgs_runtime_from(ctx), 3); }
void mgs_PPCMfmsr(CPUState* ctx)  { mgs_set_guest_gpr(mgs_runtime_from(ctx), 3, s_msr); }
void mgs_PPCMtmsr(CPUState* ctx)  { s_msr = mgs_guest_gpr(mgs_runtime_from(ctx), 3); }

/* --- cache control: genuinely nothing to do ----------------------------- */

void mgs_ICFlashInvalidate(CPUState* ctx)
{
    /* Sets HID0[ICFI] on hardware. There is no guest instruction cache. */
    (void)ctx;
}

void mgs_ICEnable(CPUState* ctx)            { (void)ctx; }
void mgs_ICDisable(CPUState* ctx)           { (void)ctx; }
void mgs_ICInvalidateRange(CPUState* ctx)   { (void)ctx; }
void mgs_DCEnable(CPUState* ctx)            { (void)ctx; }
void mgs_DCInvalidateRange(CPUState* ctx)   { (void)ctx; }
void mgs_DCFlushRange(CPUState* ctx)        { (void)ctx; }
void mgs_DCFlushRangeNoSync(CPUState* ctx)  { (void)ctx; }
void mgs_DCStoreRange(CPUState* ctx)        { (void)ctx; }
void mgs_DCStoreRangeNoSync(CPUState* ctx)  { (void)ctx; }
void mgs_DCTouchRange(CPUState* ctx)        { (void)ctx; }
void mgs_L2GlobalInvalidate(CPUState* ctx)  { (void)ctx; }
void mgs_LCDisable(CPUState* ctx)           { (void)ctx; }

/* Speculation and floating-point mode are processor state with no host
 * equivalent; the SDK sets them once at boot and never reads them back.
 */
void mgs_PPCDisableSpeculation(CPUState* ctx)  { (void)ctx; }
void mgs_PPCSetFpNonIEEEMode(CPUState* ctx)    { (void)ctx; }

/* PPCHalt is the idle loop's `while (1) ;`. Returning lets the caller
 * proceed, which is what a host that schedules cooperatively wants: the guest
 * asked to stop until an interrupt, and our frame loop is that interrupt.
 */
void mgs_PPCHalt(CPUState* ctx) { (void)ctx; }
