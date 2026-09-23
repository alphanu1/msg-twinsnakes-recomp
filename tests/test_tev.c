/* The texture-environment combiner, against a configuration taken from the
 * game rather than invented.
 *
 * This exists because two combiner bugs reached the screen and neither was
 * visible as "the combiner is wrong": one made a movie into noise (the
 * colour registers' alpha was read as their red, so a blend meant to be
 * nearly transparent came out opaque), and one made it purple. Both would
 * have been caught here in a second.
 *
 * The configuration below is what MGS programmes to turn YUV 4:2:0 into RGB,
 * read out of a running frame with MGS_TRACE_TEVCFG. It is two passes over
 * three I8 planes:
 *
 *   pass 1, replacing:   R and B      pass 2, ADDING:   G
 *     konst (148,0,148) x luma          konst (0,148,0) x luma
 *     konst (0,0,255)   x chroma U      konst (0,50,0)  x chroma U, SUBTRACT
 *     konst (203,0,0)   x chroma V      konst (0,103,0) x chroma V, SUBTRACT
 *     offset (-111,0,-138)              offset (0,68,0)
 *     all scaled x2 at the last stage
 *
 * Those numbers ARE BT.601, which is what makes them a test rather than a
 * transcription - 2*203/255 = 1.592 against the standard's 1.596, 2*255/255
 * = 2.0 against 2.018, 2*50/255 = 0.392 against 0.391, 2*103/255 = 0.808
 * against 0.813, and the offsets -222 and -276 against -222.9 and -276.9.
 * So the combiner's output can be checked against the colour the standard
 * says, not against what this implementation happens to produce.
 */
#include "gx/tev.h"
#include "gx/bp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while (0)

static MgsGxBp bp;

static uint32_t argb(int a, int r, int g, int b)
{
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

/* GX_SetTevColor / GX_SetTevColorS10: alpha in the high half of the RA word,
 * red in the low half; green high and blue low in the BG word. */
static void set_tev_color(unsigned idx, int r, int g, int b, int a, int konst)
{
    uint32_t type = konst ? (1u << 23) : 0u;
    mgs_bp_write(&bp, (uint8_t)(0xE0u + idx * 2u),
                 type | ((uint32_t)(a & 0x7FF) << 12) | (uint32_t)(r & 0x7FF));
    mgs_bp_write(&bp, (uint8_t)(0xE1u + idx * 2u),
                 type | ((uint32_t)(g & 0x7FF) << 12) | (uint32_t)(b & 0x7FF));
}

static void set_stages(unsigned n)
{
    mgs_bp_write(&bp, BP_GEN_MODE, ((n - 1u) & 0xFu) << 10);
}

/* One colour stage: out = d (+/-) lerp(a, b, c), then bias, scale, clamp. */
static void set_stage(unsigned s, unsigned a, unsigned b, unsigned c,
                      unsigned d, unsigned op, unsigned scale, unsigned clamp,
                      unsigned dest)
{
    mgs_bp_write(&bp, (uint8_t)(BP_TEV_COLOR_ENV + s * 2u),
                 (a << 12) | (b << 8) | (c << 4) | d |
                 (op << 18) | (scale << 20) | (clamp << 19) | (dest << 22));
    /* Alpha: pass the previous alpha through unchanged. */
    mgs_bp_write(&bp, (uint8_t)(BP_TEV_ALPHA_ENV + s * 2u),
                 (7u << 13) | (7u << 10) | (7u << 7) | (0u << 4) |
                 (1u << 19));
}

/* kcsel for a stage: which konst register its colour inputs see. */
static void set_konst_sel(unsigned s, unsigned sel)
{
    uint8_t reg = (uint8_t)(BP_TEV_KSEL + (s >> 1));
    uint32_t v = mgs_bp_get(&bp, reg);
    if (s & 1u) v = (v & ~(0x1Fu << 14)) | (sel << 14);
    else        v = (v & ~(0x1Fu << 4))  | (sel << 4);
    mgs_bp_write(&bp, reg, v);
}

/* ---- the register layout itself -------------------------------------- */

static void test_register_layout(void)
{
    MgsTevCompiled t;

    /* A colour register, written the way the SDK writes one. Alpha must NOT
     * come back as red: that swap is what turned the movie into noise. */
    set_tev_color(1, 200, 100, 50, 7, 0);
    set_stages(1);
    mgs_tev_compile(&bp, &t);
    CHECK(t.reg[1][0] == 200);
    CHECK(t.reg[1][1] == 100);
    CHECK(t.reg[1][2] == 50);
    CHECK(t.reg[1][3] == 7);

    /* Signed: the YUV offsets are negative and a mask would make them large
     * positives. -111 is what this game's red offset actually is. */
    set_tev_color(1, -111, 68, -138, 0, 0);
    mgs_tev_compile(&bp, &t);
    CHECK(t.reg[1][0] == -111);
    CHECK(t.reg[1][1] == 68);
    CHECK(t.reg[1][2] == -138);

    /* Bit 23 routes the same address to the konst register instead, and the
     * colour register must survive it. */
    set_tev_color(1, 148, 0, 148, 0, 1);
    mgs_tev_compile(&bp, &t);
    CHECK(t.reg[1][0] == -111);          /* still the colour register */
    CHECK(bp.konst[1][0] == 148);
    CHECK(bp.konst[1][1] == 0);
    CHECK(bp.konst[1][2] == 148);
}

/* ---- konst selection --------------------------------------------------- */

static void test_konst_selection(void)
{
    MgsTevCompiled t;
    set_stages(2);
    set_tev_color(2, 90, 80, 70, 60, 1);      /* konst register 2 */

    set_konst_sel(0, 14u);                    /* K2: the whole rgb */
    set_konst_sel(1, 22u);                    /* K2_G: green, splatted */
    mgs_tev_compile(&bp, &t);
    CHECK(t.kc[0][0] == 90 && t.kc[0][1] == 80 && t.kc[0][2] == 70);
    CHECK(t.kc[1][0] == 80 && t.kc[1][1] == 80 && t.kc[1][2] == 80);

    set_konst_sel(0, 4u);                     /* the constant 1/2 */
    mgs_tev_compile(&bp, &t);
    CHECK(t.kc[0][0] == 128 && t.kc[0][2] == 128);

    /* Selectors 8-11 are invalid and read zero, not one. */
    set_konst_sel(0, 9u);
    mgs_tev_compile(&bp, &t);
    CHECK(t.kc[0][0] == 0 && t.kc[0][1] == 0 && t.kc[0][2] == 0);
}

/* ---- the game's YUV to RGB, against BT.601 ----------------------------- */

/* The standard, at the precision the game's own coefficients carry. */
static void bt601(int y, int u, int v, int* r, int* g, int* b)
{
    int yy = 298 * (y - 16), uu = u - 128, vv = v - 128;
    int rr = (yy + 409 * vv + 128) >> 8;
    int gg = (yy - 100 * uu - 208 * vv + 128) >> 8;
    int bb = (yy + 516 * uu + 128) >> 8;
    *r = rr < 0 ? 0 : (rr > 255 ? 255 : rr);
    *g = gg < 0 ? 0 : (gg > 255 ? 255 : gg);
    *b = bb < 0 ? 0 : (bb > 255 ? 255 : bb);
}

/* Build one of the two passes. `konsts` are the three stages' konst
 * registers, `offset` the value in colour register 1. `sub` says which
 * stages subtract. */
static void build_pass(const int konsts[3][3], const int offset[3],
                       const int sub[3])
{
    unsigned s;
    memset(&bp, 0, sizeof bp);
    set_stages(3);
    set_tev_color(1, offset[0], offset[1], offset[2], 0, 0);
    for (s = 0; s < 3u; ++s) {
        /* konst registers 1, 2, 3 hold the three stages' coefficients. */
        set_tev_color(s + 1u, konsts[s][0], konsts[s][1], konsts[s][2], 0, 1);
        set_konst_sel(s, 12u + s + 1u);       /* K1, K2, K3 - whole rgb */
        /* a = zero(15), b = texture colour(8), c = konst(14),
         * d = colour register 1 on the first stage, then the running result.
         * The last stage scales by two and clamps. */
        set_stage(s, 15u, 8u, 14u, s == 0u ? 2u : 0u,
                  sub[s], s == 2u ? 1u : 0u, s == 2u ? 1u : 0u, 0u);
    }
}

static int run_pass(uint32_t t0, uint32_t t1, uint32_t t2, int chan)
{
    MgsTevCompiled t;
    MgsTevInput in;
    uint32_t stage_tex[3];
    uint8_t stage_has[3];
    uint32_t out;

    stage_tex[0] = t0; stage_tex[1] = t1; stage_tex[2] = t2;
    stage_has[0] = stage_has[1] = stage_has[2] = 1;

    memset(&in, 0, sizeof in);
    in.texture = t0;
    in.has_texture = 1;
    in.raster = 0xFFFFFFFFu;
    in.stage_tex = stage_tex;
    in.stage_has = stage_has;

    mgs_tev_compile(&bp, &t);
    out = mgs_tev_run_compiled(&t, &in);
    return (int)((out >> (chan == 0 ? 16 : chan == 1 ? 8 : 0)) & 0xFFu);
}

static void test_yuv_to_rgb(void)
{
    /* The two passes, exactly as the game programmes them. */
    static const int rb_konst[3][3] = { {148,0,148}, {0,0,255}, {203,0,0} };
    static const int rb_offset[3]   = { -111, 0, -138 };
    static const int rb_sub[3]      = { 0, 0, 0 };
    static const int g_konst[3][3]  = { {0,148,0}, {0,50,0}, {0,103,0} };
    static const int g_offset[3]    = { 0, 68, 0 };
    static const int g_sub[3]       = { 0, 1, 1 };

    /* Y, U, V triples spanning the range, including the dark near-neutral
     * frames the movie actually contains (the intro measures luma 42,
     * chroma 132 and 122). */
    static const int samples[][3] = {
        {  42, 133, 123 },   /* the real frame */
        {  16, 128, 128 },   /* black */
        { 235, 128, 128 },   /* white */
        { 128, 128, 128 },   /* mid grey */
        {  81,  90, 240 },   /* saturated red */
        { 145,  54,  34 },   /* saturated green */
        {  41, 240, 110 },   /* saturated blue */
    };
    unsigned k;

    for (k = 0; k < sizeof samples / sizeof samples[0]; ++k) {
        int y = samples[k][0], u = samples[k][1], v = samples[k][2];
        /* I8 planes: every channel of the texel carries the same value. */
        uint32_t ty = argb(y, y, y, y), tu = argb(u, u, u, u),
                 tv = argb(v, v, v, v);
        int er, eg, eb, gr, gg, gb;
        int got_r, got_g, got_b;

        bt601(y, u, v, &er, &eg, &eb);

        build_pass(rb_konst, rb_offset, rb_sub);
        got_r = run_pass(ty, tu, tv, 0);
        got_b = run_pass(ty, tu, tv, 2);

        build_pass(g_konst, g_offset, g_sub);
        got_g = run_pass(ty, tu, tv, 1);

        /* Within 8 of the standard. The game's coefficients are themselves
         * rounded to 8 bits, so exact equality would be testing the
         * rounding rather than the combiner. */
        gr = got_r - er; gg = got_g - eg; gb = got_b - eb;
        if (gr < -8 || gr > 8 || gg < -8 || gg > 8 || gb < -8 || gb > 8) {
            printf("FAIL YUV(%3d,%3d,%3d): got RGB(%3d,%3d,%3d), "
                   "BT.601 says (%3d,%3d,%3d)\n",
                   y, u, v, got_r, got_g, got_b, er, eg, eb);
            ++failures;
        }
    }
}

int main(void)
{
    memset(&bp, 0, sizeof bp);
    test_register_layout();
    memset(&bp, 0, sizeof bp);
    test_konst_selection();
    test_yuv_to_rgb();

    if (failures) { printf("%d failure(s)\n", failures); return 1; }
    printf("tev: ok\n");
    return 0;
}
