/* Texture decoding.
 *
 * Every format here is TILED, and the tile size and the order within it both
 * depend on the format. The failures are quiet: a wrong tile width produces a
 * recognisable image with its blocks transposed, and a wrong channel order
 * produces a perfectly sharp image in the wrong colours - both read as bad art
 * rather than as a decode bug. So each format is given texels whose correct
 * output is known by construction, and the awkward ones are given their own
 * case rather than being covered by a general loop.
 */
#include "gx/texture.h"
#include "gx/bp.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

#define ADDR 0x80200000u

static GuestMemory mem;

static void poke(uint32_t off, const uint8_t* bytes, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) guest_write8(&mem, ADDR + off + i, bytes[i]);
}

int main(void)
{
    static uint32_t out[64 * 64];

    if (!guest_memory_init(&mem)) { printf("FAIL: no guest memory\n"); return 1; }

    /* --- RGB565: a 4x4 tile, so a 4x4 texture is exactly one tile ------- */
    {
        /* 0xF800 is full red, 0x07E0 full green, 0x001F full blue. */
        uint8_t t[32];
        unsigned i;
        for (i = 0; i < 16u; ++i) { t[i * 2] = 0xF8; t[i * 2 + 1] = 0x00; }
        t[0] = 0x07; t[1] = 0xE0;                    /* texel 0 green */
        poke(0, t, 32);
        CHECK(mgs_tex_decode(&mem, ADDR, GX_TF_RGB565, 4, 4, NULL, 0, out));
        CHECK(out[0] == 0xFF00FF00u);                /* green, opaque */
        CHECK(out[1] == 0xFFFF0000u);                /* red */
        CHECK(out[15] == 0xFFFF0000u);
    }

    /* --- RGB5A3: two formats in one bit --------------------------------- */
    {
        uint8_t t[32];
        unsigned i;
        for (i = 0; i < 32u; ++i) t[i] = 0;
        /* Top bit set: RGB555, opaque. 0xFC00 = red 0x1F. */
        t[0] = 0xFC; t[1] = 0x00;
        /* Top bit clear: ARGB3444 with alpha 0 - fully transparent. */
        t[2] = 0x0F; t[3] = 0x00;
        poke(0, t, 32);
        CHECK(mgs_tex_decode(&mem, ADDR, GX_TF_RGB5A3, 4, 4, NULL, 0, out));
        CHECK((out[0] >> 24) == 0xFFu);
        CHECK(((out[0] >> 16) & 0xFFu) == 0xFFu);
        CHECK((out[1] >> 24) == 0x00u);              /* alpha 0 */
    }

    /* --- RGBA8: a 4x4 tile in TWO 32-byte halves ------------------------ */
    {
        /* Alpha and red in the first half, green and blue in the second.
         * Read as one contiguous block this gives a plausible wrong image,
         * which is why it has its own case. */
        uint8_t t[64];
        unsigned i;
        for (i = 0; i < 64u; ++i) t[i] = 0;
        t[0] = 0xFF; t[1] = 0x11;                    /* texel 0: a=FF r=11 */
        t[32] = 0x22; t[33] = 0x33;                  /* texel 0: g=22 b=33 */
        poke(0, t, 64);
        CHECK(mgs_tex_decode(&mem, ADDR, GX_TF_RGBA8, 4, 4, NULL, 0, out));
        CHECK(out[0] == 0xFF112233u);
    }

    /* --- I4: two texels per byte, high nibble first --------------------- */
    {
        uint8_t t[32];
        memset(t, 0, sizeof t);
        t[0] = 0xF0;                                 /* texel 0 = F, texel 1 = 0 */
        poke(0, t, 32);
        CHECK(mgs_tex_decode(&mem, ADDR, GX_TF_I4, 8, 8, NULL, 0, out));
        CHECK(out[0] == 0xFFFFFFFFu);
        CHECK(out[1] == 0x00000000u);
    }

    /* --- C8: an indexed format reads its colour from the palette -------- */
    {
        uint8_t t[32];
        uint16_t tlut[256];
        unsigned i;
        memset(t, 0, sizeof t);
        t[0] = 3u;                                   /* texel 0 -> entry 3 */
        poke(0, t, 32);
        for (i = 0; i < 256u; ++i) tlut[i] = 0u;
        tlut[3] = 0x001Fu;                           /* blue, as RGB565 */
        CHECK(mgs_tex_decode(&mem, ADDR, GX_TF_C8, 8, 4, tlut, GX_TL_RGB565, out));
        CHECK(out[0] == 0xFF0000FFu);
    }

    /* --- a format we cannot decode is REFUSED, not guessed at ----------- */
    CHECK(!mgs_tex_decode(&mem, ADDR, 0x7u, 4, 4, NULL, 0, out));
    /* --- and so is a texture that runs off the end of memory ------------
     * Not address zero: that folds to offset zero, which IS readable - it is
     * the OS's low memory. An address near the top of RAM with a texture too
     * large to fit is the case that must be refused. */
    CHECK(!mgs_tex_decode(&mem, 0x80000000u + GUEST_RAM_SIZE - 16u,
                          GX_TF_RGB565, 64, 64, NULL, 0, out));

    /* --- the cache returns the same texture twice ----------------------- */
    {
        MgsTexCache c;
        const MgsTexture *a, *b;
        uint8_t t[32];
        memset(t, 0x5A, sizeof t);
        poke(0, t, 32);
        mgs_tex_cache_init(&c);
        a = mgs_tex_get(&c, &mem, ADDR, GX_TF_RGB565, 4, 4, 0, 0);
        b = mgs_tex_get(&c, &mem, ADDR, GX_TF_RGB565, 4, 4, 0, 0);
        CHECK(a != NULL && a == b);
        CHECK(c.decodes == 1u && c.hits == 1u);

        /* Invalidation means the next fetch decodes again - that is what
         * GXInvalidateTexAll has to mean, or a game that reuses an address
         * keeps drawing the old texture. */
        mgs_tex_cache_invalidate(&c);
        (void)mgs_tex_get(&c, &mem, ADDR, GX_TF_RGB565, 4, 4, 0, 0);
        CHECK(c.decodes == 2u);
        mgs_tex_cache_free(&c);
    }

    /* --- sampling: clamp, repeat and mirror ----------------------------- */
    {
        MgsTexture t;
        uint32_t texels[4] = { 0xFF000000u, 0xFF010000u, 0xFF020000u, 0xFF030000u };
        t.texels = texels; t.width = 4; t.height = 1; t.valid = 1;
        /* Clamp: past the right edge stays on the last texel. */
        CHECK(mgs_tex_sample(&t, 2.0f, 0.0f, 0, 0, 0) == 0xFF030000u);
        /* Repeat: 1.25 is a quarter of the way in again. */
        CHECK(mgs_tex_sample(&t, 1.25f, 0.0f, 1, 1, 0) == 0xFF010000u);
    }

    /* --- THE CACHE MUST NOTICE A CHANGED TEXTURE ----------------------
     *
     * The cache keys on the address and compares a hash of the CONTENT, and
     * the content is SAMPLED on a stride rather than read whole - a 512x448
     * RGBA8 surface is 917 KB and hashing all of it per lookup is not free.
     * Sparse sampling is fine; the stride is the risk.
     *
     * Textures are stored in tiles whose size is a power of two, so a stride
     * sharing a factor with the tile size only ever reads the same few byte
     * positions inside it. At 512x448 RGBA8 the stride was 917504/4096 =
     * 224, a multiple of the 64-byte tile: the walk saw offsets 0 and 32 and
     * nothing else - the alpha and the green of texel 0 - and the alpha was
     * constant across the image. The hash never moved while a movie played.
     * One decode was served for the whole scene and the video froze on
     * whatever was in the buffer when it was first bound.
     *
     * What that missed is a change spread across the image at a tile offset
     * the walk never visits, which is what this builds: one byte per tile,
     * at each offset in turn. A single isolated byte is NOT tested, because
     * sparse sampling is entitled to miss one byte in 917 KB - asserting
     * that would be asserting the sampling away. */
    {
        MgsTexCache cache;
        const unsigned w = 512u, h = 448u;      /* the size that failed */
        const unsigned tile = 64u;              /* RGBA8: 4x4 texels */
        const unsigned bytes = 512u * 448u * 4u;
        unsigned off, k;

        mgs_tex_cache_init(&cache);
        for (k = 0; k < bytes; k += 4u)
            guest_write32(&mem, ADDR + k, 0x11223344u);

        for (off = 0; off < tile; ++off) {
            const MgsTexture* before;
            const MgsTexture* after;
            uint32_t sum_before = 0, sum_after = 0;
            unsigned q;

            before = mgs_tex_get(&cache, &mem, ADDR, 0x6u, w, h, 0, 0);
            if (!before) { printf("FAIL: no texture\n"); ++failures; break; }
            for (q = 0; q < w * h; q += 97u) sum_before += before->texels[q];

            /* One byte per tile, at this offset, right across the image. */
            for (k = off; k < bytes; k += tile)
                guest_write8(&mem, ADDR + k,
                             (uint8_t)(guest_read8(&mem, ADDR + k) ^ 0xFFu));

            after = mgs_tex_get(&cache, &mem, ADDR, 0x6u, w, h, 0, 0);
            if (!after) { printf("FAIL: no texture\n"); ++failures; break; }
            for (q = 0; q < w * h; q += 97u) sum_after += after->texels[q];

            if (sum_before == sum_after) {
                printf("FAIL: every tile changed at offset %u of 64 and the "
                       "%ux%u RGBA8 texture did not re-decode\n",
                       off, w, h);
                ++failures;
            }
        }
        mgs_tex_cache_free(&cache);
    }

    guest_memory_free(&mem);
    printf(failures ? "texture: FAILED\n" : "texture: ok\n");
    return failures ? 1 : 0;
}
