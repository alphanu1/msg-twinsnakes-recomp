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

#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* BP register 0x52, the copy command. */
#define COPY_CLEAR      (1u << 11)
#define COPY_TO_XFB     (1u << 14)

/* THE COPY'S PIXEL FORMAT IS BITS 3-6, AND IT IS NOT THE TEXTURE FORMAT.
 *
 * The field the hardware reads is four bits at 3, and the texture format it
 * names is that value rotated: the low bit becomes the high one. Dolphin
 * spells it `target_pixel_format / 2 + (target_pixel_format & 1) * 8`, and
 * the hardware's behaviour is the reference here (rule 12).
 *
 * What was here read bits 4-7 and did no rotation. The two agree only when
 * bit 3 and bit 7 happen to be equal, which they are for the copies this
 * game has been observed to make - so it was not WRONG on anything measured,
 * and would have become wrong silently on the first copy that differed. The
 * trace beside it already printed bits 3-6, so the code disagreed with its
 * own diagnostic. */
static unsigned copy_tex_format(uint32_t cmd)
{
    unsigned tpf = (cmd >> 3) & 0xFu;
    return (tpf >> 1) | ((tpf & 1u) << 3);
}

static MgsEfb s_efb;
static MgsGx  s_gx;
static MgsGxRaster s_raster;

/* Frame pacing. 0 means uncapped, which is what a headless run wants. */
static unsigned  s_fps_cap;
static long long s_next_frame_ns;
static uint64_t s_presented;

MgsEfb* mgs_display_efb(void);
MgsEfb* mgs_display_efb(void) { return &s_efb; }

uint64_t mgs_display_frames(void);
uint64_t mgs_display_frames(void) { return s_presented; }

const MgsGx* mgs_display_gx(void);
const MgsGx* mgs_display_gx(void) { return &s_gx; }

const MgsGxRaster* mgs_display_raster(void);
const MgsGxRaster* mgs_display_raster(void) { return &s_raster; }

/* The pool is owned by main, the rasteriser lives here. */
/* REMEMBERED, NOT APPLIED IMMEDIATELY.
 *
 * main creates the pool during start-up but the display - and with it
 * mgs_raster_init - is not brought up until the guest first configures the
 * video interface, and that init clears the pointer. Handing the pool
 * straight to the rasteriser therefore set a field that was zeroed again
 * before the first triangle, and the split across cores silently never
 * happened: measurably, 13.5 Mpx/s either way. */
static void* s_jobs;

void mgs_display_set_jobs(void* pool);
void mgs_display_set_fps_cap(unsigned fps);
void mgs_display_set_fps_cap(unsigned fps)
{
    const char* e = getenv("MGS_FPS_CAP");
    if (e && *e) fps = (unsigned)strtoul(e, NULL, 10);
    s_fps_cap = fps;
    s_next_frame_ns = 0ll;
    if (fps) fprintf(stderr, "[video] frame rate capped at %u fps "
                             "(MGS_FPS_CAP=0 to disable)\n", fps);
}

void mgs_display_set_jobs(void* pool)
{
    s_jobs = pool;
    mgs_raster_set_jobs(&s_raster, pool);
}

/* The command stream's destination. Bound to the MMIO layer so every byte the
 * game writes to the write-gather pipe is parsed rather than counted. */
static GuestMemory* s_mem;
static uint64_t s_recorded;

uint64_t mgs_display_recorded(void);
uint64_t mgs_display_recorded(void) { return s_recorded; }

static void copy_exec_cb(void* user, uint32_t cmd);

static void fifo_sink(void* user, uint32_t value, unsigned size)
{
    MgsMmio* mmio = mgs_host_mmio();
    unsigned i;

    (void)user;

    /* RECORDING A DISPLAY LIST IS NOT DRAWING.
     *
     * `GXBeginDisplayList` points the CPU-side FIFO at a buffer in main
     * memory and leaves the graphics processor's alone. The game keeps
     * storing to the same address - 0xCC008000, the write-gather pipe - but
     * the data is being written down, not executed.
     *
     * Sending those bytes to the parser executes the recording, under
     * whatever vertex descriptor is live at record time rather than the one
     * the list will be called under. That is exactly what was happening: the
     * engine records its sphere-map geometry at 24 bytes a vertex while the
     * live descriptor still described the boot logo's 20, so the parser
     * consumed four bytes too few per vertex and lost the stream on the
     * first primitive after the logo.
     *
     * So the bytes go where the hardware would put them, and the parser
     * never sees them until `GXCallDisplayList` plays the buffer back. */
    if (s_mem && mgs_mmio_recording(mmio)) {
        uint32_t wp  = mgs_mmio_cpu_fifo_wrptr(mmio);
        uint32_t end = mgs_mmio_cpu_fifo_end(mmio);
        uint32_t base = mgs_mmio_cpu_fifo_base(mmio);

        for (i = 0; i < size; ++i) {
            guest_write8(s_mem, wp,
                         (uint8_t)(value >> (8u * (size - 1u - i))));
            ++wp;
            /* The FIFO is a ring. A list that overruns its buffer is the
             * game's business, not ours, and wrapping is what the hardware
             * does. */
            if (end && wp >= end) wp = base;
        }
        mgs_mmio_set_cpu_fifo_wrptr(mmio, wp);
        s_recorded += size;
        return;
    }

    /* DRAWING LEAVES THE WRITE POINTER ALONE, and that is deliberate.
     *
     * On hardware both pointers move: the pipe advances the write pointer
     * and the graphics processor advances the read pointer as it consumes.
     * The SDK watches the distance between them to know how full the FIFO
     * is. This host has no asynchronous graphics processor - a command is
     * executed by the parser the moment it is written - so there is no read
     * pointer to advance. Moving the write pointer alone describes a FIFO
     * that only ever fills, and the SDK duly stops issuing: it took the
     * boot from 55 frames and 8,158 commands to 2 and 578.
     *
     * Recording is the opposite case and does advance it, because there the
     * absence of a consumer is the truth rather than an artefact - nothing
     * drains a display-list buffer, and `GXEndDisplayList` needs the pointer
     * to have moved to know how much was recorded. */
    mgs_gx_write(&s_gx, value, size);
}

/* The run loop's interrupt flag, read from inside the rasteriser. */
static int raster_abandon(void) { return mgs_module_interrupted != 0; }

void mgs_display_init(GuestMemory* mem);
void mgs_display_init(GuestMemory* mem)
{
    s_mem = mem;
    /* Copies run inside the parse, in stream order. */
    s_gx.copy_exec = copy_exec_cb;
    s_gx.copy_user = NULL;
    mgs_efb_init(&s_efb);
    mgs_gx_init(&s_gx, mem);
    mgs_raster_init(&s_raster, &s_efb);
    mgs_raster_set_jobs(&s_raster, s_jobs);
    /* So a run that is mid-draw can still be stopped and still report what it
     * drew. mgs_raster_triangle explains why the run loop's own check is not
     * enough. */
    s_raster.abandon = raster_abandon;
    s_gx.abandon = raster_abandon;
    s_gx.triangle = mgs_raster_triangle;
    s_gx.user = &s_raster;
    mgs_mmio_set_fifo_sink(mgs_host_mmio(), fifo_sink, NULL);
}

/* The fullest frame seen so far, and where to write it. See MGS_SAVE_BEST. */
static const char* s_best_path;
static unsigned s_best_lit, s_best_w, s_best_h;

/* The embedded framebuffer, straight out, with no YUV round trip. */
static const char* s_seq_prefix;
static int s_seq_init;
static unsigned s_seq_every = 400u;
static unsigned s_seq_from;

static int save_efb_ppm(const char* path, unsigned w, unsigned h)
{
    FILE* f = fopen(path, "wb");
    unsigned y, x;
    if (!f) return 0;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            uint32_t v = s_efb.pixels[y * MGS_EFB_WIDTH + x];
            uint8_t px[3];
            px[0] = (uint8_t)((v >> 16) & 0xFFu);
            px[1] = (uint8_t)((v >> 8) & 0xFFu);
            px[2] = (uint8_t)(v & 0xFFu);
            fwrite(px, 1, 3, f);
        }
    fclose(f);
    return 1;
}

unsigned mgs_display_best_lit(void);
unsigned mgs_display_best_lit(void) { return s_best_lit; }
unsigned mgs_display_best_w(void);
unsigned mgs_display_best_w(void) { return s_best_w; }
unsigned mgs_display_best_h(void);
unsigned mgs_display_best_h(void) { return s_best_h; }
void mgs_display_set_best_path(const char* p);
void mgs_display_set_best_path(const char* p) { s_best_path = p; }

/* RUN ONE COPY, WHERE THE COMMAND SITS IN THE STREAM.
 *
 * This used to be deferred: the parser stored the command in a single slot
 * and the run loop's display hook ran it later. Two things follow from that,
 * and both were happening.
 *
 * A second copy issued before the hook ran REPLACED the first, which then
 * never happened - 2,464 of 9,350 copies in one run, 21% of them, silently.
 *
 * And the copies that did run were reordered against the draws. Drawing is
 * synchronous here ("a command is executed by the parser the moment it is
 * written", above), so deferring only the copies puts every draw in a frame
 * before every copy in it. The movie's frame is drawn, copied to a texture,
 * drawn again sampling that texture, and copied to the framebuffer - and
 * the texture and the framebuffer are THE SAME BUFFER, which the game is
 * entitled to do because on hardware the texture is finished with before the
 * framebuffer copy overwrites it. Flattened, the framebuffer's YUV 4:2:2
 * landed on the texture before the draw that samples it, so the draw read
 * luma and chroma bytes as RGBA8 texels: noise, fed back through the next
 * copy, into the next frame.
 *
 * Every value here comes from the PARSER's register state, not from a scan
 * of the byte stream. The stream cannot be read without knowing where
 * commands begin, and a destination address taken from a false match writes
 * 600 KB of framebuffer over whatever it points at. */
static void run_copy(uint32_t cmd)
{
    {
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
        {   /* EVERY copy, in the order the parser reads them, with the
             * one bit that says which kind it is. Copies run inside the
             * parse now, so this sequence is the real one - which is what
             * makes it worth printing at all. */
            static int on = -1; static unsigned n, armed;
            if (on < 0) on = getenv("MGS_TRACE_COPYSEQ") != NULL;
            ++n;
            /* Arm on the first LARGE texture copy - the one whose
             * destination is a framebuffer - and print the window around
             * it. The small caption copies run thousands of times and would
             * bury it. */
            if (on && !armed && !(cmd & COPY_TO_XFB) && copy_w > 256u)
                armed = n;
            {   /* MGS_TRACE_BUF=<addr>: this copy, on the same timeline as
                 * the decodes, so "written correctly" and "decoded as noise"
                 * can be put in order against each other. */
                static long watch = -1;
                uint32_t d = dest ? (0x80000000u | ((dest << 5) & 0x03FFFFFFu))
                                  : 0u;
                if (watch == -1) { const char* e = getenv("MGS_TRACE_BUF");
                                   watch = e ? (long)strtoul(e, NULL, 0) : 0; }
                if (watch && (uint32_t)watch == d)
                    fprintf(stderr, "[buf] %6llu  COPY   0x%08X %ux%u "
                            "fmt 0x%X  %s\n",
                            (unsigned long long)++mgs_gx_seq, d,
                            copy_w, copy_h, copy_tex_format(cmd),
                            (cmd & COPY_TO_XFB) ? "-> framebuffer (YUV)"
                                                : "-> texture (tiled)");
            }
            if (armed && n >= armed && n < armed + 40u) {
                fprintf(stderr, "[copyseq] %2u  %-3s dest 0x%08X  %ux%u  "
                        "fmt 0x%X  stride %u  dest set %u since last copy\n",
                        n,
                        (cmd & COPY_TO_XFB) ? "XFB" : "tex",
                        dest ? (0x80000000u | ((dest << 5) & 0x03FFFFFFu)) : 0u,
                        copy_w, copy_h, copy_tex_format(cmd),
                        stride << 5, s_gx.bp.efb_addr_writes);
            }
            s_gx.bp.efb_addr_writes = 0;
        }
        if (getenv("MGS_TRACE_GX"))
            fprintf(stderr, "[gx] copy cmd=0x%06X dest=0x%08X stride=%u "
                            "%ux%u xfb=%d clear=%d fmt=0x%X\n",
                    cmd, s_efb.copy_dest, s_efb.copy_stride, copy_w, copy_h,
                    (cmd & COPY_TO_XFB) != 0, (cmd & COPY_CLEAR) != 0,
                    copy_tex_format(cmd));
        /* KEEP THE BEST FRAME THE RUN EVER PRODUCES, not whatever happens
         * to be in the buffer when the step limit hits.
         *
         * The EFB at exit measured 100% black while 1,167,715 lit pixels had
         * been written during the run - so the last frame is not the most
         * informative one, and "is anything being drawn at all" cannot be
         * answered from it. This samples every copy to the external buffer,
         * counts what is lit, and keeps the fullest. It runs only when
         * MGS_SAVE_BEST names a file. */
        /* MGS_SAVE_SEQ=<prefix> writes every Nth frame to prefix_NNNN.ppm
         * (N from MGS_SAVE_EVERY, default 400). Watching a sequence is the
         * only way to see a fault that appears partway through a movie and
         * then stops: one frame cannot show a progression. */
        if (!s_seq_init) { s_seq_init = 1;
            s_seq_prefix = getenv("MGS_SAVE_SEQ");
            { const char* e = getenv("MGS_SAVE_EVERY");
              if (e && *e) s_seq_every = (unsigned)strtoul(e, NULL, 0);
              if (!s_seq_every) s_seq_every = 1u; }
            { const char* e = getenv("MGS_SAVE_FROM_COPY");
              if (e && *e) s_seq_from = (unsigned)strtoul(e, NULL, 0); } }
        /* WHERE WE WRITE versus WHERE THE SCREEN READS.
         *
         * The copy takes its destination from the blitter's address register;
         * presentation takes its source from the video interface's field base.
         * With two framebuffers alternating, any disagreement means we fill
         * one and show the other - which flashes between a good frame and a
         * never-written one rather than being steadily wrong. */
        /* A LIVE LINE ABOUT THE PICTURE, in the ordinary run.
         *
         * Roughness - the mean difference between neighbouring pixels -
         * separates artwork from noise by an order of magnitude: a drawn
         * frame scores a few, uncorrelated pixels score tens. Printed every
         * 120 frames, and immediately whenever it crosses between the two,
         * so a run in a terminal says when the picture broke and what it was
         * sampling at the time rather than needing a special build.
         */
        /* HOLD THE GUEST TO A FRAME RATE.
         *
         * The rasteriser running on every core removed the thing that had
         * been pacing the game by accident: it was slow, so it ran at about
         * the right speed. With that gone the intro logos play far too fast,
         * because nothing in the runtime ever limited how quickly the guest
         * could finish a frame.
         *
         * An XFB copy is one finished game frame - that is what the guest
         * does when it has drawn everything and wants it shown - so it is
         * the honest place to wait, and waiting here throttles the guest
         * itself rather than just the presentation.
         *
         * OFF BY DEFAULT IN HEADLESS RUNS. Sleeping on the host clock makes
         * a run unreproducible, and reproducibility is what the headless
         * path exists for; main turns this on only for a window. The
         * deadline is carried forward rather than reset each frame, so an
         * occasional slow frame is absorbed instead of accumulating drift,
         * and a frame that overruns by more than one period resynchronises
         * rather than trying to catch up forever. */
        if ((cmd & COPY_TO_XFB) && s_fps_cap) {
            struct timespec now;
            long long period = 1000000000ll / (long long)s_fps_cap;
            clock_gettime(CLOCK_MONOTONIC, &now);
            {
                long long t = (long long)now.tv_sec * 1000000000ll + now.tv_nsec;
                if (s_next_frame_ns == 0ll || t > s_next_frame_ns + period)
                    s_next_frame_ns = t + period;          /* first frame, or resync */
                else {
                    long long wait = s_next_frame_ns - t;
                    if (wait > 0ll) {
                        struct timespec ts;
                        ts.tv_sec  = (time_t)(wait / 1000000000ll);
                        ts.tv_nsec = (long)(wait % 1000000000ll);
                        nanosleep(&ts, NULL);
                    }
                    s_next_frame_ns += period;
                }
            }
        }

        if (cmd & COPY_TO_XFB) {
            static unsigned vn; static int was_noisy = -1;
            unsigned yy, cnt = 0u, rough = 0u, lit = 0u;
            /* The channels separately. "Roughness 16, ok" said nothing about
             * the picture being purple, and a frame number is what turns a
             * colour fault into a first occurrence. */
            unsigned long sum_r = 0, sum_g = 0, sum_b = 0;
            for (yy = 0; yy < copy_h && yy < MGS_EFB_HEIGHT; yy += 8u) {
                unsigned xx;
                for (xx = 1u; xx < copy_w && xx < MGS_EFB_WIDTH; xx += 4u) {
                    uint32_t a = s_efb.pixels[yy * MGS_EFB_WIDTH + xx - 1u];
                    uint32_t b = s_efb.pixels[yy * MGS_EFB_WIDTH + xx];
                    int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
                    int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
                    rough += (unsigned)(va > vb ? va - vb : vb - va);
                    if (b & 0x00FFFFFFu) ++lit;
                    sum_r += (b >> 16) & 0xFFu;
                    sum_g += (b >> 8) & 0xFFu;
                    sum_b += b & 0xFFu;
                    ++cnt;
                }
            }
            {
                unsigned r = cnt ? rough / cnt : 0u;
                int noisy = r > 20u;
                if (noisy != was_noisy || (vn % 120u) == 0u) {
                    const MgsGxRaster* rr = &s_raster;
                    unsigned i2 = (rr->drawlog_at - 1u) & 63u;
                    printf("[video] frame %5u  %ux%u  roughness %3u %-5s  "
                           "rgb %3u,%3u,%3u  lit %3u%%  xfb 0x%08X",
                           vn, copy_w, copy_h, r, noisy ? "NOISE" : "ok",
                           cnt ? (unsigned)(sum_r / cnt) : 0u,
                           cnt ? (unsigned)(sum_g / cnt) : 0u,
                           cnt ? (unsigned)(sum_b / cnt) : 0u,
                           cnt ? lit * 100u / cnt : 0u,
                           mgs_mmio_xfb_address(mgs_host_mmio()));
                    if (rr->drawlog_w[i2])
                        printf("  last texture %ux%u fmt 0x%X @0x%08X r%u",
                               rr->drawlog_w[i2], rr->drawlog_h[i2],
                               rr->drawlog_fmt[i2], rr->drawlog_addr[i2],
                               rr->drawlog_rough[i2]);
                    printf("\n");
                    fflush(stdout);
                    was_noisy = noisy;
                }
                ++vn;
            }
        }

        /* WHEN DOES THE BUFFER TURN? Once per frame, cheap, and on the
         * first crossing dump the draws that led to it. Everything measured
         * so far sits on one side of this transition or the other. */
        if ((cmd & COPY_TO_XFB) && getenv("MGS_FIND_TURN")) {
            static int turned;
            unsigned yy, cnt = 0u, rough = 0u;
            for (yy = 0; yy < copy_h && yy < MGS_EFB_HEIGHT; yy += 8u) {
                unsigned xx;
                for (xx = 1u; xx < copy_w && xx < MGS_EFB_WIDTH; xx += 4u) {
                    uint32_t a = s_efb.pixels[yy * MGS_EFB_WIDTH + xx - 1u];
                    uint32_t b = s_efb.pixels[yy * MGS_EFB_WIDTH + xx];
                    int va = (int)(((a >> 16) & 0xFF) + ((a >> 8) & 0xFF) + (a & 0xFF)) / 3;
                    int vb = (int)(((b >> 16) & 0xFF) + ((b >> 8) & 0xFF) + (b & 0xFF)) / 3;
                    rough += (unsigned)(va > vb ? va - vb : vb - va);
                    ++cnt;
                }
            }
            if (!turned && cnt && rough / cnt > 20u) {
                const MgsGxRaster* rr = &s_raster;
                unsigned k;
                turned = 1;
                fprintf(stderr, "[turn] buffer went noisy: roughness %u, "
                        "frame %llu\n", rough / cnt,
                        (unsigned long long)s_efb.copies);
                fprintf(stderr, "[turn] the last large textures sampled, "
                        "oldest first:\n");
                fprintf(stderr, "[turn]   (untextured draws are not listed; "
                        "an empty list means nothing sampled was noisy)\n");
                for (k = 64u; k > 0u; --k) {
                    unsigned i2 = (rr->drawlog_at - k) & 63u;
                    if (!rr->drawlog_w[i2]) continue;
                    fprintf(stderr, "[turn]   %ux%u fmt 0x%X at 0x%08X "
                            "roughness %u%s\n",
                            rr->drawlog_w[i2], rr->drawlog_h[i2],
                            rr->drawlog_fmt[i2], rr->drawlog_addr[i2],
                            rr->drawlog_rough[i2],
                            rr->drawlog_rough[i2] > 20u ? "  <-- noise" : "");
                }
            }
        }

        if ((cmd & COPY_TO_XFB) && getenv("MGS_TRACE_XFBPAIR")) {
            static unsigned n;
            if (n++ < 24u) {
                uint32_t tl0 = mgs_bp_get(&s_gx.bp, BP_EFB_BOX_TL);
                fprintf(stderr, "[xfb] source box top-left (%u,%u) %ux%u\n",
                        tl0 & 0x3FFu, (tl0 >> 10) & 0x3FFu, copy_w, copy_h);
                uint32_t shown = mgs_mmio_xfb_address(mgs_host_mmio());
                fprintf(stderr, "[xfb] copy -> 0x%08X   VI shows 0x%08X   %s\n",
                        s_efb.copy_dest, shown,
                        s_efb.copy_dest == shown ? "same" : "DIFFERENT");
            }
        }

        if ((cmd & COPY_TO_XFB) && s_seq_prefix) {
            static unsigned seq_n, seq_i;
            unsigned idx = seq_n++;
            if (idx >= s_seq_from && ((idx - s_seq_from) % s_seq_every) == 0u
                && seq_i < 240u) {
                char path[512];
                snprintf(path, sizeof path, "%s_%04u.ppm", s_seq_prefix, seq_i++);
                save_efb_ppm(path, copy_w, copy_h);
            }
        }

        if ((cmd & COPY_TO_XFB) && s_best_path) {
            unsigned lit = 0u, yy, xx;
            for (yy = 0; yy < copy_h && yy < MGS_EFB_HEIGHT; ++yy)
                for (xx = 0; xx < copy_w && xx < MGS_EFB_WIDTH; ++xx)
                    if (s_efb.pixels[yy * MGS_EFB_WIDTH + xx] & 0x00FFFFFFu)
                        ++lit;
            if (lit > s_best_lit) {
                s_best_lit = lit;
                s_best_w = copy_w; s_best_h = copy_h;
                save_efb_ppm(s_best_path, copy_w, copy_h);
            }
        }

        /* MGS_NO_RASTER skips triangle rasterisation, to measure its share of
     * the wall clock. Everything else still runs: the guest executes, the
     * FIFO parses, copies happen. */
    if (!getenv("MGS_NO_COPY")) {
            /* A copy to TEXTURE is not a copy to the screen, and both arrive
             * through this one command. The source rectangle's top-left
             * matters here in a way it does not for the framebuffer: a
             * render-to-texture pass reads a small box out of the embedded
             * buffer, often the scratch strip to the right of the visible
             * area, not the origin. */
            if (cmd & COPY_TO_XFB) {
                mgs_efb_copy(&s_efb, s_mem, copy_w, copy_h, 1,
                             (cmd & COPY_CLEAR) != 0);
            } else if (!getenv("MGS_NO_RTT")) {
                /* A TEXTURE COPY, BUT NOT OVER THE SCREEN.
                 *
                 * These arrive in two sizes and only one was designed for.
                 * The small ones - 64x64, 21,041 a movie - are the caption
                 * compositor, and writing them is what made captions appear.
                 * The others are 512x448 with the FRAMEBUFFER as their
                 * destination, and encoding those as tiled texels deposits
                 * 458 KB of scrambled data over the picture: the video
                 * rectangle turns to noise while the letterbox around it
                 * stays clean, which is exactly what was reported.
                 *
                 * What such a copy is FOR is not yet established, so it is
                 * left alone rather than guessed at. Doing nothing is what
                 * the code did before render-to-texture existed, and the
                 * screen was better for it. */
                uint32_t tl = mgs_bp_get(&s_gx.bp, BP_EFB_BOX_TL);
                mgs_efb_copy_tex(&s_efb, s_mem, tl & 0x3FFu,
                                 (tl >> 10) & 0x3FFu,
                                 copy_w, copy_h, copy_tex_format(cmd));
                if (cmd & COPY_CLEAR)
                    mgs_efb_copy(&s_efb, s_mem, copy_w, copy_h, 0, 1);
            }
        }

        /* The depth buffer is cleared with the colour buffer. Leaving it
         * would have the next frame's geometry tested against the last
         * frame's depths, which hides whatever is behind where something
         * used to be. */
        if (cmd & COPY_CLEAR) mgs_raster_reset_depth(&s_raster);
    }
}

/* The parser calls this the instant it reads the copy command. */
static void copy_exec_cb(void* user, uint32_t cmd)
{
    (void)user;
    run_copy(cmd);
}

/* Drain anything still pending. With the callback installed nothing should
 * be, but a copy issued before display init would otherwise sit there
 * forever, and the loop costs one predictable branch. */
void mgs_display_service(MgsMmio* mmio, GuestMemory* mem, unsigned height);
void mgs_display_service(MgsMmio* mmio, GuestMemory* mem, unsigned height)
{
    uint32_t cmd;

    (void)mmio;
    (void)mem;
    (void)height;

    while (mgs_gx_take_copy(&s_gx, &cmd)) run_copy(cmd);
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

    /* MGS_SAVE_FROM picks which buffer to read.
     *
     * "copy" reads the last EFB copy destination rather than what the video
     * interface is scanning, and the two are NOT the same address: the game
     * double-buffers, so VI points at the buffer finished last frame while
     * the copy just filled the other one. A hex address reads that address.
     * Without this the only view of the frame is whichever buffer VI happens
     * to name, which cannot distinguish "we drew nothing" from "we are
     * looking at the wrong buffer". */
    {
        const char* from = getenv("MGS_SAVE_FROM");
        if (from) {
            /* "efb" writes the embedded framebuffer itself, before any copy.
             * That is the one view that separates "the rasteriser drew
             * nothing" from "the copy out is losing it", and the two need
             * opposite fixes. It is written here rather than converted
             * through YUV, because the EFB is already in the host's layout. */
            if (!strcmp(from, "efb")) {
                unsigned ex, ey;
                f = fopen(path, "wb");
                if (!f) return 0;
                fprintf(f, "P6\n%u %u\n255\n", w, h);
                for (ey = 0; ey < h; ++ey)
                    for (ex = 0; ex < w; ++ex) {
                        uint32_t v = s_efb.pixels[ey * MGS_EFB_WIDTH + ex];
                        uint8_t px[3];
                        px[0] = (uint8_t)((v >> 16) & 0xFFu);
                        px[1] = (uint8_t)((v >> 8) & 0xFFu);
                        px[2] = (uint8_t)(v & 0xFFu);
                        fwrite(px, 1, 3, f);
                    }
                fclose(f);
                return 1;
            }
            if (!strcmp(from, "copy")) xfb = s_efb.copy_dest;
            else xfb = (uint32_t)strtoul(from, NULL, 0);
        }
    }

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
/* Is a token waiting, without consuming it? The finish interrupt is held
 * back for a little guest time after the token appears, so the caller needs
 * to know one is pending before it decides to wait. */
int mgs_display_peek_draw_done(void);
int mgs_display_peek_draw_done(void) { return s_gx.draw_done_tokens != 0u; }

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

    /* WHOSE GEOMETRY IS THIS?
     *
     * w/h/stride come from the last EFB COPY, which is right only while GX is
     * what puts pixels in the external framebuffer. The video decoder writes
     * decoded frames there directly, without a copy, so during a movie these
     * are stale values from whatever GX last copied - and reading a frame
     * with the wrong stride is exactly what produces regular column striping
     * and a row that repeats. The video interface's own registers are what
     * hardware scans out with, so log both and compare. */
    if (getenv("MGS_TRACE_XFB")) {
        static uint32_t last_xfb; static unsigned lw, lh, ls;
        if (xfb != last_xfb || w != lw || h != lh ||
            s_efb.copy_stride != ls) {
            const uint8_t* vi = mgs_mmio_vi_regs(mmio);
            last_xfb = xfb; lw = w; lh = h; ls = s_efb.copy_stride;
            fprintf(stderr, "[xfb] addr 0x%08X  copy %ux%u stride %u  "
                    "VI_HSW 0x%04X VI_VTR 0x%04X\n",
                    xfb, w, h, s_efb.copy_stride,
                    vi ? (unsigned)((vi[0x48] << 8) | vi[0x49]) : 0u,
                    vi ? (unsigned)((vi[0x00] << 8) | vi[0x01]) : 0u);
        }
    }

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
