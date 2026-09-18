/* Module export glue, extended to declare the REL.
 *
 * Derived from ModernGekko's vendor/dolphin/module-template/module_export.c
 * at edd460a, with one change: the descriptor's trailing rel_modules and
 * num_rel_modules fields are populated. Upstream leaves them zero because its
 * toolchain only ever builds main.dol.
 *
 * Twin Snakes keeps 92% of its code in mgso_pal.rel, and the runtime needs
 * the REL's linked layout to route execution into it.
 */
// RecompCore per-game native module export glue (game id set at build time).
//
// Wraps the DolRecomp-generated constant-time chunk dispatcher behind the
// StaticRecomp module ABI. All environment access goes through the CPUState
// hook pointers the chassis installs; this dylib has no host dependencies.

#include "generated.h"
#include "moderngekko/module_abi.h"

#include "StaticRecompABI.h"

#if defined(DOLRECOMP_MODULE_HAVE_X86_64_V3) && defined(_MSC_VER)
#include <intrin.h>
#elif defined(DOLRECOMP_MODULE_HAVE_X86_64_V3) && defined(__x86_64__) && \
    (defined(__GNUC__) || defined(__clang__))
#include <cpuid.h>
#endif

#if defined(DOLRECOMP_MODULE_HAVE_X86_64_V3)
static int host_has_x86_64_v3(void)
{
#if defined(_M_X64) && defined(_MSC_VER)
    int leaf[4];
    __cpuid(leaf, 1);
    const unsigned int leaf1_ecx = (unsigned int)leaf[2];
    const unsigned int leaf1_required = (1u << 12) | (1u << 22) | (1u << 27) |
        (1u << 28) | (1u << 29);
    if ((leaf1_ecx & leaf1_required) != leaf1_required ||
        (_xgetbv(0) & 6u) != 6u)
        return 0;
    __cpuidex(leaf, 7, 0);
    if (((unsigned int)leaf[1] & ((1u << 3) | (1u << 5) | (1u << 8))) !=
        ((1u << 3) | (1u << 5) | (1u << 8)))
        return 0;
    __cpuid(leaf, (int)0x80000001u);
    return ((unsigned int)leaf[2] & (1u << 5)) != 0;
#elif defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    static int supported = -1;
    if (supported < 0)
    {
        unsigned int eax, ebx, ecx, edx;
        supported = __get_cpuid(1, &eax, &ebx, &ecx, &edx) != 0;
        const unsigned int leaf1_required = (1u << 12) | (1u << 22) | (1u << 27) |
            (1u << 28) | (1u << 29);
        supported = supported && (ecx & leaf1_required) == leaf1_required;
        if (supported)
        {
            unsigned int xcr0_low;
            unsigned int xcr0_high;
            __asm__ volatile("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0));
            supported = (xcr0_low & 6u) == 6u;
        }
        supported = supported && __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) != 0 &&
            (ebx & ((1u << 3) | (1u << 5) | (1u << 8))) ==
                ((1u << 3) | (1u << 5) | (1u << 8));
        supported = supported && __get_cpuid(0x80000001u, &eax, &ebx, &ecx, &edx) != 0 &&
            (ecx & (1u << 5)) != 0;
    }
    return supported;
#else
    return 0;
#endif
}
#endif

static int selected_dispatch(CPUState* ctx, u32 address)
{
#if defined(DOLRECOMP_MODULE_HAVE_X86_64_V3)
    if (host_has_x86_64_v3())
        return dolrecomp_call__x86_64_v3(ctx, address);
#endif
    return dolrecomp_call(ctx, address);
}

void dolrecomp_indirect_dispatch(CPUState* ctx, u32 address)
{
    (void)selected_dispatch(ctx, address);
}

static int chassis_dispatch(CPUState* ctx, u32 address)
{
    return selected_dispatch(ctx, address);
}

static void chassis_on_state_loaded(CPUState* ctx)
{
    // Re-arm host FP rounding/flush state from the freshly loaded guest FPSCR.
    ppc_fpscr_updated(ctx);
}

#include "module_tables.inc"

static const StaticRecompModuleDesc s_desc = {
    STATICRECOMP_ABI_VERSION,
    GXRUNTIME_CPU_ABI_VERSION,
    (u32)sizeof(CPUState),
    MODULE_GAME_ID,
    DOLRECOMP_ENTRY_POINT,
    chassis_dispatch,
    chassis_on_state_loaded,
    s_code_ranges,
    MODULE_CODE_RANGE_COUNT,
    s_smc_ranges,
    MODULE_SMC_RANGE_COUNT,
    s_chunk_ranges,
    MODULE_CHUNK_RANGE_COUNT,
    s_chunk_hashes,
    s_rel_modules,
    MODULE_REL_MODULE_COUNT,
};

#if defined(_WIN32)
#define RECOMP_MODULE_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define RECOMP_MODULE_EXPORT __attribute__((visibility("default")))
#else
#define RECOMP_MODULE_EXPORT
#endif

RECOMP_MODULE_EXPORT const StaticRecompModuleDesc* staticrecomp_get_module(void)
{
    return &s_desc;
}
