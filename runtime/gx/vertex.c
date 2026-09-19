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

#include <string.h>

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
    float scale = 1.0f;
    unsigned i;
    for (i = 0; i < shift; ++i) scale *= 0.5f;

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

/* Array indices, matching the attribute order the hardware uses. */
#define ARR_POS   9u
#define ARR_NRM  10u
#define ARR_CLR0 11u
#define ARR_CLR1 12u
#define ARR_TEX0 13u

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
    if (!src) return;

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
    for (i = 0; i < 8u; ++i)
        if (f->kind[GX_VA_TEX0MTXIDX + i] != GX_ATTR_NONE) v->tex_matrix[i] = r8(&r);

    read_position(gx, &r, f, v);

    /* Normals are read past rather than used: lighting is the transform
     * unit's, and this renderer does not light yet. Skipping the right NUMBER
     * of bytes is what matters - getting it wrong desynchronises the vertex. */
    if (f->kind[GX_VA_NRM] != GX_ATTR_NONE) {
        unsigned c = (f->nrm_format == 4u) ? 4u : (f->nrm_format >= 2u) ? 2u : 1u;
        skip_attr(&r, f->kind[GX_VA_NRM], c * (f->nrm_count == 3u ? 9u : 3u));
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
    return r.at;
}
