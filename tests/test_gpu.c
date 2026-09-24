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

    mgs_gpu_shutdown();
    printf(failures ? "gpu: FAILED\n" : "gpu: ok\n");
    return failures ? 1 : 0;
}
