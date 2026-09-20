#include <stdio.h>
#include <stdlib.h>
#include "efb.h"

#include <string.h>

void mgs_efb_init(MgsEfb* efb)
{
    memset(efb, 0, sizeof *efb);
    efb->copy_stride = MGS_EFB_WIDTH * 2u;   /* one YUV 4:2:2 line */
}

void mgs_efb_set_clear(MgsEfb* efb, uint32_t argb) { efb->clear_argb = argb; }

void mgs_efb_set_dest(MgsEfb* efb, uint32_t guest_addr, uint32_t stride)
{
    efb->copy_dest = guest_addr;
    if (stride) efb->copy_stride = stride;
}

/* RGB to YCbCr, ITU-R BT.601, which is what the GameCube's copy pipeline
 * uses. The coefficients are the standard's, not tuned: a wrong matrix shows
 * up as a colour cast that is easy to mistake for a rendering bug.
 */
static void rgb_to_ycbcr(uint32_t argb, int* y, int* cb, int* cr)
{
    int r = (int)((argb >> 16) & 0xFF);
    int g = (int)((argb >> 8) & 0xFF);
    int b = (int)(argb & 0xFF);

    *y  = ((  66 * r + 129 * g +  25 * b + 128) >> 8) + 16;
    *cb = (( -38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
    *cr = (( 112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
}

static uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }


/* EFB TO TEXTURE.
 *
 * Until now a copy that was not to the external framebuffer did nothing at
 * all, and a boot that reaches the movie makes 21,041 of them - 64x64, format
 * 0xC - every one discarded. Whatever the game sampled afterwards was
 * therefore whatever happened to be in that memory, which is how a caption
 * compositor renders as noise.
 *
 * Textures are TILED: 4x4 texels for the 16-bit formats, 8x4 for the 8-bit
 * ones, and the tiles run left to right then top to bottom. Writing linearly
 * gives a picture that is recognisably the right thing cut into squares and
 * shuffled, which is a distinctive and easily mistaken kind of wrong.
 */
static void tex_tile_shape(unsigned fmt, unsigned* tw, unsigned* th,
                           unsigned* bpp)
{
    switch (fmt) {
        case 0x6u: *tw = 4; *th = 4; *bpp = 32; break;   /* RGBA8 */
        case 0x0u: *tw = 8; *th = 8; *bpp = 4;  break;   /* I4  */
        case 0x1u: case 0x8u: case 0x9u: case 0xAu:
                   *tw = 8; *th = 4; *bpp = 8;  break;   /* I8, R8, G8, B8 */
        case 0x2u: *tw = 8; *th = 4; *bpp = 8;  break;   /* IA4 */
        default:   *tw = 4; *th = 4; *bpp = 16; break;   /* IA8, 565, 5A3, RG8, GB8 */
    }
}

static uint16_t encode_texel(unsigned fmt, uint32_t argb)
{
    unsigned a = (argb >> 24) & 0xFFu, r = (argb >> 16) & 0xFFu;
    unsigned g = (argb >> 8) & 0xFFu,  b = argb & 0xFFu;
    unsigned i = (r * 77u + g * 151u + b * 28u) >> 8;   /* luminance */

    switch (fmt) {
        case 0x3u: return (uint16_t)((a << 8) | i);                  /* IA8 */
        case 0x4u: return (uint16_t)(((r & 0xF8u) << 8) |            /* RGB565 */
                                     ((g & 0xFCu) << 3) | (b >> 3));
        case 0x5u:                                                    /* RGB5A3 */
            if (a >= 0xE0u)
                return (uint16_t)(0x8000u | ((r >> 3) << 10) |
                                  ((g >> 3) << 5) | (b >> 3));
            return (uint16_t)(((a >> 5) << 12) | ((r >> 4) << 8) |
                              ((g >> 4) << 4) | (b >> 4));
        case 0xBu: return (uint16_t)((r << 8) | g);                  /* RG8 */
        case 0xCu: return (uint16_t)((g << 8) | b);                  /* GB8 */
        default:   return (uint16_t)((a << 8) | i);
    }
}

void mgs_efb_copy_tex(MgsEfb* efb, GuestMemory* mem,
                      unsigned sx, unsigned sy,
                      unsigned width, unsigned height, unsigned fmt)
{
    unsigned tw, th, bpp, tiles_x, tx, ty, x, y;

    if (!efb->copy_dest || !mem || !width || !height) return;

    /* WHAT IS IN THE EMBEDDED BUFFER AT THE MOMENT OF THE COPY?
     *
     * A texture copy can only be as good as what it copies. If the video is
     * drawn into the EFB as noise then every stage after this one is faithful
     * and the fault is upstream, in the drawing. Roughness separates the two:
     * a picture scores a few, uncorrelated pixels score tens. Measured over
     * the source rectangle, not the whole buffer. */
    if (getenv("MGS_TRACE_COPYSRC")) {
        static unsigned n;
        /* Only the large ones. The 64x64 caption copies run thousands of
         * times and would fill any cap long before the movie starts. */
        if (width > 256u && n++ < 10u) {
            unsigned yy, cnt = 0u, rough = 0u, lit = 0u;
            for (yy = 0; yy < height && yy + sy < MGS_EFB_HEIGHT; yy += 4u) {
                unsigned xx;
                for (xx = 1u; xx < width && xx + sx < MGS_EFB_WIDTH; xx += 2u) {
                    uint32_t a = efb->pixels[(sy + yy) * MGS_EFB_WIDTH + sx + xx - 1u];
                    uint32_t b = efb->pixels[(sy + yy) * MGS_EFB_WIDTH + sx + xx];
                    int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
                    int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
                    rough += (unsigned)(va > vb ? va - vb : vb - va);
                    if (b & 0x00FFFFFFu) ++lit;
                    ++cnt;
                }
            }
            fprintf(stderr, "[copysrc] %ux%u at (%u,%u) -> 0x%08X  "
                    "EFB roughness %u  lit %u%%\n",
                    width, height, sx, sy, efb->copy_dest,
                    cnt ? rough / cnt : 0u, cnt ? lit * 100u / cnt : 0u);
        }
    }
    tex_tile_shape(fmt, &tw, &th, &bpp);
    tiles_x = (width + tw - 1u) / tw;

    for (ty = 0; ty < (height + th - 1u) / th; ++ty) {
        for (tx = 0; tx < tiles_x; ++tx) {
            unsigned tile_bytes = tw * th * bpp / 8u;
            /* THE STRIDE IS THE DISTANCE BETWEEN ROWS OF TILES, and it is not
             * the same as packing them tightly. These copies are 64 wide with
             * 4x4 tiles, so a row of tiles is 16 * 32 = 512 bytes - but the
             * game programmes a stride of 1024. Packing at 512 puts every row
             * after the first at the wrong address, which writes a correctly
             * encoded texture into a scrambled layout. */
            /* TILES PACKED, NOT SPACED BY THE COPY STRIDE.
             *
             * The stride register is shared with the framebuffer path and
             * reads 1024 for these 64-wide copies - the external buffer's
             * line pitch, not this texture's. Spacing tile rows by it writes
             * 16 KB where the texture is 8 KB, over whatever follows: that
             * took a run from 0 desyncs to 26,323,104, because what follows
             * includes display lists. Tried, measured, reverted. */
            /* The stride is the distance between rows of TILES, and for
             * these copies it is exactly tiles_x * tile_bytes - 8192 for a
             * 512-wide RGBA8 target, 1024 for a 64-wide one. Both match what
             * the game programmes, which is what confirmed the format. */
            uint32_t row = efb->copy_stride ? efb->copy_stride
                                            : tiles_x * tile_bytes;
            uint32_t base = efb->copy_dest + ty * row + tx * tile_bytes;
            for (y = 0; y < th; ++y) {
                for (x = 0; x < tw; ++x) {
                    unsigned px = tx * tw + x, py = ty * th + y;
                    unsigned ex, ey;
                    uint32_t argb;
                    uint8_t* p;

                    if (px >= width || py >= height) continue;
                    ex = sx + px; ey = sy + py;
                    if (ex >= MGS_EFB_WIDTH || ey >= MGS_EFB_HEIGHT) continue;
                    argb = efb->pixels[ey * MGS_EFB_WIDTH + ex];

                    if (bpp == 32u) {
                        /* RGBA8 IS TWO HALVES, NOT ONE BLOCK.
                         *
                         * A 4x4 tile is 64 bytes: sixteen alpha/red pairs,
                         * then sixteen green/blue pairs. Written as one run
                         * of 32-bit texels it produces a plausible wrong
                         * image rather than an obviously wrong one, which is
                         * the failure that hides. */
                        unsigned t_i = y * tw + x;
                        p = guest_ptr(mem, base + t_i * 2u, 2u);
                        if (p) {
                            p[0] = (uint8_t)(argb >> 24);        /* A */
                            p[1] = (uint8_t)(argb >> 16);        /* R */
                        }
                        p = guest_ptr(mem, base + 32u + t_i * 2u, 2u);
                        if (p) {
                            p[0] = (uint8_t)(argb >> 8);         /* G */
                            p[1] = (uint8_t)argb;                /* B */
                        }
                    } else if (bpp == 16u) {
                        uint16_t t = encode_texel(fmt, argb);
                        p = guest_ptr(mem, base + (y * tw + x) * 2u, 2u);
                        if (!p) continue;
                        p[0] = (uint8_t)(t >> 8); p[1] = (uint8_t)t;
                    } else if (bpp == 8u) {
                        unsigned v = (fmt == 0x8u) ? ((argb >> 16) & 0xFFu)
                                   : (fmt == 0x9u) ? ((argb >> 8) & 0xFFu)
                                   : (fmt == 0xAu) ? (argb & 0xFFu)
                                   : (((argb >> 16) & 0xFFu) * 77u +
                                      ((argb >> 8) & 0xFFu) * 151u +
                                      (argb & 0xFFu) * 28u) >> 8;
                        p = guest_ptr(mem, base + y * tw + x, 1u);
                        if (!p) continue;
                        *p = (uint8_t)v;
                    }
                }
            }
        }
    }
    /* DOES THE ENCODER ROUND-TRIP?
     *
     * A flat source must produce flat bytes. If the embedded buffer is
     * uniform and what lands in memory is not, the encoder is manufacturing
     * the noise rather than copying it - and that would make it the source,
     * not a victim, of everything downstream. Comparing the spread of the
     * source against the spread of the bytes written answers it directly.
     */
    if (getenv("MGS_CHECK_ENCODE")) {
        static unsigned n;
        if (width > 256u && n++ < 6u) {
            unsigned i, srcmin = 255u, srcmax = 0u, bmin = 255u, bmax = 0u;
            const uint8_t* wrote = guest_ptr(mem, efb->copy_dest, 4096u);
            for (i = 0; i < 2048u; ++i) {
                unsigned px = (sy + (i / 64u)) * MGS_EFB_WIDTH + sx + (i % 64u);
                uint32_t c = efb->pixels[px];
                unsigned v = (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3u;
                if (v < srcmin) srcmin = v;
                if (v > srcmax) srcmax = v;
            }
            if (wrote) {
                for (i = 0; i < 4096u; ++i) {
                    if (wrote[i] < bmin) bmin = wrote[i];
                    if (wrote[i] > bmax) bmax = wrote[i];
                }
            }
            fprintf(stderr, "[encode] %ux%u -> 0x%08X   source spread %u..%u"
                    "   bytes written %u..%u%s\n",
                    width, height, efb->copy_dest, srcmin, srcmax, bmin, bmax,
                    (srcmax - srcmin < 8u && bmax - bmin > 64u)
                        ? "   <-- FLAT IN, NOISY OUT" : "");
        }
    }

    ++efb->tex_copies;
}

void mgs_efb_copy(MgsEfb* efb, GuestMemory* mem,
                  unsigned width, unsigned height, int to_xfb, int clear)
{
    unsigned x, line;

    ++efb->copies;

    /* A copy to a texture stays on the graphics side: it never touches the
     * external framebuffer, and writing guest memory for one would corrupt
     * whatever the game put there. */
    if (to_xfb && efb->copy_dest && mem) {
        if (height > MGS_EFB_HEIGHT) height = MGS_EFB_HEIGHT;
        if (width > MGS_EFB_WIDTH) width = MGS_EFB_WIDTH;
        if (!width || !height) return;
        /* Remembered so presentation reads the framebuffer at the size it
         * was written. A 512-wide buffer read as 640 wide skews every line
         * progressively, which looks like a torn image rather than a size
         * mistake. */
        efb->copy_width = width;
        efb->copy_height = height;

        for (line = 0; line < height; ++line) {
            const uint32_t* src = &efb->pixels[line * MGS_EFB_WIDTH];
            uint32_t addr = efb->copy_dest + line * efb->copy_stride;

            /* Two pixels share one chroma pair, which is what 4:2:2 means:
             * Y0 Cb Y1 Cr. Averaging the two chroma samples is what the
             * hardware's filter does, and taking only the first would tint
             * every vertical edge. */
            for (x = 0; x + 1u < width; x += 2u) {
                int y0, cb0, cr0, y1, cb1, cr1;
                uint8_t* p;

                rgb_to_ycbcr(src[x], &y0, &cb0, &cr0);
                rgb_to_ycbcr(src[x + 1u], &y1, &cb1, &cr1);

                p = guest_ptr(mem, addr + x * 2u, 4u);
                if (!p) break;                  /* off the end: stop, quietly */
                p[0] = clamp8(y0);
                p[1] = clamp8((cb0 + cb1) / 2);
                p[2] = clamp8(y1);
                p[3] = clamp8((cr0 + cr1) / 2);
            }
        }
    }

    if (clear) {
        unsigned i, n = MGS_EFB_WIDTH * MGS_EFB_HEIGHT;
        for (i = 0; i < n; ++i) efb->pixels[i] = efb->clear_argb;
        ++efb->clears;
    }
}

/* YCbCr back to RGB, the same standard in reverse. */
static uint32_t ycbcr_to_rgb(int y, int cb, int cr)
{
    int c = y - 16, d = cb - 128, e = cr - 128;
    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;
    return 0xFF000000u | ((uint32_t)clamp8(r) << 16) |
           ((uint32_t)clamp8(g) << 8) | clamp8(b);
}

int mgs_xfb_to_rgb(const GuestMemory* mem, uint32_t xfb_addr, uint32_t stride,
                   unsigned width, unsigned height, uint32_t* out)
{
    unsigned line, x;

    if (!mem || !out || !xfb_addr) return 0;
    if (!stride) stride = width * 2u;
    if (!guest_ptr(mem, xfb_addr, 4u)) return 0;

    for (line = 0; line < height; ++line) {
        const uint8_t* p = guest_ptr(mem, xfb_addr + line * stride, width * 2u);
        if (!p) return line != 0u;              /* partial is still a picture */

        for (x = 0; x + 1u < width; x += 2u) {
            int y0 = p[x * 2u + 0u], cb = p[x * 2u + 1u];
            int y1 = p[x * 2u + 2u], cr = p[x * 2u + 3u];
            out[line * width + x]      = ycbcr_to_rgb(y0, cb, cr);
            out[line * width + x + 1u] = ycbcr_to_rgb(y1, cb, cr);
        }
    }
    return 1;
}
