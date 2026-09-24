/* Decoding one vertex, whatever the game chose to encode it as.
 *
 * The GameCube lets a game pick, per attribute: how many components, what
 * numeric type, how many fractional bits if it is fixed point, and whether
 * the value is inline or an index into an array in main memory. That is a
 * large space and the game uses a lot of it - positions as s16 with a shift
 * for geometry, f32 for effects, indexed for shared meshes and direct for
 * throwaway quads.
 *
 * Every one of those combinations ends here as the same MgsGxVertex. That is
 * the point: the rasteriser has one vertex layout to be correct about, and
 * every format bug is in this file rather than spread through the renderer.
 */
#include "fifo.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <math.h>

typedef struct Reader {
    const uint8_t* p;
    unsigned n, at;
} Reader;

static uint8_t r8(Reader* r)
{
    return r->at < r->n ? r->p[r->at++] : 0u;
}

static uint16_t r16(Reader* r)
{
    uint16_t v = (uint16_t)(((uint16_t)r8(r)) << 8);
    return (uint16_t)(v | r8(r));
}

static uint32_t r32(Reader* r)
{
    uint32_t v = (uint32_t)r16(r) << 16;
    return v | r16(r);
}

/* One component, as a float. Fixed-point formats carry their scale in the
 * attribute table's shift field, which is why `shift` is a parameter rather
 * than baked in: the same s16 stream means different numbers at different
 * shifts, and ignoring it puts geometry thousands of units from the camera.
 */
static float component(Reader* r, unsigned format, unsigned shift)
{
    /* 2^-shift, built in the exponent rather than by halving in a loop
     * once per component of every vertex. Exact either way: every halving
     * of a power of two is exact, and shift is at most 31, well inside the
     * normal range. */
    float scale;
    {
        uint32_t bits = (127u - (shift & 31u)) << 23;
        memcpy(&scale, &bits, sizeof scale);
    }

    switch (format) {
        case 0: return (float)(uint8_t)r8(r) * scale;
        case 1: return (float)(int8_t)r8(r) * scale;
        case 2: return (float)(uint16_t)r16(r) * scale;
        case 3: return (float)(int16_t)r16(r) * scale;
        case 4: { uint32_t bits = r32(r); float f; memcpy(&f, &bits, sizeof f); return f; }
        default: return 0.0f;
    }
}

static uint32_t expand(unsigned v, unsigned bits)
{
    /* Replicate the high bits downward so full-scale stays full-scale: 0x1F
     * in five bits must become 0xFF, not 0xF8. A plain shift darkens every
     * colour slightly, which reads as a gamma problem rather than a bug. */
    unsigned out = v << (8u - bits);
    return (uint32_t)(out | (out >> bits));
}

static uint32_t read_color(Reader* r, unsigned format)
{
    switch (format) {
        case 0: {                                   /* rgb565 */
            uint16_t c = r16(r);
            return 0xFF000000u | (expand((c >> 11) & 0x1Fu, 5) << 16) |
                   (expand((c >> 5) & 0x3Fu, 6) << 8) | expand(c & 0x1Fu, 5);
        }
        case 1: {                                   /* rgb888 */
            uint32_t rr = r8(r), gg = r8(r), bb = r8(r);
            return 0xFF000000u | (rr << 16) | (gg << 8) | bb;
        }
        case 2: {                                   /* rgb888x */
            uint32_t rr = r8(r), gg = r8(r), bb = r8(r);
            (void)r8(r);
            return 0xFF000000u | (rr << 16) | (gg << 8) | bb;
        }
        case 3: {                                   /* rgba4444 */
            uint16_t c = r16(r);
            return (expand((c >> 12) & 0xFu, 4) << 24) |
                   (expand((c >> 8) & 0xFu, 4) << 16) |
                   (expand((c >> 4) & 0xFu, 4) << 8) | expand(c & 0xFu, 4);
        }
        case 4: {                                   /* rgba6666, 24 bits */
            uint32_t c = ((uint32_t)r8(r) << 16) | ((uint32_t)r8(r) << 8) | r8(r);
            return (expand((c >> 0) & 0x3Fu, 6) << 24) |
                   (expand((c >> 18) & 0x3Fu, 6) << 16) |
                   (expand((c >> 12) & 0x3Fu, 6) << 8) | expand((c >> 6) & 0x3Fu, 6);
        }
        case 5: {                                   /* rgba8888 */
            uint32_t rr = r8(r), gg = r8(r), bb = r8(r), aa = r8(r);
            return (aa << 24) | (rr << 16) | (gg << 8) | bb;
        }
        default: return 0xFFFFFFFFu;
    }
}

/* An indexed attribute: the stream carries an index, the value is in an array
 * whose base and stride are in CP registers. Reading it needs guest memory,
 * and a bad base must refuse rather than read wild. */
static const uint8_t* array_element(const MgsGx* gx, unsigned array,
                                    unsigned index, unsigned size)
{
    uint32_t base = gx->array_base[array & 0xFu];
    uint32_t stride = gx->array_stride[array & 0xFu];
    if (!base || !stride) return NULL;
    return guest_ptr(gx->mem, base + index * stride, size);
}

/* THE ARRAY INDEX IS NOT THE ATTRIBUTE NUMBER.
 *
 * These were 9, 10, 11, 12, 13 - the values of GX_VA_POS, GX_VA_NRM,
 * GX_VA_CLR0, GX_VA_CLR1 and GX_VA_TEX0 in the ATTRIBUTE enum. The arrays
 * are numbered separately, from zero, and the SDK does the conversion
 * itself:
 *
 *     libogc   GX_SetArray:  idx = attr - GX_VA_POS;
 *                            GX_LOAD_CP_REG(0xA0 + idx, ptr);
 *                            GX_LOAD_CP_REG(0xB0 + idx, stride);
 *     Dolphin  CPArray:      Position = 0, Normal = 1, Color0 = 2,
 *                            Color1 = 3, TexCoord0 = 4 ... TexCoord7 = 11
 *
 * So every indexed attribute was read from the wrong array. Position looked
 * for its base and stride in slot 9, which is TexCoord5's, and a stride of
 * zero there made `array_element` refuse - correctly, on the wrong slot.
 *
 * What that cost: 9,260,039 indexed positions a run silently left at the
 * origin, which a perspective projection turns into w = 0, which the
 * rasteriser then rejected as "behind the eye" - 5,508,491 triangles, a
 * THIRD of everything submitted. The scene was decoded, transformed and
 * discarded, and every counter along the way reported success. */
#define ARR_POS   0u
#define ARR_NRM   1u
#define ARR_CLR0  2u
#define ARR_CLR1  3u
#define ARR_TEX0  4u

static void read_position(const MgsGx* gx, Reader* r, const MgsGxVertexFormat* f,
                          MgsGxVertex* v)
{
    MgsGxAttrKind k = f->kind[GX_VA_POS];
    unsigned bytes = 0;
    const uint8_t* src = NULL;
    Reader ar;

    if (k == GX_ATTR_NONE) return;
    if (k == GX_ATTR_DIRECT) {
        v->x = component(r, f->pos_format, f->pos_shift);
        v->y = component(r, f->pos_format, f->pos_shift);
        v->z = f->pos_count == 3u ? component(r, f->pos_format, f->pos_shift) : 0.0f;
        return;
    }

    {
        unsigned index = (k == GX_ATTR_INDEX8) ? r8(r) : r16(r);
        switch (f->pos_format) {
            case 0: case 1: bytes = f->pos_count; break;
            case 2: case 3: bytes = f->pos_count * 2u; break;
            default:        bytes = f->pos_count * 4u; break;
        }
        src = array_element(gx, ARR_POS, index, bytes);
    }
    /* A POSITION THAT CANNOT BE FETCHED IS NOT A POSITION OF ZERO.
     *
     * This returned silently, leaving the vertex at the origin, and the
     * rasteriser then rejected the triangle as "behind the eye" - because
     * with a perspective projection w is -z and the origin transformed by
     * an identity matrix gives w = 0. Five and a half million triangles a
     * run, a third of everything submitted, disappeared that way, and every
     * counter in the pipeline said the geometry was fine.
     *
     * Counted here, where it is known, rather than inferred from a symptom
     * five stages downstream. */
    if (!src) {
        MgsGx* m = (MgsGx*)gx;
        ++m->pos_fetch_failed;
        if (!gx->array_base[ARR_POS]) ++m->pos_no_base;
        else if (!gx->array_stride[ARR_POS]) ++m->pos_no_stride;
        else ++m->pos_out_of_range;
        return;
    }

    ar.p = src; ar.n = bytes; ar.at = 0;
    v->x = component(&ar, f->pos_format, f->pos_shift);
    v->y = component(&ar, f->pos_format, f->pos_shift);
    v->z = f->pos_count == 3u ? component(&ar, f->pos_format, f->pos_shift) : 0.0f;
}

static void read_color_attr(const MgsGx* gx, Reader* r, const MgsGxVertexFormat* f,
                            unsigned which, MgsGxVertex* v)
{
    MgsGxAttrKind k = f->kind[GX_VA_CLR0 + which];
    unsigned fmt = f->clr_format[which];

    if (k == GX_ATTR_NONE) return;
    if (k == GX_ATTR_DIRECT) { v->color[which] = read_color(r, fmt); return; }

    {
        unsigned index = (k == GX_ATTR_INDEX8) ? r8(r) : r16(r);
        unsigned bytes = (fmt == 0u || fmt == 3u) ? 2u : (fmt == 1u || fmt == 4u) ? 3u : 4u;
        const uint8_t* src = array_element(gx, ARR_CLR0 + which, index, bytes);
        Reader ar;
        if (!src) return;
        ar.p = src; ar.n = bytes; ar.at = 0;
        v->color[which] = read_color(&ar, fmt);
    }
}

static void skip_attr(Reader* r, MgsGxAttrKind k, unsigned direct_bytes)
{
    if (k == GX_ATTR_NONE) return;
    if (k == GX_ATTR_INDEX8) { (void)r8(r); return; }
    if (k == GX_ATTR_INDEX16) { (void)r16(r); return; }
    while (direct_bytes--) (void)r8(r);
}

/* Decode one vertex out of the stream. Returns the bytes consumed, which the
 * caller checks against the size it computed: a mismatch means the format was
 * misread, and continuing would mis-parse every vertex after it. */
unsigned mgs_gx_decode_vertex(const MgsGx* gx, const MgsGxVertexFormat* f,
                              const uint8_t* data, unsigned n, MgsGxVertex* v);
unsigned mgs_gx_decode_vertex(const MgsGx* gx, const MgsGxVertexFormat* f,
                              const uint8_t* data, unsigned n, MgsGxVertex* v)
{
    Reader r; unsigned i;

    r.p = data; r.n = n; r.at = 0;
    memset(v, 0, sizeof *v);
    v->color[0] = v->color[1] = 0xFFFFFFFFu;

    /* Matrix indices come first, in attribute order, and are always inline.
     * When a vertex does NOT carry one - the common case for static geometry
     * - the current index is in a command-processor register instead, and
     * defaulting to zero silently draws every such object at whatever matrix
     * zero happens to hold. */
    if (f->kind[GX_VA_PNMTXIDX] != GX_ATTR_NONE) v->pos_matrix = r8(&r);
    else v->pos_matrix = gx->cp_matrix_index_a & 0x3Fu;
    /* THE DEFAULT IS THE CP REGISTER, not identity.
     *
     * MatrixIndexA and MatrixIndexB hold the position matrix index AND the
     * eight texture matrix indices, six bits each - PosNormal at 0, Tex0 at
     * 6, Tex1 at 12, Tex2 at 18, Tex3 at 24 in A, and Tex4..7 at 0,6,12,18
     * in B (Dolphin, `CPMemory.h`, TMatrixIndexA/B). A vertex that carries
     * no per-coordinate index uses those, and most vertices here do not
     * carry one: 1.4 million of some twenty million. Defaulting to identity
     * would leave the great majority untransformed for the same reason the
     * whole texture matrix was missing. */
    for (i = 0; i < 4u; ++i)
        v->tex_matrix[i] =
            (gx->cp_matrix_index_a >> (6u + 6u * i)) & 0x3Fu;
    for (i = 4u; i < 8u; ++i)
        v->tex_matrix[i] =
            (gx->cp_matrix_index_b >> (6u * (i - 4u))) & 0x3Fu;
    for (i = 0; i < 8u; ++i)
        if (f->kind[GX_VA_TEX0MTXIDX + i] != GX_ATTR_NONE) {
            v->tex_matrix[i] = r8(&r);
            /* GX_IDENTITY is 60. Anything else means the game wants a
             * texture matrix applied, and nothing here applies one. */
            ++((MgsGx*)gx)->tex_mtx_seen;
            if (v->tex_matrix[i] != 60u) {
                ++((MgsGx*)gx)->tex_mtx_nonidentity;
                /* WHAT IS ACTUALLY IN THE MATRIX. "The game uses texture
                 * matrices" and "not applying them changes the picture" are
                 * different claims: an identity-valued matrix at a
                 * non-identity INDEX would make the count above alarming and
                 * harmless. Print a few and settle it. */
                static int texmtx_on = -1;
                if (texmtx_on < 0)
                    texmtx_on = getenv("MGS_TRACE_TEXMTX") != NULL;
                if (texmtx_on) {
                    static unsigned said;
                    if (said < 6u) {
                        unsigned row = v->tex_matrix[i], k;
                        ++said;
                        fprintf(stderr, "[texmtx] coord %u index %u:", i, row);
                        for (k = 0; k < 8u; ++k)
                            fprintf(stderr, " %7.3f",
                                    (double)gx->xf_matrix[(row * 4u + k) & 0xFFu]);
                        fprintf(stderr, "\n");
                    }
                }
            }
        }

    read_position(gx, &r, f, v);

    /* NORMALS, READ RATHER THAN SKIPPED.
     *
     * They were skipped because nothing lit anything. But texture
     * coordinate generation takes them as a SOURCE, and this game builds
     * 16.2M coordinates a minute from the normal and 4.4M from each
     * binormal - its environment and lighting maps. Fed nothing, those
     * surfaces sampled one texel.
     *
     * Fixed point is NOT the attribute table's shift here: a normal's
     * scale is fixed by its type - s8 over 64, u8 over 128, s16 over
     * 16384, u16 over 32768 - as in Dolphin's VertexLoader_Normal.
     * NBT is normal, then the two binormals, in stream order. In the
     * three-index form (NormalIndex3) index k reads components 3k..3k+2
     * of ITS OWN array entry, not the first three. */
    if (f->kind[GX_VA_NRM] != GX_ATTR_NONE) {
        MgsGxAttrKind k = f->kind[GX_VA_NRM];
        unsigned fmt = f->nrm_format;
        unsigned cs = (fmt == 4u) ? 4u : (fmt >= 2u) ? 2u : 1u;
        unsigned sh = (fmt == 4u) ? 0u
                    : (fmt == 0u) ? 7u : (fmt == 1u) ? 6u
                    : (fmt == 2u) ? 15u : 14u;
        unsigned vecs = (f->nrm_count == 3u) ? 3u : 1u;
        float nv[3][3];
        unsigned j;
        memset(nv, 0, sizeof nv);
        if (k == GX_ATTR_DIRECT) {
            for (j = 0; j < vecs; ++j) {
                nv[j][0] = component(&r, fmt, sh);
                nv[j][1] = component(&r, fmt, sh);
                nv[j][2] = component(&r, fmt, sh);
            }
        } else if (f->nrm_index3 && vecs == 3u) {
            for (j = 0; j < 3u; ++j) {
                unsigned index = (k == GX_ATTR_INDEX8) ? r8(&r) : r16(&r);
                const uint8_t* src = array_element(gx, ARR_NRM, index,
                                                   cs * 3u * (j + 1u));
                Reader ar;
                if (!src) continue;
                ar.p = src; ar.n = cs * 3u * (j + 1u); ar.at = cs * 3u * j;
                nv[j][0] = component(&ar, fmt, sh);
                nv[j][1] = component(&ar, fmt, sh);
                nv[j][2] = component(&ar, fmt, sh);
            }
        } else {
            unsigned index = (k == GX_ATTR_INDEX8) ? r8(&r) : r16(&r);
            const uint8_t* src = array_element(gx, ARR_NRM, index,
                                               cs * 3u * vecs);
            if (src) {
                Reader ar;
                ar.p = src; ar.n = cs * 3u * vecs; ar.at = 0;
                for (j = 0; j < vecs; ++j) {
                    nv[j][0] = component(&ar, fmt, sh);
                    nv[j][1] = component(&ar, fmt, sh);
                    nv[j][2] = component(&ar, fmt, sh);
                }
            }
        }
        v->nx = nv[0][0]; v->ny = nv[0][1]; v->nz = nv[0][2];
        for (j = 0; j < 3u; ++j) { v->bt[0][j] = nv[1][j]; v->bt[1][j] = nv[2][j]; }
    }

    read_color_attr(gx, &r, f, 0, v);
    read_color_attr(gx, &r, f, 1, v);

    for (i = 0; i < 8u; ++i) {
        MgsGxAttrKind k = f->kind[GX_VA_TEX0 + i];
        if (k == GX_ATTR_NONE) continue;
        if (k == GX_ATTR_DIRECT) {
            v->u[i] = component(&r, f->tex_format[i], f->tex_shift[i]);
            if (f->tex_count[i] == 2u)
                v->v[i] = component(&r, f->tex_format[i], f->tex_shift[i]);
        } else {
            unsigned index = (k == GX_ATTR_INDEX8) ? r8(&r) : r16(&r);
            unsigned cs = (f->tex_format[i] == 4u) ? 4u
                        : (f->tex_format[i] >= 2u) ? 2u : 1u;
            unsigned bytes = cs * f->tex_count[i];
            const uint8_t* src = array_element(gx, ARR_TEX0 + i, index, bytes);
            Reader ar;
            if (!src) continue;
            ar.p = src; ar.n = bytes; ar.at = 0;
            v->u[i] = component(&ar, f->tex_format[i], f->tex_shift[i]);
            if (f->tex_count[i] == 2u)
                v->v[i] = component(&ar, f->tex_format[i], f->tex_shift[i]);
        }
    }

    /* THE TEXTURE MATRIX, APPLIED.
     *
     * The stream carries a texture-matrix INDEX per coordinate, and the
     * matrix itself lives in the same XF matrix memory as the position
     * matrices - GX_PNMTX0..9 are rows 0,3,..27 and GX_TEXMTX0..9 are rows
     * 30,33,..57, with GX_IDENTITY at 60. So `xf_matrix` already held these
     * and nothing multiplied by them: 1,398,946 vertices a run carried a
     * non-identity index and every one was ignored.
     *
     * What the game actually puts there settles whether that mattered:
     *
     *     [  0.270  0.421  0  0 ]    a rotation of 57 degrees
     *     [ -0.421  0.270  0  0 ]    combined with a scale of 0.5
     *
     * so those surfaces were drawn at twice the texture size and unrotated.
     * That is Ben's "objects are incorrect".
     *
     * GX_TG_MTX2x4 against the stream's own coordinates, which takes the
     * input as (s, t, 1, 1). The 3x4 form and the texgen configuration at
     * XF 0x1040 are still dropped - counted in `xf_texgen_writes` - and a
     * matrix whose third and fourth columns are zero, as this one's are,
     * gives the same answer either way. */
    /* MGS_NO_TEXMTX withdraws this, so "did applying the matrix change the
     * picture" is a switch rather than a rebuild - HANDOFF F162 records two
     * runs concluded from a build that had not finished. */
    {
        static int off = -1;
        if (off < 0) off = getenv("MGS_NO_TEXMTX") != NULL;
        if (off) return r.at;
    }
    /* TEXTURE COORDINATE GENERATION, per output coordinate.
     *
     * This used to assume output i came from input TEXi through matrix i.
     * The hardware does not: each output's texgen register (XF 0x1040+i)
     * names a SOURCE row - the position, the normal, or any of TEX0..7 -
     * and this game builds its outputs from TEX0 and TEX1 (0x280 and 0x300,
     * 128,441 writes between them). A vertex carrying only TEX0 has no
     * TEX2 or TEX3, so outputs 2 and 3 were computed from zeros, and a
     * matrix applied to (0,0,1,1) is the same constant for every vertex.
     * One texel per draw. Rendered with each texture replaced by its own
     * coordinate ramp, whole models came out a single colour - clamped to
     * (1,1) or (0,0) - which is the flat shading Ben has been reporting.
     *
     * Follows Dolphin's software TransformUnit (TransformTexCoordRegular):
     * source, then a 2x4 or 3x4 matrix, then - with dual texture on - an
     * optional normalise and a 3x4 post-matrix from XF 0x0500. Per-pixel
     * perspective division by q is NOT done here: a coordinate with q != 1
     * is divided at the vertex and counted, because that is an approximation
     * and the count says how much it is being relied on. */
    {
        static const float k_ident[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        MgsGx* gm = (MgsGx*)gx;
        float in_u[8], in_v[8];
        unsigned n = gx->xf_num_texgen > 8u ? 8u : gx->xf_num_texgen;
        for (i = 0; i < 8u; ++i) { in_u[i] = v->u[i]; in_v[i] = v->v[i]; }

        for (i = 0; i < n; ++i) {
            uint32_t cfg = gx->xf_texgen[i];
            unsigned stq  = (cfg >> 1) & 1u;      /* 0 ST (2x4), 1 STQ (3x4) */
            unsigned abc1 = (cfg >> 2) & 1u;      /* input form */
            unsigned type = (cfg >> 4) & 7u;      /* 0 regular */
            unsigned srow = (cfg >> 7) & 0x1Fu;
            unsigned row  = v->tex_matrix[i];
            const float* m;
            float sx, sy, sz, s0, t0, q0;

            if (type != 0u) {
                ++gm->texgen_unsupported; ++gm->texgen_unsup_type[type];
                continue;
            }
            if (srow == 0u) {                     /* geometry */
                sx = v->x; sy = v->y; sz = v->z;
            } else if (srow == 1u) {              /* normal */
                sx = v->nx; sy = v->ny; sz = v->nz;
            } else if (srow == 3u || srow == 4u) {  /* binormals */
                sx = v->bt[srow - 3u][0]; sy = v->bt[srow - 3u][1];
                sz = v->bt[srow - 3u][2];
            } else if (srow >= 5u && srow <= 12u) {
                sx = in_u[srow - 5u]; sy = in_v[srow - 5u]; sz = 1.0f;
            } else {
                /* The colour row, or a row the hardware does not define. */
                ++gm->texgen_unsupported; ++gm->texgen_unsup_row[srow];
                continue;
            }
            ++gm->texgen_regular;
            /* NaN to 1, as Dolphin does (Shadow the Hedgehog's eyelids). */
            if (sx != sx) sx = 1.0f;
            if (sy != sy) sy = 1.0f;
            if (sz != sz) sz = 1.0f;

            /* GX_IDENTITY and above: the SDK loads an identity there. The
             * position-matrix rows below 30 are legitimate here - that is
             * how coordinates are generated from geometry - so the old
             * guard against them is gone. */
            if (row >= 60u || row * 4u + (stq ? 12u : 8u) > 64u * 4u)
                m = k_ident;
            else
                m = &gx->xf_matrix[row * 4u];
            if (m != k_ident) ++gm->tex_mtx_applied[i];

            if (abc1) {
                s0 = m[0] * sx + m[1] * sy + m[2]  * sz + m[3];
                t0 = m[4] * sx + m[5] * sy + m[6]  * sz + m[7];
                q0 = stq ? m[8] * sx + m[9] * sy + m[10] * sz + m[11] : 1.0f;
            } else {
                s0 = m[0] * sx + m[1] * sy + m[2]  + m[3];
                t0 = m[4] * sx + m[5] * sy + m[6]  + m[7];
                q0 = stq ? m[8] * sx + m[9] * sy + m[10] + m[11] : 1.0f;
            }

            if (gx->xf_dualtex) {
                uint32_t pi = gx->xf_postinfo[i];
                unsigned pidx = pi & 0x3Fu;
                const float* pm = (pidx * 4u + 12u <= 64u * 4u)
                                ? &gx->xf_post[pidx * 4u] : k_ident;
                float a = s0, b = t0, c = q0;
                if ((pi >> 8) & 1u) {
                    float len = sqrtf(a * a + b * b + c * c);
                    if (len > 0.0f) { a /= len; b /= len; c /= len; }
                }
                s0 = pm[0] * a + pm[1] * b + pm[2]  * c + pm[3];
                t0 = pm[4] * a + pm[5] * b + pm[6]  * c + pm[7];
                q0 = pm[8] * a + pm[9] * b + pm[10] * c + pm[11];
            }

            if (q0 == 0.0f) {
                /* The hardware's special case, from Dolphin. */
                s0 = s0 / 2.0f; t0 = t0 / 2.0f;
                s0 = s0 < -1.0f ? -1.0f : s0 > 1.0f ? 1.0f : s0;
                t0 = t0 < -1.0f ? -1.0f : t0 > 1.0f ? 1.0f : t0;
            } else if (q0 != 1.0f) {
                ++gm->texgen_q_not_one;
                s0 /= q0; t0 /= q0;
            }
            {
                float du = s0 - in_u[i], dv = t0 - in_v[i];
                if (du < 0.0f) du = -du;
                if (dv < 0.0f) dv = -dv;
                if (du > 1e-6f || dv > 1e-6f) ++gm->tex_mtx_moved;
                else ++gm->tex_mtx_unmoved;
            }
            v->u[i] = s0;
            v->v[i] = t0;
        }
    }
    return r.at;
}
