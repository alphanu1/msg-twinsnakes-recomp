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
    int      no_depth;            /* MGS_NO_DEPTH: force every pixel through */
    int      blend_enable, blend_sub;   /* BP 0x41 CMODE0, per draw */
    int      color_update, alpha_update;
    unsigned blend_src, blend_dst;
    uint64_t blended, write_masked;
    uint32_t cmode_key[16]; uint64_t cmode_hit[16]; unsigned cmode_n;
    uint64_t black_over_lit;       /* black pixels drawn over lit ones */
    uint64_t black_over_lit_x[20]; /* ...by column, 32px buckets */

    uint64_t submitted, clipped, drawn, pixels, textured, alpha_killed;
    uint64_t pixels_lit;      /* of `pixels`, how many were not black */
    uint64_t tev_stages[16];  /* triangles by TEV stage count */
    uint64_t tex_on_later_stage; /* stage 0 had none, a later stage did */
    /* WHAT THE UNTEXTURED MAJORITY ACTUALLY ASKS THE COMBINER FOR.
     *
     * 514,048 triangles are drawn with no texture and come out black, and no
     * register read at the end of a run can say why: genMode and TEV_ORDER0
     * both read correctly there. These are sampled PER DRAW and kept as a
     * small histogram of distinct values, which is what a question about
     * something that varies during a run needs. */
    uint32_t cenv_key[16];    /* distinct TEV_COLOR_ENV seen, untextured */
    uint64_t cenv_hits[16];
    unsigned cenv_n;
    uint32_t rascol_key[16];  /* distinct vertex colours, untextured */
    uint64_t rascol_hits[16];
    unsigned rascol_n;

    uint64_t covered;         /* pixels inside a triangle, before depth */
    uint64_t depth_failed;    /* ...of those, rejected by the depth test */
    uint64_t tex_preloaded;   /* draws whose texture was preloaded into TMEM */
    uint64_t tex_second_window; /* image address resolved into the overlay window */
    uint32_t vp_halfw[8], vp_ox[8];  /* distinct viewports seen, by half-width */
    uint64_t vp_hits[8];
    unsigned vp_n;
    uint64_t tex2d_maxx[20];  /* textured 2D triangles by right edge, 32px buckets */
    uint64_t all2d_maxx[20];  /* ALL 2D triangles, same buckets */
    uint32_t texuse_addr[24]; unsigned texuse_fmt[24];
    uint16_t texuse_w[24], texuse_h[24];
    unsigned texuse_n; int trace_texuse;
    uint32_t uv_ss[12]; float uv_lo[12], uv_hi[12]; unsigned uv_n;
    int      trace_preload;   /* MGS_TRACE_PRELOAD */
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
