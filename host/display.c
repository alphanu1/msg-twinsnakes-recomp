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

void mgs_display_init(GuestMemory* mem);
void mgs_display_init(GuestMemory* mem)
{
    mgs_efb_init(&s_efb);
    mgs_gx_init(&s_gx, mem);
    mgs_raster_init(&s_raster, &s_efb);
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

    while (mgs_mmio_take_copy(mmio, &cmd)) {
        /* The clear colour arrives as two registers of two bytes: alpha and
         * red, then green and blue. */
        uint32_t a = (mmio->bp_clear_ar >> 8) & 0xFFu;
        uint32_t r =  mmio->bp_clear_ar       & 0xFFu;
        uint32_t g = (mmio->bp_clear_gb >> 8) & 0xFFu;
        uint32_t b =  mmio->bp_clear_gb       & 0xFFu;

        mgs_efb_set_clear(&s_efb, (a << 24) | (r << 16) | (g << 8) | b);
        mgs_efb_set_dest(&s_efb, mmio->bp_copy_dest
                                 ? (0x80000000u | (mmio->bp_copy_dest & 0x03FFFFFFu))
                                 : 0u,
                         mmio->bp_copy_stride);
        mgs_efb_copy(&s_efb, mem, height,
                     (cmd & COPY_TO_XFB) != 0, (cmd & COPY_CLEAR) != 0);

        /* The depth buffer is cleared with the colour buffer. Leaving it
         * would have the next frame's geometry tested against the last
         * frame's depths, which hides whatever is behind where something
         * used to be. */
        if (cmd & COPY_CLEAR) mgs_raster_reset_depth(&s_raster);
    }
}

/* Present whatever the video interface is scanning. Returns 0 if there is
 * nothing to present, so the caller can leave the boot overlay up rather than
 * replace it with a black rectangle. */
int mgs_display_present(MgsMmio* mmio, const GuestMemory* mem);
int mgs_display_present(MgsMmio* mmio, const GuestMemory* mem)
{
    uint32_t xfb = mgs_mmio_xfb_address(mmio);
    uint32_t* fb = mgs_video_framebuffer();

    if (!xfb || !fb) return 0;
    if (!mgs_xfb_to_rgb(mem, xfb, s_efb.copy_stride,
                        MGS_XFB_WIDTH, MGS_XFB_HEIGHT, fb))
        return 0;

    ++s_presented;
    return 1;
}
