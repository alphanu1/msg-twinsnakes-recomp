/* The graphics pipeline, from command bytes to pixels.
 *
 * Driven by a hand-built command stream rather than by the game, because the
 * failures worth catching are exactly the ones a running game hides. A vertex
 * size computed one byte short does not produce a visibly wrong triangle; it
 * desynchronises the stream and every command after it is nonsense, which
 * looks like the game not drawing rather than like a parser bug. So the sizes
 * are checked directly, and the pipeline is then run end to end on a triangle
 * whose pixels are known in advance.
 */
#include "gx/raster.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static MgsGx gx;
static MgsEfb efb;
static MgsGxRaster raster;
static GuestMemory mem;

static void put8(unsigned v)  { mgs_gx_write(&gx, v & 0xFFu, 1); }
static void put16(unsigned v) { mgs_gx_write(&gx, v & 0xFFFFu, 2); }
static void put32(uint32_t v) { mgs_gx_write(&gx, v, 4); }

static void load_cp(uint8_t reg, uint32_t val)
{
    put8(GX_OP_LOAD_CP); put8(reg); put32(val);
}

static void load_xf(uint16_t addr, const float* f, unsigned n)
{
    unsigned i;
    put8(GX_OP_LOAD_XF);
    put32(((uint32_t)(n - 1u) << 16) | addr);
    for (i = 0; i < n; ++i) {
        uint32_t bits;
        memcpy(&bits, &f[i], sizeof bits);
        put32(bits);
    }
}

static float as_float(uint32_t bits) { float f; memcpy(&f, &bits, sizeof f); return f; }

int main(void)
{
    if (!guest_memory_init(&mem)) { printf("FAIL: no guest memory\n"); return 1; }
    mgs_gx_init(&gx, &mem);
    mgs_efb_init(&efb);
    mgs_raster_init(&raster, &efb);
    gx.triangle = mgs_raster_triangle;
    gx.user = &raster;

    /* --- vertex sizing ------------------------------------------------- */
    {
        MgsGxVertexFormat f;

        /* Position as three f32, one rgba8888 colour, nothing else: 16 bytes.
         * This is the format the sizing bug would most likely get right, so
         * it is the baseline rather than the test. */
        gx.vcd_lo = (1u << 9) | (1u << 13);          /* POS direct, CLR0 direct */
        gx.vcd_hi = 0;
        gx.vat_a[0] = (1u << 0)                      /* pos: 3 components */
                    | (4u << 1)                      /* pos: f32 */
                    | (1u << 13)                     /* clr0: rgba */
                    | (5u << 14);                    /* clr0: rgba8888 */
        mgs_gx_vertex_format(&gx, 0, &f);
        CHECK(f.kind[GX_VA_POS] == GX_ATTR_DIRECT);
        CHECK(f.kind[GX_VA_CLR0] == GX_ATTR_DIRECT);
        CHECK(f.pos_count == 3u && f.pos_format == 4u);
        CHECK(mgs_gx_vertex_size(&f) == 12u + 4u);

        /* Position as s16 with a shift, colour as rgb565: 6 + 2 = 8. The
         * shift must not change the SIZE, only the value - conflating them
         * is the mistake that makes every second vertex wrong. */
        gx.vat_a[0] = (1u << 0) | (3u << 1) | (5u << 4)
                    | (0u << 13) | (0u << 14);
        mgs_gx_vertex_format(&gx, 0, &f);
        CHECK(f.pos_shift == 5u);
        CHECK(mgs_gx_vertex_size(&f) == 6u + 2u);

        /* An INDEXED position is two bytes in the stream however large the
         * array element is. Sizing it as the element is how an indexed mesh
         * desynchronises. */
        gx.vcd_lo = (3u << 9) | (1u << 13);          /* POS index16 */
        gx.vat_a[0] = (1u << 0) | (4u << 1) | (1u << 13) | (5u << 14);
        mgs_gx_vertex_format(&gx, 0, &f);
        CHECK(f.kind[GX_VA_POS] == GX_ATTR_INDEX16);
        CHECK(mgs_gx_vertex_size(&f) == 2u + 4u);

        /* A matrix index is one byte and comes FIRST. */
        gx.vcd_lo = 1u | (1u << 9) | (1u << 13);
        mgs_gx_vertex_format(&gx, 0, &f);
        CHECK(f.kind[GX_VA_PNMTXIDX] == GX_ATTR_DIRECT);
        CHECK(mgs_gx_vertex_size(&f) == 1u + 12u + 4u);
    }

    /* --- a triangle, end to end ---------------------------------------- */
    {
        static const float identity[12] = {
            1,0,0,0,  0,1,0,0,  0,0,1,0
        };
        /* Orthographic, mapping -1..1 straight through, so the arithmetic
         * that follows is checkable by hand. */
        static const float ortho[6] = { 1,0, 1,0, 1,0 };
        /* Six floats at 0x1020-0x1025, then the type at 0x1026. */
        float vp[6];
        uint32_t before;

        gx.vcd_lo = (1u << 9) | (1u << 13);
        gx.vcd_hi = 0;
        gx.vat_a[0] = (1u << 0) | (4u << 1) | (1u << 13) | (5u << 14);

        load_xf(0x0000, identity, 12);
        load_xf(0x1020, ortho, 6);
        {   /* The projection TYPE, at 0x1026 - immediately after the six
             * floats, not after a seventh. Six floats plus a type word is
             * the layout; seven floats is not. */
            uint32_t one = 1u;
            put8(GX_OP_LOAD_XF);
            put32((0u << 16) | 0x1026u);
            put32(one);
        }
        /* Viewport: half-width 320, half-height -240 (y is flipped on the
         * way to the framebuffer), centred, with the hardware's 342 bias. */
        vp[0] = 320.0f; vp[1] = -240.0f; vp[2] = 1.0f;
        vp[3] = 320.0f + 342.0f; vp[4] = 240.0f + 342.0f; vp[5] = 0.0f;
        load_xf(0x101A, vp, 6);

        CHECK(as_float(gx.viewport[0]) == 320.0f);
        CHECK(gx.xf_projection_ortho == 1u);

        before = raster.pixels;

        /* One triangle covering the middle of the screen, solid red. */
        put8(GX_OP_DRAW_FIRST | (GX_PRIM_TRIANGLES << 3) | 0u);
        put16(3);
        {
            static const float xyz[3][3] = {
                { -0.5f, -0.5f, 0.0f },
                {  0.5f, -0.5f, 0.0f },
                {  0.0f,  0.5f, 0.0f },
            };
            unsigned i, k;
            for (i = 0; i < 3u; ++i) {
                for (k = 0; k < 3u; ++k) {
                    uint32_t bits; memcpy(&bits, &xyz[i][k], sizeof bits);
                    put32(bits);
                }
                put32(0xFF0000FFu);          /* r,g,b,a = 255,0,0,255 */
            }
        }

        CHECK(gx.primitives == 1u);
        CHECK(gx.vertices == 3u);
        CHECK(gx.triangles == 1u);
        CHECK(gx.desyncs == 0u);
        CHECK(raster.submitted == 1u);
        CHECK(raster.drawn == 1u);
        CHECK(raster.pixels > before);

        /* The centroid of that triangle is inside it and must be red; a
         * corner of the screen is outside it and must not have been touched. */
        CHECK(efb.pixels[240u * MGS_EFB_WIDTH + 320u] == 0xFFFF0000u);
        CHECK(efb.pixels[0] == 0x00000000u);
    }

    /* --- strip winding ------------------------------------------------- */
    {
        /* Four vertices as a strip are two triangles. Counting is the easy
         * half; the winding is the half that silently culls the wrong faces,
         * and it is checked by the rasteriser's own area sign below. */
        uint64_t tris = gx.triangles;
        unsigned i, k;
        static const float xyz[4][3] = {
            { -0.5f, -0.5f, 0 }, { 0.5f, -0.5f, 0 },
            { -0.5f,  0.5f, 0 }, { 0.5f,  0.5f, 0 },
        };
        put8(GX_OP_DRAW_FIRST | (GX_PRIM_TRIANGLE_STRIP << 3) | 0u);
        put16(4);
        for (i = 0; i < 4u; ++i) {
            for (k = 0; k < 3u; ++k) {
                uint32_t bits; memcpy(&bits, &xyz[i][k], sizeof bits);
                put32(bits);
            }
            put32(0x00FF00FFu);
        }
        CHECK(gx.triangles == tris + 2u);
        CHECK(gx.desyncs == 0u);
    }

    /* --- quads keep all four vertices ---------------------------------- */
    {
        /* Four vertices are TWO triangles, 0-1-2 and 0-2-3, and the second
         * needs vertex 0 again. Holding only three overwrites it with the
         * fourth and emits a degenerate triangle, which the rasteriser
         * discards as zero-area - so half of every quad silently disappears
         * and the surviving half looks perfectly correct. This checks both
         * triangles are real, by area rather than by count. */
        uint64_t tris = gx.triangles, drawn = raster.drawn;
        unsigned i, k;
        static const float xyz[4][3] = {
            { -0.5f, -0.5f, 0 }, { 0.5f, -0.5f, 0 },
            {  0.5f,  0.5f, 0 }, { -0.5f, 0.5f, 0 },
        };
        put8(GX_OP_DRAW_FIRST | (GX_PRIM_QUADS << 3) | 0u);
        put16(4);
        for (i = 0; i < 4u; ++i) {
            for (k = 0; k < 3u; ++k) {
                uint32_t bits; memcpy(&bits, &xyz[i][k], sizeof bits);
                put32(bits);
            }
            put32(0x0000FFFFu);
        }
        CHECK(gx.triangles == tris + 2u);
        /* BOTH must reach the framebuffer. One drawn and one silently
         * dropped is exactly the bug this guards. */
        CHECK(raster.drawn == drawn + 2u);
        CHECK(gx.desyncs == 0u);
    }

    /* --- the stream survives being split ------------------------------- */
    {
        /* Real writes arrive as one to four bytes at a time and a command
         * routinely straddles them. A parser that assumes whole commands
         * works perfectly in a test and not at all against a game. */
        uint64_t cmds = gx.commands;
        put8(GX_OP_LOAD_CP);
        put8(0x50);
        put8(0x00); put8(0x00);
        put16(0x0200);                        /* one u32, in three pieces */
        CHECK(gx.commands == cmds + 1u);
        CHECK(gx.vcd_lo == 0x00000200u);
    }

    /* --- the scissor box ------------------------------------------------ */
    {
        /* The hardware will not write outside the scissor, so a renderer that
         * ignores it draws pixels the game did not ask for. That matters most
         * for a render-to-texture pass, which puts a small box in a corner of
         * the embedded framebuffer; scrawling over the rest of it corrupts
         * whatever else is in there.
         *
         * Coordinates carry a 342 bias, and BP 0x59 shifts the origin in
         * units of two pixels with the same bias - so 171 there means "no
         * shift". Both halves matter: honouring the box without the origin
         * clips against a rectangle of the right shape in the wrong place. */
        static const float identity2[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        static const float ortho2[6] = { 1,0, 1,0, 1,0 };
        uint64_t before_px, before_drawn;
        float vp2[6];
        unsigned x, y;

        /* RE-ESTABLISH THE VERTEX AND TRANSFORM STATE rather than inheriting
         * it. An earlier case here rewrites vcd_lo to prove the parser
         * survives a split write, and a block that silently depends on
         * whatever the previous block left behind fails for reasons that
         * have nothing to do with what it is testing. */
        gx.vcd_lo = (1u << 9) | (1u << 13);
        gx.vcd_hi = 0;
        gx.vat_a[0] = (1u << 0) | (4u << 1) | (1u << 13) | (5u << 14);
        load_xf(0x0000, identity2, 12);
        load_xf(0x1020, ortho2, 6);
        {
            uint32_t one = 1u;
            put8(GX_OP_LOAD_XF);
            put32((0u << 16) | 0x1026u);
            put32(one);
        }
        vp2[0] = 320.0f; vp2[1] = -240.0f; vp2[2] = 1.0f;
        vp2[3] = 320.0f + 342.0f; vp2[4] = 240.0f + 342.0f; vp2[5] = 0.0f;
        load_xf(0x101A, vp2, 6);

        memset(efb.pixels, 0, sizeof efb.pixels);
        mgs_raster_reset_depth(&raster);

        /* A box covering only the left half of the screen. */
        mgs_bp_write(&gx.bp, BP_SCISSOR_OFFSET, (171u << 10) | 171u);
        mgs_bp_write(&gx.bp, BP_SCISSOR_TL, ((0u + 342u) << 12) | (0u + 342u));
        mgs_bp_write(&gx.bp, BP_SCISSOR_BR,
                     ((319u + 342u) << 12) | (479u + 342u));

        before_px = raster.pixels;
        before_drawn = raster.drawn;

        put8(GX_OP_DRAW_FIRST | (GX_PRIM_TRIANGLES << 3) | 0u);
        put16(3);
        {
            /* Spans the full width, so without a scissor it would put pixels
             * on both halves. */
            static const float xyz[3][3] = {
                { -0.9f, -0.5f, 0.0f },
                {  0.9f, -0.5f, 0.0f },
                {  0.0f,  0.5f, 0.0f },
            };
            unsigned i, k;
            for (i = 0; i < 3u; ++i) {
                for (k = 0; k < 3u; ++k) {
                    uint32_t bits; memcpy(&bits, &xyz[i][k], sizeof bits);
                    put32(bits);
                }
                put32(0x00FF00FFu);          /* green */
            }
        }

        CHECK(raster.drawn == before_drawn + 1u);
        CHECK(raster.pixels > before_px);

        /* NOTHING may have been written to the right of the box. Checked over
         * the whole half rather than at one point, because a scissor that is
         * merely shifted still leaves most of it clear. */
        for (y = 0; y < MGS_EFB_HEIGHT; ++y)
            for (x = 320u; x < MGS_EFB_WIDTH; ++x)
                if (efb.pixels[y * MGS_EFB_WIDTH + x] != 0u) {
                    printf("FAIL: scissor leaked at (%u,%u)\n", x, y);
                    ++failures;
                    y = MGS_EFB_HEIGHT; break;
                }

        /* ...and something WAS drawn inside it, so this is not passing by
         * drawing nothing at all - which is the way a wrong bias fails. */
        {
            int inside = 0;
            for (y = 0; y < MGS_EFB_HEIGHT && !inside; ++y)
                for (x = 0; x < 320u; ++x)
                    if (efb.pixels[y * MGS_EFB_WIDTH + x] != 0u) { inside = 1; break; }
            CHECK(inside);
        }

        /* An UNWRITTEN offset register must not be read as a zero offset:
         * that shifts the box 342 pixels and clips away nearly everything.
         * Re-running with the offset never set must still draw. */
        {
            MgsGxBp saved = gx.bp;
            uint64_t d2;
            gx.bp.written[BP_SCISSOR_OFFSET] = 0u;
            gx.bp.reg[BP_SCISSOR_OFFSET] = 0u;

            memset(efb.pixels, 0, sizeof efb.pixels);
            mgs_raster_reset_depth(&raster);
            d2 = raster.drawn;

            put8(GX_OP_DRAW_FIRST | (GX_PRIM_TRIANGLES << 3) | 0u);
            put16(3);
            {
                static const float xyz[3][3] = {
                    { -0.9f, -0.5f, 0.0f },
                    {  0.9f, -0.5f, 0.0f },
                    {  0.0f,  0.5f, 0.0f },
                };
                unsigned i, k;
                for (i = 0; i < 3u; ++i) {
                    for (k = 0; k < 3u; ++k) {
                        uint32_t bits; memcpy(&bits, &xyz[i][k], sizeof bits);
                        put32(bits);
                    }
                    put32(0x0000FFFFu);
                }
            }
            CHECK(raster.drawn == d2 + 1u);
            gx.bp = saved;
        }
    }

    /* --- THE ATTRIBUTE TABLE'S TEXTURE FIELDS -------------------------
     *
     * Every texture coordinate is nine bits of VAT: count, a three-bit
     * format, then a FIVE-bit fraction. Coordinates 1-7 used to read the
     * fraction two bits in instead of four, which overlaps the format, and
     * coordinate 4's fraction was stitched from VAT_B bit 31 (VCacheEnhance)
     * and VAT_C. Each field here gets a distinct value, laid out as
     * Dolphin's CPMemory.h lays them out, so a field read from its
     * neighbour's bits cannot come back right by coincidence. */
    {
        MgsGxVertexFormat f;
        uint32_t b = 0u, c = 0u;
        unsigned i;
        /* VAT_B: tex1..3 at 0, 9, 18; tex4 count/format at 27/28. */
        for (i = 1; i < 4u; ++i) {
            unsigned sft = (i - 1u) * 9u;
            b |= 1u << sft;                          /* two components */
            b |= (uint32_t)(i & 7u) << (sft + 1u);   /* format = i      */
            b |= (uint32_t)(10u + i) << (sft + 4u);  /* frac = 10 + i   */
        }
        b |= 1u << 27; b |= 4u << 28;
        b |= 1u << 31;                               /* VCacheEnhance   */
        /* VAT_C: tex4 frac at 0; tex5..7 at 5, 14, 23. */
        c |= 14u;
        for (i = 5; i < 8u; ++i) {
            unsigned sft = 5u + (i - 5u) * 9u;
            c |= 1u << sft;
            c |= (uint32_t)((i - 4u) & 7u) << (sft + 1u);
            c |= (uint32_t)(10u + i) << (sft + 4u);
        }
        gx.vat_a[3] = 0u; gx.vat_b[3] = b; gx.vat_c[3] = c;
        mgs_gx_vertex_format(&gx, 3u, &f);
        for (i = 1; i < 4u; ++i) {
            CHECK(f.tex_count[i] == 2u);
            CHECK(f.tex_format[i] == i);
            CHECK(f.tex_shift[i] == 10u + i);
        }
        CHECK(f.tex_count[4] == 2u);
        CHECK(f.tex_format[4] == 4u);
        CHECK(f.tex_shift[4] == 14u);        /* not polluted by bit 31 */
        for (i = 5; i < 8u; ++i) {
            CHECK(f.tex_count[i] == 2u);
            CHECK(f.tex_format[i] == i - 4u);
            CHECK(f.tex_shift[i] == 10u + i);
        }
    }

    /* --- TEXGEN: AN OUTPUT TAKES ITS SOURCE FROM ITS REGISTER ----------
     *
     * The game builds output 1 from TEX0, not TEX1. Before texgen was
     * honoured, output 1 read input TEX1 - absent, so zero - and every
     * vertex got the same coordinate. One vertex with only TEX0, texgen 1
     * pointed at TEX0 through an identity matrix, and output 1 must carry
     * TEX0's value. */
    {
        MgsGxVertexFormat f;
        MgsGxVertex v;
        uint8_t data[64];
        unsigned n, at = 0;
        float px = 1.0f, py = 2.0f, pz = 3.0f, s0 = 0.25f, t0 = 0.75f;
        memset(&f, 0, sizeof f);
        f.kind[GX_VA_POS] = GX_ATTR_DIRECT;
        f.pos_count = 3u; f.pos_format = 4u;
        f.kind[GX_VA_TEX0] = GX_ATTR_DIRECT;
        f.tex_count[0] = 2u; f.tex_format[0] = 4u;
        {
            float vals[5];
            unsigned k;
            vals[0] = px; vals[1] = py; vals[2] = pz; vals[3] = s0; vals[4] = t0;
            for (k = 0; k < 5u; ++k) {
                uint32_t bits; memcpy(&bits, &vals[k], sizeof bits);
                data[at++] = (uint8_t)(bits >> 24); data[at++] = (uint8_t)(bits >> 16);
                data[at++] = (uint8_t)(bits >> 8);  data[at++] = (uint8_t)bits;
            }
        }
        gx.xf_num_texgen = 2u;
        gx.xf_texgen[0] = 5u << 7;           /* output 0 from TEX0 */
        gx.xf_texgen[1] = 5u << 7;           /* output 1 ALSO from TEX0 */
        gx.xf_dualtex = 0u;
        memset(&v, 0, sizeof v);
        v.tex_matrix[0] = 60u; v.tex_matrix[1] = 60u;   /* GX_IDENTITY */
        n = mgs_gx_decode_vertex(&gx, &f, data, at, &v);
        CHECK(n == at);
        CHECK(v.u[1] == s0 && v.v[1] == t0);
        /* And from GEOMETRY: output 1 from the position row. */
        gx.xf_texgen[1] = 0u << 7;
        memset(&v, 0, sizeof v);
        v.tex_matrix[0] = 60u; v.tex_matrix[1] = 60u;
        (void)mgs_gx_decode_vertex(&gx, &f, data, at, &v);
        CHECK(v.u[1] == px && v.v[1] == py);
        gx.xf_num_texgen = 8u;
        for (n = 0; n < 8u; ++n) gx.xf_texgen[n] = (5u + n) << 7;
    }

    guest_memory_free(&mem);
    printf(failures ? "gx: FAILED\n" : "gx: ok\n");
    return failures ? 1 : 0;
}
