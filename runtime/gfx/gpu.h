/* The SDL3 GPU backend: phase 3's renderer, starting from the device.
 *
 * WHY THIS EXISTS, in the design document's own words: the software
 * rasteriser "was fast enough for menus and far too slow for the intro
 * movie - 13.5 Mpx/s, about 320 cycles a pixel", and "a CPU rasteriser will
 * not render the Dock and the Heliport at native resolution. It buys time
 * until the Vulkan backend exists." That prediction has now been met in
 * practice: with the vertex arrays fixed (F302) the game draws what it
 * should, 2.36 billion pixels a run, and the port went from a few per cent
 * faster than real time to 40% slower in the cinematic - which is heard as
 * the audio device starving.
 *
 * ONE BACKEND, NOT SEVERAL. SDL3's GPU API takes SPIR-V, DXBC/DXIL or MSL
 * and picks Vulkan, D3D12 or Metal itself, so this is one code path rather
 * than a Vulkan backend plus a D3D12 one. Decided 2026-09-22 in the design
 * document; nothing here is Vulkan-specific.
 *
 * WHAT IT IS NOT, yet. This is the device, its offscreen targets and the
 * means to get pixels back. The TEV-to-shader generator, the vertex
 * converter and GPU-side EFB copies are the rest of phase 3.
 */
#ifndef MGS_GFX_GPU_H
#define MGS_GFX_GPU_H

#include <stdint.h>

/* Bring the device up. Returns 0 when there is no usable GPU, which is not
 * an error: a headless or software-only host keeps the CPU rasteriser. */
int  mgs_gpu_init(unsigned width, unsigned height);
void mgs_gpu_shutdown(void);

/* Which backend SDL chose, for the startup line. NULL when not initialised. */
const char* mgs_gpu_driver(void);

/* True once the device and its targets exist. */
int mgs_gpu_ready(void);

/* Clear the colour target. ARGB, to match the embedded buffer's layout. */
void mgs_gpu_clear(uint32_t argb);

/* Copy the colour target back into a host buffer in the embedded buffer's
 * layout, so the existing copy, framebuffer and presentation paths keep
 * working while the rest of phase 3 is built. This is a readback and it is
 * deliberately the FIRST thing that works: it makes every later step
 * checkable against the software rasteriser pixel for pixel. */
int mgs_gpu_read_back(uint32_t* argb, unsigned width, unsigned height,
                      unsigned stride);

/* ONLY THE RECTANGLE THE COPY IS ABOUT TO READ.
 *
 * A readback is a fenced full-pipeline stall plus a download, and this game
 * makes 19,202 of them in a run - most of them render-to-texture copies of
 * a few dozen scanlines. Downloading 640x528 for a 64-line copy is 88% of
 * that traffic spent on rows nobody is going to look at.
 *
 * Writes into `argb` at its own (x, y), so the caller's buffer stays whole
 * and every other reader of it keeps working. */
int mgs_gpu_read_back_rect(uint32_t* argb, unsigned x, unsigned y,
                           unsigned w, unsigned h, unsigned stride);

/* ONE HOST VERTEX, which is what the design document asks for: "Build one
 * converter that turns any GX vertex stream into a fixed host layout, then
 * the host renderer only ever sees one vertex format."
 *
 * Position arrives in CLIP space - the transform stays on the CPU for now,
 * where it is already verified against Dolphin. See gx.vert. */
/* FOUR TEXTURE COORDINATE SETS, because TEV stages do not share one.
 *
 * Each stage names its own texture map AND its own coordinate generator, so
 * a composite that reads three planes reads them at three different sets of
 * coordinates. One `u, v` per vertex is right for the 92% of this game's
 * triangles that run a single stage and wrong for the rest - and the rest
 * is the cinematic.
 *
 * Four rather than eight: the hardware allows eight maps, and this game's
 * multi-stage draws bind at most four distinct ones (counted, see
 * `units_overflowed` in the exit report). Eight would double the vertex for
 * nothing. */
#define MGS_GPU_TEX_UNITS 4u

typedef struct MgsGpuVertex {
    float    x, y, z, w;
    float    r, g, b, a;
    float    uv[MGS_GPU_TEX_UNITS][2];
} MgsGpuVertex;

/* One texture unit's binding: the decoded texels and the key that says
 * whether the GPU already holds them. */
typedef struct MgsGpuBind {
    const uint32_t* texels;
    unsigned        w, h;
    uint64_t        key;       /* 0 = nothing bound; a 1x1 white stands in */
    /* HOW THIS ONE IS SAMPLED, which is per texture and not per device.
     * GX gives every map its own wrap mode on each axis (0 clamp, 1 repeat,
     * 2 mirror) and its own magnification filter, and the software path has
     * always honoured them. Sampling everything with REPEAT and NEAREST
     * made a clamped texture wrap at its edges - a thin band of the
     * opposite side bleeding in, which looks like a seam rather than like a
     * sampler fault. */
    unsigned char   wrap_s, wrap_t, bilinear;
} MgsGpuBind;

/* Draw a triangle list into the colour target. `tex` is ARGB in the host's
 * layout, or NULL for untextured, in which case a 1x1 white texel stands in
 * so one pipeline covers both. Unit 0 only; the test's entry point. */
int mgs_gpu_draw(const MgsGpuVertex* verts, unsigned count,
                 const uint32_t* tex, unsigned tex_w, unsigned tex_h);

/* THE PER-DRAW STATE THAT HAS TO REACH THE PIPELINE.
 *
 * GX decides blending, the depth comparison and the alpha test per draw,
 * and a pipeline object bakes all three in - so a change of any of them
 * ends the batch and selects a different pipeline. The first version baked
 * "opaque, depth less-or-equal, no alpha test" for everything, and it
 * showed: a fade-out just replaced instead of fading, and untextured white
 * geometry that should have been blended away was painted solid.
 *
 * The factors are GX's own numbering, translated in gpu.c, because that is
 * the form the rasteriser already has them in. */
typedef struct MgsGpuState {
    unsigned char blend_enable;
    unsigned char blend_src, blend_dst, blend_sub;
    unsigned char depth_test, depth_write, depth_func;
    /* GX MASKS COLOUR AND ALPHA SEPARATELY (CMODE1 against CMODE0), and
     * the embedded buffer's alpha is not decoration: later draws blend
     * against it and an EFB copy carries it into a texture. Writing alpha
     * on a draw that asked for colour only is a quiet corruption of both. */
    unsigned char colour_write, alpha_write;
} MgsGpuState;

/* THE COMBINER STATE, IN THE LAYOUT THE SHADER READS IT.
 *
 * gxtev.frag interprets the TEV combiner rather than having one generated
 * for it, so the state travels as a uniform block instead of as GLSL. The
 * field order and the ivec4 padding are std140's, which is why everything
 * here is a group of four 32-bit words even where only one is used: a
 * scalar in a uniform array would still occupy sixteen bytes, so saying so
 * is clearer than relying on the rule.
 *
 * Integers in the hardware's own 0-255 range, exactly as runtime/gx/tev.c
 * holds them, because the point of this path is that the two can be
 * compared pixel for pixel. */
typedef struct MgsGpuTev {
    int32_t  reg[4][4];      /* the TEV registers as the draw starts        */
    uint32_t env[16][4];     /* [0] colour environment, [1] alpha, 2 unused */
    int32_t  konst[16][4];   /* xyz the stage's konst colour, w its alpha   */
    int32_t  swap[4][4];     /* the four swap tables, as channel indices    */
    int32_t  unit[16][4];    /* [0] which texture unit this stage samples   */
    int32_t  ctl[4];         /* stages, configured, has_texture, swap_set   */
    int32_t  atest[4];       /* ref0, ref1, op0, op1                        */
    int32_t  atest2[4];      /* logic, enabled                              */
} MgsGpuTev;

/* ---- batching ----------------------------------------------------------
 *
 * A draw call per triangle would be far slower than the software rasteriser
 * it is replacing - this game submits twelve million a run. Triangles are
 * gathered while the texture stays the same and drawn in one call when it
 * changes, when the batch fills, or when the frame ends.
 *
 * `key` identifies the texture's CONTENTS, so the same art bound twice is
 * uploaded once. Zero means untextured. */
/* `tev` may be NULL, which selects the base shader's fixed "rasterised
 * colour times the texture" - the same thing the combiner's unconfigured
 * default does. `tev_key` identifies the state so a change of it can end
 * the batch without comparing 700 bytes twelve million times a run. */
void mgs_gpu_batch_tri(const MgsGpuVertex* a, const MgsGpuVertex* b,
                       const MgsGpuVertex* c,
                       const MgsGpuBind* binds, const MgsGpuState* state,
                       const MgsGpuTev* tev, uint64_t tev_key);

/* Draw whatever is gathered. Called when the state changes and before the
 * embedded buffer is read. */
void mgs_gpu_batch_flush(void);

/* Clear the colour target, for the copy that clears. */
void mgs_gpu_begin_frame(uint32_t clear_argb, int do_clear);

/* Counters for the exit report. */
void mgs_gpu_stats(uint64_t* frames, uint64_t* readbacks, uint64_t* bytes);
void mgs_gpu_batch_stats(uint64_t* tris, uint64_t* flushes,
                         uint64_t* uploads, uint64_t* cache_hits);

#endif
