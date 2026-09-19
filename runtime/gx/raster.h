/* Turning triangles into pixels.
 *
 * The last stage, and the one with the least room for interpretation: given
 * three vertices in the game's own coordinates, put the right pixels in the
 * embedded framebuffer. Everything upstream exists to deliver exactly that.
 *
 * Three transforms happen here, in this order, because that is the order the
 * hardware does them and any other order is a different picture:
 *
 *   1. The POSITION matrix, chosen per vertex by its matrix index, takes the
 *      model's coordinates into view space.
 *   2. The PROJECTION matrix takes view space into clip space. The GameCube
 *      stores it as six or seven floats rather than a full 4x4, because a
 *      projection matrix is mostly zeroes - and which six depends on whether
 *      it is perspective or orthographic.
 *   3. The VIEWPORT maps clip space onto the framebuffer, including the depth
 *      range. It is also where the half-pixel offset lives.
 *
 * Scanline fill with an edge function, a depth buffer, and Gouraud-interpolated
 * vertex colour. No texturing yet: a textured triangle needs the texture cache
 * and the texture environment stages, which are their own problem. Untextured
 * geometry in the right place is worth more than textured geometry in the
 * wrong place, and it is the thing that proves every stage before it.
 */
#ifndef MGS_GX_RASTER_H
#define MGS_GX_RASTER_H

#include "fifo.h"
#include "efb.h"
#include "texture.h"
#include "tev.h"

typedef struct MgsGxRaster {
    MgsEfb* efb;
    MgsTexCache tex;
    float   depth[MGS_EFB_WIDTH * MGS_EFB_HEIGHT];
    unsigned width, height;

    int      depth_test;          /* BP 0x40: enable, function, update */
    unsigned depth_func;
    int      depth_update;
    int      cull;                /* BP 0x41 bits 14-15 */
    int      trace;               /* MGS_TRACE_RASTER: explain rejections */
    unsigned trace_limit;         /* how many triangles to explain */
    int      no_scissor;          /* MGS_NO_SCISSOR: ignore the scissor box */

    uint64_t submitted, clipped, drawn, pixels, textured, alpha_killed;
    uint64_t pixels_lit;      /* of `pixels`, how many were not black */
    uint64_t tev_stages[16];  /* triangles by TEV stage count */
    uint64_t tex_on_later_stage; /* stage 0 had none, a later stage did */
    uint64_t tex_wanted;      /* stage 0 asked for a texture */
    uint64_t tex_bind_failed; /* ...and we could not supply one */

    /* Returns non-zero when drawing should stop - the host has been asked to
     * quit and is waiting for this call to come back. Optional; NULL means
     * draw everything. See mgs_raster_triangle for why this is needed at all. */
    int (*abandon)(void);
} MgsGxRaster;

void mgs_raster_init(MgsGxRaster* r, MgsEfb* efb);
void mgs_raster_reset_depth(MgsGxRaster* r);

/* The triangle callback for MgsGx. `gx->user` must be the MgsGxRaster. */
void mgs_raster_triangle(MgsGx* gx, const MgsGxVertex* a,
                         const MgsGxVertex* b, const MgsGxVertex* c);

#endif
