/* The embedded framebuffer, and the copy out to the external one.
 *
 * The GameCube renders into a small on-die buffer - the EFB, 640x528 at most -
 * and never scans it out. `GXCopyDisp` copies it to an EXTERNAL framebuffer in
 * main memory, converting to YUV 4:2:2 on the way, and the video interface
 * scans THAT. Nothing appears on a television until that copy happens.
 *
 * This is the whole path, minus the drawing. The EFB exists, GX clears it, the
 * copy converts and writes it to the address the game chose, and the video
 * interface presents from there. A rasteriser has one job to add: draw into
 * this buffer. It does not have to invent the route to the screen, and the
 * route can be proven correct before any triangle exists - which is the point
 * of building it first.
 *
 * WHAT IS HONEST ABOUT THIS AND WHAT IS NOT. The clear is real: hardware
 * clears the EFB to the colour in the pixel engine's copy-clear registers, and
 * so does this. The copy is real: the same conversion, the same stride, the
 * address the game programmed. What is missing is geometry - nothing writes
 * pixels other than the clear - so the screen shows exactly what the game
 * clears it to, and no more. That is a truthful picture of an unfinished
 * renderer rather than a stand-in for one.
 */
#ifndef MGS_EFB_H
#define MGS_EFB_H

#include <stdint.h>
#include "../memory/guest.h"

/* The EFB's maximum extent. 640x528 is the largest the hardware allows, and
 * PAL uses the full height; NTSC uses 480 of it. */
#define MGS_EFB_WIDTH   640
#define MGS_EFB_HEIGHT  528

typedef struct MgsEfb {
    /* XRGB8888, one entry per pixel. The host's own layout, not the
     * hardware's: the EFB's real formats (RGB8, RGBA6, RGB565 with Z) are a
     * concern for the rasteriser and for EFB-format-dependent effects, not
     * for getting a picture out. */
    uint32_t pixels[MGS_EFB_WIDTH * MGS_EFB_HEIGHT];

    uint32_t clear_argb;      /* pixel engine copy-clear colour */
    uint32_t copy_dest;       /* guest address the last copy targeted */
    uint32_t copy_stride;     /* bytes per line, as the game programmed it */
    unsigned copy_width;      /* the source rectangle the game last copied */
    unsigned copy_height;
    uint64_t copies, tex_copies;          /* copies to the external framebuffer */
    /* COPIES TO THE EXTERNAL FRAMEBUFFER ONLY, which is what a FRAME is.
     *
     * `copies` counts EVERY EFB copy, the texture path's clear included, so
     * presentation keyed on it put a half-drawn framebuffer on screen every
     * time the game rendered to a texture. MangoHud read 49 fps where the
     * game draws 25 and the picture was green and striped because it was
     * being shown mid-write. Ben spotted the rate first - "the frame rate is
     * not dropping to 25 for the video, that's probably the reason" - and it
     * was. */
    uint64_t xfb_copies;
    uint64_t clears;          /* copies that also cleared */
} MgsEfb;

void mgs_efb_init(MgsEfb* efb);

/* The pixel engine's copy-clear colour, from BP registers 0x4F and 0x50. */
void mgs_efb_set_clear(MgsEfb* efb, uint32_t argb);

/* Destination and stride for the next copy, from BP registers 0x4B and 0x4E. */
void mgs_efb_set_dest(MgsEfb* efb, uint32_t guest_addr, uint32_t stride);

/* Execute a copy. `clear` clears the EFB afterwards, as the hardware does
 * when bit 11 of BP register 0x52 is set. Only a copy to the EXTERNAL
 * framebuffer writes guest memory; a copy to a texture is the renderer's
 * business and is counted, not performed. */
/* A copy to TEXTURE rather than to the external framebuffer. `sx`/`sy` are
 * the source rectangle's top-left in the embedded buffer and `fmt` the copy
 * format from the command. */
void mgs_efb_copy_tex(MgsEfb* efb, GuestMemory* mem,
                      unsigned sx, unsigned sy,
                      unsigned width, unsigned height, unsigned fmt);

void mgs_efb_copy(MgsEfb* efb, GuestMemory* mem,
                  unsigned width, unsigned height, int to_xfb, int clear);

/* Read an external framebuffer out of guest memory into XRGB8888, for
 * presentation. Returns 0 if the address is not readable. */
int mgs_xfb_to_rgb(const GuestMemory* mem, uint32_t xfb_addr, uint32_t stride,
                   unsigned width, unsigned height, uint32_t* out);

#endif
