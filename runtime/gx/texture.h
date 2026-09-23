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
    uint64_t hash;           /* of the encoded bytes: contents, not address */
    uint32_t efb_serial;     /* the EFB copy that produced these texels, or 0 */
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
    uint32_t shape_key[16]; uint64_t shape_hit[16]; unsigned shape_n;
    uint32_t rough_key[24]; uint64_t rough_sum[24], rough_cnt[24];
    unsigned rough_max[24], rough_n;
    uint32_t rough_addr[24], rough_bytes[24];
    uint64_t   clock;
    uint64_t   hits, misses, decodes, evictions, refused;
    /* Why a lookup was refused, split out: a size we will not take, a texel
     * count past the cap, a palette that is not mapped, a failed
     * allocation, or a format the decoder does not implement. */
    uint64_t refused_size, refused_texels, refused_palette;
    uint64_t refused_alloc, refused_decode;
    int      trace_refusals;   /* MGS_TRACE_TEXREFUSE */

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

/* An EFB copy has deposited texels at this address.
 *
 * The graphics processor serves textures out of its own memory, and a copy
 * to the framebuffer does not disturb them. Only another copy to the same
 * place does. Saying so here is what lets the cache keep serving texels that
 * main memory no longer holds - which is what the hardware does. */
void mgs_tex_note_efb_copy(uint32_t addr);

extern uint64_t mgs_gx_seq;

#endif
