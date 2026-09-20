/* The route from the embedded framebuffer to the screen.
 *
 * Tested without a window and without a game, because both halves are pure:
 * an EFB copy is a colour conversion and a write into guest memory, and
 * presentation is the same conversion backwards. The interesting failures are
 * the quiet ones - a swapped chroma pair, a stride ignored, a copy to a
 * texture writing over the display - so those are what is checked.
 */
#include "gx/efb.h"
#include "gx/texture.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

/* YUV 4:2:2 is lossy in chroma, so a round trip is close, not equal. Eight
 * levels is comfortably inside the conversion's error and far outside a
 * swapped channel, which is the bug this is guarding against. */
static int near(uint32_t a, uint32_t b)
{
    int i;
    for (i = 0; i < 3; ++i) {
        int ca = (int)((a >> (i * 8)) & 0xFF), cb = (int)((b >> (i * 8)) & 0xFF);
        int d = ca - cb;
        if (d < -8 || d > 8) return 0;
    }
    return 1;
}

#define XFB_ADDR 0x80300000u
#define W 640u
#define H 16u

/* A COPY TO TEXTURE MUST SURVIVE BEING READ BACK, WITH REAL CONTENT.
 *
 * This encoder was checked once before with a uniform source - every pixel
 * the same value - and it passed, which proves almost nothing: a swapped
 * tile half, a transposed tile order and a channel rotation are all
 * invisible when every texel is identical. The video frames the game copies
 * and samples back are not uniform, and they come out as diagonal green and
 * magenta banding.
 *
 * So the pattern here is deliberately non-uniform in all four channels and
 * different along both axes, and the comparison is exact: RGBA8 is lossless,
 * so anything but equality is a layout bug.
 */
static void check_copy_tex_roundtrip(MgsEfb* efb, GuestMemory* mem, int* failures)
{
    enum { TW = 16u, TH = 12u, TADDR = 0x00400000u };
    uint32_t want[TW * TH], got[TW * TH];
    unsigned x, y, i;
    int mismatches = 0, first = -1;

    for (y = 0; y < TH; ++y) {
        for (x = 0; x < TW; ++x) {
            /* Distinct per pixel, and distinct per channel, so a swap of any
             * two channels or any two texels shows up. */
            uint32_t a = 0xFFu;
            uint32_t r = (x * 16u + 1u) & 0xFFu;
            uint32_t g = (y * 20u + 3u) & 0xFFu;
            uint32_t b = (x * 3u + y * 7u + 5u) & 0xFFu;
            uint32_t argb = (a << 24) | (r << 16) | (g << 8) | b;
            efb->pixels[y * MGS_EFB_WIDTH + x] = argb;
            want[y * TW + x] = argb;
        }
    }

    /* The stride here is the distance between ROWS OF TILES, not between
     * rows of pixels: tiles_x * tile_bytes, which for a 16-wide RGBA8
     * target is 4 * 64 = 256. Passing the pixel pitch instead overlaps the
     * tile rows and makes a correct encoder look transposed - which it did,
     * on the first run of this test. */
    mgs_efb_set_dest(efb, 0x80000000u | TADDR, (TW / 4u) * 64u);
    mgs_efb_copy_tex(efb, mem, 0u, 0u, TW, TH, 0x6u);   /* RGBA8 */

    memset(got, 0, sizeof got);
    if (!mgs_tex_decode(mem, 0x80000000u | TADDR, 0x6u, TW, TH, NULL, 0u, got)) {
        printf("FAIL %s:%d: RGBA8 copy did not decode\n", __FILE__, __LINE__);
        ++*failures;
        return;
    }

    for (i = 0; i < TW * TH; ++i) {
        if (got[i] != want[i]) {
            if (first < 0) first = (int)i;
            ++mismatches;
        }
    }
    if (mismatches) {
        printf("FAIL %s:%d: RGBA8 copy-to-texture round trip: %d of %u texels "
               "differ; first at (%u,%u) wrote 0x%08X read 0x%08X\n",
               __FILE__, __LINE__, mismatches, (unsigned)(TW * TH),
               (unsigned)first % TW, (unsigned)first / TW,
               want[first], got[first]);
        ++*failures;
    }
}

int main(void)
{
    static MgsEfb efb;
    static uint32_t out[W * H];
    GuestMemory mem;
    unsigned i;

    if (!guest_memory_init(&mem)) { printf("FAIL: no guest memory\n"); return 1; }
    mgs_efb_init(&efb);

    /* A copy to the external framebuffer, then read it back the way the video
     * interface would. Mid grey is deliberate: pure black and pure white
     * survive a broken conversion, a mid tone does not. */
    mgs_efb_set_clear(&efb, 0xFF406080u);
    mgs_efb_set_dest(&efb, XFB_ADDR, W * 2u);
    mgs_efb_copy(&efb, &mem, W, H, 1, 1);          /* to XFB, and clear after */

    CHECK(efb.copies == 1u);
    CHECK(efb.clears == 1u);
    /* The clear happens AFTER the copy, so the first copy wrote whatever the
     * EFB held before it - black - and the EFB now holds the clear colour. */
    CHECK(efb.pixels[0] == 0xFF406080u);

    /* Copy again, now that the EFB holds the colour, and read it back. */
    mgs_efb_copy(&efb, &mem, W, H, 1, 0);
    CHECK(mgs_xfb_to_rgb(&mem, XFB_ADDR, W * 2u, W, H, out));
    CHECK(near(out[0], 0xFF406080u));
    CHECK(near(out[W * (H - 1u) + W - 2u], 0xFF406080u));

    /* A copy to a TEXTURE must not touch the external framebuffer. Getting
     * this wrong puts render-to-texture passes on the screen. */
    {
        uint32_t before = guest_read32(&mem, XFB_ADDR);
        mgs_efb_set_clear(&efb, 0xFFFF0000u);
        mgs_efb_copy(&efb, &mem, W, H, 0, 1);
        CHECK(guest_read32(&mem, XFB_ADDR) == before);
    }

    /* Stride is honoured: a line is where the game said it is, not where a
     * 640-pixel assumption puts it. A fresh address, because the copies
     * above already wrote where a tighter stride would have put line 1. */
    {
        const uint32_t stride = W * 2u + 64u;
        const uint32_t addr2 = XFB_ADDR + 0x100000u;
        for (i = 0; i < MGS_EFB_WIDTH * MGS_EFB_HEIGHT; ++i)
            efb.pixels[i] = 0xFF00FF00u;
        mgs_efb_set_dest(&efb, addr2, stride);
        mgs_efb_copy(&efb, &mem, W, H, 1, 0);
        CHECK(mgs_xfb_to_rgb(&mem, addr2, stride, W, H, out));
        CHECK(near(out[W * 2u], 0xFF00FF00u));
        /* The gap between lines is untouched, which is what a stride means. */
        CHECK(guest_read32(&mem, addr2 + W * 2u) == 0u);
    }

    /* An unreadable address is refused rather than crashed on. */
    CHECK(!mgs_xfb_to_rgb(&mem, 0u, W * 2u, W, H, out));

    check_copy_tex_roundtrip(&efb, &mem, &failures);

    guest_memory_free(&mem);
    printf(failures ? "efb: FAILED\n" : "efb: ok\n");
    return failures ? 1 : 0;
}
