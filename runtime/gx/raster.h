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

/* Per-band counters, folded into the totals when the bands rejoin.
 *
 * A band owns a disjoint set of scanlines, so the framebuffer and the depth
 * buffer need no locking. The tallies are the only shared state in the inner
 * loop, and the cheapest way to keep them shared-free is not to share them.
 */
typedef struct MgsRasterTally {
    uint64_t covered, depth_failed, alpha_killed, pixels_lit, pixels;
    uint64_t blended, write_masked, black_over_lit;
    uint64_t black_over_lit_x[20];
} MgsRasterTally;

typedef struct MgsGxRaster {
    void*    jobs;           /* MgsJobPool: bands of a large triangle */
    unsigned max_bands;      /* 1 disables threading (MGS_RASTER_THREADS) */
    MgsRasterTally tally;    /* the inner loop's counters, folded per triangle */

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
    int      blend_noop;                /* ONE/ZERO: same as a plain write */
    int      color_update, alpha_update;
    unsigned blend_src, blend_dst;
    uint64_t blended, write_masked;
    uint64_t area_tris[20], area_px[20];  /* triangles and pixels by size */
    int      find_turn, turn_found, count_black;
    int      skip_all;       /* MGS_NO_RASTER: count and return */
    int      trace_noisy;    /* MGS_TRACE_NOISY: score bound textures */
    unsigned noisy_logged;
    /* The last draws, each with the texture it sampled: addr, format, size
     * and how noisy that texture was. Dumped when the embedded buffer first
     * turns to noise, to name the draw that did it. */
    uint32_t drawlog_addr[64];
    uint16_t drawlog_w[64], drawlog_h[64];
    uint8_t  drawlog_fmt[64], drawlog_rough[64];
    unsigned drawlog_at;

    /* Which draw turns the embedded buffer noisy: the score either side of
     * one full-screen draw, and what it had bound. Diagnostic only. */
    unsigned  noise_before, noise_armed;
    unsigned  composite_dump;
    unsigned  col_before[3], col_armed;
    unsigned  col_tex[3], col_tex_a;
    int       col_watch;
    /* Untextured draws: the alpha environment, the blend state and the
     * alpha the combiner produces. See the note at their use. */
    uint32_t  aenv_key[16];   uint64_t aenv_hits[16];   unsigned aenv_n;
    uint32_t  blend_key[16];  uint64_t blend_hits[16];  unsigned blend_n;
    uint32_t  outa_key[16];   uint64_t outa_hits[16];   unsigned outa_n;
    uint64_t  untex_onscreen, untex_offscreen, untex_straddle;
    uint64_t  near_clipped;   /* triangles split at the near plane */
    uint64_t  behind_mag[16]; /* how far behind the eye a rejected tri is */
    uint64_t  behind_zero_matrix;   /* ...with an all-zero position matrix */
    int       trace_behind; unsigned behind_shown;
    int       gpu;            /* MGS_GPU=1: fill on the GPU, not here */
    int       time_raster;
    uint64_t  ns_serial, ns_banded, tris_serial, tris_banded, bands_total;
    uint64_t  behind_mtx[8];        /* ...by matrix index, in groups of 8 */
    int       note_pixels;    /* MGS_TRACE_CENV: sample written colours */
    uint32_t  outc_key[16];   uint64_t outc_hits[16];   unsigned outc_n;
    int       scissor_box[4], scissor_seen;
    /* Diagnostics, read once at init like every other option here. */
    const char* dump_composite;
    const char* dump_drawseq;
    unsigned    trace_tevcfg;
    unsigned    trace_drawh;
    int         trace_drawh_set;
    int         trace_drawall, trace_drawnoise;
    int         trace_drawcolour;
    unsigned    trace_drawcolour_from;
    unsigned* noise_said;
    uint32_t  noise_tex_addr;
    uint8_t   noise_tex_fmt;
    uint16_t  noise_tex_w, noise_tex_h;
    uint32_t cmode_key[16]; uint64_t cmode_hit[16]; unsigned cmode_n;
    uint64_t black_over_lit;       /* black pixels drawn over lit ones */
    uint64_t black_over_lit_x[20]; /* ...by column, 32px buckets */

    uint64_t submitted, clipped, drawn, pixels, textured, alpha_killed;
    uint64_t pixels_lit;      /* of `pixels`, how many were not black */
    uint64_t tev_stages[16];  /* triangles by TEV stage count */
    uint64_t tex_on_later_stage; /* stage 0 had none, a later stage did */
    /* Draws where a LATER stage binds its own texture map. This is the
     * size of what the GPU path cannot yet do: it binds one sampler, so
     * every stage there sees stage zero's texel. Counted rather than
     * assumed - an early boot showed 64 such triangles and a full run
     * shows rather more. */
    /* HOW FAR THE TEXTURE COORDINATES SPAN ACROSS A TEXTURED TRIANGLE,
     * in texels, bucketed by power of two. A triangle whose three
     * vertices share one texel is drawn in one flat colour however
     * good the texture is - which is what "no textures on the 3d
     * models" looks like from the outside, and what a wrong texture
     * coordinate produces. Counted because the decode and the bind
     * both already report success, so the fault is downstream of
     * them or it is not there at all. */
    uint64_t uv_span[12];
    uint64_t multi_tex_tris;
    /* Draws with more distinct (map, coordinate) pairs than the GPU
     * path has texture units. Those stages fall back to unit zero and
     * are wrong; the number says whether four units is enough. */
    uint64_t tex_units_overflowed;
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
    /* WHICH TEXTURE THE DRAWN TRIANGLES ACTUALLY BIND.
     *
     * The decode counters say what was decoded, which is a different
     * question: CMPR decodes to 244 distinct colours and looks healthy,
     * while the models on screen are flat-shaded. That is only a
     * contradiction if the model triangles are sampling those decodes -
     * and nothing measured whether they are. Counted per format, with the
     * very small textures separated out because a 2x2 or a 1x1 bound to a
     * model IS a flat colour however well it decoded. */
    uint64_t tri_by_texfmt[16];
    uint64_t tri_tex_tiny;    /* bound texture <= 4x4 */
    uint64_t tri_tex_small;   /* <= 16x16 */
    /* Triangles wholly in front of GX's near plane (z < -w), and those
     * crossing it and cut back to it. See the clipping in raster.c. */
    uint64_t near_plane_rejected, near_plane_clipped;

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

/* Hand the rasteriser the host worker pool. Until this is called it runs
 * every triangle on the calling thread, which is what the unit tests want. */
void mgs_raster_set_jobs(MgsGxRaster* r, void* pool);

#endif
