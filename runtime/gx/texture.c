#include "texture.h"

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- colour unpacking -------------------------------------------------- */

static uint32_t expand(unsigned v, unsigned bits)
{
    unsigned out = v << (8u - bits);
    return (uint32_t)(out | (out >> bits));
}

static uint32_t rgb565(uint16_t c)
{
    return 0xFF000000u | (expand((c >> 11) & 0x1Fu, 5) << 16) |
           (expand((c >> 5) & 0x3Fu, 6) << 8) | expand(c & 0x1Fu, 5);
}

/* RGB5A3 is two formats in one bit. With the top bit set it is RGB555 and
 * fully opaque; clear, it is ARGB3444. Treating it as one of the two gives
 * an image that is right for some texels and wrong for the rest - which
 * reads as corrupt art rather than as a decode bug. */
static uint32_t rgb5a3(uint16_t c)
{
    if (c & 0x8000u)
        return 0xFF000000u | (expand((c >> 10) & 0x1Fu, 5) << 16) |
               (expand((c >> 5) & 0x1Fu, 5) << 8) | expand(c & 0x1Fu, 5);
    return (expand((c >> 12) & 0x7u, 3) << 24) |
           (expand((c >> 8) & 0xFu, 4) << 16) |
           (expand((c >> 4) & 0xFu, 4) << 8) | expand(c & 0xFu, 4);
}

static uint32_t ia8(uint16_t c)
{
    uint32_t i = c & 0xFFu, a = (c >> 8) & 0xFFu;
    return (a << 24) | (i << 16) | (i << 8) | i;
}

static uint32_t tlut_color(const uint16_t* tlut, unsigned index, uint32_t format)
{
    uint16_t c = tlut ? tlut[index & 0x3FFFu] : 0u;
    switch (format) {
        case 0u: return ia8(c);
        case 1u: return rgb565(c);
        default: return rgb5a3(c);
    }
}

static uint16_t be16(const uint8_t* p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }

/* ---- the tiled layouts ------------------------------------------------- */

/* Every format is a grid of tiles, each tile stored whole before the next.
 * These are the tile dimensions and the bits per texel; the two together give
 * the tile's size in bytes, which is what the address arithmetic needs. */
static int tile_shape(uint32_t format, unsigned* tw, unsigned* th, unsigned* bpp)
{
    switch (format) {
        case 0x0u: *tw = 8; *th = 8; *bpp = 4;  return 1;   /* I4 */
        case 0x1u: *tw = 8; *th = 4; *bpp = 8;  return 1;   /* I8 */
        case 0x2u: *tw = 8; *th = 4; *bpp = 8;  return 1;   /* IA4 */
        case 0x3u: *tw = 4; *th = 4; *bpp = 16; return 1;   /* IA8 */
        case 0x4u: *tw = 4; *th = 4; *bpp = 16; return 1;   /* RGB565 */
        case 0x5u: *tw = 4; *th = 4; *bpp = 16; return 1;   /* RGB5A3 */
        case 0x6u: *tw = 4; *th = 4; *bpp = 32; return 1;   /* RGBA8 */
        case 0x8u: *tw = 8; *th = 8; *bpp = 4;  return 1;   /* C4 */
        case 0x9u: *tw = 8; *th = 4; *bpp = 8;  return 1;   /* C8 */
        case 0xAu: *tw = 4; *th = 4; *bpp = 16; return 1;   /* C14X2 */
        case 0xEu: *tw = 8; *th = 8; *bpp = 4;  return 1;   /* CMPR */
        default:   return 0;
    }
}

static unsigned round_up(unsigned v, unsigned to) { return (v + to - 1u) / to * to; }

/* Bytes a whole texture occupies, so the read can be bounds-checked before
 * any of it is touched. */
static unsigned texture_bytes(uint32_t format, unsigned w, unsigned h)
{
    unsigned tw, th, bpp;
    if (!tile_shape(format, &tw, &th, &bpp)) return 0u;
    return round_up(w, tw) * round_up(h, th) * bpp / 8u;
}

/* DXT1, as the GameCube stores it. Two differences from the PC's, and both
 * are quiet: the 16-bit colours are big-endian, and the two-bit selectors
 * within each byte run high-to-low rather than low-to-high. */
static void decode_cmpr_block(const uint8_t* src, uint32_t* out, unsigned stride)
{
    uint16_t c0 = be16(src), c1 = be16(src + 2);
    uint32_t pal[4];
    unsigned y, x;

    pal[0] = rgb565(c0);
    pal[1] = rgb565(c1);
    if (c0 > c1) {
        unsigned i;
        for (i = 0; i < 3u; ++i) {
            unsigned sh = i * 8u;
            unsigned a = (pal[0] >> sh) & 0xFFu, b = (pal[1] >> sh) & 0xFFu;
            pal[2] = (pal[2] & ~(0xFFu << sh)) | (((2u * a + b) / 3u) << sh);
            pal[3] = (pal[3] & ~(0xFFu << sh)) | (((a + 2u * b) / 3u) << sh);
        }
        pal[2] |= 0xFF000000u; pal[3] |= 0xFF000000u;
    } else {
        unsigned i;
        for (i = 0; i < 3u; ++i) {
            unsigned sh = i * 8u;
            unsigned a = (pal[0] >> sh) & 0xFFu, b = (pal[1] >> sh) & 0xFFu;
            pal[2] = (pal[2] & ~(0xFFu << sh)) | (((a + b) / 2u) << sh);
        }
        pal[2] |= 0xFF000000u;
        pal[3] = 0u;                      /* the transparent selector */
    }

    for (y = 0; y < 4u; ++y) {
        uint8_t bits = src[4u + y];
        for (x = 0; x < 4u; ++x) {
            unsigned sel = (bits >> (6u - 2u * x)) & 3u;
            out[y * stride + x] = pal[sel];
        }
    }
}

int mgs_tex_decode(const GuestMemory* mem, uint32_t addr, uint32_t format,
                   unsigned width, unsigned height,
                   const uint16_t* tlut, uint32_t tlut_format,
                   uint32_t* out)
{
    unsigned tw, th, bpp, bytes, tx, ty, x, y;
    const uint8_t* base;

    if (!mem || !out || !width || !height) return 0;
    if (!tile_shape(format, &tw, &th, &bpp)) return 0;

    bytes = texture_bytes(format, width, height);
    if (!bytes) return 0;
    base = guest_ptr(mem, addr, bytes);
    if (!base) return 0;

    {
        unsigned tiles_x = round_up(width, tw) / tw;
        unsigned tile_bytes = tw * th * bpp / 8u;

        for (ty = 0; ty < round_up(height, th) / th; ++ty) {
            for (tx = 0; tx < tiles_x; ++tx) {
                const uint8_t* t = base + (ty * tiles_x + tx) * tile_bytes;

                if (format == 0xEu) {
                    /* A compressed tile is 8x8 made of four 4x4 DXT1 blocks,
                     * in reading order. */
                    unsigned sub;
                    for (sub = 0; sub < 4u; ++sub) {
                        unsigned bx = (sub & 1u) * 4u, by = (sub >> 1) * 4u;
                        uint32_t tmp[16];
                        unsigned yy, xx;
                        decode_cmpr_block(t + sub * 8u, tmp, 4u);
                        for (yy = 0; yy < 4u; ++yy) {
                            unsigned py = ty * th + by + yy;
                            if (py >= height) break;
                            for (xx = 0; xx < 4u; ++xx) {
                                unsigned px = tx * tw + bx + xx;
                                if (px >= width) break;
                                out[py * width + px] = tmp[yy * 4u + xx];
                            }
                        }
                    }
                    continue;
                }

                for (y = 0; y < th; ++y) {
                    unsigned py = ty * th + y;
                    if (py >= height) break;
                    for (x = 0; x < tw; ++x) {
                        unsigned px = tx * tw + x;
                        unsigned at = y * tw + x;
                        uint32_t c;
                        if (px >= width) break;

                        switch (format) {
                            case 0x0u: {                      /* I4 */
                                uint8_t b = t[at / 2u];
                                unsigned v = (at & 1u) ? (b & 0xFu) : (b >> 4);
                                uint32_t i = expand(v, 4);
                                c = (i << 24) | (i << 16) | (i << 8) | i;
                                break;
                            }
                            case 0x1u: {                      /* I8 */
                                uint32_t i = t[at];
                                c = (i << 24) | (i << 16) | (i << 8) | i;
                                break;
                            }
                            case 0x2u: {                      /* IA4 */
                                uint32_t i = expand(t[at] & 0xFu, 4);
                                uint32_t a = expand(t[at] >> 4, 4);
                                c = (a << 24) | (i << 16) | (i << 8) | i;
                                break;
                            }
                            case 0x3u: c = ia8(be16(t + at * 2u)); break;
                            case 0x4u: c = rgb565(be16(t + at * 2u)); break;
                            case 0x5u: c = rgb5a3(be16(t + at * 2u)); break;
                            case 0x6u: {
                                /* RGBA8: a 4x4 tile as TWO 32-byte halves,
                                 * alpha+red then green+blue. Read as one
                                 * block it gives a plausible wrong image. */
                                unsigned i = at;
                                uint32_t a = t[i * 2u], r = t[i * 2u + 1u];
                                uint32_t g = t[32u + i * 2u];
                                uint32_t b = t[32u + i * 2u + 1u];
                                c = (a << 24) | (r << 16) | (g << 8) | b;
                                break;
                            }
                            case 0x8u: {                      /* C4 */
                                uint8_t b = t[at / 2u];
                                unsigned v = (at & 1u) ? (b & 0xFu) : (b >> 4);
                                c = tlut_color(tlut, v, tlut_format);
                                break;
                            }
                            case 0x9u: c = tlut_color(tlut, t[at], tlut_format); break;
                            case 0xAu: c = tlut_color(tlut, be16(t + at * 2u) & 0x3FFFu,
                                                      tlut_format); break;
                            default: c = 0xFFFF00FFu; break;
                        }
                        out[py * width + px] = c;
                    }
                }
            }
        }
    }
    return 1;
}

/* ---- the cache --------------------------------------------------------- */

void mgs_tex_cache_init(MgsTexCache* c)
{
    memset(c, 0, sizeof *c);
    c->trace_refusals = getenv("MGS_TRACE_TEXREFUSE") != NULL;
}

void mgs_tex_cache_free(MgsTexCache* c)
{
    unsigned i;
    for (i = 0; i < MGS_TEX_CACHE_ENTRIES; ++i) {
        free(c->entry[i].texels);
        c->entry[i].texels = NULL;
        c->entry[i].valid = 0;
    }
}

void mgs_tex_cache_invalidate(MgsTexCache* c)
{
    unsigned i;
    for (i = 0; i < MGS_TEX_CACHE_ENTRIES; ++i) c->entry[i].valid = 0;
}

static MgsTexture* find_slot(MgsTexCache* c)
{
    unsigned i, oldest = 0;
    uint64_t best = ~0ull;

    for (i = 0; i < MGS_TEX_CACHE_ENTRIES; ++i)
        if (!c->entry[i].valid) return &c->entry[i];

    for (i = 0; i < MGS_TEX_CACHE_ENTRIES; ++i)
        if (c->entry[i].generation < best) { best = c->entry[i].generation; oldest = i; }

    ++c->evictions;
    free(c->entry[oldest].texels);
    c->entry[oldest].texels = NULL;
    c->entry[oldest].valid = 0;
    return &c->entry[oldest];
}

/* A CONTENT HASH, BECAUSE AN ADDRESS IS NOT AN IDENTITY.
 *
 * The cache matched on address, format and size and never looked at the
 * bytes, and nothing ever invalidated it. That is correct only for textures
 * whose contents never change. A video frame is the opposite case: the
 * decoder writes every new frame into the SAME buffer, so after the first
 * decode every lookup is a hit and the picture stops moving - which is
 * exactly how the movie froze, and how a buffer caught mid-write stays on
 * screen as garbage for good.
 *
 * FNV-1a over the encoded bytes. Large textures are sampled on a stride
 * rather than read whole, because this runs per lookup and a 512x448 RGB565
 * frame is 448 KB: hashing all of it every draw would trade one bug for a
 * different performance complaint. The stride is chosen so a changed frame
 * cannot miss - a video frame differs in far more than one sample - while a
 * static texture costs a few hundred bytes to confirm.
 */
static uint64_t content_hash(const uint8_t* p, unsigned bytes)
{
    uint64_t h = 1469598103934665603ull;
    unsigned step = 1u, i;

    if (bytes > 4096u) step = bytes / 4096u;   /* ~4 KB sampled, at most */

    for (i = 0; i < bytes; i += step) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    h ^= bytes;
    h *= 1099511628211ull;
    return h;
}

const MgsTexture* mgs_tex_get(MgsTexCache* c, const GuestMemory* mem,
                              uint32_t addr, uint32_t format,
                              unsigned width, unsigned height,
                              uint32_t tlut_addr, uint32_t tlut_format)
{
    unsigned i;
    MgsTexture* t;
    uint16_t* palette = NULL;
    uint16_t palette_copy[16384];
    uint64_t hash;
    MgsTexture* reuse = NULL;

    /* FIVE DIFFERENT REFUSALS SHARED ONE COUNTER, which made "2,964 refused"
     * unactionable: a bad size, an unmapped palette and a format the decoder
     * does not know are three different jobs. Split. */
    if (!width || !height || width > 1024u || height > 1024u) {
        ++c->refused; ++c->refused_size;
        if (c->trace_refusals)
            fprintf(stderr, "[tex] refused SIZE: %ux%u format=0x%X\n",
                    width, height, format);
        return NULL;
    }
    if (width * height > MGS_TEX_MAX_TEXELS) {
        ++c->refused; ++c->refused_texels; return NULL;
    }

    {
        unsigned nbytes = texture_bytes(format, width, height);
        const uint8_t* src = nbytes ? guest_ptr(mem, addr, nbytes) : NULL;
        hash = src ? content_hash(src, nbytes) : 0u;
    }

    /* ONE ENTRY PER TEXTURE IDENTITY, NOT ONE PER VERSION.
     *
     * A changed texture must REPLACE its entry, never add a second one.
     * Adding was the first version of this and it was far worse than the bug
     * it fixed: a video re-decoding every frame filled all 256 slots with
     * stale copies of itself in seconds, after which every allocation evicted
     * something still in use and the whole texture set re-decoded every
     * frame. The boot stalled on the Konami logo. */
    for (i = 0; i < MGS_TEX_CACHE_ENTRIES; ++i) {
        MgsTexture* e = &c->entry[i];
        if (e->valid && e->addr == addr && e->format == format &&
            e->width == width && e->height == height &&
            e->tlut_addr == tlut_addr && e->tlut_format == tlut_format) {
            if (e->hash == hash) {
                e->generation = ++c->clock;
                ++c->hits;
                return e;
            }
            /* Same texture, new contents: take this slot back. */
            free(e->texels);
            e->texels = NULL;
            e->valid = 0;
            reuse = e;
            break;
        }
    }
    ++c->misses;

    /* Indexed formats need the palette, and it lives at its own address.
     * Copying it rather than pointing into guest memory keeps the decoded
     * texture valid if the game overwrites the palette afterwards. */
    if (format == 0x8u || format == 0x9u || format == 0xAu) {
        unsigned entries = (format == 0x8u) ? 16u : (format == 0x9u) ? 256u : 16384u;
        const uint8_t* p = guest_ptr(mem, tlut_addr, entries * 2u);
        if (!p) { ++c->refused; ++c->refused_palette; return NULL; }
        for (i = 0; i < entries; ++i) palette_copy[i] = be16(p + i * 2u);
        palette = palette_copy;
    }

    t = reuse ? reuse : find_slot(c);
    t->texels = (uint32_t*)malloc((size_t)width * height * sizeof(uint32_t));
    if (!t->texels) { ++c->refused; ++c->refused_alloc; return NULL; }

    if (!mgs_tex_decode(mem, addr, format, width, height,
                        palette, tlut_format, t->texels)) {
        free(t->texels);
        t->texels = NULL;
        ++c->refused; ++c->refused_decode;
        if (c->trace_refusals)
            fprintf(stderr, "[tex] refused DECODE: format=0x%X %ux%u addr=0x%08X\n",
                    format, width, height, addr);
        return NULL;
    }

    /* WHAT IS ACTUALLY BEING DECODED, by shape.
     *
     * A texture re-decoded every frame is a dynamic one, and during the movie
     * that is the video frame itself. Counting decodes by (format, size)
     * names it without having to guess from the picture. */
    {
        unsigned k;
        uint32_t shape = (format << 24) | ((width & 0xFFFu) << 12)
                       | (height & 0xFFFu);
        for (k = 0; k < c->shape_n; ++k)
            if (c->shape_key[k] == shape) { ++c->shape_hit[k]; break; }
        if (k == c->shape_n && c->shape_n < 16u) {
            c->shape_key[c->shape_n] = shape;
            c->shape_hit[c->shape_n] = 1u;
            ++c->shape_n;
        }
    }

    t->hash = hash;
    t->addr = addr; t->format = format;
    t->width = (uint16_t)width; t->height = (uint16_t)height;
    t->tlut_addr = tlut_addr; t->tlut_format = tlut_format;
    t->generation = ++c->clock;
    t->valid = 1;
    ++c->decodes;
    return t;
}

/* ---- sampling ---------------------------------------------------------- */

static int wrap_coord(int v, int size, unsigned mode)
{
    if (size <= 0) return 0;
    switch (mode) {
        case 1: {                                  /* repeat */
            int m = v % size;
            return m < 0 ? m + size : m;
        }
        case 2: {                                  /* mirror */
            int period = size * 2;
            int m = v % period;
            if (m < 0) m += period;
            return (m < size) ? m : (period - 1 - m);
        }
        default:                                   /* clamp */
            return v < 0 ? 0 : (v >= size ? size - 1 : v);
    }
}

static uint32_t texel(const MgsTexture* t, int x, int y,
                      unsigned wrap_s, unsigned wrap_t)
{
    x = wrap_coord(x, t->width, wrap_s);
    y = wrap_coord(y, t->height, wrap_t);
    return t->texels[(unsigned)y * t->width + (unsigned)x];
}

static uint32_t blend4(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                       float fx, float fy)
{
    unsigned i;
    uint32_t out = 0;
    for (i = 0; i < 4u; ++i) {
        float ca = (float)((a >> (i * 8)) & 0xFFu);
        float cb = (float)((b >> (i * 8)) & 0xFFu);
        float cc = (float)((c >> (i * 8)) & 0xFFu);
        float cd = (float)((d >> (i * 8)) & 0xFFu);
        float top = ca + (cb - ca) * fx;
        float bot = cc + (cd - cc) * fx;
        int v = (int)(top + (bot - top) * fy + 0.5f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out |= (uint32_t)v << (i * 8);
    }
    return out;
}

uint32_t mgs_tex_sample(const MgsTexture* t, float u, float v,
                        unsigned wrap_s, unsigned wrap_t, int bilinear)
{
    float fx, fy;
    int x, y;

    if (!t || !t->texels) return 0xFFFFFFFFu;

    /* The half-texel offset is the hardware's: a coordinate of 0 samples the
     * CENTRE of the first texel, not its corner. Omitting it shifts every
     * texture by half a texel, which is invisible on a wall and obvious on a
     * user interface. */
    fx = u * (float)t->width - 0.5f;
    fy = v * (float)t->height - 0.5f;
    x = (int)(fx < 0.0f ? fx - 1.0f : fx);
    y = (int)(fy < 0.0f ? fy - 1.0f : fy);

    if (!bilinear)
        return texel(t, (int)(u * (float)t->width), (int)(v * (float)t->height),
                     wrap_s, wrap_t);

    return blend4(texel(t, x, y, wrap_s, wrap_t),
                  texel(t, x + 1, y, wrap_s, wrap_t),
                  texel(t, x, y + 1, wrap_s, wrap_t),
                  texel(t, x + 1, y + 1, wrap_s, wrap_t),
                  fx - (float)x, fy - (float)y);
}
