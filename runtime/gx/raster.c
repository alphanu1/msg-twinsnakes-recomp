#include "raster.h"
#include "platform/jobs.h"
#include "fifo.h"
#include "gfx/gpu.h"

#include <time.h>
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
    r->note_pixels = getenv("MGS_TRACE_CENV") != NULL;
    r->trace_behind = getenv("MGS_TRACE_BEHIND") != NULL;
    r->time_raster = getenv("MGS_TRACE_RASTERTIME") != NULL;
    r->gpu = 0;   /* set by the host once the device is up */
    /* The diagnostics added while chasing the video faults, read ONCE like
     * everything else here. Called per draw they were thousands of string
     * lookups a frame, which is a measurable cost to leave behind in a
     * renderer that was profiled down to 10.6s. */
    r->dump_composite = getenv("MGS_DUMP_COMPOSITE");
    r->trace_drawnoise = getenv("MGS_TRACE_DRAWNOISE") != NULL;
    r->trace_drawall = getenv("MGS_TRACE_DRAWALL") != NULL;
    r->dump_drawseq = getenv("MGS_DUMP_DRAWSEQ");
    {
        const char* e = getenv("MGS_TRACE_TEVCFG");
        r->trace_tevcfg = (e && *e) ? (unsigned)strtoul(e, NULL, 0) : 0u;
    }
    {
        const char* e = getenv("MGS_TRACE_DRAWH");
        r->trace_drawh = (e && *e) ? (unsigned)strtoul(e, NULL, 0) : 0u;
        r->trace_drawh_set = e != NULL;
    }
    {
        const char* e = getenv("MGS_TRACE_DRAWCOLOUR");
        r->trace_drawcolour = e != NULL;
        r->trace_drawcolour_from = (e && *e) ? (unsigned)strtoul(e, NULL, 0)
                                             : 0u;
    }
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

/* One vertex partway along the edge to another.
 *
 * EVERY field is interpolated with the same parameter, and that is exact
 * rather than approximate: the modelview and the projection are both linear,
 * so a point at t along an edge in OBJECT space is the same point at t along
 * that edge in clip space. The division by w is what stops being linear, and
 * that happens after this. */
static MgsGxVertex lerp_vertex(const MgsGxVertex* a, const MgsGxVertex* b,
                               float t)
{
    MgsGxVertex o = *a;
    unsigned i;
    o.x = a->x + (b->x - a->x) * t;
    o.y = a->y + (b->y - a->y) * t;
    o.z = a->z + (b->z - a->z) * t;
    o.nx = a->nx + (b->nx - a->nx) * t;
    o.ny = a->ny + (b->ny - a->ny) * t;
    o.nz = a->nz + (b->nz - a->nz) * t;
    for (i = 0; i < 2u; ++i) {
        unsigned ch;
        uint32_t out = 0;
        for (ch = 0; ch < 4u; ++ch) {
            unsigned sh = ch * 8u;
            float ca = (float)((a->color[i] >> sh) & 0xFFu);
            float cb = (float)((b->color[i] >> sh) & 0xFFu);
            float v = ca + (cb - ca) * t;
            unsigned q = (unsigned)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
            out |= (uint32_t)q << sh;
        }
        o.color[i] = out;
    }
    for (i = 0; i < 8u; ++i) {
        o.u[i] = a->u[i] + (b->u[i] - a->u[i]) * t;
        o.v[i] = a->v[i] + (b->v[i] - a->v[i]) * t;
    }
    return o;
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
    int                 note_pixels;
    /* PER-STAGE TEXTURES. `stage_count` is 0 for the single-texture case,
     * which is all but 64 triangles in a boot, and then none of this is
     * touched. See the note where these are filled in. */
    const MgsTexture*   stage_tex[8];
    unsigned            stage_coord[8], stage_ws[8], stage_wt[8];
    unsigned char       stage_bilinear[8];
    unsigned            stage_count;
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
                uint32_t stex[8];
                uint8_t  shas[8];

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

                /* AND ONE TEXEL PER STAGE where the stages bind different
                 * maps. Each has its own coordinate set and its own wrap and
                 * filter state, so each is a separate interpolation and a
                 * separate sample - there is no shortcut that reuses stage
                 * zero's. */
                in.stage_tex = NULL;
                in.stage_has = NULL;
                if (sp->stage_count) {
                    unsigned st;
                    for (st = 0; st < sp->stage_count; ++st) {
                        const MgsTexture* t2 = sp->stage_tex[st];
                        unsigned cs = sp->stage_coord[st];
                        if (!t2) { stex[st] = 0xFFFFFFFFu; shas[st] = 0; continue; }
                        {
                            float u2 = k0 * vin[0]->u[cs] + k1 * vin[1]->u[cs] +
                                       k2 * vin[2]->u[cs];
                            float v2 = k0 * vin[0]->v[cs] + k1 * vin[1]->v[cs] +
                                       k2 * vin[2]->v[cs];
                            stex[st] = mgs_tex_sample(t2, u2, v2,
                                                      sp->stage_ws[st],
                                                      sp->stage_wt[st],
                                                      sp->stage_bilinear[st]);
                            shas[st] = 1;
                        }
                    }
                    in.stage_tex = stex;
                    in.stage_has = shas;
                }

                pixel = mgs_tev_run_compiled(tev, &in);

                /* WHAT COLOUR ACTUALLY LANDS, for untextured draws.
                 *
                 * Everything upstream says these should be white: the
                 * combiner takes the rasterised colour straight through,
                 * the vertex colour is 0xFFFFFFFF on all 10.3 million of
                 * them, they are opaque, blending is off, nothing is
                 * depth-rejected or masked, and 10.6 million of them land
                 * fully on screen. The buffer never goes above 39. One of
                 * those statements is wrong and this is the one place that
                 * can say which - the value at the moment it is written.
                 *
                 * Sampled one pixel in 1024 and racy across the worker
                 * threads, deliberately: the question is WHICH VALUES
                 * appear, and a lost count does not change the answer. */
                if (sp->note_pixels && !sp->tex) {
                    static unsigned n;
                    if (((n++) & 1023u) == 0u)
                        note_value(sp->r->outc_key, sp->r->outc_hits,
                                   &sp->r->outc_n, pixel);
                }

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

/* MGS_TRACE_RASTERTIME: how long the GUEST THREAD spends in here.
 *
 * The machine has 32 cores and the renderer uses three and a half, and
 * there are two very different reasons that could be: the rasteriser is
 * slow and serial, or the rasteriser is fine and the guest is the
 * bottleneck. Rasterisation happens INSIDE the guest's dispatch call and
 * the band barrier is on the guest thread, so the time spent here is time
 * the guest is not running - which is the number that decides it. */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void raster_dispatch(MgsGxRaster* r, const RasterSpan* sp, int y0, int y1)
{
    RasterBand band[64];
    MgsRasterTally total;
    unsigned n, i;
    int rows = y1 - y0;
    uint64_t t_in = r->time_raster ? now_ns() : 0;

    memset(&total, 0, sizeof total);

    n = r->max_bands;
    if (n > 64u) n = 64u;
    if (n > (unsigned)(rows / RASTER_BAND_MIN_ROWS)) n = (unsigned)(rows / RASTER_BAND_MIN_ROWS);
    if (!r->jobs || n < 2u) {
        raster_span(sp, y0, y1, &total);
        tally_fold(r, &total);
        if (r->time_raster) {
            r->ns_serial += now_ns() - t_in;
            ++r->tris_serial;
        }
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
    if (r->time_raster) {
        r->ns_banded += now_ns() - t_in;
        ++r->tris_banded;
        r->bands_total += n;
    }
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

/* ---- the combiner, in the shape the GPU's fragment shader reads ---------
 *
 * gxtev.frag INTERPRETS the TEV state rather than being generated from it,
 * so what crosses to the GPU is this block rather than GLSL. Building it is
 * a pure function of the BP registers, which is why it is cached on
 * `bp->rev`: the combiner changes a few times a frame and the draws between
 * those changes number in the thousands.
 *
 * The batch KEY is a hash of the block, not the revision. A revision
 * changes when any register is written - a texture address, say - and
 * keying on that would end the batch for changes the shader cannot see.
 * Hashing the block means two draws with the same combiner batch together
 * however they arrived at it.
 */
static uint64_t tev_block_hash(const MgsGpuTev* t)
{
    const unsigned char* p = (const unsigned char*)t;
    size_t i, n = sizeof *t;
    uint64_t h = 1469598103934665603ull;         /* FNV-1a */
    for (i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h ? h : 1ull;
}

static void tev_block_build(const MgsGxBp* bp, const MgsTevCompiled* t,
                            MgsGpuTev* out)
{
    unsigned i, s;

    memset(out, 0, sizeof *out);
    for (i = 0; i < 4u; ++i) {
        out->reg[i][0] = t->reg[i][0];
        out->reg[i][1] = t->reg[i][1];
        out->reg[i][2] = t->reg[i][2];
        out->reg[i][3] = t->reg[i][3];
        out->swap[i][0] = (int32_t)t->swap[i][0];
        out->swap[i][1] = (int32_t)t->swap[i][1];
        out->swap[i][2] = (int32_t)t->swap[i][2];
        out->swap[i][3] = (int32_t)t->swap[i][3];
    }
    for (s = 0; s < 16u; ++s) {
        int on = (s < t->stages);
        out->env[s][0]   = on ? t->ce[s] : 0u;
        out->env[s][1]   = on ? t->ae[s] : 0u;
        out->konst[s][0] = on ? t->kc[s][0] : 0;
        out->konst[s][1] = on ? t->kc[s][1] : 0;
        out->konst[s][2] = on ? t->kc[s][2] : 0;
        out->konst[s][3] = on ? t->ka[s] : 0;
    }
    out->ctl[0] = (int32_t)t->stages;
    out->ctl[1] = t->configured;
    out->ctl[3] = t->swap_set;

    /* The alpha test, which the shader does with `discard`. Whether it
     * tests at all is decided here for the same reason the CPU path asks:
     * this game writes ALPHA_COMPARE = 0x3F0000 and kills no pixels in a
     * whole run, and a discard in the shader would cost the early-depth
     * rejection for nothing. */
    if (!mgs_tev_alpha_test_always(bp)) {
        uint32_t r = mgs_bp_get(bp, BP_ALPHA_COMPARE);
        out->atest[0]  = (int32_t)(r & 0xFFu);
        out->atest[1]  = (int32_t)((r >> 8) & 0xFFu);
        out->atest[2]  = (int32_t)((r >> 16) & 7u);
        out->atest[3]  = (int32_t)((r >> 19) & 7u);
        out->atest2[0] = (int32_t)((r >> 22) & 3u);
        out->atest2[1] = 1;
    }
}

void mgs_raster_triangle(MgsGx* gx, const MgsGxVertex* a,
                         const MgsGxVertex* b, const MgsGxVertex* c)
{
    MgsGxRaster* r = (MgsGxRaster*)gx->user;
    const MgsGxVertex* vin[3];
    const MgsTexture* tex = NULL;
    unsigned tex_coord = 0, wrap_s = 0, wrap_t = 0;
    const MgsTexture* stage_tex[8];
    unsigned stage_coord[8], stage_ws[8], stage_wt[8], stage_n = 0u;
    unsigned char stage_bil[8];
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

    /* NEAR-PLANE CLIPPING, instead of dropping the triangle whole.
     *
     * A vertex at or behind the eye cannot be divided by w, and what this
     * did was reject the whole triangle. Honest, and expensive: **5,508,491
     * of 16,223,099 triangles - 34% of everything submitted - were thrown
     * away** in one run of the opening. A scene with the camera inside it
     * loses most of its environment that way, which is what an empty
     * cinematic looks like.
     *
     * Clipping is done by SUBSTITUTION rather than by a separate clip-space
     * path: the crossing point is found from w, and a replacement vertex is
     * built by interpolating the original's own fields at that parameter.
     * That is exact, because everything before the divide is linear - see
     * lerp_vertex. The replacements then go through the identical route,
     * which is what keeps this from becoming a second renderer.
     *
     * The clip plane sits ABOVE the rejection threshold (0.001 against
     * 0.0001) so a vertex produced here always passes the test below, and
     * the recursion is one level deep and cannot repeat. */
    {
        /* THE RECURSION HAS TO BE BOUNDED, and the first version was not.
         *
         * A replacement vertex sits exactly ON the near plane in exact
         * arithmetic, and in floating point it can land a hair behind it.
         * Then the clip fires again on the triangle it just produced, and
         * again, until the stack is gone: the run died silently with no exit
         * report, always at the same point, which is what that looks like.
         *
         * One level is all the geometry needs - a triangle crossing the
         * plane yields pieces that are entirely in front of it - so a second
         * level means the arithmetic disagreed with itself, and rejecting
         * there is both safe and rare. */
        static int depth;
        const float near_w = 0.001f;
        float wv[3];
        unsigned behind = 0u;

        for (i = 0; i < 3u; ++i) {
            float view[3], clipv[3];
            transform(position_matrix(gx, vin[i]->pos_matrix),
                      vin[i]->x, vin[i]->y, vin[i]->z, view);
            project(gx, view, clipv, &wv[i]);
            if (wv[i] < near_w) ++behind;
        }
        if (behind == 3u) {
            /* HOW FAR behind, and whether it is scene geometry.
             *
             * A third of everything submitted is rejected here, and "behind
             * the eye" means two very different things depending on the
             * number: a few units behind is a camera sitting inside the
             * geometry, and thousands of units behind is a transform that
             * has put the world in the wrong place. */
            ++r->clipped;
            {   /* WHICH MATRIX, AND IS IT EMPTY?
                 *
                 * 5,416,809 of the 5.5 million rejected triangles are less
                 * than ONE unit behind the eye, which is not "behind the
                 * camera" - it is w == 0. A transform that returns zero does
                 * that, and an unloaded position matrix is all zeroes. */
                const float* m = position_matrix(gx, vin[0]->pos_matrix);
                int zero = 1; unsigned q;
                for (q = 0; q < 12u; ++q) if (m[q] != 0.0f) { zero = 0; break; }
                if (zero) ++r->behind_zero_matrix;
                /* MGS_TRACE_BEHIND: a few of them in full. Five million
                 * triangles landing at w == 0 with a matrix that is not
                 * empty means either the positions or the matrix is not
                 * what it should be, and only the numbers say which. */
                if (r->trace_behind && r->behind_shown < 6u) {
                    float view0[3];
                    ++r->behind_shown;
                    transform(m, vin[0]->x, vin[0]->y, vin[0]->z, view0);
                    fprintf(stderr,
                        "[behind] obj (%.2f,%.2f,%.2f) mtx %u  ->  view "
                        "(%.3f,%.3f,%.3f)  w %.5f  ortho %u\n"
                        "[behind]   matrix %.3f %.3f %.3f %.3f / %.3f %.3f "
                        "%.3f %.3f / %.3f %.3f %.3f %.3f\n",
                        vin[0]->x, vin[0]->y, vin[0]->z, vin[0]->pos_matrix,
                        view0[0], view0[1], view0[2], wv[0],
                        gx->xf_projection_ortho,
                        m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
                        m[8], m[9], m[10], m[11]);
                }
                if (vin[0]->pos_matrix < 64u)
                    ++r->behind_mtx[vin[0]->pos_matrix / 8u];
            }
            {
                float worst = wv[0] < wv[1] ? (wv[0] < wv[2] ? wv[0] : wv[2])
                                            : (wv[1] < wv[2] ? wv[1] : wv[2]);
                unsigned b2 = 0u;
                float m = -worst;
                while (m >= 1.0f && b2 < 15u) { m /= 8.0f; ++b2; }
                ++r->behind_mag[b2];
            }
            return;
        }
        if (behind && depth >= 1) { ++r->clipped; return; }
        if (behind) {
            MgsGxVertex poly[4];
            unsigned n = 0u;
            for (i = 0; i < 3u; ++i) {
                unsigned j = (i + 1u) % 3u;
                int in_i = wv[i] >= near_w, in_j = wv[j] >= near_w;
                if (in_i && n < 4u) poly[n++] = *vin[i];
                if (in_i != in_j && n < 4u) {
                    float d = wv[j] - wv[i];
                    float t = (d != 0.0f) ? (near_w - wv[i]) / d : 0.0f;
                    if (t < 0.0f) t = 0.0f;
                    if (t > 1.0f) t = 1.0f;
                    poly[n++] = lerp_vertex(vin[i], vin[j], t);
                }
            }
            ++r->near_clipped;
            ++depth;
            if (n >= 3u) {
                mgs_raster_triangle(gx, &poly[0], &poly[1], &poly[2]);
                if (n == 4u)
                    mgs_raster_triangle(gx, &poly[0], &poly[2], &poly[3]);
            }
            --depth;
            return;
        }
    }

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

    /* WHERE UNTEXTURED GEOMETRY LANDS, BEFORE ANY CLAMPING.
     *
     * 7.8 million triangles a run are drawn white, fully opaque, with
     * blending off and nothing rejecting them - and the screen is not
     * white. Either they are painted and covered, or they are not on the
     * screen at all, and x0/x1 below cannot tell the difference because
     * they are already clamped to it. These are the raw projected extents.
     *
     * Counted by where the box sits relative to the viewport, which is the
     * only distinction that matters here. */
    mgs_gx_order_note(!tex_enabled ? 's'
                      : ((maxx - minx) > 400.0f ? 'Q' : 'q'));
    if (!tex_enabled) {
        if (maxx < 0.0f || minx > (float)r->width ||
            maxy < 0.0f || miny > (float)r->height)
            ++r->untex_offscreen;
        else if (minx >= 0.0f && maxx <= (float)r->width &&
                 miny >= 0.0f && maxy <= (float)r->height)
            ++r->untex_onscreen;
        else
            ++r->untex_straddle;
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

            r->scissor_box[0] = sl; r->scissor_box[1] = st;
            r->scissor_box[2] = sr; r->scissor_box[3] = sb;
            r->scissor_seen = 1;
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

            /* RESOLVE EVERY STAGE, not just stage zero.
             *
             * This game's movie composites its video frame from three maps
             * in three TEV stages - a luminance plane and two chroma planes.
             * With one texel shared by all three, two stages sampled luma
             * and the picture came out green-and-magenta striped over a
             * correct-looking luminance structure, which is exactly what it
             * looked like on screen.
             *
             * Only done when more than one stage is enabled, and the arrays
             * stay empty otherwise: 2,491,121 of 2,491,185 triangles in a
             * boot are single-stage and must not pay for this. */
            stage_n = 0u;
            if (n > 1u) {
                unsigned k, lim = n > 8u ? 8u : n;
                int any_extra = 0;
                for (k = 0; k < lim; ++k) {
                    unsigned mk, ck; int onk;
                    stage_texture(&gx->bp, k, &mk, &ck, &onk);
                    stage_tex[k] = NULL;
                    stage_coord[k] = ck;
                    stage_ws[k] = 0u; stage_wt[k] = 0u; stage_bil[k] = 0u;
                    if (!onk) continue;
                    stage_tex[k] = bind_texture(r, gx, mk);
                    if (stage_tex[k]) {
                        unsigned ws2, wt2; int bi2;
                        texture_wrap(&gx->bp, mk, &ws2, &wt2, &bi2);
                        stage_ws[k] = ws2; stage_wt[k] = wt2;
                        stage_bil[k] = (unsigned char)bi2;
                        if (k) any_extra = 1;
                    }
                }
                /* Nothing gained if only stage zero ever binds one. */
                if (any_extra) { stage_n = lim; ++r->multi_tex_tris; }
            }
        }

        /* Sampled here, once per triangle, while the registers still hold
         * what this draw used. */
        if (!tex_enabled) {
            note_value(r->cenv_key, r->cenv_hits, &r->cenv_n,
                       mgs_bp_get(&gx->bp, BP_TEV_COLOR_ENV));
            note_value(r->rascol_key, r->rascol_hits, &r->rascol_n,
                       a->color[0]);
            /* WHAT THE BLEND DOES TO THEM.
             *
             * 883 of 1,371 million pixels go through blending, none are
             * depth-rejected and none are masked - so the untextured
             * geometry IS drawn and the blend is what decides whether any
             * of it survives. Its colour combiner outputs the rasterised
             * colour, which is white; the screen is not white. The alpha
             * and the two factors are the only things left that can make
             * white invisible, so they are counted here beside the colour
             * environment that was already being counted. */
            note_value(r->aenv_key, r->aenv_hits, &r->aenv_n,
                       mgs_bp_get(&gx->bp, BP_TEV_ALPHA_ENV));
            note_value(r->blend_key, r->blend_hits, &r->blend_n,
                       (uint32_t)((r->blend_enable && !r->blend_noop) ? 0x10000u : 0u)
                       | (uint32_t)(r->blend_src << 4) | (uint32_t)r->blend_dst
                       | (uint32_t)(r->blend_sub ? 0x20000u : 0u));
            {   /* And the alpha the combiner actually produces. */
                MgsTevCompiled tc3;
                MgsTevInput ti3;
                mgs_tev_compile(&gx->bp, &tc3);
                memset(&ti3, 0, sizeof ti3);
                ti3.raster = a->color[0];
                ti3.texture = 0xFFFFFFFFu;
                note_value(r->outa_key, r->outa_hits, &r->outa_n,
                           (mgs_tev_run_compiled(&tc3, &ti3) >> 24) & 0xFFu);
            }
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

            /* MGS_DUMP_COMPOSITE=<dir>: the three planes a composite draw
             * binds, and the buffer it produces, FROM THE SAME MOMENT.
             *
             * Comparing the port's frame against a reference built from
             * planes dumped at a different instant cannot settle a colour
             * question - the two are different pictures. These are the
             * actual inputs to one draw, so the expected colour can be
             * computed from them and compared pixel for pixel.
             *
             * Rule 8: game data. Scratchpad only, never the tree. */
            if (tex && stage_n >= 3u && tex->height == 320u &&
                r->dump_composite) {
                static unsigned done;
                const char* dir = r->dump_composite;
                if (done < 2u) {
                    unsigned k;
                    char path[512];
                    for (k = 0; k < 3u; ++k) {
                        const MgsTexture* st = stage_tex[k];
                        FILE* f;
                        if (!st) continue;
                        snprintf(path, sizeof path, "%s/plane%u_%u.pgm",
                                 dir, done, k);
                        f = fopen(path, "wb");
                        if (!f) continue;
                        fprintf(f, "P5\n%u %u\n255\n", st->width, st->height);
                        {
                            unsigned px;
                            for (px = 0; px < st->width * st->height; ++px)
                                fputc((int)(st->texels[px] & 0xFFu), f);
                        }
                        fclose(f);
                        fprintf(stderr, "[composite] %u stage %u: %ux%u "
                                "fmt 0x%X at 0x%08X -> %s\n", done, k,
                                st->width, st->height, st->format, st->addr,
                                path);
                    }
                    r->composite_dump = ++done;   /* write the buffer after */
                }
            }

            /* MGS_TRACE_TEVCFG=<w>: the whole combiner configuration for
             * the first draws that bind a texture that wide. The movie's
             * composite is three stages over a luma plane and two chroma
             * planes, and "the colour is wrong" cannot be narrowed without
             * seeing what those stages were asked to compute. */
            if (tex && r->trace_tevcfg) {
                static unsigned shown;
                unsigned want = r->trace_tevcfg;
                if (tex->height == want && shown < 6u) {
                    MgsTevCompiled tc;
                    unsigned st;
                    ++shown;
                    mgs_tev_compile(&gx->bp, &tc);
                    fprintf(stderr, "[tevcfg] texture %ux%u fmt 0x%X at "
                            "0x%08X, %u stages, %u extra textures, "
                            "blend %s (src %u dst %u%s)\n",
                            tex->width, tex->height, tex->format, tex->addr,
                            tc.stages, stage_n,
                            (r->blend_enable && !r->blend_noop) ? "ON" : "off",
                            r->blend_src, r->blend_dst,
                            r->blend_sub ? ", subtract" : "");
                    for (st = 0; st < tc.stages; ++st) {
                        uint32_t ce = tc.ce[st], ae = tc.ae[st];
                        fprintf(stderr,
                            "[tevcfg]  stage %u colour: a=%u b=%u c=%u d=%u "
                            "op=%u bias=%u scale=%u clamp=%u dest=%u "
                            "konst=(%d,%d,%d)\n", st,
                            (ce >> 12) & 0xFu, (ce >> 8) & 0xFu,
                            (ce >> 4) & 0xFu, ce & 0xFu,
                            (ce >> 18) & 1u, (ce >> 16) & 3u,
                            (ce >> 20) & 3u, (ce >> 19) & 1u,
                            (ce >> 22) & 3u,
                            tc.kc[st][0], tc.kc[st][1], tc.kc[st][2]);
                        fprintf(stderr, "[tevcfg]  stage %u swap: "
                                "raster table %u, texture table %u\n", st,
                                ae & 3u, (ae >> 2) & 3u);
                        fprintf(stderr,
                            "[tevcfg]  stage %u alpha : a=%u b=%u c=%u d=%u "
                            "op=%u bias=%u scale=%u clamp=%u dest=%u "
                            "konst=%d\n", st,
                            (ae >> 13) & 7u, (ae >> 10) & 7u,
                            (ae >> 7) & 7u, (ae >> 4) & 7u,
                            (ae >> 18) & 1u, (ae >> 16) & 3u,
                            (ae >> 20) & 3u, (ae >> 19) & 1u,
                            (ae >> 22) & 3u, tc.ka[st]);
                    }
                    for (st = 0; st < 4u; ++st)
                        fprintf(stderr, "[tevcfg]  register %u = "
                                "(%d,%d,%d,a %d)   konst %u = "
                                "(%d,%d,%d,a %d)\n", st, tc.reg[st][0],
                                tc.reg[st][1], tc.reg[st][2], tc.reg[st][3],
                                st, gx->bp.konst[st][0], gx->bp.konst[st][1],
                                gx->bp.konst[st][2], gx->bp.konst[st][3]);
                    for (st = 0; st < 8u; ++st)
                        fprintf(stderr, "[tevcfg]  KSEL[%u] (0x%02X) = "
                                "0x%06X\n", st, 0xF6u + st,
                                mgs_bp_get(&gx->bp, (uint8_t)(0xF6u + st)));
                }
            }

            /* WHICH DRAW TURNS THE BUFFER NOISY?
             *
             * Every trace so far scores the buffer once a frame and the
             * texture at bind time, and both said the same thing: the
             * texture is clean and the buffer is noise. Neither can name the
             * draw BETWEEN them. This scores the embedded buffer either side
             * of one draw, so the draw that does it says so itself.
             *
             * Only full-screen draws - the movie composite is one - because
             * scoring the buffer around every primitive costs more than the
             * frame. */
            /* MGS_TRACE_DRAWCOLOUR: which draw changes the buffer's COLOUR
             * BALANCE. The composite leaves a blue-ish picture in the right
             * proportions, and what reaches the screen is purple, so
             * something after it moves the channels apart. Same method as
             * the noise trace - score the buffer either side of one
             * full-screen draw - with the channels kept separate. */
            /* MGS_TRACE_DRAWH=<height> narrows this to one kind of draw.
             * The composite binds a 512x320 luma plane; the quad that
             * presents the finished frame binds a 512x448 surface. Watching
             * both at once buries whichever is being asked about. */
            {
                unsigned want_h = r->trace_drawh;
                /* MGS_TRACE_DRAWALL widens this to EVERY draw, textured
                 * or not. The big-texture filter was hiding the answer: a
                 * room's background can be untextured geometry, and a
                 * full-screen tint can come from a draw that binds nothing
                 * at all. Sampling is coarser here because this runs around
                 * thousands of draws a frame rather than a handful. */
                r->col_watch = r->trace_drawall
                    ? 1
                    : (tex && tex->width >= 256u &&
                       (want_h ? tex->height == want_h
                               : tex->height >= 256u));
            }
            if (r->col_watch && r->trace_drawcolour) {
                unsigned yy, xx3, cnt = 0u;
                unsigned long sr = 0, sg = 0, sb = 0;
                for (yy = 0; yy < MGS_EFB_HEIGHT; yy += 16u)
                    for (xx3 = 0; xx3 < MGS_EFB_WIDTH; xx3 += 8u) {
                        uint32_t v = r->efb->pixels[yy * MGS_EFB_WIDTH + xx3];
                        sr += (v >> 16) & 0xFFu;
                        sg += (v >> 8) & 0xFFu;
                        sb += v & 0xFFu;
                        ++cnt;
                    }
                r->col_before[0] = (unsigned)(sr / (cnt ? cnt : 1u));
                r->col_before[1] = (unsigned)(sg / (cnt ? cnt : 1u));
                r->col_before[2] = (unsigned)(sb / (cnt ? cnt : 1u));
                r->col_armed = 1;
                {   /* What the quad is about to paint, and how strongly. */
                    unsigned k2, m = 0; unsigned long tr = 0, tg = 0, tb = 0;
                    MgsTevCompiled tc2; MgsTevInput ti2;
                    unsigned long ta = 0;
                    mgs_tev_compile(&gx->bp, &tc2);
                    memset(&ti2, 0, sizeof ti2);
                    for (k2 = 0; k2 < tex->width * tex->height; k2 += 997u) {
                        uint32_t c2 = tex->texels[k2];
                        tr += (c2 >> 16) & 0xFFu;
                        tg += (c2 >> 8) & 0xFFu;
                        tb += c2 & 0xFFu;
                        ti2.texture = c2; ti2.has_texture = 1;
                        ti2.raster = 0xFFFFFFFFu;
                        ta += (mgs_tev_run_compiled(&tc2, &ti2) >> 24) & 0xFFu;
                        ++m;
                    }
                    if (m) { r->col_tex[0] = (unsigned)(tr / m);
                             r->col_tex[1] = (unsigned)(tg / m);
                             r->col_tex[2] = (unsigned)(tb / m);
                             r->col_tex_a  = (unsigned)(ta / m); }
                }
                r->noise_tex_addr = tex->addr;
                r->noise_tex_fmt = (uint8_t)tex->format;
                r->noise_tex_w = (uint16_t)tex->width;
                r->noise_tex_h = (uint16_t)tex->height;
            }
            /* Deliberately NOT disarmed here. A multi-stage draw binds
             * several textures, and the composite's last bind is a 256x160
             * chroma plane - clearing on a bind that does not match threw
             * away the arm the luma bind had just made, and the composite
             * draws never reported at all. The end of the draw disarms. */

            if (tex && tex->width >= 256u && r->trace_drawnoise) {
                static unsigned said;
                unsigned yy, xx2, cnt = 0u, before = 0u;
                for (yy = 0; yy < MGS_EFB_HEIGHT; yy += 16u)
                    for (xx2 = 1u; xx2 < MGS_EFB_WIDTH; xx2 += 8u) {
                        uint32_t a1 = r->efb->pixels[yy * MGS_EFB_WIDTH + xx2 - 1u];
                        uint32_t b1 = r->efb->pixels[yy * MGS_EFB_WIDTH + xx2];
                        int va = (int)(((a1 >> 16) & 0xFF) + ((a1 >> 8) & 0xFF)
                                       + (a1 & 0xFF)) / 3;
                        int vb = (int)(((b1 >> 16) & 0xFF) + ((b1 >> 8) & 0xFF)
                                       + (b1 & 0xFF)) / 3;
                        before += (unsigned)(va > vb ? va - vb : vb - va);
                        ++cnt;
                    }
                r->noise_before = cnt ? before / cnt : 0u;
                r->noise_tex_addr = tex->addr;
                r->noise_tex_fmt = tex->format;
                r->noise_tex_w = (uint16_t)tex->width;
                r->noise_tex_h = (uint16_t)tex->height;
                r->noise_armed = said < 12u ? 1 : 0;
                r->noise_said = &said;
            } else {
                r->noise_armed = 0;
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
                {   /* The span in TEXELS, not in normalised units: a
                     * coordinate range of 0.01 is a whole texel on a 128-wide
                     * texture and a tenth of one on a 16-wide. */
                    float u0 = vin[0]->u[tex_coord], u1 = vin[1]->u[tex_coord];
                    float u2 = vin[2]->u[tex_coord];
                    float v0 = vin[0]->v[tex_coord], v1 = vin[1]->v[tex_coord];
                    float v2 = vin[2]->v[tex_coord];
                    float umin = u0 < u1 ? (u0 < u2 ? u0 : u2)
                                         : (u1 < u2 ? u1 : u2);
                    float umax = u0 > u1 ? (u0 > u2 ? u0 : u2)
                                         : (u1 > u2 ? u1 : u2);
                    float vmin = v0 < v1 ? (v0 < v2 ? v0 : v2)
                                         : (v1 < v2 ? v1 : v2);
                    float vmax = v0 > v1 ? (v0 > v2 ? v0 : v2)
                                         : (v1 > v2 ? v1 : v2);
                    float du = (umax - umin) * (float)tex->width;
                    float dv = (vmax - vmin) * (float)tex->height;
                    float d = du > dv ? du : dv;
                    unsigned b = 0;
                    if (d < 0.0f) d = -d;
                    while (d >= 1.0f && b < 11u) { d *= 0.5f; ++b; }
                    r->uv_span[b] += 1u;
                }
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
    /* THE GPU PATH, when one is up.
     *
     * The transform stays here - `transform`, `project` and `to_screen` are
     * verified against Dolphin and against the game's own numbers - and
     * only the FILLING goes to the GPU. Screen pixels are turned back into
     * clip space by the inverse of the mapping just applied, multiplied by
     * w, so the GPU's own divide reproduces exactly the same position AND
     * perspective correction is preserved. Passing screen coordinates with
     * w = 1 would be simpler and would make every texture swim.
     *
     * Depth goes through as 0..1, which is what SDL's GPU expects and what
     * `to_screen` already produces.
     *
     * The colour is the rasterised vertex colour and the texture is stage
     * 0's. That is the base shader's model, not the combiner's: a full
     * TEV-to-shader generator is the next piece, and until it exists the
     * GPU path is checkable but not correct for multi-stage draws. */
    if (r->gpu) {
        MgsGpuVertex gv[3];
        MgsGpuBind   binds[MGS_GPU_TEX_UNITS];
        unsigned k;
        unsigned unit_of_stage[16];
        unsigned unit_coord[MGS_GPU_TEX_UNITS];
        unsigned units = 0u;
        float tw = (float)r->width, th = (float)r->height;

        /* WHICH TEXTURE EACH STAGE SAMPLES, resolved into at most four
         * units.
         *
         * A TEV stage names its own map AND its own coordinate generator,
         * and the movie's composite is three stages over three maps. Two
         * stages naming the same (map, coordinate) pair share a unit -
         * which is why this deduplicates rather than handing stage k unit
         * k: five-stage draws are common in this game and would otherwise
         * overflow four units without needing to. */
        memset(binds, 0, sizeof binds);
        for (k = 0; k < 16u; ++k) unit_of_stage[k] = 0u;
        if (stage_n) {
            unsigned st;
            for (st = 0; st < stage_n && st < 16u; ++st) {
                const MgsTexture* sx = stage_tex[st];
                unsigned c = stage_coord[st], u;
                if (!sx) { unit_of_stage[st] = 0u; continue; }
                /* The pointer as well as the key: a content hash of zero
                 * means "not cacheable", and two such textures must not be
                 * folded into one unit by both hashing to nothing. */
                for (u = 0; u < units; ++u)
                    if (binds[u].key == sx->hash &&
                        binds[u].texels == sx->texels &&
                        unit_coord[u] == c) break;
                if (u == units) {
                    if (units >= MGS_GPU_TEX_UNITS) {
                        /* More distinct maps than units. Counted, not
                         * silently folded onto unit 0 and forgotten. */
                        ++r->tex_units_overflowed;
                        unit_of_stage[st] = 0u;
                        continue;
                    }
                    binds[u].texels = sx->texels;
                    binds[u].w = sx->width; binds[u].h = sx->height;
                    binds[u].key = sx->hash;
                    binds[u].wrap_s = (unsigned char)stage_ws[st];
                    binds[u].wrap_t = (unsigned char)stage_wt[st];
                    binds[u].bilinear = stage_bil[st];
                    unit_coord[u] = c;
                    ++units;
                }
                unit_of_stage[st] = u;
            }
        }
        if (!units) {
            /* Single-stage, or no stage bound one: stage zero's texture on
             * unit zero, which is what every other draw here does. */
            binds[0].texels = tex ? tex->texels : NULL;
            binds[0].w = tex ? tex->width : 0u;
            binds[0].h = tex ? tex->height : 0u;
            binds[0].key = tex ? tex->hash : 0ull;
            binds[0].wrap_s = (unsigned char)wrap_s;
            binds[0].wrap_t = (unsigned char)wrap_t;
            binds[0].bilinear = (unsigned char)(bilinear ? 1 : 0);
            unit_coord[0] = tex_coord;
            units = 1u;
        }
        for (k = units; k < MGS_GPU_TEX_UNITS; ++k) unit_coord[k] = tex_coord;
        for (k = 0; k < 3u; ++k) {
            float w1 = (iw[k] != 0.0f) ? 1.0f / iw[k] : 1.0f;
            float ndx = (sx[k] / tw) * 2.0f - 1.0f;
            /* SDL's GPU normalised device space has +1 at the TOP, so a
             * screen row counted downwards is negated here. Measured, not
             * assumed: the first version mapped it straight through and
             * every logo came out upside down. */
            float ndy = 1.0f - (sy[k] / th) * 2.0f;
            uint32_t col = vin[k]->color[0];
            gv[k].x = ndx * w1;
            gv[k].y = ndy * w1;
            gv[k].z = sz[k] * w1;
            gv[k].w = w1;
            gv[k].r = (float)((col >> 16) & 0xFFu) / 255.0f;
            gv[k].g = (float)((col >> 8) & 0xFFu) / 255.0f;
            gv[k].b = (float)(col & 0xFFu) / 255.0f;
            gv[k].a = (float)((col >> 24) & 0xFFu) / 255.0f;
            {   /* One set of coordinates per unit, each read from the
                 * generator that unit's stage named. */
                unsigned u;
                for (u = 0; u < MGS_GPU_TEX_UNITS; ++u) {
                    unsigned c = unit_coord[u] < 8u ? unit_coord[u] : 0u;
                    int have = binds[u].texels != NULL;
                    gv[k].uv[u][0] = have ? vin[k]->u[c] : 0.0f;
                    gv[k].uv[u][1] = have ? vin[k]->v[c] : 0.0f;
                }
            }
        }
        {   /* The draw state the game asked for, which the pipeline has
             * to bake in. Leaving it out drew everything opaque with one
             * depth mode: fades did not fade and untextured white geometry
             * covered the picture. */
            MgsGpuState st;
            /* Built once per change of the BP state, not once per triangle.
             * `has_texture` is the only field that varies per draw with the
             * combiner unchanged, so it is set after the cache. */
            static MgsGpuTev  s_tev_block;
            static uint64_t   s_tev_hash;
            static uint32_t   s_tev_rev = 0xFFFFFFFFu;
            static int32_t    s_tev_tex = -1;

            if (s_tev_rev != gx->bp.rev) {
                tev_block_build(&gx->bp, &tev, &s_tev_block);
                s_tev_rev = gx->bp.rev;
                s_tev_tex = -1;
            }
            {   /* `has_texture` and the stage-to-unit map are the two
                 * fields that change with the DRAW rather than with the BP
                 * state, so they are written after the cache and the hash
                 * is taken over the result. */
                int32_t has = tex ? 1 : 0;
                int changed = (has != s_tev_tex);
                unsigned st;
                for (st = 0; st < 16u; ++st) {
                    int32_t v = (int32_t)unit_of_stage[st];
                    if (s_tev_block.unit[st][0] != v) {
                        s_tev_block.unit[st][0] = v;
                        changed = 1;
                    }
                }
                if (changed) {
                    s_tev_block.ctl[2] = has;
                    s_tev_tex = has;
                    s_tev_hash = tev_block_hash(&s_tev_block);
                }
            }
            memset(&st, 0, sizeof st);
            st.blend_enable = (r->blend_enable && !r->blend_noop) ? 1u : 0u;
            st.blend_src = (unsigned char)r->blend_src;
            st.blend_dst = (unsigned char)r->blend_dst;
            st.blend_sub = r->blend_sub ? 1u : 0u;
            st.depth_test = r->depth_test ? 1u : 0u;
            st.depth_write = r->depth_update ? 1u : 0u;
            st.depth_func = (unsigned char)r->depth_func;
            st.colour_write = r->color_update ? 1u : 0u;
            st.alpha_write = r->alpha_update ? 1u : 0u;
            mgs_gpu_batch_tri(&gv[0], &gv[1], &gv[2], binds, &st,
                              &s_tev_block, s_tev_hash);
        }
        return;
    }

    {
        RasterSpan sp;
        sp.gx = gx; sp.r = r;
        sp.vin[0] = vin[0]; sp.vin[1] = vin[1]; sp.vin[2] = vin[2];
        sp.tex = tex; sp.tex_coord = tex_coord;
        sp.wrap_s = wrap_s; sp.wrap_t = wrap_t; sp.bilinear = bilinear;
        sp.alpha_always = alpha_always; sp.tev = tev;
        sp.note_pixels = r->note_pixels;
        sp.stage_count = stage_n;
        if (stage_n) {
            unsigned k;
            for (k = 0; k < stage_n; ++k) {
                sp.stage_tex[k] = stage_tex[k];
                sp.stage_coord[k] = stage_coord[k];
                sp.stage_ws[k] = stage_ws[k];
                sp.stage_wt[k] = stage_wt[k];
                sp.stage_bilinear[k] = stage_bil[k];
            }
        }
        memcpy(sp.sx, sx, sizeof sx); memcpy(sp.sy, sy, sizeof sy);
        memcpy(sp.sz, sz, sizeof sz); memcpy(sp.iw, iw, sizeof iw);
        sp.area = area;
        sp.inv_area = 1.0f / area;
        sp.dw0dx = -(sy[2] - sy[1]) * sp.inv_area;
        sp.dw1dx = -(sy[0] - sy[2]) * sp.inv_area;
        sp.x0 = x0; sp.x1 = x1;
        raster_dispatch(r, &sp, y0, y1);
    }

    if (r->composite_dump) {
        const char* dir = r->dump_composite;
        char path[512];
        FILE* f;
        snprintf(path, sizeof path, "%s/efb_%u.ppm", dir ? dir : ".",
                 r->composite_dump - 1u);
        f = fopen(path, "wb");
        if (f) {
            unsigned yy, xx3;
            fprintf(f, "P6\n%u %u\n255\n", 512u, 448u);
            for (yy = 0; yy < 448u; ++yy)
                for (xx3 = 0; xx3 < 512u; ++xx3) {
                    uint32_t v = r->efb->pixels[yy * MGS_EFB_WIDTH + xx3];
                    fputc((int)((v >> 16) & 0xFFu), f);
                    fputc((int)((v >> 8) & 0xFFu), f);
                    fputc((int)(v & 0xFFu), f);
                }
            fclose(f);
            fprintf(stderr, "[composite] buffer after the draw -> %s\n", path);
        }
        r->composite_dump = 0;
    }

    if (r->col_armed) {
        static unsigned said;
        unsigned yy, xx3, cnt = 0u;
        unsigned long sr = 0, sg = 0, sb = 0;
        for (yy = 0; yy < MGS_EFB_HEIGHT; yy += 16u)
            for (xx3 = 0; xx3 < MGS_EFB_WIDTH; xx3 += 8u) {
                uint32_t v = r->efb->pixels[yy * MGS_EFB_WIDTH + xx3];
                sr += (v >> 16) & 0xFFu;
                sg += (v >> 8) & 0xFFu;
                sb += v & 0xFFu;
                ++cnt;
            }
        if (cnt) {
            unsigned a2v = (unsigned)(sr / cnt), b2 = (unsigned)(sg / cnt),
                     c2 = (unsigned)(sb / cnt);
            /* The balance, not the brightness: a draw that lifts everything
             * is a fade, and a draw that lifts red and blue past green is
             * what turns the picture purple. */
            int was = (int)r->col_before[0] + (int)r->col_before[2]
                    - 2 * (int)r->col_before[1];
            int now = (int)a2v + (int)c2 - 2 * (int)b2;
            /* MGS_TRACE_DRAWCOLOUR=<frame>: from that frame on, report
             * EVERY full-screen draw rather than only the ones that shift
             * the balance. Which draw first introduces the cast cannot be
             * seen from the ones that worsen it - by then it is circulating
             * through the copy that feeds the next frame. */
            unsigned f0 = r->trace_drawcolour_from;
            unsigned fr = (unsigned)r->efb->copies;
            /* With MGS_TRACE_DRAWH naming one kind of draw, report every
             * one of them: the question is then what that draw does, not
             * which draw to look at. */
            if ((r->trace_drawh_set
                 || (f0 && fr >= f0 && fr < f0 + 400u)
                 || (!f0 && now - was > 12))
                && said < 400u) {
                ++said;
                /* ...and the buffer itself, so the SHAPE can be seen. The
                 * colour means said the picture was purple; they cannot say
                 * that it is purple in a band with a hard edge, which is a
                 * geometry fault and not an arithmetic one. */
                if (r->dump_drawseq) {
                    char path[512];
                    FILE* f;
                    snprintf(path, sizeof path, "%s/draw_%02u.ppm",
                             r->dump_drawseq, said);
                    f = fopen(path, "wb");
                    if (f) {
                        unsigned yy2, xx4;
                        fprintf(f, "P6\n%u %u\n255\n", 512u, 448u);
                        for (yy2 = 0; yy2 < 448u; ++yy2)
                            for (xx4 = 0; xx4 < 512u; ++xx4) {
                                uint32_t v = r->efb->pixels[yy2 * MGS_EFB_WIDTH
                                                            + xx4];
                                fputc((int)((v >> 16) & 0xFFu), f);
                                fputc((int)((v >> 8) & 0xFFu), f);
                                fputc((int)(v & 0xFFu), f);
                            }
                        fclose(f);
                    }
                }
                fprintf(stderr, "[drawcolour] frame %u  ", fr);
                fprintf(stderr, "[drawcolour] (%u,%u,%u) -> (%u,%u,%u)  "
                        "r+b-2g %d -> %d   texture %ux%u fmt 0x%X at "
                        "0x%08X  %u stages, blend %s (src %u dst %u)"
                        "   texture (%u,%u,%u) alpha %u"
                        "   depth %s func %u write %s   alpha test %s"
                        "   box x %d-%d y %d-%d   scissor %d-%d, %d-%d "
                        "(%s)\n",
                        r->col_before[0], r->col_before[1], r->col_before[2],
                        a2v, b2, c2, was, now,
                        r->noise_tex_w, r->noise_tex_h, r->noise_tex_fmt,
                        r->noise_tex_addr, stage_n,
                        (r->blend_enable && !r->blend_noop) ? "ON" : "off",
                        r->blend_src, r->blend_dst,
                        r->col_tex[0], r->col_tex[1], r->col_tex[2],
                        r->col_tex_a,
                        r->depth_test ? "on" : "off", r->depth_func,
                        r->depth_update ? "yes" : "no",
                        mgs_tev_alpha_test_always(&gx->bp) ? "always passes"
                                                           : "ACTIVE",
                        x0, x1, y0, y1,
                        r->scissor_box[0], r->scissor_box[2],
                        r->scissor_box[1], r->scissor_box[3],
                        r->scissor_seen ? "set" : "none");
            /* The triangle's own screen coordinates and its texture
             * coordinates. A composite quad that should cover the movie
             * rectangle and instead covers a sliver is either being placed
             * wrongly or scaled wrongly, and the vertices say which. */
            fprintf(stderr, "[drawcolour]   screen (%.1f,%.1f) (%.1f,%.1f) "
                    "(%.1f,%.1f)   uv (%.3f,%.3f) (%.3f,%.3f) (%.3f,%.3f)\n",
                    sx[0], sy[0], sx[1], sy[1], sx[2], sy[2],
                    vin[0]->u[tex_coord], vin[0]->v[tex_coord],
                    vin[1]->u[tex_coord], vin[1]->v[tex_coord],
                    vin[2]->u[tex_coord], vin[2]->v[tex_coord]);
            /* ...and where the vertices started, and which matrix moved
             * them. A quad that should cover the screen and lands off the
             * right edge is either given the wrong positions or the wrong
             * matrix, and only the object-space values tell them apart. */
            {
                const float* m = position_matrix(gx, vin[0]->pos_matrix);
                fprintf(stderr, "[drawcolour]   object (%.2f,%.2f,%.2f) "
                        "(%.2f,%.2f,%.2f) (%.2f,%.2f,%.2f)  mtx %u  "
                        "ortho %u\n"
                        "[drawcolour]   matrix  %.3f %.3f %.3f %.3f / "
                        "%.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f\n"
                        "[drawcolour]   proj    %.4f %.4f %.4f %.4f "
                        "%.4f %.4f\n",
                        vin[0]->x, vin[0]->y, vin[0]->z,
                        vin[1]->x, vin[1]->y, vin[1]->z,
                        vin[2]->x, vin[2]->y, vin[2]->z,
                        vin[0]->pos_matrix, gx->xf_projection_ortho,
                        m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
                        m[8], m[9], m[10], m[11],
                        gx->xf_projection[0], gx->xf_projection[1],
                        gx->xf_projection[2], gx->xf_projection[3],
                        gx->xf_projection[4], gx->xf_projection[5]);
            }
            }
        }
        r->col_armed = 0;
    }

    /* ...and the buffer again, now that this draw has run. */
    if (r->noise_armed) {
        unsigned yy, xx2, cnt = 0u, after = 0u;
        for (yy = 0; yy < MGS_EFB_HEIGHT; yy += 16u)
            for (xx2 = 1u; xx2 < MGS_EFB_WIDTH; xx2 += 8u) {
                uint32_t a1 = r->efb->pixels[yy * MGS_EFB_WIDTH + xx2 - 1u];
                uint32_t b1 = r->efb->pixels[yy * MGS_EFB_WIDTH + xx2];
                int va = (int)(((a1 >> 16) & 0xFF) + ((a1 >> 8) & 0xFF)
                               + (a1 & 0xFF)) / 3;
                int vb = (int)(((b1 >> 16) & 0xFF) + ((b1 >> 8) & 0xFF)
                               + (b1 & 0xFF)) / 3;
                after += (unsigned)(va > vb ? va - vb : vb - va);
                ++cnt;
            }
        after = cnt ? after / cnt : 0u;
        if (after > 20u && r->noise_before <= 20u && r->noise_said) {
            ++*r->noise_said;
            fprintf(stderr, "[drawnoise] buffer %u -> %u  texture %ux%u "
                    "fmt 0x%X at 0x%08X  %u extra stages, %u TEV stages, "
                    "blend %s (src %u dst %u%s), alpha test %s, "
                    "colour update %s\n",
                    r->noise_before, after, r->noise_tex_w, r->noise_tex_h,
                    r->noise_tex_fmt, r->noise_tex_addr, stage_n,
                    mgs_tev_stage_count(&gx->bp),
                    (r->blend_enable && !r->blend_noop) ? "ON" : "off",
                    r->blend_src, r->blend_dst,
                    r->blend_sub ? ", subtract" : "",
                    alpha_always ? "always passes" : "active",
                    r->color_update ? "on" : "off");
            {   /* The alpha is what decides whether this draw is visible at
                 * all: the blend is src*a + dst*(1-a). Print the combiner's
                 * alpha register, the texture's own alpha, and what the
                 * combiner actually produces for that texel - the three
                 * disagreeing is the whole question. */
                MgsTevInput ti;
                unsigned k, n2 = 0; unsigned long suma = 0, sumt = 0;
                for (k = 0; tex && k < tex->width * tex->height; k += 997u) {
                    sumt += (tex->texels[k] >> 24) & 0xFFu;
                    ti.texture = tex->texels[k];
                    ti.raster = 0xFFFFFFFFu;
                    suma += (mgs_tev_run_compiled(&tev, &ti) >> 24) & 0xFFu;
                    ++n2;
                }
                if (n2)
                    fprintf(stderr, "[drawnoise]   alpha env 0x%08X  "
                            "texture alpha mean %lu  combiner alpha mean "
                            "%lu (of 255)\n",
                            tev.ae[0], sumt / n2, suma / n2);
            }
        }
        r->noise_armed = 0;
    }
}
