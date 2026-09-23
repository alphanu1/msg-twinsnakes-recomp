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

/* ONE TIMELINE FOR COPIES AND DECODES.
 *
 * "The texture decodes as noise" and "the copy wrote it correctly" are both
 * true readings of separate traces, and separate traces cannot say which
 * came FIRST. This counter is shared with the copy path so the two can be
 * read as one sequence. Diagnostic only; nothing branches on it. */
uint64_t mgs_gx_seq;

/* WHICH ADDRESSES HOLD TEXELS AN EFB COPY PUT THERE.
 *
 * The content hash asks "have these bytes changed", and for a texture the
 * game builds with an EFB copy that is the wrong question. The game copies
 * the finished frame BOTH to a texture and to the framebuffer, and it uses
 * the same memory for both - which is legal, because on hardware the texture
 * unit reads its own memory and a copy to the framebuffer does not reach it.
 * Main memory ends up holding YUV 4:2:2 while the texture still reads as
 * texels.
 *
 * Reading main memory at every bind instead, we saw the YUV: decoded as
 * RGBA8 it is smooth and PURPLE, which is what the whole frame turned. The
 * measured sequence for one buffer ends
 *
 *     ... F T T D F T T D F D T T D F D T T D F D ...
 *              (F = framebuffer copy, T = texture copy, D = decode)
 *
 * and those `F D` pairs - a framebuffer copy and then a bind, with no
 * texture copy between - are the corrupted frames.
 *
 * So a texture whose texels came from an EFB copy is validated by WHICH
 * COPY made it, not by what main memory says now. A serial per address,
 * bumped by each copy there; ordinary textures keep the content hash,
 * because those really are changed by writing to them. */
static struct { uint32_t addr, serial; } s_efb_copy[32];
static unsigned s_efb_copy_n;

void mgs_tex_note_efb_copy(uint32_t addr)
{
    unsigned i;
    for (i = 0; i < s_efb_copy_n; ++i)
        if (s_efb_copy[i].addr == addr) { ++s_efb_copy[i].serial; return; }
    if (s_efb_copy_n < 32u) {
        s_efb_copy[s_efb_copy_n].addr = addr;
        s_efb_copy[s_efb_copy_n].serial = 1u;
        ++s_efb_copy_n;
    }
}

static uint32_t efb_serial(uint32_t addr)
{
    unsigned i;
    for (i = 0; i < s_efb_copy_n; ++i)
        if (s_efb_copy[i].addr == addr) return s_efb_copy[i].serial;
    return 0u;
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

    /* THE STRIDE MUST BE ODD, and that is the whole of this function.
     *
     * Textures are stored in TILES whose size is a power of two - 64 bytes
     * for RGBA8, 32 for most others - so a stride that shares a factor with
     * the tile size never leaves the same few byte positions inside it.
     *
     * A 512x448 RGBA8 surface is 917,504 bytes, and 917504/4096 is 224.
     * 224 is a multiple of 32, so the walk visits offsets 0 and 32 within
     * every tile it touches and nothing else: for RGBA8 that is the alpha
     * and the green of texel 0. The alpha was constant across the image, so
     * half the samples never moved, and the hash came out the SAME on every
     * frame of a playing movie. Measured: the hash sat at
     * 0xBCD7FB68DE2FB799 for hundreds of consecutive lookups while a plain
     * sum of the same memory went 2,293,760 -> 3,367,518 -> 2,394,389.
     *
     * The cache then served one decode for the life of the scene - so the
     * video stopped updating, and whatever happened to be in that buffer at
     * the moment of the first decode stayed on screen. That is the same
     * failure this hash was added to fix (the comment above), reintroduced
     * by the sampling rather than by the key.
     *
     * An odd stride is coprime with every power of two, so the walk covers
     * all byte positions within a tile. */
    if (bytes > 4096u) step = (bytes / 4096u) | 1u;   /* ~4 KB, odd stride */

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
    uint32_t serial;
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

    serial = efb_serial(addr);
    if (serial) {
        /* Validated by the copy that made it. Hashing main memory here
         * would not just be wasted - it would be asking the wrong
         * question, and getting a wrong answer every frame. */
        hash = 0u;
    } else {
        unsigned nbytes = texture_bytes(format, width, height);
        const uint8_t* src = nbytes ? guest_ptr(mem, addr, nbytes) : NULL;
        hash = src ? content_hash(src, nbytes) : 0u;
    }

    /* THE PALETTE IS PART OF THE TEXTURE'S CONTENT, NOT PART OF ITS ADDRESS.
     *
     * The cache keyed on the TLUT's *address* and hashed only the texels, so
     * a palette reloaded with different colours at the same address returned
     * the previous decode. The game does exactly that: the subtitles came
     * out pink or blue depending on what the movie had last loaded into that
     * TLUT, which is how this was found - Ben noticed the colour tracked
     * whether the video frame beside it had decoded.
     *
     * Folding the palette bytes into the same hash makes a recoloured
     * palette a content change, which the existing "same texture, new
     * contents: take this slot back" path then handles correctly. */
    if (format == 0x8u || format == 0x9u || format == 0xAu) {
        unsigned pentries =
            (format == 0x8u) ? 16u : (format == 0x9u) ? 256u : 16384u;
        const uint8_t* pal = guest_ptr(mem, tlut_addr, pentries * 2u);
        if (pal) hash = hash * 1099511628211ull
                      ^ content_hash(pal, pentries * 2u);
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
            if (serial ? (e->efb_serial == serial) : (e->hash == hash)) {
                e->generation = ++c->clock;
                ++c->hits;
                {   /* A hit on a watched buffer is as interesting as a miss:
                     * a video frame that stops re-decoding has stopped
                     * moving, and only the hits say so. */
                    static long watch = -1; static unsigned n2;
                    if (watch == -1) { const char* ev = getenv("MGS_TRACE_BUF");
                                       watch = ev ? (long)strtoul(ev, NULL, 0)
                                                  : 0; }
                    if (watch && (uint32_t)watch == addr && n2++ < 400u)
                    {
                        /* The hash against a plain sum of the same memory:
                         * if the bytes move and the hash does not, the
                         * SAMPLING is at fault, not the cache. */
                        unsigned nb = texture_bytes(format, width, height);
                        const uint8_t* sp = guest_ptr(mem, addr, nb);
                        unsigned long long sum = 0; unsigned q;
                        if (sp) for (q = 0; q < nb; q += 7u) sum += sp[q];
                        fprintf(stderr, "[buf] %6llu  HIT    0x%08X "
                                "%ux%u fmt 0x%X  hash 0x%016llX  "
                                "byte sum %llu\n",
                                (unsigned long long)++mgs_gx_seq, addr,
                                width, height, format,
                                (unsigned long long)hash, sum);
                    }
                }
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
        if (!p) {
            ++c->refused; ++c->refused_palette;
            /* SAY WHICH ADDRESS, as the decode path already does. A count
             * of 3,198 palette refusals says a paletted texture never
             * reaches the screen and nothing about whether the TLUT address
             * is zero (never loaded), out of range, or simply too short for
             * the entry count this format implies. */
            if (c->trace_refusals)
                fprintf(stderr, "[tex] refused PALETTE: format=0x%X %ux%u "
                                "tlut=0x%08X entries=%u tlutfmt=%u\n",
                        format, width, height, tlut_addr, entries,
                        tlut_format);
            return NULL;
        }
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

    /* MGS_DUMP_TEX=<w>x<h> writes the first few decoded textures of that
     * shape as PPM. The video frame is a 512x448 RGBA8 texture decoded once
     * per frame; whether the fault is in the game's decode or in ours cannot
     * be told from the screen, because both end as noise. Looking at the
     * texture itself separates them. */
    {
        static int want_w = -1, want_h, want_fmt, dumped;
        if (want_w < 0) {
            /* "<w>x<h>" or "<fmt>:<w>x<h>" - the format matters, because two
             * different textures share 512x448 here and only one is the
             * video frame. Matching on size alone picked the wrong one and
             * produced a confident wrong conclusion. */
            /* MGS_DUMP_TEX=<w>x<h> and optionally MGS_DUMP_TEXFMT=<hex>.
             * The format matters: two different textures share 512x448 here
             * and only one is the video frame. Matching on size alone picked
             * the wrong one and produced a confident wrong conclusion. */
            const char* e = getenv("MGS_DUMP_TEX");
            const char* ef = getenv("MGS_DUMP_TEXFMT");
            want_w = 0;
            want_fmt = ef ? (int)strtoul(ef, NULL, 16) : -1;
            if (e && sscanf(e, "%dx%d", &want_w, &want_h) != 2) want_w = 0;
            if (e) fprintf(stderr, "[tex] dump armed: %dx%d fmt %d\n",
                           want_w, want_h, want_fmt);
        }
        /* MGS_DUMP_TEXSKIP=<n>: skip the first n matching decodes. The
         * first four of a shape are from before the movie starts, and the
         * question is what the texture looks like DURING it. */
        {
            static long skip = -1;
            if (skip < 0) { const char* e = getenv("MGS_DUMP_TEXSKIP");
                            skip = e ? strtol(e, NULL, 0) : 0; }
            if (want_w && (int)width == want_w && (int)height == want_h &&
                (want_fmt < 0 || (int)format == want_fmt) && skip > 0) {
                --skip;
                goto no_dump;
            }
        }
        if (want_w && (int)width == want_w && (int)height == want_h &&
            (want_fmt < 0 || (int)format == want_fmt) && dumped < 4) {
            char path[256];
            FILE* f;
            snprintf(path, sizeof path, "%s/tex_%d.ppm",
                     getenv("MGS_DUMP_DIR") ? getenv("MGS_DUMP_DIR") : ".",
                     dumped);
            f = fopen(path, "wb");
            fprintf(stderr, "[tex] dumping %ux%u fmt 0x%X -> %s (%s)\n",
                    width, height, format, path, f ? "ok" : "FAILED");
            if (f) {
                unsigned px;
                fprintf(f, "P6\n%u %u\n255\n", width, height);
                for (px = 0; px < width * height; ++px) {
                    uint32_t c = t->texels[px];
                    fputc((c >> 16) & 0xFF, f);
                    fputc((c >> 8) & 0xFF, f);
                    fputc(c & 0xFF, f);
                }
                fclose(f);
                /* THE SOURCE BYTES, alongside the decoded result.
                 *
                 * A decoded frame of uniform grey means one of two very
                 * different things: the bytes were a picture and we detiled
                 * them wrongly, or the bytes were never a picture. Only the
                 * source settles it - structure at 64-byte tile boundaries
                 * says the former, uncorrelated bytes the latter. */
                {
                    unsigned nb = texture_bytes(format, width, height);
                    const uint8_t* src = guest_ptr(mem, addr, nb);
                    char rp[256];
                    snprintf(rp, sizeof rp, "%s/raw_%d.bin",
                             getenv("MGS_DUMP_DIR") ? getenv("MGS_DUMP_DIR") : ".",
                             dumped);
                    if (src) {
                        FILE* rf = fopen(rp, "wb");
                        if (rf) { fwrite(src, 1, nb < 65536u ? nb : 65536u, rf);
                                  fclose(rf); }
                    }
                    fprintf(stderr, "[tex]   source 0x%08X %u bytes %s\n",
                            addr, nb, src ? "mapped" : "UNMAPPED");
                }
                ++dumped;
            }
        }
    }
no_dump:
    (void)0;

    /* IS THIS TEXTURE A PICTURE OR IS IT NOISE?
     *
     * The screen shows noise of 9,000-odd colours while the video frame and
     * the render-to-texture target both decode as clean images. Something
     * else supplies it. Roughness - the mean difference between horizontally
     * adjacent texels - separates the two by an order of magnitude: artwork
     * scores a few, uncorrelated bytes score tens. Scored here, per shape, so
     * the culprit names itself instead of being dumped one guess at a time. */
    {
        unsigned yy, rough = 0u, cnt = 0u;
        for (yy = 0; yy < height; yy += 4u) {
            unsigned xx;
            for (xx = 1u; xx < width; xx += 2u) {
                uint32_t a = t->texels[yy * width + xx - 1u];
                uint32_t b = t->texels[yy * width + xx];
                int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
                int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
                rough += (unsigned)(va > vb ? va - vb : vb - va);
                ++cnt;
            }
        }
        if (cnt) {
            unsigned k, r = rough / cnt;
            uint32_t shape = (format << 24) | ((width & 0xFFFu) << 12)
                           | (height & 0xFFFu);
            for (k = 0; k < c->rough_n; ++k)
                if (c->rough_key[k] == shape) break;
            if (k == c->rough_n && c->rough_n < 24u) {
                c->rough_key[c->rough_n] = shape;
                ++c->rough_n;
            }
            if (k < 24u) {
                c->rough_sum[k] += r;
                ++c->rough_cnt[k];
                if (r > c->rough_max[k]) c->rough_max[k] = r;
                /* WHERE the noisy one lives. A texture that decodes as noise
                 * either never had a picture written to it, or had one
                 * written and then overwritten by something else. Knowing its
                 * address lets the second be checked against every other
                 * writer's destination. */
                if (r > 20u) {
                    c->rough_addr[k] = addr;
                    c->rough_bytes[k] = texture_bytes(format, width, height);
                }
            }
        }
    }

    {   /* MGS_TRACE_BUF=<addr>: every decode of one buffer, with the
         * roughness of what came out. */
        static long watch = -1;
        if (watch == -1) { const char* e = getenv("MGS_TRACE_BUF");
                           watch = e ? (long)strtoul(e, NULL, 0) : 0; }
        if (watch && (uint32_t)watch == addr) {
            unsigned yy, cnt = 0u, rough = 0u;
            for (yy = 0; yy < height; yy += 8u)
                for (i = 1u; i < width; i += 4u) {
                    uint32_t a1 = t->texels[yy * width + i - 1u];
                    uint32_t b1 = t->texels[yy * width + i];
                    int va = (int)(((a1 >> 16) & 0xFF) + ((a1 >> 8) & 0xFF)
                                   + (a1 & 0xFF)) / 3;
                    int vb = (int)(((b1 >> 16) & 0xFF) + ((b1 >> 8) & 0xFF)
                                   + (b1 & 0xFF)) / 3;
                    rough += (unsigned)(va > vb ? va - vb : vb - va);
                    ++cnt;
                }
            fprintf(stderr, "[buf] %6llu  DECODE 0x%08X %ux%u fmt 0x%X  "
                    "roughness %u\n", (unsigned long long)++mgs_gx_seq,
                    addr, width, height, format, cnt ? rough / cnt : 0u);
        }
    }
    t->hash = hash;
    t->efb_serial = serial;
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
