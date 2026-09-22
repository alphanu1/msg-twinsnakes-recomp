#include <string.h>
#include "tev.h"

/* How many stages the general-mode register says are active. */
static unsigned stage_count(const MgsGxBp* bp)
{
    unsigned n = ((mgs_bp_get(bp, BP_GEN_MODE) >> 10) & 0xFu) + 1u;
    return n > 16u ? 16u : n;
}

/* The same count, for the rasteriser: how many stages the combiner will run
 * is what decides how many textures SHOULD have been sampled. */
unsigned mgs_tev_stage_count(const MgsGxBp* bp);
unsigned mgs_tev_stage_count(const MgsGxBp* bp) { return stage_count(bp); }

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* One colour channel of a TEV register, as the game set it. The registers are
 * written as pairs: 0xE0+2n carries red and alpha, 0xE1+2n green and blue,
 * each as an 11-bit signed value. */
static void tev_register(const MgsGxBp* bp, unsigned index, int* out)
{
    uint32_t lo = mgs_bp_get(bp, (uint8_t)(BP_TEV_REGISTER_L + index * 2u));
    uint32_t hi = mgs_bp_get(bp, (uint8_t)(BP_TEV_REGISTER_L + index * 2u + 1u));
    out[3] = (int)(lo & 0x7FFu);           /* alpha */
    out[0] = (int)((lo >> 12) & 0x7FFu);   /* red */
    out[2] = (int)(hi & 0x7FFu);           /* blue */
    out[1] = (int)((hi >> 12) & 0x7FFu);   /* green */
}

/* Colour input selectors, as the colour-environment register encodes them. */
static void color_input(unsigned sel, const int reg[4][4], const MgsTevInput* in,
                        int konst, int* out)
{
    unsigned i;
    switch (sel) {
        case 0: for (i = 0; i < 3u; ++i) out[i] = reg[0][i]; return;   /* cprev */
        case 1: for (i = 0; i < 3u; ++i) out[i] = reg[0][3]; return;   /* aprev */
        case 2: for (i = 0; i < 3u; ++i) out[i] = reg[1][i]; return;   /* c0 */
        case 3: for (i = 0; i < 3u; ++i) out[i] = reg[1][3]; return;   /* a0 */
        case 4: for (i = 0; i < 3u; ++i) out[i] = reg[2][i]; return;   /* c1 */
        case 5: for (i = 0; i < 3u; ++i) out[i] = reg[2][3]; return;   /* a1 */
        case 6: for (i = 0; i < 3u; ++i) out[i] = reg[3][i]; return;   /* c2 */
        case 7: for (i = 0; i < 3u; ++i) out[i] = reg[3][3]; return;   /* a2 */
        case 8:                                                        /* texc */
            out[0] = (int)((in->texture >> 16) & 0xFFu);
            out[1] = (int)((in->texture >> 8) & 0xFFu);
            out[2] = (int)(in->texture & 0xFFu);
            return;
        case 9:                                                        /* texa */
            for (i = 0; i < 3u; ++i) out[i] = (int)((in->texture >> 24) & 0xFFu);
            return;
        case 10:                                                       /* rasc */
            out[0] = (int)((in->raster >> 16) & 0xFFu);
            out[1] = (int)((in->raster >> 8) & 0xFFu);
            out[2] = (int)(in->raster & 0xFFu);
            return;
        case 11:                                                       /* rasa */
            for (i = 0; i < 3u; ++i) out[i] = (int)((in->raster >> 24) & 0xFFu);
            return;
        case 12: for (i = 0; i < 3u; ++i) out[i] = 255; return;        /* one */
        case 13: for (i = 0; i < 3u; ++i) out[i] = 128; return;        /* half */
        case 14: for (i = 0; i < 3u; ++i) out[i] = konst; return;      /* konst */
        default: for (i = 0; i < 3u; ++i) out[i] = 0; return;          /* zero */
    }
}

static int alpha_input(unsigned sel, const int reg[4][4], const MgsTevInput* in,
                       int konst)
{
    switch (sel) {
        case 0: return reg[0][3];                                      /* aprev */
        case 1: return reg[1][3];                                      /* a0 */
        case 2: return reg[2][3];                                      /* a1 */
        case 3: return reg[3][3];                                      /* a2 */
        case 4: return (int)((in->texture >> 24) & 0xFFu);             /* texa */
        case 5: return (int)((in->raster >> 24) & 0xFFu);              /* rasa */
        case 6: return konst;                                          /* konst */
        default: return 0;                                             /* zero */
    }
}

/* The combiner itself. `op` selects add or subtract; bias and scale are the
 * hardware's fixed set, and `compare` mode replaces the whole expression with
 * a comparison - which is how games do stencil-like effects without a
 * stencil buffer. */
/* INLINE, AND 32-BIT.
 *
 * This was 36% of the whole program in a sampling profile - not because the
 * arithmetic is heavy but because it was an out-of-line call with eight
 * arguments, made four times for every pixel (three colour components and
 * alpha). It is small enough to inline; the compiler declined to because it
 * is called from four separate sites.
 *
 * "long" bought nothing: the widest intermediate is an input at full scale
 * times 256 plus a bias and a x4 scale, which is about 1.07e9 and inside a
 * 32-bit int. Signed division by a power of two is not a shift, so the two
 * divides stay written as divides for exactness rather than being "optimised"
 * into shifts that would round the wrong way for negative values.
 */
static inline int combine(int a, int b, int c, int d, unsigned op, unsigned bias,
                          unsigned scale, unsigned clamp_out)
{
    int bias_v = (bias == 1u) ? 128 : (bias == 2u) ? -128 : 0;
    int cc = c + (c >> 7);      /* 0-255 read as 0-256 so 255 reaches a full one */
    int v;

    v = d * 256 + a * (256 - cc) + b * cc;
    v = v / 256;

    v = (op == 1u) ? d - (v - d) : v;
    v += bias_v;

    if (scale == 1u) v *= 2;
    else if (scale == 2u) v *= 4;
    else if (scale == 3u) v /= 2;

    if (clamp_out) return clamp255(v);
    return v;
}


void mgs_tev_compile(const MgsGxBp* bp, MgsTevCompiled* out)
{
    unsigned i, s;
    out->stages = stage_count(bp);
    if (out->stages > 16u) out->stages = 16u;
    out->configured = bp->written[BP_GEN_MODE] != 0;
    for (i = 0; i < 4u; ++i) tev_register(bp, i, out->reg[i]);
    for (s = 0; s < out->stages; ++s) {
        out->ce[s] = mgs_bp_get(bp, (uint8_t)(BP_TEV_COLOR_ENV + s * 2u));
        out->ae[s] = mgs_bp_get(bp, (uint8_t)(BP_TEV_ALPHA_ENV + s * 2u));
    }
}

uint32_t mgs_tev_run_compiled(const MgsTevCompiled* t, const MgsTevInput* in)
{
    int reg[4][4];
    unsigned s, i;

    memcpy(reg, t->reg, sizeof reg);

    if (!t->configured) {
        if (!in->has_texture) return in->raster;
        {
            uint32_t out = 0;
            for (i = 0; i < 4u; ++i) {
                int tx = (int)((in->texture >> (i * 8)) & 0xFFu);
                int r  = (int)((in->raster  >> (i * 8)) & 0xFFu);
                out |= (uint32_t)clamp255(tx * r / 255) << (i * 8);
            }
            return out;
        }
    }

    for (s = 0; s < t->stages; ++s) {
        uint32_t ce = t->ce[s], ae = t->ae[s];
        int a[3], b[3], c[3], d[3];
        int out[4];
        unsigned dst_c = (ce >> 22) & 3u, dst_a = (ae >> 22) & 3u;
        int konst_c = 255, konst_a = 255;
        /* This stage's own texel, where the stages differ. The copy is made
         * only when they do; with one shared texture `cur` is `in` and this
         * costs a predictable branch. */
        MgsTevInput sv;
        const MgsTevInput* cur = in;

        if (in->stage_tex) {
            sv = *in;
            sv.texture     = in->stage_tex[s];
            sv.has_texture = in->stage_has[s];
            cur = &sv;
        }

        color_input((ce >> 12) & 0xFu, reg, cur, konst_c, a);
        color_input((ce >> 8)  & 0xFu, reg, cur, konst_c, b);
        color_input((ce >> 4)  & 0xFu, reg, cur, konst_c, c);
        color_input((ce >> 0)  & 0xFu, reg, cur, konst_c, d);

        for (i = 0; i < 3u; ++i)
            out[i] = combine(a[i], b[i], c[i], d[i],
                             (ce >> 18) & 1u, (ce >> 16) & 3u,
                             (ce >> 20) & 3u, (ce >> 19) & 1u);

        {
            int aa = alpha_input((ae >> 13) & 7u, reg, cur, konst_a);
            int ab = alpha_input((ae >> 10) & 7u, reg, cur, konst_a);
            int ac = alpha_input((ae >> 7)  & 7u, reg, cur, konst_a);
            int ad = alpha_input((ae >> 4)  & 7u, reg, cur, konst_a);
            out[3] = combine(aa, ab, ac, ad,
                             (ae >> 18) & 1u, (ae >> 16) & 3u,
                             (ae >> 20) & 3u, (ae >> 19) & 1u);
        }

        for (i = 0; i < 3u; ++i) reg[dst_c][i] = out[i];
        reg[dst_a][3] = out[3];
    }

    return ((uint32_t)clamp255(reg[0][3]) << 24) |
           ((uint32_t)clamp255(reg[0][0]) << 16) |
           ((uint32_t)clamp255(reg[0][1]) << 8) |
           (uint32_t)clamp255(reg[0][2]);
}

uint32_t mgs_tev_run(const MgsGxBp* bp, const MgsTevInput* in)
{
    int reg[4][4];                 /* prev, c0, c1, c2 - rgb then alpha */
    unsigned n = stage_count(bp), s, i;

    /* Register 0 is "prev" and starts undefined on hardware; the SDK always
     * writes it before use. Starting from the rasterised colour rather than
     * from zero means a game that relies on that write still looks right if
     * we miss the write, which would hide a bug - so it starts at the
     * register the game set. */
    for (i = 0; i < 4u; ++i) tev_register(bp, i, reg[i]);

    if (!bp->written[BP_GEN_MODE]) {
        /* Nothing configured yet: the rasterised colour, modulated by the
         * texture if there is one. This is the state before the game's first
         * GXSetTevOp, and it is the only guess in this file. */
        if (!in->has_texture) return in->raster;
        {
            uint32_t out = 0;
            for (i = 0; i < 4u; ++i) {
                int t = (int)((in->texture >> (i * 8)) & 0xFFu);
                int r = (int)((in->raster >> (i * 8)) & 0xFFu);
                out |= (uint32_t)clamp255(t * r / 255) << (i * 8);
            }
            return out;
        }
    }

    for (s = 0; s < n; ++s) {
        uint32_t ce = mgs_bp_get(bp, (uint8_t)(BP_TEV_COLOR_ENV + s * 2u));
        uint32_t ae = mgs_bp_get(bp, (uint8_t)(BP_TEV_ALPHA_ENV + s * 2u));
        int a[3], b[3], c[3], d[3];
        int out[4];
        unsigned dst_c = (ce >> 22) & 3u, dst_a = (ae >> 22) & 3u;
        int konst_c = 255, konst_a = 255;

        color_input((ce >> 12) & 0xFu, reg, in, konst_c, a);
        color_input((ce >> 8)  & 0xFu, reg, in, konst_c, b);
        color_input((ce >> 4)  & 0xFu, reg, in, konst_c, c);
        color_input((ce >> 0)  & 0xFu, reg, in, konst_c, d);

        for (i = 0; i < 3u; ++i)
            out[i] = combine(a[i], b[i], c[i], d[i],
                             (ce >> 18) & 1u, (ce >> 16) & 3u,
                             (ce >> 20) & 3u, (ce >> 19) & 1u);

        {
            int aa = alpha_input((ae >> 13) & 7u, reg, in, konst_a);
            int ab = alpha_input((ae >> 10) & 7u, reg, in, konst_a);
            int ac = alpha_input((ae >> 7)  & 7u, reg, in, konst_a);
            int ad = alpha_input((ae >> 4)  & 7u, reg, in, konst_a);
            out[3] = combine(aa, ab, ac, ad,
                             (ae >> 18) & 1u, (ae >> 16) & 3u,
                             (ae >> 20) & 3u, (ae >> 19) & 1u);
        }

        for (i = 0; i < 3u; ++i) reg[dst_c][i] = out[i];
        reg[dst_a][3] = out[3];
    }

    return ((uint32_t)clamp255(reg[0][3]) << 24) |
           ((uint32_t)clamp255(reg[0][0]) << 16) |
           ((uint32_t)clamp255(reg[0][1]) << 8) |
           (uint32_t)clamp255(reg[0][2]);
}

/* Can this alpha configuration reject anything at all?
 *
 * Both comparisons ALWAYS, or the register never written, means every pixel
 * passes - and then testing per pixel is a function call for nothing. This
 * game sets exactly that: ALPHA_COMPARE = 0x3F0000, and 0 pixels are killed
 * in a whole run.
 */
int mgs_tev_alpha_test_always(const MgsGxBp* bp);
int mgs_tev_alpha_test_always(const MgsGxBp* bp)
{
    uint32_t r;
    if (!bp->written[BP_ALPHA_COMPARE]) return 1;
    r = mgs_bp_get(bp, BP_ALPHA_COMPARE);
    return ((r >> 16) & 7u) == 7u && ((r >> 19) & 7u) == 7u
           && ((r >> 22) & 3u) == 0u;          /* ALWAYS and ALWAYS, AND */
}

int mgs_tev_alpha_test(const MgsGxBp* bp, uint32_t argb)
{
    uint32_t r = mgs_bp_get(bp, BP_ALPHA_COMPARE);
    int a = (int)((argb >> 24) & 0xFFu);
    int ref0, ref1, r0, r1;
    unsigned op0, op1, logic;

    if (!bp->written[BP_ALPHA_COMPARE]) return 1;

    ref0  = (int)(r & 0xFFu);
    ref1  = (int)((r >> 8) & 0xFFu);
    op0   = (r >> 16) & 7u;
    op1   = (r >> 19) & 7u;
    logic = (r >> 22) & 3u;

    #define TEST(op, ref) ( \
        (op) == 0 ? 0 : \
        (op) == 1 ? (a <  (ref)) : \
        (op) == 2 ? (a == (ref)) : \
        (op) == 3 ? (a <= (ref)) : \
        (op) == 4 ? (a >  (ref)) : \
        (op) == 5 ? (a != (ref)) : \
        (op) == 6 ? (a >= (ref)) : 1)

    r0 = TEST(op0, ref0);
    r1 = TEST(op1, ref1);
    #undef TEST

    switch (logic) {
        case 0: return r0 && r1;
        case 1: return r0 || r1;
        case 2: return r0 != r1;
        default: return r0 == r1;
    }
}
