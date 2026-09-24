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
static SDL_GPUGraphicsPipeline* s_pipe;
static SDL_GPUSampler*        s_sampler;
static SDL_GPUBuffer*         s_vbuf;
static unsigned               s_vbuf_verts;
static SDL_GPUTexture*        s_white;
static SDL_GPUTextureFormat   s_depth_format;

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
    /* NEVER let a failed assertion open a dialog. SDL's debug device turns
     * its internal assertions into a modal window on the user's desktop,
     * which is not what anyone running a headless test or a long batch job
     * wants - and it happened. The message still reaches stderr. */
    SDL_SetHint(SDL_HINT_ASSERT, "ignore");

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

    /* ASK WHETHER THE FORMAT IS SUPPORTED, rather than creating one and
     * reading the failure. Creating an unsupported combination trips an
     * SDL assertion - "For 2D textures: the format is unsupported for the
     * given usage" - which under a debug device opens a modal dialog, and
     * under a release device just returns NULL with the reason buried.
     *
     * The GameCube's depth buffer is 24-bit, so D24S8 is the closer match
     * and is asked for first; D32_FLOAT is the usual alternative. */
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    ci.format = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
    if (!SDL_GPUTextureSupportsFormat(s_dev, ci.format,
                                      SDL_GPU_TEXTURETYPE_2D, ci.usage))
        ci.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    s_depth_format = ci.format;
    s_depth = SDL_CreateGPUTexture(s_dev, &ci);

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
    if (s_vbuf) SDL_ReleaseGPUBuffer(s_dev, s_vbuf);
    if (s_sampler) SDL_ReleaseGPUSampler(s_dev, s_sampler);
    if (s_pipe) SDL_ReleaseGPUGraphicsPipeline(s_dev, s_pipe);
    if (s_readback) SDL_ReleaseGPUTransferBuffer(s_dev, s_readback);
    if (s_depth) SDL_ReleaseGPUTexture(s_dev, s_depth);
    if (s_colour) SDL_ReleaseGPUTexture(s_dev, s_colour);
    SDL_DestroyGPUDevice(s_dev);
    s_dev = NULL; s_colour = NULL; s_depth = NULL; s_readback = NULL;
    s_pipe = NULL; s_sampler = NULL; s_vbuf = NULL; s_vbuf_verts = 0;
    s_white = NULL;
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

/* ---- the base pipeline ------------------------------------------------ */

#if defined(MGS_HAVE_SHADERS)
static const Uint32 k_vert_spv[] =
#include "gx.vert.inc"
;
static const Uint32 k_frag_spv[] =
#include "gx.frag.inc"
;
#endif

static SDL_GPUShader* load_shader(SDL_GPUShaderStage stage,
                                  const Uint32* code, size_t bytes,
                                  Uint32 samplers)
{
    SDL_GPUShaderCreateInfo si;
    memset(&si, 0, sizeof si);
    si.code = (const Uint8*)code;
    si.code_size = bytes;
    si.entrypoint = "main";
    si.format = SDL_GPU_SHADERFORMAT_SPIRV;
    si.stage = stage;
    si.num_samplers = samplers;
    return SDL_CreateGPUShader(s_dev, &si);
}

static int build_pipeline(void)
{
#if !defined(MGS_HAVE_SHADERS)
    return 0;
#else
    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_GPUVertexBufferDescription vb;
    SDL_GPUVertexAttribute at[3];
    SDL_GPUColorTargetDescription ct;
    SDL_GPUSamplerCreateInfo sa;
    SDL_GPUShader* vs;
    SDL_GPUShader* fs;

    if (s_pipe) return 1;
    vs = load_shader(SDL_GPU_SHADERSTAGE_VERTEX, k_vert_spv,
                     sizeof k_vert_spv, 0);
    fs = load_shader(SDL_GPU_SHADERSTAGE_FRAGMENT, k_frag_spv,
                     sizeof k_frag_spv, 1);
    if (!vs || !fs) {
        fprintf(stderr, "[gpu] shader: %s\n", SDL_GetError());
        return 0;
    }

    memset(&vb, 0, sizeof vb);
    vb.slot = 0;
    vb.pitch = sizeof(MgsGpuVertex);
    vb.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    memset(at, 0, sizeof at);
    at[0].location = 0; at[0].buffer_slot = 0;
    at[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    at[0].offset = 0;
    at[1].location = 1; at[1].buffer_slot = 0;
    at[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    at[1].offset = 16;
    at[2].location = 2; at[2].buffer_slot = 0;
    at[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    at[2].offset = 32;

    memset(&ct, 0, sizeof ct);
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

    memset(&pi, 0, sizeof pi);
    pi.vertex_shader = vs;
    pi.fragment_shader = fs;
    pi.vertex_input_state.vertex_buffer_descriptions = &vb;
    pi.vertex_input_state.num_vertex_buffers = 1;
    pi.vertex_input_state.vertex_attributes = at;
    pi.vertex_input_state.num_vertex_attributes = 3;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    /* GX culls by the sign of the triangle's area and the rasteriser
     * already expanded strips and fans with the hardware's winding, so the
     * host must not cull again on its own idea of facing. */
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets = 1;
    /* THE DEPTH TEST, which the first version left off.
     *
     * Without it the last triangle drawn wins every pixel, and a 3D scene
     * comes out as a flat mess of whatever happened to be submitted last -
     * which is what it did. The game's own ZMODE register chooses the
     * comparison per draw; LESS-OR-EQUAL is the state the hardware powers on
     * with and what this game asks for on almost every draw. Honouring the
     * register per draw needs one pipeline per state and belongs with the
     * shader generator. */
    pi.target_info.has_depth_stencil_target = true;
    pi.target_info.depth_stencil_format = s_depth_format;
    pi.depth_stencil_state.enable_depth_test = true;
    pi.depth_stencil_state.enable_depth_write = true;
    pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;

    s_pipe = SDL_CreateGPUGraphicsPipeline(s_dev, &pi);
    SDL_ReleaseGPUShader(s_dev, vs);
    SDL_ReleaseGPUShader(s_dev, fs);
    if (!s_pipe) {
        fprintf(stderr, "[gpu] pipeline: %s\n", SDL_GetError());
        return 0;
    }

    memset(&sa, 0, sizeof sa);
    /* NEAREST, because that is what the GameCube does unless the game asks
     * otherwise, and a bilinear default would quietly make every comparison
     * against the software rasteriser fail by a little. */
    sa.min_filter = SDL_GPU_FILTER_NEAREST;
    sa.mag_filter = SDL_GPU_FILTER_NEAREST;
    sa.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sa.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sa.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sa.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    s_sampler = SDL_CreateGPUSampler(s_dev, &sa);
    return s_sampler != NULL;
#endif
}

/* Upload an RGBA8 image into a new texture, ON A COPY PASS THE CALLER
 * OWNS.
 *
 * The first version gave the upload its own command buffer and submitted it
 * before the draw's. Everything looked right and the texture sampled as
 * ZERO: the geometry appeared, in black, because `texture()` returned
 * vec4(0). Uploading and drawing in ONE command buffer is SDL's documented
 * pattern and removes the question of what is ordered against what.
 *
 * A NULL image gives a single white texel so an untextured draw uses the
 * same pipeline. */
static SDL_GPUTexture* upload_texture(SDL_GPUCopyPass* pass,
                                      SDL_GPUTransferBuffer** xfer_out,
                                      const uint32_t* px, unsigned w,
                                      unsigned h)
{
    SDL_GPUTextureCreateInfo ci;
    SDL_GPUTransferBufferCreateInfo tb;
    SDL_GPUTransferBuffer* xfer;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;
    SDL_GPUTexture* tex;
    uint32_t white = 0xFFFFFFFFu;
    void* map;

    if (!px || !w || !h) { px = &white; w = 1; h = 1; }

    memset(&ci, 0, sizeof ci);
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = w; ci.height = h;
    ci.layer_count_or_depth = 1; ci.num_levels = 1;
    ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    tex = SDL_CreateGPUTexture(s_dev, &ci);
    if (!tex) return NULL;

    memset(&tb, 0, sizeof tb);
    tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tb.size = w * h * 4u;
    xfer = SDL_CreateGPUTransferBuffer(s_dev, &tb);
    if (!xfer) { SDL_ReleaseGPUTexture(s_dev, tex); return NULL; }

    map = SDL_MapGPUTransferBuffer(s_dev, xfer, false);
    if (map) {
        unsigned i2;
        uint8_t* o = (uint8_t*)map;
        for (i2 = 0; i2 < w * h; ++i2) {      /* ARGB in, RGBA8 out */
            uint32_t c = px[i2];
            o[i2 * 4u + 0u] = (uint8_t)((c >> 16) & 0xFFu);
            o[i2 * 4u + 1u] = (uint8_t)((c >> 8) & 0xFFu);
            o[i2 * 4u + 2u] = (uint8_t)(c & 0xFFu);
            o[i2 * 4u + 3u] = (uint8_t)((c >> 24) & 0xFFu);
        }
        SDL_UnmapGPUTransferBuffer(s_dev, xfer);
    }

    memset(&src, 0, sizeof src);
    src.transfer_buffer = xfer;
    src.pixels_per_row = w;
    src.rows_per_layer = h;
    memset(&dst, 0, sizeof dst);
    dst.texture = tex;
    dst.w = w; dst.h = h; dst.d = 1;
    SDL_UploadToGPUTexture(pass, &src, &dst, false);

    *xfer_out = xfer;
    return tex;
}

/* ---- the texture cache, GPU side --------------------------------------
 *
 * The decoded-texel cache in gx/texture.c answers "what does this texture
 * look like"; this one answers "is it already on the GPU". They are
 * different questions with different lifetimes: a texture can be a cache
 * hit on the CPU side and still need uploading the first time the GPU sees
 * it.
 *
 * Keyed on the CONTENT hash the CPU cache already computes, so the same art
 * bound from two addresses uploads once, and a video frame that genuinely
 * changed gets a new entry rather than a stale one.
 */
#define GPU_TEX_SLOTS 256u

static struct {
    uint64_t        key;
    SDL_GPUTexture* tex;
    uint64_t        used;
} s_gtex[GPU_TEX_SLOTS];
static uint64_t s_gtex_clock;
static uint64_t s_uploads, s_cache_hits;

static SDL_GPUTexture* cached_texture(SDL_GPUCopyPass* pass,
                                      SDL_GPUTransferBuffer** xfer_out,
                                      const uint32_t* px, unsigned w,
                                      unsigned h, uint64_t key)
{
    unsigned i, victim = 0;
    uint64_t oldest = ~0ull;

    *xfer_out = NULL;
    if (key) {
        for (i = 0; i < GPU_TEX_SLOTS; ++i) {
            if (s_gtex[i].tex && s_gtex[i].key == key) {
                s_gtex[i].used = ++s_gtex_clock;
                ++s_cache_hits;
                return s_gtex[i].tex;
            }
        }
    }
    for (i = 0; i < GPU_TEX_SLOTS; ++i) {
        if (!s_gtex[i].tex) { victim = i; oldest = 0; break; }
        if (s_gtex[i].used < oldest) { oldest = s_gtex[i].used; victim = i; }
    }
    {
        SDL_GPUTexture* t = upload_texture(pass, xfer_out, px, w, h);
        if (!t) return NULL;
        ++s_uploads;
        if (key) {
            if (s_gtex[victim].tex)
                SDL_ReleaseGPUTexture(s_dev, s_gtex[victim].tex);
            s_gtex[victim].tex = t;
            s_gtex[victim].key = key;
            s_gtex[victim].used = ++s_gtex_clock;
        }
        return t;
    }
}

static int mgs_gpu_draw_keyed(const MgsGpuVertex* verts, unsigned count,
                              const uint32_t* tex, unsigned tex_w,
                              unsigned tex_h, uint64_t key)
{
    SDL_GPUCommandBuffer* cmd;
    SDL_GPUColorTargetInfo ct;
    SDL_GPURenderPass* pass;
    SDL_GPUCopyPass* cp;
    SDL_GPUBufferBinding bind;
    SDL_GPUTextureSamplerBinding tsb;
    SDL_GPUTexture* t;
    SDL_GPUTransferBuffer* tex_xfer = NULL;
    SDL_GPUTransferBuffer* vtx_xfer = NULL;
    Uint32 vbytes;

    if (!s_dev || !verts || count < 3u) return 0;
    if (!build_pipeline()) return 0;

    /* The vertex buffer grows to fit and is kept: a frame submits the same
     * shape of work over and over, so reallocating per draw would be the
     * dominant cost of the whole path. */
    if (s_vbuf_verts < count) {
        SDL_GPUBufferCreateInfo bi;
        if (s_vbuf) SDL_ReleaseGPUBuffer(s_dev, s_vbuf);
        memset(&bi, 0, sizeof bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = (Uint32)(count * sizeof(MgsGpuVertex));
        s_vbuf = SDL_CreateGPUBuffer(s_dev, &bi);
        s_vbuf_verts = s_vbuf ? count : 0u;
        if (!s_vbuf) return 0;
    }
    vbytes = (Uint32)(count * sizeof(MgsGpuVertex));

    cmd = SDL_AcquireGPUCommandBuffer(s_dev);
    if (!cmd) return 0;

    /* ONE command buffer: upload, then draw. */
    cp = SDL_BeginGPUCopyPass(cmd);
    if (!cp) { SDL_SubmitGPUCommandBuffer(cmd); return 0; }
    {
        SDL_GPUTransferBufferCreateInfo tb;
        SDL_GPUTransferBufferLocation src;
        SDL_GPUBufferRegion dst;
        void* map;
        memset(&tb, 0, sizeof tb);
        tb.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tb.size = vbytes;
        vtx_xfer = SDL_CreateGPUTransferBuffer(s_dev, &tb);
        if (vtx_xfer) {
            map = SDL_MapGPUTransferBuffer(s_dev, vtx_xfer, false);
            if (map) memcpy(map, verts, vbytes);
            SDL_UnmapGPUTransferBuffer(s_dev, vtx_xfer);
            memset(&src, 0, sizeof src);
            src.transfer_buffer = vtx_xfer;
            memset(&dst, 0, sizeof dst);
            dst.buffer = s_vbuf;
            dst.size = vbytes;
            SDL_UploadToGPUBuffer(cp, &src, &dst, false);
        }
    }
    t = cached_texture(cp, &tex_xfer, tex, tex_w, tex_h, key);
    SDL_EndGPUCopyPass(cp);

    if (!t || !vtx_xfer) {
        SDL_SubmitGPUCommandBuffer(cmd);
        if (t && !key) SDL_ReleaseGPUTexture(s_dev, t);
        if (tex_xfer) SDL_ReleaseGPUTransferBuffer(s_dev, tex_xfer);
        if (vtx_xfer) SDL_ReleaseGPUTransferBuffer(s_dev, vtx_xfer);
        return 0;
    }

    memset(&ct, 0, sizeof ct);
    ct.texture = s_colour;
    ct.load_op = SDL_GPU_LOADOP_LOAD;   /* keep what is already there */
    ct.store_op = SDL_GPU_STOREOP_STORE;
    {
        SDL_GPUDepthStencilTargetInfo ds;
        memset(&ds, 0, sizeof ds);
        ds.texture = s_depth;
        ds.load_op = SDL_GPU_LOADOP_LOAD;    /* the frame's depth so far */
        ds.store_op = SDL_GPU_STOREOP_STORE;
        ds.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        ds.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        pass = SDL_BeginGPURenderPass(cmd, &ct, 1, &ds);
    }
    if (pass) {
        SDL_BindGPUGraphicsPipeline(pass, s_pipe);
        memset(&bind, 0, sizeof bind);
        bind.buffer = s_vbuf;
        SDL_BindGPUVertexBuffers(pass, 0, &bind, 1);
        memset(&tsb, 0, sizeof tsb);
        tsb.texture = t;
        tsb.sampler = s_sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &tsb, 1);
        SDL_DrawGPUPrimitives(pass, count, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmd);

    /* The transfer buffers go; SDL keeps them alive until the command
     * buffer retires. The TEXTURE only goes when it was not cached - a
     * cached one is owned by the cache and releasing it here would free
     * something the next draw expects to find. */
    if (!key) SDL_ReleaseGPUTexture(s_dev, t);
    if (tex_xfer) SDL_ReleaseGPUTransferBuffer(s_dev, tex_xfer);
    if (vtx_xfer) SDL_ReleaseGPUTransferBuffer(s_dev, vtx_xfer);
    ++s_frames;
    return 1;
}

int mgs_gpu_draw(const MgsGpuVertex* verts, unsigned count,
                 const uint32_t* tex, unsigned tex_w, unsigned tex_h)
{
    return mgs_gpu_draw_keyed(verts, count, tex, tex_w, tex_h, 0);
}

/* ---- batching --------------------------------------------------------- */

#define BATCH_MAX 65536u        /* vertices, so 21,845 triangles */

static MgsGpuVertex* s_batch;
static unsigned      s_batch_n;
static const uint32_t* s_batch_tex;
static unsigned      s_batch_tw, s_batch_th;
static uint64_t      s_batch_key;
static int           s_batch_has;
static uint64_t      s_batch_tris, s_batch_flushes;

void mgs_gpu_batch_flush(void)
{
    if (!s_dev || !s_batch_n) { s_batch_n = 0; s_batch_has = 0; return; }
    mgs_gpu_draw_keyed(s_batch, s_batch_n, s_batch_tex,
                       s_batch_tw, s_batch_th, s_batch_key);
    s_batch_tris += s_batch_n / 3u;
    ++s_batch_flushes;
    s_batch_n = 0;
    s_batch_has = 0;
}

void mgs_gpu_batch_tri(const MgsGpuVertex* a, const MgsGpuVertex* b,
                       const MgsGpuVertex* c,
                       const uint32_t* tex, unsigned tex_w, unsigned tex_h,
                       uint64_t key)
{
    if (!s_dev) return;
    if (!s_batch) {
        s_batch = (MgsGpuVertex*)malloc(BATCH_MAX * sizeof(MgsGpuVertex));
        if (!s_batch) return;
    }
    /* A different texture, or a full batch, ends this one. Comparing the
     * KEY rather than the pointer is what lets a re-decoded video frame end
     * the batch while identical art carries on. */
    if (s_batch_has && (key != s_batch_key || s_batch_n + 3u > BATCH_MAX))
        mgs_gpu_batch_flush();

    s_batch_tex = tex; s_batch_tw = tex_w; s_batch_th = tex_h;
    s_batch_key = key; s_batch_has = 1;
    s_batch[s_batch_n++] = *a;
    s_batch[s_batch_n++] = *b;
    s_batch[s_batch_n++] = *c;
}

void mgs_gpu_begin_frame(uint32_t clear_argb, int do_clear)
{
    if (!s_dev) return;
    mgs_gpu_batch_flush();
    if (do_clear) mgs_gpu_clear(clear_argb);
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
int  mgs_gpu_draw(const MgsGpuVertex* v, unsigned n, const uint32_t* t,
                  unsigned w, unsigned h)
{ (void)v; (void)n; (void)t; (void)w; (void)h; return 0; }
void mgs_gpu_batch_tri(const MgsGpuVertex* a, const MgsGpuVertex* b,
                       const MgsGpuVertex* c, const uint32_t* t,
                       unsigned w, unsigned h, uint64_t k)
{ (void)a; (void)b; (void)c; (void)t; (void)w; (void)h; (void)k; }
void mgs_gpu_batch_flush(void) { }
void mgs_gpu_begin_frame(uint32_t c, int d) { (void)c; (void)d; }

#endif

void mgs_gpu_batch_stats(uint64_t* tris, uint64_t* flushes,
                         uint64_t* uploads, uint64_t* cache_hits)
{
#if defined(MGS_HAVE_SDL3)
    if (tris) *tris = s_batch_tris;
    if (flushes) *flushes = s_batch_flushes;
    if (uploads) *uploads = s_uploads;
    if (cache_hits) *cache_hits = s_cache_hits;
#else
    if (tris) *tris = 0; if (flushes) *flushes = 0;
    if (uploads) *uploads = 0; if (cache_hits) *cache_hits = 0;
#endif
}

void mgs_gpu_stats(uint64_t* frames, uint64_t* readbacks, uint64_t* bytes)
{
    if (frames) *frames = s_frames;
    if (readbacks) *readbacks = s_readbacks;
    if (bytes) *bytes = s_bytes;
}
