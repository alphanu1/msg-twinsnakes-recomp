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

/* ONE HOST VERTEX, which is what the design document asks for: "Build one
 * converter that turns any GX vertex stream into a fixed host layout, then
 * the host renderer only ever sees one vertex format."
 *
 * Position arrives in CLIP space - the transform stays on the CPU for now,
 * where it is already verified against Dolphin. See gx.vert. */
typedef struct MgsGpuVertex {
    float    x, y, z, w;
    float    r, g, b, a;
    float    u, v;
} MgsGpuVertex;

/* Draw a triangle list into the colour target. `tex` is RGBA8 in the host's
 * layout, or NULL for untextured, in which case a 1x1 white texel stands in
 * so one pipeline covers both. */
int mgs_gpu_draw(const MgsGpuVertex* verts, unsigned count,
                 const uint32_t* tex, unsigned tex_w, unsigned tex_h);

/* Counters for the exit report. */
void mgs_gpu_stats(uint64_t* frames, uint64_t* readbacks, uint64_t* bytes);

#endif
