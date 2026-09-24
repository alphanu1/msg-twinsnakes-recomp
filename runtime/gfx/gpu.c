#include "gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t s_frames, s_readbacks, s_bytes;

#if defined(MGS_HAVE_SDL3)
#include <SDL3/SDL.h>

static SDL_GPUDevice*         s_dev;
static SDL_GPUTexture*        s_colour;
static SDL_GPUTexture*        s_depth;
static SDL_GPUTransferBuffer* s_readback;
static unsigned               s_w, s_h;
static const char*            s_driver;

/* The embedded buffer is 640x528 of ARGB. The colour target matches it so a
 * readback is a memcpy with a channel swizzle rather than a rescale, which
 * is what makes the GPU path comparable to the software one PIXEL FOR
 * PIXEL - the only way to tell a shader bug from a state bug. */
int mgs_gpu_init(unsigned width, unsigned height)
{
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTransferBufferCreateInfo tb;

    if (s_dev) return 1;
    if (!width || !height) return 0;
    if (getenv("MGS_NO_GPU")) return 0;

    /* The GPU API needs the video subsystem, even for offscreen work: SDL
     * keeps the device alongside its display handling. It does NOT need a
     * window, which is what lets a headless run render and read back. */
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        fprintf(stderr, "[gpu] no video subsystem: %s\n", SDL_GetError());
        return 0;
    }

    /* SPIR-V only for now: that is what shaderc produces and what the
     * Vulkan and (through SDL) the other backends accept. Naming no driver
     * lets SDL choose. */
    s_dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,
                                getenv("MGS_GPU_DEBUG") != NULL, NULL);
    if (!s_dev) {
        fprintf(stderr, "[gpu] no device: %s\n", SDL_GetError());
        return 0;
    }
    s_driver = SDL_GetGPUDeviceDriver(s_dev);

    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = width;
    ci.height = height;
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    s_colour = SDL_CreateGPUTexture(s_dev, &ci);

    ci.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    s_depth = SDL_CreateGPUTexture(s_dev, &ci);
    if (!s_depth) {
        /* Not every device has D24S8; D32 is the usual alternative. The
         * GameCube's own depth buffer is 24-bit, so this is the closer of
         * the two and worth asking for first. */
        ci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        s_depth = SDL_CreateGPUTexture(s_dev, &ci);
    }

    memset(&tb, 0, sizeof tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    tb.size = width * height * 4u;
    s_readback = SDL_CreateGPUTransferBuffer(s_dev, &tb);

    if (!s_colour || !s_depth || !s_readback) {
        fprintf(stderr, "[gpu] targets failed: %s\n", SDL_GetError());
        mgs_gpu_shutdown();
        return 0;
    }
    s_w = width; s_h = height;
    fprintf(stderr, "[gpu] %s, %ux%u colour + depth\n",
            s_driver ? s_driver : "?", width, height);
    return 1;
}

void mgs_gpu_shutdown(void)
{
    if (!s_dev) return;
    if (s_readback) SDL_ReleaseGPUTransferBuffer(s_dev, s_readback);
    if (s_depth) SDL_ReleaseGPUTexture(s_dev, s_depth);
    if (s_colour) SDL_ReleaseGPUTexture(s_dev, s_colour);
    SDL_DestroyGPUDevice(s_dev);
    s_dev = NULL; s_colour = NULL; s_depth = NULL; s_readback = NULL;
}

const char* mgs_gpu_driver(void) { return s_dev ? s_driver : NULL; }
int mgs_gpu_ready(void) { return s_dev != NULL; }

void mgs_gpu_clear(uint32_t argb)
{
    SDL_GPUCommandBuffer* cmd;
    SDL_GPUColorTargetInfo ct;
    SDL_GPUDepthStencilTargetInfo ds;
    SDL_GPURenderPass* pass;

    if (!s_dev) return;
    cmd = SDL_AcquireGPUCommandBuffer(s_dev);
    if (!cmd) return;

    memset(&ct, 0, sizeof ct);
    ct.texture = s_colour;
    ct.clear_color.r = (float)((argb >> 16) & 0xFFu) / 255.0f;
    ct.clear_color.g = (float)((argb >> 8) & 0xFFu) / 255.0f;
    ct.clear_color.b = (float)(argb & 0xFFu) / 255.0f;
    ct.clear_color.a = (float)((argb >> 24) & 0xFFu) / 255.0f;
    ct.load_op = SDL_GPU_LOADOP_CLEAR;
    ct.store_op = SDL_GPU_STOREOP_STORE;

    memset(&ds, 0, sizeof ds);
    ds.texture = s_depth;
    ds.clear_depth = 1.0f;
    ds.load_op = SDL_GPU_LOADOP_CLEAR;
    ds.store_op = SDL_GPU_STOREOP_STORE;
    ds.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    ds.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;

    pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &ds);
    if (pass) SDL_EndGPURenderPass(pass);
    SDL_SubmitGPUCommandBuffer(cmd);
    ++s_frames;
}

int mgs_gpu_read_back(uint32_t* argb, unsigned width, unsigned height,
                      unsigned stride)
{
    SDL_GPUCommandBuffer* cmd;
    SDL_GPUCopyPass* pass;
    SDL_GPUTextureRegion src;
    SDL_GPUTextureTransferInfo dst;
    SDL_GPUFence* fence;
    const uint8_t* mapped;
    unsigned y, x;

    if (!s_dev || !argb) return 0;
    if (width > s_w) width = s_w;
    if (height > s_h) height = s_h;

    cmd = SDL_AcquireGPUCommandBuffer(s_dev);
    if (!cmd) return 0;

    memset(&src, 0, sizeof src);
    src.texture = s_colour;
    src.w = s_w; src.h = s_h; src.d = 1;

    memset(&dst, 0, sizeof dst);
    dst.transfer_buffer = s_readback;
    dst.pixels_per_row = s_w;
    dst.rows_per_layer = s_h;

    pass = SDL_BeginGPUCopyPass(cmd);
    if (!pass) { SDL_SubmitGPUCommandBuffer(cmd); return 0; }
    SDL_DownloadFromGPUTexture(pass, &src, &dst);
    SDL_EndGPUCopyPass(pass);

    /* A fence, not a guess. The download is on the GPU timeline and reading
     * the transfer buffer before it lands gives whatever was there before -
     * which would look exactly like a rendering bug. */
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (!fence) return 0;
    SDL_WaitForGPUFences(s_dev, true, &fence, 1);
    SDL_ReleaseGPUFence(s_dev, fence);

    mapped = (const uint8_t*)SDL_MapGPUTransferBuffer(s_dev, s_readback, false);
    if (!mapped) return 0;
    /* R8G8B8A8 on the wire, ARGB in the embedded buffer. */
    for (y = 0; y < height; ++y) {
        const uint8_t* row = mapped + (size_t)y * s_w * 4u;
        uint32_t* out = argb + (size_t)y * stride;
        for (x = 0; x < width; ++x) {
            out[x] = ((uint32_t)row[x * 4u + 3u] << 24) |
                     ((uint32_t)row[x * 4u + 0u] << 16) |
                     ((uint32_t)row[x * 4u + 1u] << 8) |
                      (uint32_t)row[x * 4u + 2u];
        }
    }
    SDL_UnmapGPUTransferBuffer(s_dev, s_readback);
    ++s_readbacks;
    s_bytes += (uint64_t)width * height * 4u;
    return 1;
}

#else  /* built without SDL3 */

int  mgs_gpu_init(unsigned w, unsigned h) { (void)w; (void)h; return 0; }
void mgs_gpu_shutdown(void) { }
const char* mgs_gpu_driver(void) { return NULL; }
int  mgs_gpu_ready(void) { return 0; }
void mgs_gpu_clear(uint32_t argb) { (void)argb; }
int  mgs_gpu_read_back(uint32_t* a, unsigned w, unsigned h, unsigned s)
{ (void)a; (void)w; (void)h; (void)s; return 0; }

#endif

void mgs_gpu_stats(uint64_t* frames, uint64_t* readbacks, uint64_t* bytes)
{
    if (frames) *frames = s_frames;
    if (readbacks) *readbacks = s_readbacks;
    if (bytes) *bytes = s_bytes;
}
