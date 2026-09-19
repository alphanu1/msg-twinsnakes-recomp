#include "raster.h"
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

    /* The image address is in 32-byte units, like everything else here. */
    return mgs_tex_get(&r->tex, gx->mem,
                       0x80000000u | ((i3 & 0x00FFFFFFu) << 5),
                       format, width, height, tlut_addr, tlut_format);
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

void mgs_raster_triangle(MgsGx* gx, const MgsGxVertex* a,
                         const MgsGxVertex* b, const MgsGxVertex* c)
{
    MgsGxRaster* r = (MgsGxRaster*)gx->user;
    const MgsGxVertex* vin[3];
    const MgsTexture* tex = NULL;
    unsigned tex_coord = 0, wrap_s = 0, wrap_t = 0;
    int bilinear = 0, tex_enabled = 0;
    float sx[3], sy[3], sz[3], iw[3];
    float minx, maxx, miny, maxy, area;
    int x0, x1, y0, y1, px, py;
    unsigned i;

    if (!r || !r->efb) return;
    ++r->submitted;

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
        if (tex_enabled) {
            tex = bind_texture(r, gx, map);
            if (tex) {
                texture_wrap(&gx->bp, map, &wrap_s, &wrap_t, &bilinear);
                ++r->textured;
            }
        }
    }

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

        for (px = x0; px < x1; ++px) {
            float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
            float w0 = edge(sx[1], sy[1], sx[2], sy[2], fx, fy) / area;
            float w1 = edge(sx[2], sy[2], sx[0], sy[0], fx, fy) / area;
            float w2 = 1.0f - w0 - w1;
            float z, pw;
            unsigned at;

            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

            z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
            at = (unsigned)py * r->width + (unsigned)px;
            if (!depth_passes(r, z, r->depth[at])) continue;

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
                    k0 = w0 * iw[0] / pw; k1 = w1 * iw[1] / pw;
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

                pixel = mgs_tev_run(&gx->bp, &in);

                /* The alpha test runs AFTER the combiner and before anything
                 * is written, depth included. Cut-out foliage and text rely
                 * on it entirely; without it every transparent texel becomes
                 * an opaque square that also writes depth. */
                if (!mgs_tev_alpha_test(&gx->bp, pixel)) {
                    ++r->alpha_killed;
                    continue;
                }

                r->efb->pixels[at] = pixel;
            }

            if (r->depth_update) r->depth[at] = z;
            ++r->pixels;
        }
    }
}
