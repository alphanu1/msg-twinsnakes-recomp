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

typedef struct MgsTevInput {
    uint32_t texture;        /* ARGB, the sampled texel */
    uint32_t raster;         /* ARGB, the interpolated vertex colour */
    int      has_texture;
} MgsTevInput;

/* Run every enabled stage and return the final pixel, ARGB. */
uint32_t mgs_tev_run(const MgsGxBp* bp, const MgsTevInput* in);

/* Does the alpha test, as configured, let this pixel through? The test runs
 * after the combiner and is what makes cut-out foliage and text work at all;
 * ignoring it draws every transparent texel as an opaque black square. */
int mgs_tev_alpha_test(const MgsGxBp* bp, uint32_t argb);

#endif
