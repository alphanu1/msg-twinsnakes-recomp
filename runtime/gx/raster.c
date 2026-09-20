#include "raster.h"
#include "platform/jobs.h"
#include "fifo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mgs_raster_init(MgsGxRaster* r, MgsEfb* efb)
{
    memset(r, 0, sizeof *r);
    mgs_tex_cache_init(&r->tex);
    r->efb = efb;
    r->width = MGS_EFB_WIDTH;
    r->height = MGS_EFB_HEIGHT;
    r->depth_test = 1;
    r->depth_update = 1;
    r->depth_func = 3;            /* less-or-equal, the usual default */
    r->trace_noisy = getenv("MGS_TRACE_NOISY") != NULL;
    r->skip_all = getenv("MGS_NO_RASTER") != NULL;
    r->find_turn = getenv("MGS_FIND_TURN") != NULL;
    r->count_black = getenv("MGS_COUNT_BLACK") != NULL;
    /* MGS_RASTER_THREADS=N caps how many bands a large triangle is split
     * into; =1 keeps the whole thing on this thread, which is the control
     * a bit-identical comparison needs. Default 0 means "ask the pool". */
    {
        const char* e = getenv("MGS_RASTER_THREADS");
        r->max_bands = e ? (unsigned)strtoul(e, NULL, 10) : 0u;
        r->jobs = NULL;
    }
    r->color_update = 1;          /* power-on: writes enabled, no blend */
    r->alpha_update = 1;
    /* MGS_TRACE_RASTER=N explains the first N triangles; bare =1 keeps the
     * old behaviour of eight. Configurable because the interesting geometry
     * is no longer the first thing drawn: the boot logo's handful of
     * triangles now come and go long before the engine's own geometry
     * starts, and a fixed count of eight can only ever describe the logo. */
    /* MGS_NO_SCISSOR ignores the box, to tell "the game clipped this" apart
     * from "we computed the box wrongly". The engine sets a scissor box
     * OFFSET of 1,024 pixels for its sphere-map pass (BP 0x59 = 0x02ACAB),
     * which is larger than the framebuffer is wide, and a coordinate
     * convention misread there clips everything rather than clipping
     * nothing - so the two readings have to be comparable from one binary. */
    r->no_scissor = getenv("MGS_NO_SCISSOR") != NULL;
    /* MGS_NO_DEPTH forces every pixel past the depth test.
     *
     * Not a fix and not a rendering mode - a question. The game stops issuing
     * clearing copies after the 59th, so the depth buffer holds one early
     * frame's depths for the rest of the run and rejects 94.9% of everything
     * drawn afterwards. Whether that is why nothing new appears is answerable
     * in one run: turn the test off and see if the picture fills in. */
    r->no_depth = getenv("MGS_NO_DEPTH") != NULL;
    r->trace_preload = getenv("MGS_TRACE_PRELOAD") != NULL;
    r->trace_texuse = getenv("MGS_TRACE_TEXUSE") != NULL;
    {
        const char* e = getenv("MGS_TRACE_RASTER");
        r->trace = e != NULL;
        r->trace_limit = 8u;
        if (e) {
            unsigned long v = strtoul(e, NULL, 0);
            if (v > 1ul) r->trace_limit = (unsigned)v;
        }
    }
    mgs_raster_reset_depth(r);
}

void mgs_raster_reset_depth(MgsGxRaster* r)
{
    unsigned i, n = MGS_EFB_WIDTH * MGS_EFB_HEIGHT;
    for (i = 0; i < n; ++i) r->depth[i] = 1.0f;
}

/* The position matrix for a vertex. The transform unit holds 64 rows of four
 * floats; a matrix index selects a group of three rows, and the index the
 * vertex carries is in units of four rows - not of matrices. Treating it as a
 * matrix number puts every object at another object's position. */
static const float* position_matrix(const MgsGx* gx, unsigned index)
{
    unsigned row = (index & 0x3Fu) * 4u;
    if (row + 12u > 64u * 4u) row = 0u;
    return &gx->xf_matrix[row];
}

static void transform(const float* m, float x, float y, float z, float* out)
{
    out[0] = m[0] * x + m[1] * y + m[2]  * z + m[3];
    out[1] = m[4] * x + m[5] * y + m[6]  * z + m[7];
    out[2] = m[8] * x + m[9] * y + m[10] * z + m[11];
}

/* Projection, from the transform unit's packed form.
 *
 * Perspective keeps six values: two scales, two offsets and the two depth
 * terms, with w taken from -z. Orthographic keeps the same six but w is 1.
 * The `ortho` flag is a separate register and is the thing that decides which
 * - inferring it from the values is possible and is exactly the kind of guess
 * that produces a scene that is almost right.
 */
static void project(const MgsGx* gx, const float* view, float* clip, float* w)
{
    const float* p = gx->xf_projection;

    if (gx->xf_projection_ortho) {
        clip[0] = p[0] * view[0] + p[1];
        clip[1] = p[2] * view[1] + p[3];
        clip[2] = p[4] * view[2] + p[5];
        *w = 1.0f;
    } else {
        clip[0] = p[0] * view[0] + p[1] * view[2];
        clip[1] = p[2] * view[1] + p[3] * view[2];
        clip[2] = p[4] * view[2] + p[5];
        *w = -view[2];
    }
}

static float f_from_bits(uint32_t bits)
{
    float f; memcpy(&f, &bits, sizeof f); return f;
}

/* Clip space to pixels. The viewport registers hold half-width, half-height,
 * depth scale, and the three corresponding offsets; the offsets carry a fixed
 * 342-pixel bias that the hardware subtracts, which is why it appears here as
 * a constant rather than as something derived. */
static void to_screen(const MgsGxRaster* r, const MgsGx* gx,
                      const float* clip, float w, float* sx, float* sy, float* sz)
{
    float wx = f_from_bits(gx->viewport[0]);
    float wy = f_from_bits(gx->viewport[1]);
    float wz = f_from_bits(gx->viewport[2]);
    float ox = f_from_bits(gx->viewport[3]);
    float oy = f_from_bits(gx->viewport[4]);
    float oz = f_from_bits(gx->viewport[5]);
    float inv = (w != 0.0f) ? 1.0f / w : 0.0f;

    if (wx == 0.0f && wy == 0.0f) {         /* not programmed yet */
        wx = (float)r->width * 0.5f;  ox = wx + 342.0f;
        wy = -(float)r->height * 0.5f; oy = (float)r->height * 0.5f + 342.0f;
        wz = 1.0f; oz = 0.0f;
    }

    *sx = clip[0] * inv * wx + (ox - 342.0f);
    *sy = clip[1] * inv * wy + (oy - 342.0f);
    *sz = clip[2] * inv * wz + oz;
}


/* ---- texture binding ---------------------------------------------------
 *
 * Which texture a stage samples is three registers away from the stage
 * itself: the TEV order register says which texture map and which coordinate
 * set the stage uses, TX_SETIMAGE0 gives the size and format, TX_SETIMAGE3
 * the address, and TX_SETTLUT the palette. They are read together here so
 * that a stage's texture is resolved in one place.
 */
static const MgsTexture* bind_texture(MgsGxRaster* r, MgsGx* gx, unsigned map)
{
    const MgsGxBp* bp = &gx->bp;
    uint8_t base0, base3, basel;
    uint32_t i0, i3, tl;
    unsigned width, height, format, tlut_off;
    uint32_t tlut_addr = 0, tlut_format = 0;

    if (map >= 8u) return NULL;

    /* Textures 0-3 and 4-7 live in two separate register blocks. */
    base0 = (uint8_t)((map < 4u ? BP_TX_SETIMAGE0 : BP_TX_SETIMAGE0_4) + (map & 3u));
    base3 = (uint8_t)((map < 4u ? BP_TX_SETIMAGE3 : BP_TX_SETIMAGE3_4) + (map & 3u));
    basel = (uint8_t)((map < 4u ? BP_TX_SETTLUT   : BP_TX_SETTLUT_4)   + (map & 3u));

    if (!bp->written[base0] || !bp->written[base3]) return NULL;

    i0 = mgs_bp_get(bp, base0);
    i3 = mgs_bp_get(bp, base3);
    tl = mgs_bp_get(bp, basel);

    width  = (i0 & 0x3FFu) + 1u;
    height = ((i0 >> 10) & 0x3FFu) + 1u;
    format = (i0 >> 20) & 0xFu;

    if (format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2) {
        tlut_off = tl & 0x3FFu;
        tlut_addr = bp->tlut_src[tlut_off];
        tlut_format = (tl >> 10) & 3u;
        if (!tlut_addr) return NULL;      /* palette never loaded: refuse */
    }

    /* IS THIS TEXTURE FETCHED FROM MEMORY, OR ALREADY IN TMEM?
     *
     * TX_SETIMAGE1 bit 21 is the hardware's `image_type`: 0 means the texture
     * unit fetches from the address in SETIMAGE3, 1 means the game has
     * PRELOADED the texture into texture memory and that address is not used
     * for fetching at all. Reading SETIMAGE3 regardless is how a preloaded
     * texture turns into a nonsense pointer - which is exactly what
     * 0x835006C0 is, 53 MB into a 24 MB machine, refused 2,622 times.
     *
     * Reported rather than handled: TMEM is not modelled yet, so the honest
     * outcome is still a refusal, but a refusal that says WHICH kind. */
    {
        uint8_t base1 = (uint8_t)((map < 4u ? BP_TX_SETIMAGE1
                                            : BP_TX_SETIMAGE1_4) + (map & 3u));
        uint32_t i1 = mgs_bp_get(bp, base1);
        if (bp->written[base1] && ((i1 >> 21) & 1u)) {
            ++r->tex_preloaded;
            if (r->trace_preload)
                fprintf(stderr, "[tex] PRELOADED into TMEM: map=%u fmt=0x%X "
                                "%ux%u  SETIMAGE1=0x%06X tmem_even=0x%X  "
                                "(SETIMAGE3=0x%06X is not a fetch address)\n",
                        map, format, width, height, i1, i1 & 0x7FFFu, i3);
            return NULL;
        }
    }

    /* THE IMAGE ADDRESS, AND THE SECOND WINDOW IT MAY HAVE COME FROM.
     *
     * The address is in 32-byte units. Reconstructing it as MEM1 is right for
     * every texture the game allocates - but not for one it points at inside
     * its own static data, because the engine overlay does not live in MEM1
     * here. It lives in the second address window at 0x7E000000, which is not
     * an address a GameCube has.
     *
     * `GXInitTexObj` converts its pointer to a physical address the way the
     * hardware does, by masking to 26 bits, and stores that shifted down by
     * five. For a MEM1 pointer that is exactly right. For an overlay pointer
     * it silently produces a physical address that means nothing:
     *
     *     0x7F5006C0 & 0x03FFFFFF = 0x035006C0,  >> 5 = 0x1A8036
     *
     * and rebuilding THAT as MEM1 gives 0x835006C0 - 55.6 MB into a 24 MB
     * machine. That is the font texture, refused 2,964 times, and it is why
     * every line of text on the memory-card screen stops mid-word.
     *
     * The information is not lost, only ambiguous, and the ranges resolve it.
     * A masked-to-26-bits address that cannot be in MEM1 must have come from
     * the window above it, and `0x7C000000 | phys` inverts the mask exactly
     * for every address in [0x7E000000, 0x80000000). MEM1 is tried first, so
     * an ordinary texture is unaffected.
     *
     * This is a consequence of where the overlay is placed, not a fault in
     * the game, and it will stop mattering if the overlay ever moves into
     * MEM1 proper. Until then the reconstruction belongs here, where the
     * address is turned back into a pointer. */
    {
        uint32_t phys = (i3 & 0x00FFFFFFu) << 5;
        uint32_t addr = 0x80000000u | phys;

        if (addr >= 0x80000000u + GUEST_RAM_SIZE) {
            uint32_t alt = 0x7C000000u | phys;
            if (alt >= GUEST_VMEM_BASE &&
                alt <  GUEST_VMEM_BASE + GUEST_VMEM_SIZE) {
                addr = alt;
                ++r->tex_second_window;
            }
        }
        /* Which (address, format, size) triples are sampled, kept distinct.
         * An EFB-to-texture encoder has to write the layout the game will
         * READ, so the sampling side is what specifies it - guessing from the
         * copy format alone would only be half the contract. */
        if (r->trace_texuse) {
            unsigned k;
            for (k = 0; k < r->texuse_n; ++k)
                if (r->texuse_addr[k] == addr && r->texuse_fmt[k] == format)
                    break;
            if (k == r->texuse_n && r->texuse_n < 24u) {
                r->texuse_addr[k] = addr;
                r->texuse_fmt[k] = format;
                r->texuse_w[k] = (uint16_t)width;
                r->texuse_h[k] = (uint16_t)height;
                r->texuse_n++;
            }
        }
        return mgs_tex_get(&r->tex, gx->mem, addr,
                           format, width, height, tlut_addr, tlut_format);
    }
}

/* Which texture map and coordinate set a stage uses. Two stages share one
 * register, low half then high half. */
static void stage_texture(const MgsGxBp* bp, unsigned stage,
                          unsigned* map, unsigned* coord, int* enabled)
{
    uint32_t reg = mgs_bp_get(bp, (uint8_t)(BP_TEV_ORDER + stage / 2u));
    unsigned sh = (stage & 1u) ? 12u : 0u;
    *map     = (reg >> (sh + 0u)) & 7u;
    *coord   = (reg >> (sh + 3u)) & 7u;
    *enabled = (int)((reg >> (sh + 6u)) & 1u);
}

/* Wrap modes for a texture, from its mode register. */
static void texture_wrap(const MgsGxBp* bp, unsigned map,
                         unsigned* wrap_s, unsigned* wrap_t, int* bilinear)
{
    uint8_t reg = (uint8_t)((map < 4u ? BP_TX_SETMODE0 : BP_TX_SETMODE0_4) + (map & 3u));
    uint32_t v = mgs_bp_get(bp, reg);
    *wrap_s = v & 3u;
    *wrap_t = (v >> 2) & 3u;
    /* Magnification filter: 0 is nearest, 1 is linear. */
    *bilinear = (int)((v >> 4) & 1u);
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static uint32_t lerp_color(uint32_t a, uint32_t b, uint32_t c,
                           float wa, float wb, float wc)
{
    unsigned i;
    uint32_t out = 0;
    for (i = 0; i < 4u; ++i) {
        float v = (float)((a >> (i * 8)) & 0xFFu) * wa +
                  (float)((b >> (i * 8)) & 0xFFu) * wb +
                  (float)((c >> (i * 8)) & 0xFFu) * wc;
        int k = (int)(v + 0.5f);
        if (k < 0) k = 0;
        if (k > 255) k = 255;
        out |= (uint32_t)k << (i * 8);
    }
    return out;
}

static int depth_passes(const MgsGxRaster* r, float z, float was)
{
    if (r->no_depth) return 1;
    if (!r->depth_test) return 1;
    switch (r->depth_func) {
        case 0: return 0;                 /* never */
        case 1: return z <  was;
        case 2: return z == was;
        case 3: return z <= was;
        case 4: return z >  was;
        case 5: return z != was;
        case 6: return z >= was;
        default: return 1;                /* always */
    }
}

/* One channel of a GX blend factor.
 *
 * The factor ids are shared between the two operands and NAMED relative to
 * the other one: 2 is "the other operand's colour" and 3 is one minus it, so
 * GX_BL_SRCCLR and GX_BL_DSTCLR are the same number read from opposite sides.
 * Passing which side is being computed is what keeps that straight.
 *
 * Values are 0-255 and the caller divides by 255 after multiplying, so a
 * factor of ONE really is one rather than 255/256.
 */
static inline int blend_factor(unsigned id, int src, int dst,
                                   int src_a, int dst_a, int for_src)
{
    switch (id) {
        case 0: return 0;                              /* ZERO */
        case 1: return 255;                            /* ONE */
        case 2: return for_src ? dst : src;            /* the other colour */
        case 3: return 255 - (for_src ? dst : src);    /* one minus it */
        case 4: return src_a;                          /* SRCALPHA */
        case 5: return 255 - src_a;                    /* INVSRCALPHA */
        case 6: return dst_a;                          /* DSTALPHA */
        default: return 255 - dst_a;                   /* INVDSTALPHA */
    }
}

/* A tiny fixed histogram: keep the first 16 distinct values and count them.
 *
 * Sixteen is enough because the interesting answer is "one value, used half a
 * million times" or "three values" - a renderer configuration that genuinely
 * took more than sixteen shapes would itself be the finding. Overflow is
 * counted into the last slot rather than dropped, so the totals still add up.
 */
static void note_value(uint32_t* keys, uint64_t* hits, unsigned* n, uint32_t v)
{
    unsigned i;
    for (i = 0; i < *n; ++i)
        if (keys[i] == v) { ++hits[i]; return; }
    if (*n < 16u) { keys[*n] = v; hits[*n] = 1u; ++*n; return; }
    ++hits[15];
}

/* THE SCANLINE LOOP, SEPARATED FROM THE SETUP THAT FEEDS IT.
 *
 * Splitting it out costs nothing on its own - the caller still runs it over
 * the triangle's full height - but it is what lets a band of rows be handed
 * to another core later, and it makes the per-pixel work visible as its own
 * function in a profile rather than buried in 480 lines of state decoding.
 */
typedef struct RasterSpan {
    MgsGx*              gx;
    MgsGxRaster*        r;
    const MgsGxVertex*  vin[3];
    const MgsTexture*   tex;
    unsigned            tex_coord, wrap_s, wrap_t;
    int                 bilinear;
    int                 alpha_always;
    MgsTevCompiled      tev;
    float               sx[3], sy[3], sz[3], iw[3];
    float               area, inv_area, dw0dx, dw1dx;
    int                 x0, x1;
} RasterSpan;

static void raster_span(const RasterSpan* sp, int y0, int y1,
                        MgsRasterTally* t)
{
    MgsGx* gx = sp->gx;
    MgsGxRaster* r = sp->r;
    const MgsGxVertex* const* vin = sp->vin;
    const MgsTexture* tex = sp->tex;
    unsigned tex_coord = sp->tex_coord, wrap_s = sp->wrap_s, wrap_t = sp->wrap_t;
    int bilinear = sp->bilinear, alpha_always = sp->alpha_always;
    const MgsTevCompiled* tev = &sp->tev;
    const float* sx = sp->sx; const float* sy = sp->sy;
    const float* sz = sp->sz; const float* iw = sp->iw;
    float area = sp->area;
    float inv_area = sp->inv_area, dw0dx = sp->dw0dx, dw1dx = sp->dw1dx;
    int x0 = sp->x0, x1 = sp->x1;
    int px, py;

    for (py = y0; py < y1; ++py) {
        /* ONCE PER SCANLINE, not once per triangle.
         *
         * The per-triangle check above cannot release a run that is inside a
         * single large triangle, and that is exactly where a wedged host was
         * found sitting: the phase marker said `raster`, the abandon hook was
         * installed, and the process still would not stop. A row is at most a
         * few hundred pixels, so this bounds the response time to something
         * far below a human's patience while costing one predictable branch
         * per row. */
        if (r->abandon && r->abandon()) return;

        /* TWO DIVIDES PER PIXEL, GONE.
         *
         * A sampling profile put 50% of the whole program in this function,
         * and the weights were the reason: an edge function evaluated from
         * scratch and then divided by the area, twice, for every pixel on
         * screen. An edge function is affine in x, so along a row it is an
         * add; and dividing by the area once per triangle turns the divides
         * into multiplies. The row start is still evaluated exactly rather
         * than carried down from the row above, so drift is bounded by one
         * row's width instead of accumulating over the whole triangle.
         */
        {
        float fy = (float)py + 0.5f;

        for (px = x0; px < x1; ++px) {
            float fx = (float)px + 0.5f;
            float w0 = edge(sx[1], sy[1], sx[2], sy[2], fx, fy) / area;
            float w1 = edge(sx[2], sy[2], sx[0], sy[0], fx, fy) / area;
            float w2 = 1.0f - w0 - w1;
            float z, pw;
            unsigned at;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            at = (unsigned)py * r->width + (unsigned)px;
            /* COUNTED. 3.3 million extra triangles reached this loop and
             * produced not one pixel, and the totals were byte-identical to a
             * run with a seventh of the geometry - so something rejects every
             * candidate after the coverage test. Splitting "outside the
             * triangle" from "failed the depth test" is the difference
             * between a geometry fault and a stale depth buffer, and the
             * depth buffer is only reset on a clearing copy: 56 of them in a
             * boot, all early. */
            ++t->covered;
            if (!depth_passes(r, z, r->depth[at])) { ++t->depth_failed; continue; }

            /* Perspective-correct interpolation of the vertex colour: the
             * weights are in screen space, and dividing by the interpolated
             * 1/w corrects them. Skipping this is the classic warped-texture
             * artefact, and it bends Gouraud shading the same way. */
            pw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2];
            {
                MgsTevInput in;
                uint32_t pixel;
                float k0, k1, k2;

                if (pw > 0.0f) {
                    /* One reciprocal, two multiplies. A divide is an order
                     * of magnitude dearer than a multiply and this ran twice
                     * for every pixel on screen. */
                    float inv = 1.0f / pw;
                    k0 = w0 * iw[0] * inv; k1 = w1 * iw[1] * inv;
                    k2 = 1.0f - k0 - k1;
                } else {
                    k0 = 1.0f; k1 = 0.0f; k2 = 0.0f;
                }

                in.raster = lerp_color(vin[0]->color[0], vin[1]->color[0],
                                       vin[2]->color[0], k0, k1, k2);
                in.has_texture = 0;
                in.texture = 0xFFFFFFFFu;

                if (tex) {
                    float u = k0 * vin[0]->u[tex_coord] +
                              k1 * vin[1]->u[tex_coord] +
                              k2 * vin[2]->u[tex_coord];
                    float v = k0 * vin[0]->v[tex_coord] +
                              k1 * vin[1]->v[tex_coord] +
                              k2 * vin[2]->v[tex_coord];
                    in.texture = mgs_tex_sample(tex, u, v, wrap_s, wrap_t, bilinear);
                    in.has_texture = 1;
                }

                pixel = mgs_tev_run_compiled(tev, &in);

                /* The alpha test runs AFTER the combiner and before anything
                 * is written, depth included. Cut-out foliage and text rely
                 * on it entirely; without it every transparent texel becomes
                 * an opaque square that also writes depth. */
                /* HOISTED: the game sets ALPHA_COMPARE to ALWAYS/ALWAYS,
                 * so the test cannot fail and 0 pixels are killed in a whole
                 * run - yet it was a function call for every pixel drawn.
                 * Decided once per triangle; the per-pixel call remains for
                 * the configurations that can actually reject. */
                if (!alpha_always && !mgs_tev_alpha_test(&gx->bp, pixel)) {
                    ++t->alpha_killed;
                    continue;
                }

                /* COUNTED SEPARATELY FROM `pixels`, because "12 million
                 * pixels written" and "12 million BLACK pixels written" look
                 * identical in a tally and mean opposite things. One says the
                 * rasteriser works; the other says every stage upstream of
                 * the colour is working and the colour is not. */
                if (pixel & 0x00FFFFFFu) ++t->pixels_lit;

                /* DOES ANYTHING BLACK PAINT OVER SOMETHING LIT, AND WHERE?
                 *
                 * 165,888 triangles a run are configured a=b=c=d=ZERO -
                 * "output black" - and they are the large ones, about 66
                 * pixels each. One of those covering the right of the screen
                 * would truncate every line of text at the same column
                 * regardless of content, which is the signature this bug has
                 * always had.
                 *
                 * COUNTED, NOT SUPPRESSED. The first version of this skipped
                 * black writes to see if the text reappeared, and that
                 * changed the boot: 7,235 GX commands instead of 2,476,033.
                 * Altering the EFB alters what the copies put in guest
                 * memory, so a "diagnostic" that changes pixels is not a
                 * diagnostic at all. This one only observes. */
                if (r->count_black && !(pixel & 0x00FFFFFFu) &&
                    (r->efb->pixels[at] & 0x00FFFFFFu)) {
                    unsigned bx = (unsigned)px / 32u;
                    ++t->black_over_lit;
                    if (bx < 20u) ++t->black_over_lit_x[bx];
                }

                /* Blend and mask, in the hardware's order: the combiner's
                 * result is the source, the framebuffer is the destination,
                 * and the update bits decide which channels survive. */
                if (!(r->blend_enable && !r->blend_noop) &&
                    r->color_update && r->alpha_update) {
                    /* The common case by a wide margin: no blending in
                     * effect and both channels writable. */
                    r->efb->pixels[at] = pixel;
                } else {
                    uint32_t dstp = r->efb->pixels[at];
                    uint32_t out = pixel;

                    if (r->blend_enable && !r->blend_noop) {
                        int sa = (int)((pixel >> 24) & 0xFFu);
                        int da = (int)((dstp  >> 24) & 0xFFu);
                        unsigned ch;
                        out = pixel & 0xFF000000u;
                        for (ch = 0; ch < 3u; ++ch) {
                            unsigned sh = ch * 8u;
                            int sc = (int)((pixel >> sh) & 0xFFu);
                            int dc = (int)((dstp  >> sh) & 0xFFu);
                            int sf = blend_factor(r->blend_src, sc, dc, sa, da, 1);
                            int df = blend_factor(r->blend_dst, sc, dc, sa, da, 0);
                            int v = r->blend_sub
                                  ? (dc * df - sc * sf) / 255
                                  : (sc * sf + dc * df) / 255;
                            if (v < 0) v = 0;
                            if (v > 255) v = 255;
                            out |= (uint32_t)v << sh;
                        }
                        ++t->blended;
                    }

                    if (!r->color_update) out = (out & 0xFF000000u)
                                              | (dstp & 0x00FFFFFFu);
                    if (!r->alpha_update) out = (out & 0x00FFFFFFu)
                                              | (dstp & 0xFF000000u);
                    if (!r->color_update && !r->alpha_update) ++t->write_masked;

                    r->efb->pixels[at] = out;
                }
            }

            if (r->depth_update) r->depth[at] = z;
            ++t->pixels;
        }
        }
    }
}

/* ONE TRIANGLE, SPLIT ACROSS CORES.
 *
 * A sampling profile put essentially the whole program inside the pixel loop,
 * and the size histogram says where those pixels are: 1,538 triangles cover
 * 94.3% of them, while 1.6 million tiny ones in the scratch strip cover 3.8%.
 * So the work is not spread thinly over millions of triangles - it is
 * concentrated in a few hundred full-screen quads, which is the shape that
 * splits across cores well.
 *
 * Bands of scanlines, not tiles: a band owns a contiguous, disjoint range of
 * rows, so two bands can never touch the same framebuffer or depth word and
 * the split needs no locking and no binning pass. Drawing order is preserved
 * because each band draws the same triangles in the same order.
 *
 * ONLY LARGE TRIANGLES. Submitting a job costs far more than eight pixels,
 * so anything below the threshold runs inline exactly as before - which is
 * also what keeps the single-core path identical rather than merely similar.
 *
 * NO ARITHMETIC CHANGES HERE, deliberately. The boot is chaotically
 * sensitive to rounding in the weights: replacing the per-pixel divide with
 * a multiply by the reciprocal - a one-ULP change - took a boot from
 * 3,708,746 triangles to 109. Splitting the same computation across cores
 * changes which core runs it and nothing else, so the result stays
 * bit-identical and can be checked as such.
 */
typedef struct RasterBand {
    const RasterSpan* sp;
    int               y0, y1;
    MgsRasterTally    t;
} RasterBand;

static void raster_band_job(void* user)
{
    RasterBand* b = (RasterBand*)user;
    raster_span(b->sp, b->y0, b->y1, &b->t);
}

/* The totals the reports print keep their existing names and meaning; only
 * the path they take to get there changed. */
static void tally_fold(MgsGxRaster* r, const MgsRasterTally* t)
{
    unsigned i;
    r->covered      += t->covered;
    r->depth_failed += t->depth_failed;
    r->alpha_killed += t->alpha_killed;
    r->pixels_lit   += t->pixels_lit;
    r->pixels       += t->pixels;
    r->blended      += t->blended;
    r->write_masked += t->write_masked;
    r->black_over_lit += t->black_over_lit;
    for (i = 0; i < 20u; ++i) r->black_over_lit_x[i] += t->black_over_lit_x[i];
}

static void tally_add(MgsRasterTally* dst, const MgsRasterTally* src)
{
    unsigned i;
    dst->covered      += src->covered;
    dst->depth_failed += src->depth_failed;
    dst->alpha_killed += src->alpha_killed;
    dst->pixels_lit   += src->pixels_lit;
    dst->pixels       += src->pixels;
    dst->blended      += src->blended;
    dst->write_masked += src->write_masked;
    dst->black_over_lit += src->black_over_lit;
    for (i = 0; i < 20u; ++i) dst->black_over_lit_x[i] += src->black_over_lit_x[i];
}

/* Below this many rows the job overhead dominates and the split loses. */
#define RASTER_BAND_MIN_ROWS 8

static void raster_dispatch(MgsGxRaster* r, const RasterSpan* sp, int y0, int y1)
{
    RasterBand band[64];
    MgsRasterTally total;
    unsigned n, i;
    int rows = y1 - y0;

    memset(&total, 0, sizeof total);

    n = r->max_bands;
    if (n > 64u) n = 64u;
    if (n > (unsigned)(rows / RASTER_BAND_MIN_ROWS)) n = (unsigned)(rows / RASTER_BAND_MIN_ROWS);
    if (!r->jobs || n < 2u) {
        raster_span(sp, y0, y1, &total);
        tally_fold(r, &total);
        return;
    }

    for (i = 0; i < n; ++i) {
        band[i].sp = sp;
        band[i].y0 = y0 + (int)((unsigned)rows * i / n);
        band[i].y1 = y0 + (int)((unsigned)rows * (i + 1u) / n);
        memset(&band[i].t, 0, sizeof band[i].t);
    }

    /* Submit all but the first, then run the first on this thread: the
     * caller would otherwise sit idle waiting, and a core is a core. A
     * refused submission runs inline, which is what the pool's contract
     * asks for and keeps this correct when the pool is full. */
    for (i = 1; i < n; ++i) {
        if (!mgs_jobs_submit((MgsJobPool*)r->jobs, raster_band_job, &band[i]))
            raster_band_job(&band[i]);
    }
    raster_band_job(&band[0]);
    mgs_jobs_wait((MgsJobPool*)r->jobs);

    for (i = 0; i < n; ++i) tally_add(&total, &band[i].t);
    tally_fold(r, &total);
}

void mgs_raster_set_jobs(MgsGxRaster* r, void* pool)
{
    unsigned avail;
    r->jobs = pool;
    if (!pool) { r->max_bands = 1u; return; }
    /* One band per worker plus this thread, which runs a band itself. */
    avail = mgs_jobs_worker_count((const MgsJobPool*)pool) + 1u;
    if (r->max_bands == 0u || r->max_bands > avail) r->max_bands = avail;
}

void mgs_raster_triangle(MgsGx* gx, const MgsGxVertex* a,
                         const MgsGxVertex* b, const MgsGxVertex* c)
{
    MgsGxRaster* r = (MgsGxRaster*)gx->user;
    const MgsGxVertex* vin[3];
    const MgsTexture* tex = NULL;
    unsigned tex_coord = 0, wrap_s = 0, wrap_t = 0;
    int alpha_always;
    MgsTevCompiled tev;

    /* MGS_NO_RASTER: count the triangle and return without touching a pixel.
     * Timing the same workload with and without the inner loops splits the
     * wall clock between drawing and everything else - guest execution, FIFO
     * parsing, copies - which need completely different work. */
    if (r->skip_all) { ++r->submitted; return; }

    /* CATCH THE CROSSING AS IT HAPPENS, not at the end of the frame.
     *
     * A frame draws thousands of triangles, so a ring of the last few dozen
     * only ever shows the tail - by which time the noise has been circulating
     * for most of the frame. Sampling the buffer every few hundred triangles
     * costs little and names the draw that turns it, which is the one fact
     * this has been missing. */
    if (r->find_turn && !r->turn_found && (r->submitted % 256u) == 0u) {
        unsigned yy, cnt = 0u, rough = 0u;
        for (yy = 64u; yy < 448u; yy += 32u) {
            unsigned xx;
            for (xx = 65u; xx < 512u; xx += 16u) {
                uint32_t a = r->efb->pixels[yy * MGS_EFB_WIDTH + xx - 1u];
                uint32_t b = r->efb->pixels[yy * MGS_EFB_WIDTH + xx];
                int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
                int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
                rough += (unsigned)(va > vb ? va - vb : vb - va);
                ++cnt;
            }
        }
        if (cnt && rough / cnt > 20u) {
            r->turn_found = 1;
            fprintf(stderr,
                    "[turn] buffer crossed into noise at triangle %llu, "
                    "roughness %u\n"
                    "[turn]   this draw: %s, vcd=%08X/%08X, tev stages %u, "
                    "blend %s, depth %s\n",
                    (unsigned long long)r->submitted, rough / cnt,
                    tex ? "textured" : "UNTEXTURED",
                    gx->vcd_lo, gx->vcd_hi,
                    ((mgs_bp_get(&gx->bp, BP_GEN_MODE) >> 10) & 0xFu) + 1u,
                    r->blend_enable ? "on" : "off",
                    r->depth_test ? "on" : "off");
            if (tex)
                fprintf(stderr, "[turn]   texture %ux%u fmt 0x%X at 0x%08X\n",
                        tex->width, tex->height, tex->format, tex->addr);
        }
    }
    int bilinear = 0, tex_enabled = 0;
    float sx[3], sy[3], sz[3], iw[3];
    float minx, maxx, miny, maxy, area;
    int x0, x1, y0, y1, px, py;
    unsigned i;

    if (!r || !r->efb) return;
    ++r->submitted;

    /* THE GAME'S OWN DEPTH STATE, per draw.
     *
     * BP 0x40 is ZMODE: bit 0 enables the depth test, bits 1-3 choose the
     * comparison, bit 4 enables depth writes. It was defined and never read -
     * the rasteriser set "test on, less-or-equal, write on" once at init and
     * used that for every triangle in the game.
     *
     * That is how a UI layer ends up UNDERNEATH the scene it was drawn after.
     * Disabling the depth test is the normal way to put something on top, and
     * ignoring the register means the request never arrives: the overlay is
     * compared against whatever depths happen to be in the buffer and loses.
     * With the buffer only cleared 56 times in 3,740 copies (F127), those
     * depths are from a frame long gone, which is why 9,422,255 black pixels
     * end up drawn over lit ones (F150).
     *
     * The function encoding matches `depth_passes` as written: 0 never,
     * 1 less, 2 equal, 3 less-or-equal, 4 greater, 5 not-equal, 6
     * greater-or-equal, 7 always.
     *
     * Before the game writes the register the defaults from mgs_raster_init
     * stand, which is the power-on state rather than a guess. */
    if (mgs_bp_is_set(&gx->bp, BP_ZMODE)) {
        uint32_t zm = mgs_bp_get(&gx->bp, BP_ZMODE);
        r->depth_test   = (int)(zm & 1u);
        r->depth_func   = (unsigned)((zm >> 1) & 7u);
        r->depth_update = (int)((zm >> 4) & 1u);
    }

    /* THE GAME'S BLEND STATE, per draw.
     *
     * BP 0x41 is CMODE0. It was defined and never read, which meant two
     * different things were being ignored:
     *
     *   - BLENDING. A translucent layer drawn over the scene was written
     *     opaque instead, so a darkening overlay became solid paint.
     *   - COLOUR UPDATE (bit 3). A pass that writes only depth or only alpha
     *     has its colour write MASKED OFF in hardware; ignoring the bit turns
     *     it into visible geometry that covers whatever was underneath.
     *
     * Either explains black where black does not belong, and this game draws
     * 165,888 triangles a run whose combiner is configured to output literal
     * zero (HANDOFF F130). On real hardware those are a mask or a blend; here
     * they were paint.
     *
     * Before the game writes the register, colour and alpha updates default
     * to enabled and blending to off, which is the power-on state. */
    if (mgs_bp_is_set(&gx->bp, BP_BLEND_MODE)) {
        uint32_t cm = mgs_bp_get(&gx->bp, BP_BLEND_MODE);
        r->blend_enable = (int)(cm & 1u);
        r->color_update = (int)((cm >> 3) & 1u);
        r->alpha_update = (int)((cm >> 4) & 1u);
        r->blend_dst    = (unsigned)((cm >> 5) & 7u);
        r->blend_src    = (unsigned)((cm >> 8) & 7u);
        r->blend_sub    = (int)((cm >> 11) & 1u);
        note_value(r->cmode_key, r->cmode_hit, &r->cmode_n, cm);

        /* BLENDING THAT CHANGES NOTHING IS NOT WORTH DOING PER PIXEL.
         *
         * src ONE, dst ZERO computes src * 1 + dst * 0, which is the plain
         * write the fast path already does - and the game uses exactly that
         * on 5,568 draws. Deciding it once per draw keeps the inner loop the
         * shape it was before blending existed, which matters when 562 of
         * 574 million pixels take this path. */
        r->blend_noop = (!r->blend_sub && r->blend_src == 1u &&
                         r->blend_dst == 0u);
    }

    /* WHERE 2D GEOMETRY REACHES, textured and untextured separately.
     *
     * A first version of this counted only TEXTURED triangles and found none
     * whose right edge lands where the text visibly ends - which is the
     * answer to a different question. At roughly 13 textured triangles per
     * frame in this viewport there are nowhere near enough of them to be
     * glyphs, so the text is drawn untextured and a textured-only histogram
     * is blind to it. Counting both is what makes the comparison mean
     * anything.
     *
     * Filled after the screen coordinates exist, which is why it is not up
     * with the other per-triangle counters. */

    /* WHICH VIEWPORTS ARE IN USE, kept as a small set of distinct ones.
     *
     * Every line of text on the memory-card screen stops at x=207-209 while
     * other geometry reaches 442, and the scissor, the depth buffer, the
     * display list and the texture path have each been ruled out. A viewport
     * is the remaining thing that maps clip space onto pixels differently for
     * different passes, so what matters is whether the 2D pass sets its own -
     * and a histogram of distinct viewports answers that in one run. */
    {
        unsigned k;
        uint32_t half_w = gx->viewport[0], ox = gx->viewport[3];
        for (k = 0; k < r->vp_n; ++k)
            if (r->vp_halfw[k] == half_w && r->vp_ox[k] == ox) break;
        if (k == r->vp_n && r->vp_n < 8u) {
            r->vp_halfw[r->vp_n] = half_w;
            r->vp_ox[r->vp_n] = ox;
            r->vp_n++;
        }
        if (k < 8u) ++r->vp_hits[k];
    }

    /* STOP DRAWING ONCE THE HOST HAS BEEN ASKED TO QUIT.
     *
     * Rasterisation happens INSIDE the guest's dispatch call: the game writes
     * to the write-gather pipe, the parser executes the command, and the
     * triangle is filled before the store completes. So a draw call covering
     * a lot of screen holds the run loop for as long as it takes, and the run
     * loop is where the interrupt flag is read - which meant Ctrl-C and
     * `timeout` both appeared to do nothing, and a run that was rendering
     * looked identical to one that had hung.
     *
     * The engine's sphere generator is what surfaced this: 32 strips of 66
     * vertices, over 2,000 triangles, in one uninterrupted stretch. Checking
     * here costs one volatile read per triangle and makes the difference
     * between a process that reports what it drew and one that has to be
     * killed with nothing printed. */
    if (r->abandon && r->abandon()) return;
    mgs_gx_phase = "raster";

    vin[0] = a; vin[1] = b; vin[2] = c;

    for (i = 0; i < 3u; ++i) {
        float view[3], clip[3], w;
        transform(position_matrix(gx, vin[i]->pos_matrix),
                  vin[i]->x, vin[i]->y, vin[i]->z, view);
        project(gx, view, clip, &w);

        /* Anything at or behind the eye cannot be divided by w. Proper
         * near-plane clipping splits the triangle; rejecting it whole is
         * coarser and is honest about being so - it drops geometry that
         * straddles the camera rather than drawing it inside out. */
        if (w <= 0.0001f) {
            ++r->clipped;
            if (r->trace && r->clipped < r->trace_limit)
                fprintf(stderr, "[raster] behind eye: v=(%.3f %.3f %.3f) "
                                "view=(%.3f %.3f %.3f) w=%.4f mtx=%u ortho=%u\n",
                        vin[i]->x, vin[i]->y, vin[i]->z,
                        view[0], view[1], view[2], w,
                        vin[i]->pos_matrix, gx->xf_projection_ortho);
            return;
        }

        iw[i] = 1.0f / w;
        to_screen(r, gx, clip, w, &sx[i], &sy[i], &sz[i]);
    }

    {
        float vp_hw;
        memcpy(&vp_hw, &gx->viewport[0], sizeof vp_hw);
        if (vp_hw > 200.0f) {
            float mx = sx[0] > sx[1] ? sx[0] : sx[1];
            int b;
            if (sx[2] > mx) mx = sx[2];
            b = (int)(mx / 32.0f);
            if (b < 0) b = 0;
            if (b > 19) b = 19;
            ++r->all2d_maxx[b];
        }
    }

    area = edge(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
    if (r->trace && r->drawn + r->clipped < r->trace_limit)
        fprintf(stderr, "[raster] screen: (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) "
                        "area=%.1f vp=(%.1f %.1f %.1f / %.1f %.1f %.1f)\n",
                sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], area,
                f_from_bits(gx->viewport[0]), f_from_bits(gx->viewport[1]),
                f_from_bits(gx->viewport[2]), f_from_bits(gx->viewport[3]),
                f_from_bits(gx->viewport[4]), f_from_bits(gx->viewport[5]));
    if (area == 0.0f) { ++r->clipped; return; }

    /* Back-face culling, by the sign of the signed area. */
    if (r->cull == 1 && area >= 0.0f) { ++r->clipped; return; }
    if (r->cull == 2 && area <= 0.0f) { ++r->clipped; return; }

    minx = sx[0]; maxx = sx[0]; miny = sy[0]; maxy = sy[0];
    for (i = 1; i < 3u; ++i) {
        if (sx[i] < minx) minx = sx[i];
        if (sx[i] > maxx) maxx = sx[i];
        if (sy[i] < miny) miny = sy[i];
        if (sy[i] > maxy) maxy = sy[i];
    }

    x0 = (int)minx; x1 = (int)maxx + 1;
    y0 = (int)miny; y1 = (int)maxy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)r->width) x1 = (int)r->width;
    if (y1 > (int)r->height) y1 = (int)r->height;

    /* THE SCISSOR BOX. Not an optimisation - the hardware will not write
     * outside it, so a renderer that ignores it draws pixels the game did not
     * ask for. That matters most for a render-to-texture pass, which sets a
     * small box in a corner of the embedded framebuffer and would otherwise
     * have its geometry scrawled over everything else in there.
     *
     * Coordinates are biased by 342 - the same bias the viewport carries -
     * and BP 0x59 shifts the box's origin, in units of two pixels, with the
     * same bias again. Both halves are needed: a pass that moves its viewport
     * moves the scissor with it, so honouring one without the other clips
     * against a rectangle that is in the right shape and the wrong place. */
    {
        uint32_t tl  = mgs_bp_get(&gx->bp, BP_SCISSOR_TL);
        uint32_t br  = mgs_bp_get(&gx->bp, BP_SCISSOR_BR);
        uint32_t off = mgs_bp_get(&gx->bp, BP_SCISSOR_OFFSET);

        /* An all-zero scissor is the power-on state, not a request to draw
         * nothing. Until the game has set one, clip to the framebuffer. */
        if ((tl || br) && !r->no_scissor) {
            /* ZERO IS A LEGAL OFFSET, so an unwritten register cannot be read
             * as one. 171 is the value that makes the offset vanish
             * (171 * 2 == 342, the same bias the coordinates carry), which is
             * the right reading of "the game set a box and no origin". Taking
             * the unwritten zero literally shifts the box 342 pixels up and
             * left and clips away most of what should be drawn. */
            unsigned ox = mgs_bp_is_set(&gx->bp, BP_SCISSOR_OFFSET)
                        ? (off & 0x3FFu) : 171u;
            unsigned oy = mgs_bp_is_set(&gx->bp, BP_SCISSOR_OFFSET)
                        ? ((off >> 10) & 0x3FFu) : 171u;
            int sl = (int)((tl >> 12) & 0xFFFu) - (int)(ox * 2u);
            int st = (int)( tl        & 0xFFFu) - (int)(oy * 2u);
            int sr = (int)((br >> 12) & 0xFFFu) - (int)(ox * 2u) + 1;
            int sb = (int)( br        & 0xFFFu) - (int)(oy * 2u) + 1;

            if (sl > x0) x0 = sl;
            if (st > y0) y0 = st;
            if (sr < x1) x1 = sr;
            if (sb < y1) y1 = sb;
        }
    }

    if (x0 >= x1 || y0 >= y1) { ++r->clipped; return; }

    ++r->drawn;

    /* The texture for stage zero. Resolving it once per triangle rather than
     * once per pixel is the difference between a cache lookup and a hash of
     * one; the binding cannot change within a primitive. */
    {
        unsigned map;
        /* A decode is up to a megatexel and happens on a cache miss, so it
         * belongs on the responsive side of the check too. */
        if (r->abandon && r->abandon()) return;
        stage_texture(&gx->bp, 0u, &map, &tex_coord, &tex_enabled);

        /* HOW MUCH IS BEING MISSED BY LOOKING AT STAGE ZERO ONLY.
         *
         * The combiner runs every stage the general-mode register asks for,
         * but exactly one texture is sampled and it is stage zero's. A stage
         * that binds its own texture therefore sees stage zero's texel, or
         * none at all - and "2 textures decoded in a whole boot" is what that
         * looks like from outside. Counted rather than assumed, because the
         * fix (sample per stage) is a different size of job depending on the
         * answer. */
        {
            unsigned st, n = mgs_tev_stage_count(&gx->bp);
            unsigned m2, c2; int on;
            if (n > 16u) n = 16u;
            if (n) ++r->tev_stages[n - 1u];
            if (!tex_enabled) {
                for (st = 1u; st < n; ++st) {
                    stage_texture(&gx->bp, st, &m2, &c2, &on);
                    if (on) { ++r->tex_on_later_stage; break; }
                }
            }
        }

        /* Sampled here, once per triangle, while the registers still hold
         * what this draw used. */
        if (!tex_enabled) {
            note_value(r->cenv_key, r->cenv_hits, &r->cenv_n,
                       mgs_bp_get(&gx->bp, BP_TEV_COLOR_ENV));
            note_value(r->rascol_key, r->rascol_hits, &r->rascol_n,
                       a->color[0]);
        }

        if (tex_enabled) {
            /* SPLIT FROM `textured`, which counts successful binds only.
             * "the game did not ask for a texture" and "the game asked and we
             * could not produce one" are different faults with different
             * fixes, and a single counter cannot tell them apart. */
            ++r->tex_wanted;

            /* WHERE ON SCREEN THE TEXTURED 2D GEOMETRY ACTUALLY REACHES.
             *
             * Every line of text on the memory-card screen stops at x=208,
             * and scissor, depth, display lists, textures and viewport are
             * all now excluded by measurement. What is left is whether the
             * glyphs past that point are drawn at all. Bucketing the right
             * edge of each textured triangle in the MAIN viewport (half-width
             * 256, the 2D pass - not the 64-wide render-to-texture strip)
             * answers it: geometry that exists but is invisible shows up
             * here, geometry that was never emitted does not. */
            /* THE RAW TEXTURE COORDINATE RANGE, and what GX says to divide
             * it by.
             *
             * mgs_tex_sample treats u,v as 0..1 and multiplies by the texture
             * size. GX does not: it normalises by SU_SSIZE+1 / SU_TSIZE+1,
             * the setup-unit size registers at BP 0x30+, which the SDK sets
             * from the texture when the object is loaded but which a game may
             * set to anything. Those registers are referenced nowhere in this
             * renderer, so if they ever differ from the texture size the
             * mapping is wrong by exactly their ratio - and the text shows
             * 120 pixels of a 160-pixel strip, which is 3/4. */
            {
                unsigned su = (unsigned)(0x30u + 2u * tex_coord);
                uint32_t ss = mgs_bp_get(&gx->bp, (uint8_t)su) & 0xFFFFu;
                float u0 = a->u[tex_coord], u1 = b->u[tex_coord], u2 = c->u[tex_coord];
                float lo = u0 < u1 ? (u0 < u2 ? u0 : u2) : (u1 < u2 ? u1 : u2);
                float hi = u0 > u1 ? (u0 > u2 ? u0 : u2) : (u1 > u2 ? u1 : u2);
                if (r->trace_texuse && r->uv_n < 12u) {
                    unsigned k;
                    for (k = 0; k < r->uv_n; ++k)
                        if (r->uv_ss[k] == ss && r->uv_lo[k] == lo && r->uv_hi[k] == hi)
                            break;
                    if (k == r->uv_n) {
                        r->uv_ss[k] = ss; r->uv_lo[k] = lo; r->uv_hi[k] = hi;
                        r->uv_n++;
                    }
                }
            }

            tex = bind_texture(r, gx, map);
            if (!tex) ++r->tex_bind_failed;

            /* WHICH TEXTURE IS NOISE AT THE MOMENT IT IS SAMPLED?
             *
             * The embedded buffer already holds noise by the time it is
             * copied, so the noise arrives through a draw, and a draw can
             * only put there what it samples. Scoring the bound texture at
             * bind time names it directly. Large ones only: a glyph is
             * legitimately busy at this scale and would drown the signal. */
            /* Record every large sampled texture, noisy or not. The
             * question is which draw turns a clean buffer dirty, and that
             * cannot be answered from the noisy ones alone. */
            /* EVERY sampled texture, whatever its size. The buffer turns
             * noisy while the last LARGE texture sampled is clean, so what
             * does it is smaller than the old threshold - or is not a
             * texture at all, which an empty log would say just as clearly. */
            if (tex) {
                unsigned yy, c2 = 0u, r2 = 0u;
                for (yy = 0; yy < tex->height; yy += 16u) {
                    unsigned xx;
                    for (xx = 1u; xx < tex->width; xx += 8u) {
                        uint32_t a = tex->texels[yy * tex->width + xx - 1u];
                        uint32_t b = tex->texels[yy * tex->width + xx];
                        int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF)
                                       + (a & 0xFF)) / 3;
                        int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF)
                                       + (b & 0xFF)) / 3;
                        r2 += (unsigned)(va > vb ? va - vb : vb - va);
                        ++c2;
                    }
                }
                {
                    unsigned i2 = r->drawlog_at & 63u;
                    r->drawlog_addr[i2] = tex->addr;
                    r->drawlog_w[i2] = (uint16_t)tex->width;
                    r->drawlog_h[i2] = (uint16_t)tex->height;
                    r->drawlog_fmt[i2] = (uint8_t)tex->format;
                    r->drawlog_rough[i2] = (uint8_t)(c2 ? (r2 / c2 > 255u ? 255u
                                                          : r2 / c2) : 0u);
                    ++r->drawlog_at;
                }
            }

            if (tex && r->trace_noisy && tex->width >= 256u) {
                unsigned yy, cnt = 0u, rough = 0u;
                for (yy = 0; yy < tex->height; yy += 8u) {
                    unsigned xx;
                    for (xx = 1u; xx < tex->width; xx += 4u) {
                        uint32_t a = tex->texels[yy * tex->width + xx - 1u];
                        uint32_t b = tex->texels[yy * tex->width + xx];
                        int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF)
                                       + (a & 0xFF)) / 3;
                        int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF)
                                       + (b & 0xFF)) / 3;
                        rough += (unsigned)(va > vb ? va - vb : vb - va);
                        ++cnt;
                    }
                }
                if (cnt && (rough / cnt) > 20u && r->noisy_logged < 10u) {
                    /* Only the noisy ones. The clean binds run for thousands
                     * of draws before the picture breaks and would fill any
                     * cap long before the interesting one appeared. */
                    unsigned rr = rough / cnt;
                    ++r->noisy_logged;
                    fprintf(stderr, "[bind] %ux%u fmt 0x%X at 0x%08X  "
                            "roughness %u%s\n", tex->width, tex->height,
                            tex->format, tex->addr, rr,
                            rr > 20u ? "   <-- NOISE" : "");
                }
            }
            if (tex) {
                texture_wrap(&gx->bp, map, &wrap_s, &wrap_t, &bilinear);
                ++r->textured;
            }
        }
    }

    /* WHERE ARE THE PIXELS? Splitting one triangle's scanlines across threads
     * pays only if the large triangles carry the work. If the cost is spread
     * over millions of small ones, the batch has to be divided instead, which
     * is a far larger change. Bucket each triangle's bounding-box area by
     * power of two so the answer is measured rather than assumed. */
    {
        unsigned area = (unsigned)((x1 - x0) * (y1 - y0));
        unsigned b = 0;
        while ((area >> b) > 1u && b < 19u) ++b;
        r->area_tris[b] += 1u;
        r->area_px[b] += area;
    }

    alpha_always = mgs_tev_alpha_test_always(&gx->bp);
    mgs_tev_compile(&gx->bp, &tev);

    /* The weights' setup, done once instead of once per pixel. */
    {
        RasterSpan sp;
        sp.gx = gx; sp.r = r;
        sp.vin[0] = vin[0]; sp.vin[1] = vin[1]; sp.vin[2] = vin[2];
        sp.tex = tex; sp.tex_coord = tex_coord;
        sp.wrap_s = wrap_s; sp.wrap_t = wrap_t; sp.bilinear = bilinear;
        sp.alpha_always = alpha_always; sp.tev = tev;
        memcpy(sp.sx, sx, sizeof sx); memcpy(sp.sy, sy, sizeof sy);
        memcpy(sp.sz, sz, sizeof sz); memcpy(sp.iw, iw, sizeof iw);
        sp.area = area;
        sp.inv_area = 1.0f / area;
        sp.dw0dx = -(sy[2] - sy[1]) * sp.inv_area;
        sp.dw1dx = -(sy[0] - sy[2]) * sp.inv_area;
        sp.x0 = x0; sp.x1 = x1;
        raster_dispatch(r, &sp, y0, y1);
    }
}
