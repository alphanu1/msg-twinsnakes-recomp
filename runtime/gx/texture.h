/* Textures: eleven formats, all tiled, none of them linear.
 *
 * The GameCube stores every texture in TILES, not scanlines, so a texel's
 * address is not width*y + x. The tile size depends on the format and the
 * order within a tile depends on the format again - RGBA8 is the awkward one,
 * storing a 4x4 tile as two separate 32-byte blocks, alpha-and-red first and
 * green-and-blue second, which reads as a plausible but wrong image if taken
 * as one block. Compressed textures are DXT1 with the two-bit indices in the
 * opposite order to the PC's, which produces a recognisable image with the
 * wrong colours - the worst kind of bug, because it looks like a palette
 * problem rather than a bit-order one.
 *
 * Decoding is to one host format, XRGB8888/ARGB8888, for the same reason the
 * vertex decoder converges: the sampler should have one layout to be right
 * about.
 *
 * DECODED ON DEMAND AND CACHED. A texture is decoded when it is first sampled
 * and kept, keyed on the guest address, the format and the size. The game
 * re-binds the same textures thousands of times a frame and decoding each
 * time would dominate the frame. The cache is invalidated when the game loads
 * new data over the same address, which is what `GXInvalidateTexAll` means.
 */
#ifndef MGS_GX_TEXTURE_H
#define MGS_GX_TEXTURE_H

#include <stdint.h>
#include "../memory/guest.h"

#define MGS_TEX_CACHE_ENTRIES 256u
#define MGS_TEX_MAX_TEXELS    (1024u * 1024u)

typedef struct MgsTexture {
    uint32_t addr;            /* guest address the texels came from */
    uint32_t format;
    uint16_t width, height;
    uint32_t tlut_addr;       /* palette guest address, for indexed formats */
    uint32_t tlut_format;
    uint32_t* texels;         /* ARGB8888, width * height */
    uint64_t generation;      /* for least-recently-used replacement */
    int      valid;
} MgsTexture;

typedef struct MgsTexCache {
    MgsTexture entry[MGS_TEX_CACHE_ENTRIES];
    uint64_t   clock;
    uint64_t   hits, misses, decodes, evictions, refused;
} MgsTexCache;

void mgs_tex_cache_init(MgsTexCache* c);
void mgs_tex_cache_free(MgsTexCache* c);
void mgs_tex_cache_invalidate(MgsTexCache* c);

/* Fetch a decoded texture, decoding it if this is the first time. Returns
 * NULL when the format is one this cannot decode or the address is not
 * readable - a refusal, counted, rather than a guess at what the texels are.
 */
const MgsTexture* mgs_tex_get(MgsTexCache* c, const GuestMemory* mem,
                              uint32_t addr, uint32_t format,
                              unsigned width, unsigned height,
                              uint32_t tlut_addr, uint32_t tlut_format);

/* Decode straight into a caller's buffer. Exposed for testing, where the
 * point is the format handling rather than the caching. */
int mgs_tex_decode(const GuestMemory* mem, uint32_t addr, uint32_t format,
                   unsigned width, unsigned height,
                   const uint16_t* tlut, uint32_t tlut_format,
                   uint32_t* out);

/* Wrap and filter a normalised coordinate. Wrap modes are the hardware's:
 * 0 clamp, 1 repeat, 2 mirror. */
uint32_t mgs_tex_sample(const MgsTexture* t, float u, float v,
                        unsigned wrap_s, unsigned wrap_t, int bilinear);

#endif
