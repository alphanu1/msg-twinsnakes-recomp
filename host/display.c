/* From the game's command stream to the window.
 *
 * Two halves, and they are independent on the hardware too:
 *
 *   The GRAPHICS side executes the game's copy: when the pixel engine's copy
 *   command appears in the FIFO, the embedded framebuffer is converted to YUV
 *   4:2:2 and written to the address the game programmed. That is what
 *   `GXCopyDisp` does, and until it happens nothing in main memory has changed.
 *
 *   The VIDEO side scans whatever the video interface is pointed at, whenever
 *   it is pointed at it, regardless of who wrote it. So presentation reads
 *   VI's own register rather than remembering where the last copy went - the
 *   game double-buffers, and presenting the buffer it just drew into rather
 *   than the one it asked to be displayed shows a frame early, permanently.
 *
 * Nothing here draws geometry. The embedded framebuffer contains what GX
 * cleared it to, so that is what reaches the screen: a truthful picture of a
 * renderer that has its route to the display finished and its rasteriser not.
 */
#include "module.h"
#include "gx/efb.h"
#include "gx/raster.h"
#include "platform/mmio.h"
#include "platform/sdl_video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BP register 0x52, the copy command. */
#define COPY_CLEAR      (1u << 11)
#define COPY_TO_XFB     (1u << 14)

static MgsEfb s_efb;
static MgsGx  s_gx;
static MgsGxRaster s_raster;
static uint64_t s_presented;

MgsEfb* mgs_display_efb(void);
MgsEfb* mgs_display_efb(void) { return &s_efb; }

uint64_t mgs_display_frames(void);
uint64_t mgs_display_frames(void) { return s_presented; }

const MgsGx* mgs_display_gx(void);
const MgsGx* mgs_display_gx(void) { return &s_gx; }

const MgsGxRaster* mgs_display_raster(void);
const MgsGxRaster* mgs_display_raster(void) { return &s_raster; }

/* The command stream's destination. Bound to the MMIO layer so every byte the
 * game writes to the write-gather pipe is parsed rather than counted. */
static void fifo_sink(void* user, uint32_t value, unsigned size)
{
    (void)user;
    mgs_gx_write(&s_gx, value, size);
}

/* The run loop's interrupt flag, read from inside the rasteriser. */
static int raster_abandon(void) { return mgs_module_interrupted != 0; }

void mgs_display_init(GuestMemory* mem);
void mgs_display_init(GuestMemory* mem)
{
    mgs_efb_init(&s_efb);
    mgs_gx_init(&s_gx, mem);
    mgs_raster_init(&s_raster, &s_efb);
    /* So a run that is mid-draw can still be stopped and still report what it
     * drew. mgs_raster_triangle explains why the run loop's own check is not
     * enough. */
    s_raster.abandon = raster_abandon;
    s_gx.triangle = mgs_raster_triangle;
    s_gx.user = &s_raster;
    mgs_mmio_set_fifo_sink(mgs_host_mmio(), fifo_sink, NULL);
}

/* Run any copy the game has asked for since the last call. Cheap when there
 * is none, which is most of the time. */
void mgs_display_service(MgsMmio* mmio, GuestMemory* mem, unsigned height);
void mgs_display_service(MgsMmio* mmio, GuestMemory* mem, unsigned height)
{
    uint32_t cmd;

    (void)mmio;
    (void)height;

    /* Every value here comes from the PARSER's register state, not from a
     * scan of the byte stream. The stream cannot be read without knowing
     * where commands begin, and a destination address taken from a false
     * match writes 600 KB of framebuffer over whatever it points at. */
    while (mgs_gx_take_copy(&s_gx, &cmd)) {
        uint32_t ar = mgs_bp_get(&s_gx.bp, BP_COPY_CLEAR_AR);
        uint32_t gb = mgs_bp_get(&s_gx.bp, BP_COPY_CLEAR_GB);
        uint32_t dest = mgs_bp_get(&s_gx.bp, BP_EFB_ADDR);
        uint32_t stride = mgs_bp_get(&s_gx.bp, BP_COPY_STRIDE);

        /* The SOURCE RECTANGLE, which is what decides how much is written.
         * Using a fixed full-screen height instead writes 600 KB for a copy
         * the game asked to be 64 lines tall, over whatever follows the
         * destination. A render-to-texture pass is small and frequent, so
         * that is not a rare case. */
        uint32_t wh = mgs_bp_get(&s_gx.bp, BP_EFB_BOX_WH);
        unsigned copy_w = (wh & 0x3FFu) + 1u;
        unsigned copy_h = ((wh >> 10) & 0x3FFu) + 1u;

        uint32_t a = (ar >> 8) & 0xFFu;
        uint32_t r =  ar       & 0xFFu;
        uint32_t g = (gb >> 8) & 0xFFu;
        uint32_t b =  gb       & 0xFFu;

        mgs_efb_set_clear(&s_efb, (a << 24) | (r << 16) | (g << 8) | b);
        /* Addresses in the command stream are in 32-byte units. */
        mgs_efb_set_dest(&s_efb,
                         dest ? (0x80000000u | ((dest << 5) & 0x03FFFFFFu)) : 0u,
                         stride << 5);
        if (getenv("MGS_TRACE_GX"))
            fprintf(stderr, "[gx] copy cmd=0x%06X dest=0x%08X stride=%u "
                            "%ux%u xfb=%d clear=%d\n",
                    cmd, s_efb.copy_dest, s_efb.copy_stride, copy_w, copy_h,
                    (cmd & COPY_TO_XFB) != 0, (cmd & COPY_CLEAR) != 0);
        if (!getenv("MGS_NO_COPY"))
            mgs_efb_copy(&s_efb, mem, copy_w, copy_h,
                         (cmd & COPY_TO_XFB) != 0, (cmd & COPY_CLEAR) != 0);

        /* The depth buffer is cleared with the colour buffer. Leaving it
         * would have the next frame's geometry tested against the last
         * frame's depths, which hides whatever is behind where something
         * used to be. */
        if (cmd & COPY_CLEAR) mgs_raster_reset_depth(&s_raster);
    }
}

/* Write the last presented frame out as a portable pixmap.
 *
 * A headless run can then be LOOKED at, which matters more here than it
 * sounds: "12 million pixels written" says the rasteriser ran, not that the
 * image is right, and the difference between those two is most of the work
 * left. No encoder, no dependency - PPM is a header and the bytes. */
int mgs_display_save_ppm(const char* path, const GuestMemory* mem);
int mgs_display_save_ppm(const char* path, const GuestMemory* mem)
{
    static uint32_t buf[MGS_EFB_WIDTH * MGS_EFB_HEIGHT];
    unsigned w = s_efb.copy_width, h = s_efb.copy_height, i;
    uint32_t xfb = mgs_mmio_xfb_address(mgs_host_mmio());
    FILE* f;

    if (!w || !h || !xfb) return 0;
    if (!mgs_xfb_to_rgb(mem, xfb, s_efb.copy_stride, w, h, buf)) return 0;

    f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (i = 0; i < w * h; ++i) {
        uint8_t px[3];
        px[0] = (uint8_t)((buf[i] >> 16) & 0xFFu);
        px[1] = (uint8_t)((buf[i] >> 8) & 0xFFu);
        px[2] = (uint8_t)(buf[i] & 0xFFu);
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    return 1;
}

/* The draw-done token, from the parser rather than from a byte scan. */
int mgs_display_take_draw_done(void);
int mgs_display_take_draw_done(void) { return mgs_gx_take_draw_done(&s_gx); }

void mgs_display_put_draw_done(void);
void mgs_display_put_draw_done(void) { ++s_gx.draw_done_tokens; }

/* Present whatever the video interface is scanning. Returns 0 if there is
 * nothing to present, so the caller can leave the boot overlay up rather than
 * replace it with a black rectangle. */
int mgs_display_present(MgsMmio* mmio, const GuestMemory* mem);
int mgs_display_present(MgsMmio* mmio, const GuestMemory* mem)
{
    static uint32_t scratch[MGS_EFB_WIDTH * MGS_EFB_HEIGHT];
    uint32_t xfb = mgs_mmio_xfb_address(mmio);
    uint32_t* fb = mgs_video_framebuffer();
    unsigned w = s_efb.copy_width, h = s_efb.copy_height;
    unsigned y, x;

    if (!xfb || !fb || !w || !h) return 0;
    if (!mgs_xfb_to_rgb(mem, xfb, s_efb.copy_stride, w, h, scratch))
        return 0;

    /* The window's framebuffer is a fixed 640x480; the game's is whatever it
     * chose - 512x448 here. Nearest scaling keeps this honest about being a
     * stand-in for the real scaler, and keeps the aspect the game intended
     * rather than letterboxing to a size it never asked for. */
    for (y = 0; y < MGS_XFB_HEIGHT; ++y) {
        unsigned sy = y * h / MGS_XFB_HEIGHT;
        for (x = 0; x < MGS_XFB_WIDTH; ++x) {
            unsigned sx = x * w / MGS_XFB_WIDTH;
            fb[y * MGS_XFB_WIDTH + x] = scratch[sy * w + sx];
        }
    }

    ++s_presented;
    return 1;
}
