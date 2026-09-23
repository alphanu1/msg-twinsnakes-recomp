/* The texture environment: how a texel and a vertex colour become a pixel.
 *
 * There is no fixed "modulate" or "decal" in this hardware. GX's named modes
 * are the SDK writing a general combiner, and what reaches the stream is that
 * combiner: up to sixteen stages, each computing
 *
 *     out = (d  op  lerp(a, b, c))  + bias, scaled, optionally clamped
 *
 * on colour and alpha independently, with inputs chosen from previous stage
 * results, four colour registers, the texture sample, the rasterised vertex
 * colour, and constants. Stages chain through those registers, so stage three
 * routinely depends on what stage one left behind.
 *
 * Implementing the combiner rather than the named modes is the only honest
 * option: a game that writes its own stages - and this one does, for its
 * lighting and its screen effects - produces combinations no named mode
 * covers, and a renderer that recognises only the named ones silently draws
 * the wrong thing rather than failing.
 *
 * Fixed-point throughout, in the hardware's own 0-255 range with the
 * intermediate headroom it allows, because the clamping behaviour at the
 * edges is visible and is not what floating point would do.
 */
#ifndef MGS_GX_TEV_H
#define MGS_GX_TEV_H

#include <stdint.h>
#include "bp.h"

unsigned mgs_tev_stage_count(const MgsGxBp* bp);

typedef struct MgsTevInput {
    uint32_t texture;        /* ARGB, the sampled texel - stage zero's */
    uint32_t raster;         /* ARGB, the interpolated vertex colour */
    int      has_texture;

    /* ONE TEXEL PER STAGE, when the stages do not all share one texture.
     *
     * The combiner runs several stages and each may bind its OWN texture
     * map. Feeding all of them stage zero's texel is what made this game's
     * movie green-and-magenta stripes: its video frame is composited from
     * three maps in three stages - luminance and two chroma planes - and
     * with one texel for all three, two of them were reading luma.
     *
     * NULL means "every stage uses `texture`", which is the overwhelmingly
     * common case (2,491,121 single-stage triangles against 64 three-stage
     * ones in a boot) and costs nothing here. */
    const uint32_t* stage_tex;
    const uint8_t*  stage_has;
} MgsTevInput;

/* Run every enabled stage and return the final pixel, ARGB. */
uint32_t mgs_tev_run(const MgsGxBp* bp, const MgsTevInput* in);

/* Does the alpha test, as configured, let this pixel through? The test runs
 * after the combiner and is what makes cut-out foliage and text work at all;
 * ignoring it draws every transparent texel as an opaque black square. */

/* THE COMBINER'S STATE, DECODED ONCE.
 *
 * mgs_tev_run reloaded all four TEV registers and re-fetched every stage's
 * configuration for EVERY PIXEL, none of which changes while a triangle is
 * being drawn. At roughly 320 cycles a pixel that is most of the cost of the
 * renderer. Compile it per draw, then the per-pixel path is arithmetic on the
 * rasterised colour and the texel.
 */
typedef struct MgsTevCompiled {
    int      reg[4][4];        /* prev, c0, c1, c2 - rgb then alpha */
    uint32_t ce[16], ae[16];   /* each stage's colour and alpha environment */
    unsigned stages;
    int      configured;       /* GEN_MODE written: false means the default */

    /* THE KONST EACH STAGE SELECTED, resolved once here rather than per
     * pixel. Every stage picks its constant independently through KSEL, and
     * the colour selectors can splat one channel across all three - which is
     * how a game supplies a scalar coefficient. Held per stage because that
     * is how the hardware holds it. */
    int      kc[16][3], ka[16];

    /* The four channel-swap tables, as source channel indices in r,g,b,a
     * order. Identity when the game has not written KSEL. */
    unsigned swap[4][4];
    int      swap_set;
} MgsTevCompiled;

void     mgs_tev_compile(const MgsGxBp* bp, MgsTevCompiled* out);
uint32_t mgs_tev_run_compiled(const MgsTevCompiled* t, const MgsTevInput* in);

int mgs_tev_alpha_test_always(const MgsGxBp* bp);
int mgs_tev_alpha_test(const MgsGxBp* bp, uint32_t argb);

#endif
