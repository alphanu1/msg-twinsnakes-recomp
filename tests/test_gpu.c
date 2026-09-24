/* The SDL3 GPU backend, from the device to a pixel and back.
 *
 * This is deliberately the first thing written and the first thing tested,
 * because phase 3's whole method depends on it: the GPU path has to be
 * comparable with the software rasteriser PIXEL FOR PIXEL, or a shader bug
 * and a state bug look the same. A clear whose colour comes back exactly is
 * the smallest statement of that, and it exercises the device, the colour
 * target, the copy pass, the fence and the channel order in one go.
 *
 * SKIPS rather than fails when there is no usable device. A machine with no
 * GPU is not a broken build, and this must be runnable in a batch run.
 */
#include "gfx/gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64u
#define H 48u

int main(void)
{
    static uint32_t px[W * H];
    unsigned i;
    int failures = 0;

    if (!mgs_gpu_init(W, H)) {
        printf("gpu: SKIPPED (no device)\n");
        return 0;
    }
    printf("gpu: device is %s\n", mgs_gpu_driver() ? mgs_gpu_driver() : "?");

    /* A colour with all four channels distinct, so a swizzle cannot pass by
     * accident. 0x8899AABB reading back as 0x88AA99BB would be a red/green
     * swap and a test using grey would never see it. */
    memset(px, 0xCD, sizeof px);
    mgs_gpu_clear(0x8899AABBu);
    if (!mgs_gpu_read_back(px, W, H, W)) {
        printf("FAIL: read back nothing\n");
        mgs_gpu_shutdown();
        return 1;
    }
    for (i = 0; i < W * H; ++i) {
        if (px[i] != 0x8899AABBu) {
            printf("FAIL: pixel %u is 0x%08X, expected 0x8899AABB\n",
                   i, px[i]);
            ++failures;
            break;
        }
    }

    /* A second clear must actually replace the first, not be ignored
     * because the target still holds the old contents. */
    mgs_gpu_clear(0x00112233u);
    if (!mgs_gpu_read_back(px, W, H, W)) { ++failures; }
    else if (px[0] != 0x00112233u) {
        printf("FAIL: second clear gave 0x%08X, expected 0x00112233\n", px[0]);
        ++failures;
    }

    /* The readback must respect a stride wider than the region, because the
     * embedded buffer is 640 wide and copies out a 512-wide box. */
    {
        static uint32_t wide[W * 2u * H];
        memset(wide, 0, sizeof wide);
        mgs_gpu_clear(0x44556677u);
        if (mgs_gpu_read_back(wide, W, H, W * 2u)) {
            if (wide[0] != 0x44556677u ||
                wide[W * 2u] != 0x44556677u ||   /* row 1 at the stride */
                wide[W] != 0u) {                 /* past the region: untouched */
                printf("FAIL: stride not honoured "
                       "(%08X %08X %08X)\n",
                       wide[0], wide[W * 2u], wide[W]);
                ++failures;
            }
        } else ++failures;
    }

    /* --- A TRIANGLE, AND WHERE IT LANDS ------------------------------
     *
     * Clip space with w = 1, so the GPU's divide is the identity and the
     * only thing being tested is the viewport mapping and the winding. The
     * triangle covers the LEFT half: x from -1 to 0 across the full height.
     * Checking a pixel inside AND one outside is what separates "it drew"
     * from "it cleared the whole target to the vertex colour", which looks
     * identical if you only sample one pixel.
     *
     * The colour is the vertex colour times a white texel, which is the
     * base shader's whole job. */
    {
        MgsGpuVertex tri[3];
        memset(tri, 0, sizeof tri);
        /* Clockwise in clip space, covering x <= 0. */
        tri[0].x = -1.0f; tri[0].y = -1.0f;
        tri[1].x =  0.0f; tri[1].y = -1.0f;
        tri[2].x = -1.0f; tri[2].y =  1.0f;
        for (i = 0; i < 3u; ++i) {
            tri[i].z = 0.0f; tri[i].w = 1.0f;
            tri[i].r = 1.0f; tri[i].g = 0.0f; tri[i].b = 0.0f; tri[i].a = 1.0f;
        }
        /* An explicit all-white 2x2, rather than the NULL shorthand: if
         * this works and NULL does not, the stand-in texel is the fault
         * and not the sampler. */
        static const uint32_t tex2[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu,
                                          0xFFFFFFFFu, 0xFFFFFFFFu };
        mgs_gpu_clear(0xFF000000u);            /* opaque black */
        if (!mgs_gpu_draw(tri, 3u, tex2, 2u, 2u)) {
            printf("FAIL: draw refused\n");
            ++failures;
        } else if (!mgs_gpu_read_back(px, W, H, W)) {
            printf("FAIL: read back nothing after the draw\n");
            ++failures;
        } else {
            /* A point well inside the left half, and one well outside it.
             * Row H/2, columns W/8 and W*7/8. */
            uint32_t inside  = px[(H / 2u) * W + (W / 8u)];
            uint32_t outside = px[(H / 2u) * W + (W * 7u / 8u)];
            if (inside != 0xFFFF0000u) {
                printf("FAIL: inside the triangle is 0x%08X, "
                       "expected 0xFFFF0000\n", inside);
                ++failures;
            }
            if (outside != 0xFF000000u) {
                printf("FAIL: outside the triangle is 0x%08X, "
                       "expected the clear 0xFF000000\n", outside);
                ++failures;
            }
        }
    }

    /* --- THE TEXEL'S CHANNELS COME BACK IN THE RIGHT ORDER ------------
     *
     * The test above samples an all-white texture, and white is invariant
     * under every channel permutation - so it passes whatever order the
     * upload writes. That matters because the upload format is chosen to
     * match the decoder's own layout (a uint32_t 0xAARRGGBB is B,G,R,A in
     * memory, which is B8G8R8A8), turning a per-pixel shuffle into a
     * memcpy. Get that wrong and every texture in the game draws with red
     * and blue swapped, which reads as bad art rather than as a bug.
     *
     * So: a texel with four distinct channels, a WHITE vertex colour so the
     * combiner passes the texel through unchanged, and an exact comparison.
     */
    {
        MgsGpuVertex tri[3];
        unsigned i;
        static const uint32_t tex3[4] = { 0xFF112233u, 0xFF112233u,
                                          0xFF112233u, 0xFF112233u };
        tri[0].x = -1.0f; tri[0].y = -1.0f;
        tri[1].x =  0.0f; tri[1].y = -1.0f;
        tri[2].x = -1.0f; tri[2].y =  1.0f;
        for (i = 0; i < 3u; ++i) {
            tri[i].z = 0.0f; tri[i].w = 1.0f;
            tri[i].r = 1.0f; tri[i].g = 1.0f; tri[i].b = 1.0f; tri[i].a = 1.0f;
            tri[i].uv[0][0] = 0.5f; tri[i].uv[0][1] = 0.5f;
        }
        /* mgs_gpu_draw passes no combiner at all, and with none the shader
         * returns the vertex colour and never samples - which is the other
         * reason the white-texture test above could not have caught this.
         * So the batch path is used, with has_texture set and the combiner
         * left unconfigured: that is the hardware's power-on "modulate",
         * and a white vertex colour makes it the texel unchanged. */
        MgsGpuBind   binds[MGS_GPU_TEX_UNITS];
        MgsGpuState  st;
        MgsGpuTev    tev;
        memset(binds, 0, sizeof binds);
        memset(&st, 0, sizeof st);
        memset(&tev, 0, sizeof tev);
        binds[0].texels = tex3; binds[0].w = 2u; binds[0].h = 2u;
        binds[0].key = 0x1234u;
        st.depth_test = 1; st.depth_write = 1; st.depth_func = 3;
        st.colour_write = 1; st.alpha_write = 1;
        tev.ctl[2] = 1;                     /* has_texture */

        mgs_gpu_clear(0xFF000000u);
        mgs_gpu_batch_tri(&tri[0], &tri[1], &tri[2], binds, &st, &tev, 0x99u);
        mgs_gpu_batch_flush();
        if (!mgs_gpu_read_back(px, W, H, W)) {
            printf("FAIL: read back nothing after the textured draw\n");
            ++failures;
        } else {
            uint32_t got = px[(H / 2u) * W + (W / 8u)];
            if (got != 0xFF112233u) {
                printf("FAIL: the texel came back as 0x%08X, expected "
                       "0xFF112233 - the upload's channel order is wrong\n",
                       got);
                ++failures;
            }
        }
    }

    mgs_gpu_shutdown();
    printf(failures ? "gpu: FAILED\n" : "gpu: ok\n");
    return failures ? 1 : 0;
}
