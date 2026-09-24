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
    /* HOW BUSY THE DECODED TEXELS ARE, 0..255, computed ONCE HERE.
     *
     * The noise hunt wanted this per draw and got it by scanning the whole
     * bound texture inside mgs_raster_triangle, on every textured triangle.
     * For a 512x448 surface that is a 917 KB walk on a 32-byte stride -
     * cache-hostile - repeated 600,000 times per fifty frames, and it was
     * not behind any diagnostic flag. It was the single largest cost in the
     * renderer and it held the game at 17-25 fps.
     *
     * Roughness is a property of the TEXELS, and the texels do not change
     * while the texture is cached; a change re-decodes, because that is
     * what the content hash is for. So it is computed where they are
     * produced - some 300 decodes per fifty frames instead of 600,000
     * scans - and every diagnostic that wanted it still has it. */
    uint8_t  rough;
    int      valid;
} MgsTexture;

#define MGS_TEX_MEMO 8u

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

    /* See mgs_tex_memo_reset. Keyed on everything that selects a texture. */
    struct {
        uint32_t addr, format, tlut_addr, tlut_format;
        uint16_t width, height;
        const MgsTexture* result;
        int      valid;
    } memo[MGS_TEX_MEMO];
    unsigned memo_next;
    uint64_t memo_hits, memo_misses;

    /* FOR EVERY DECODE: how varied the SOURCE bytes were against how
     * varied the DECODED texels came out, per format.
     *
     * The checkerboard test (MGS_TEX_CHECKER) proved the sampler, the
     * coordinates and the combiner all work - forcing a checkerboard
     * puts a checkerboard on 70-83% of the screen. So the texels
     * themselves are flat, and there are only two ways that happens:
     * the bytes we read were already flat (a data or address fault) or
     * the decoder flattened them (a decoder fault). One is upstream of
     * us and one is ours, and these two numbers tell them apart. */
    uint64_t dec_n[16];      /* decodes, by format */
    uint64_t dec_src_var[16];/* mean |byte - previous byte| x1000 */
    uint64_t dec_out_var[16];/* the same over decoded luminance */

} MgsTexCache;

/* THE LOOKUP MEMO, and why it is safe.
 *
 * `mgs_tex_get` content-hashes the texture on every call so that a game
 * rewriting texels in place is noticed. `bind_texture` calls it ONCE PER
 * TRIANGLE, so a scene of 6,160 triangles a frame performs six million
 * strided, cache-missing reads a frame purely to discover that the same
 * texture is still the same texture. That is the whole of the frame time:
 * 3.3 microseconds a triangle, about seventeen thousand cycles, to submit
 * one triangle.
 *
 * The memo returns the previous answer when the same texture is asked for
 * again, and it is dropped at every EFB copy - so a texture is hashed at
 * most once per copy rather than once per triangle. This game makes about
 * nine copies a frame and binds a few dozen distinct textures, so the
 * hashing goes from thousands of times a frame to tens.
 *
 * WHAT THIS GIVES UP, stated plainly: a texture whose texels the CPU
 * rewrites IN PLACE, between two binds inside the same copy, is drawn with
 * the previous contents until the next copy. Real hardware needs an
 * explicit invalidate for that case and would behave the same way; the
 * per-triangle hash was belt and braces beyond the console, and it cost the
 * entire frame budget. If a texture is ever seen one frame stale, this is
 * the first thing to suspect and `mgs_tex_memo_reset` is where to look.
 */

void mgs_tex_memo_reset(MgsTexCache* c);

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

/* ...and keep the bytes it deposited, because the decode happens later, at
 * bind time, and a copy to the framebuffer in between rewrites the same
 * memory. */
void mgs_tex_snapshot_efb_copy(uint32_t addr, const uint8_t* src, unsigned n);

void mgs_gx_order_note(char c);
void mgs_gx_order_dump(const char* label);

extern uint64_t mgs_gx_seq;

#endif
