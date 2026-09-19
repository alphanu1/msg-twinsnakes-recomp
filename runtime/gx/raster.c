#include "raster.h"

#include <string.h>

void mgs_raster_init(MgsGxRaster* r, MgsEfb* efb)
{
    memset(r, 0, sizeof *r);
    r->efb = efb;
    r->width = MGS_EFB_WIDTH;
    r->height = MGS_EFB_HEIGHT;
    r->depth_test = 1;
    r->depth_update = 1;
    r->depth_func = 3;            /* less-or-equal, the usual default */
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
    float sx[3], sy[3], sz[3], iw[3];
    float minx, maxx, miny, maxy, area;
    int x0, x1, y0, y1, px, py;
    unsigned i;

    if (!r || !r->efb) return;
    ++r->submitted;

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
        if (w <= 0.0001f) { ++r->clipped; return; }

        iw[i] = 1.0f / w;
        to_screen(r, gx, clip, w, &sx[i], &sy[i], &sz[i]);
    }

    area = edge(sx[0], sy[0], sx[1], sy[1], sx[2], sy[2]);
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
    if (x0 >= x1 || y0 >= y1) { ++r->clipped; return; }

    ++r->drawn;

    for (py = y0; py < y1; ++py) {
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
            if (pw > 0.0f) {
                float k0 = w0 * iw[0] / pw, k1 = w1 * iw[1] / pw;
                float k2 = 1.0f - k0 - k1;
                r->efb->pixels[at] = lerp_color(vin[0]->color[0],
                                                vin[1]->color[0],
                                                vin[2]->color[0], k0, k1, k2);
            } else {
                r->efb->pixels[at] = vin[0]->color[0];
            }

            if (r->depth_update) r->depth[at] = z;
            ++r->pixels;
        }
    }
}
