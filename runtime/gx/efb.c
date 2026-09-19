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

void mgs_efb_copy(MgsEfb* efb, GuestMemory* mem, unsigned height,
                  int to_xfb, int clear)
{
    unsigned x, line;

    ++efb->copies;

    /* A copy to a texture stays on the graphics side: it never touches the
     * external framebuffer, and writing guest memory for one would corrupt
     * whatever the game put there. */
    if (to_xfb && efb->copy_dest && mem) {
        if (height > MGS_EFB_HEIGHT) height = MGS_EFB_HEIGHT;

        for (line = 0; line < height; ++line) {
            const uint32_t* src = &efb->pixels[line * MGS_EFB_WIDTH];
            uint32_t addr = efb->copy_dest + line * efb->copy_stride;

            /* Two pixels share one chroma pair, which is what 4:2:2 means:
             * Y0 Cb Y1 Cr. Averaging the two chroma samples is what the
             * hardware's filter does, and taking only the first would tint
             * every vertical edge. */
            for (x = 0; x + 1u < MGS_EFB_WIDTH; x += 2u) {
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
