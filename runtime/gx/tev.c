#include <string.h>
#include "tev.h"

#include <stdlib.h>

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

/* RED IS THE LOW HALF AND ALPHA IS THE HIGH HALF, and they were the wrong
 * way round.
 *
 * Dolphin's `TevReg::RA` is the reference (rule 12): `BitField<0, 11, s32>
 * red` and `BitField<12, 11, s32> alpha`. This read alpha from bits 0-10 and
 * red from 12-22, so every TEV constant register returned its red as its
 * alpha and its alpha as its red.
 *
 * What that costs is not subtle, because the alpha is what the blend uses.
 * The movie's last pass draws a full-screen quad of the previous frame with
 * `src*a + dst*(1-a)`, and its alpha comes from a register whose red the
 * game had set to full: the quad came out opaque instead of a faint overlay,
 * so the first frame - sampling a buffer nothing had written yet - painted
 * uninitialised memory over the whole picture at full strength, and every
 * frame after it fed on its own output. That is the green and magenta
 * striping, and it is why the movie's planes could decode perfectly while
 * the screen showed noise.
 *
 * Bit 23 selects whether the write sets the register or its konst; it is not
 * read here yet, and the konst path is selected separately by KSEL. */
static void tev_register(const MgsGxBp* bp, unsigned index, int* out)
{
    /* Read the ROUTED value, not the register's last raw word.
     *
     * 0xE0-0xE7 carry both the colour registers and the konst registers,
     * selected by bit 23, and `mgs_bp_write` now routes each write to the
     * right one. Re-deriving the colour here from the raw word would read a
     * konst as a colour whenever the konst was written second. */
    out[0] = bp->tevreg[index][0];
    out[1] = bp->tevreg[index][1];
    out[2] = bp->tevreg[index][2];
    out[3] = bp->tevreg[index][3];
}

/* Colour input selectors, as the colour-environment register encodes them. */
static void color_input(unsigned sel, const int reg[4][4], const MgsTevInput* in,
                        const int* konst, int* out)
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
        case 14: for (i = 0; i < 3u; ++i) out[i] = konst[i]; return;   /* konst */
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


/* KONST, WHICH WAS 255 FOR EVERY STAGE.
 *
 * The constant a stage uses is chosen by KSEL (0xF6-0xFD), and it is NOT one
 * value: a selector can pick a whole konst register's rgb, or splat one of
 * its channels across all three. Splatting is how a game passes a scalar,
 * and a YUV composite passes its matrix coefficients exactly that way - so
 * pinning konst at 255 turns every coefficient into 1.0 and the colours come
 * out as a wash.
 *
 * The tables are Dolphin's `tev_ksel_table_c` and `tev_ksel_table_a`
 * (rule 12): eight fractions of one, four invalid selectors that read zero,
 * then the konst registers whole and by channel. Alpha has no "whole
 * register" form, so 12-15 are invalid there too. */
static void konst_color(const MgsGxBp* bp, unsigned sel, int* out)
{
    static const int frac[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };
    unsigned i, k = (sel - 12u) & 3u;
    if (sel < 8u) { for (i = 0; i < 3u; ++i) out[i] = frac[sel]; return; }
    if (sel < 12u) { for (i = 0; i < 3u; ++i) out[i] = 0; return; }
    if (sel < 16u) {                                   /* K<n>.rgb */
        for (i = 0; i < 3u; ++i) out[i] = bp->konst[k][i];
        return;
    }
    /* 16-31: one channel of K<n>, splatted. 16 = red, 20 = green,
     * 24 = blue, 28 = alpha. */
    {
        unsigned ch = (sel - 16u) >> 2;
        int v = bp->konst[(sel - 16u) & 3u][ch == 3u ? 3 : (int)ch];
        for (i = 0; i < 3u; ++i) out[i] = v;
    }
}

static int konst_alpha(const MgsGxBp* bp, unsigned sel)
{
    static const int frac[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };
    if (sel < 8u) return frac[sel];
    if (sel < 16u) return 0;              /* 8-15 invalid for alpha */
    {
        unsigned ch = (sel - 16u) >> 2;
        return bp->konst[(sel - 16u) & 3u][ch == 3u ? 3 : (int)ch];
    }
}

void mgs_tev_compile(const MgsGxBp* bp, MgsTevCompiled* out)
{
    unsigned i, s;
    out->stages = stage_count(bp);
    if (out->stages > 16u) out->stages = 16u;
    out->configured = bp->written[BP_GEN_MODE] != 0;
    for (i = 0; i < 4u; ++i) tev_register(bp, i, out->reg[i]);
    for (s = 0; s < out->stages; ++s) {
        unsigned ks = (unsigned)mgs_bp_get(bp,
                          (uint8_t)(BP_TEV_KSEL + (s >> 1)));
        out->ce[s] = mgs_bp_get(bp, (uint8_t)(BP_TEV_COLOR_ENV + s * 2u));
        out->ae[s] = mgs_bp_get(bp, (uint8_t)(BP_TEV_ALPHA_ENV + s * 2u));
        konst_color(bp, (s & 1u) ? ((ks >> 14) & 0x1Fu) : ((ks >> 4) & 0x1Fu),
                    out->kc[s]);
        out->ka[s] = konst_alpha(bp,
                         (s & 1u) ? ((ks >> 19) & 0x1Fu)
                                  : ((ks >> 9) & 0x1Fu));
    }

    /* THE FOUR SWAP TABLES, and libogc is the reference, not Dolphin's
     * comment.
     *
     * Table n lives in two KSEL registers: `GX_SetTevSwapModeTable` writes
     * r,g to the EVEN one (regA = swapid*2) and b,a to the ODD one.
     * Dolphin's `TevKSel` comment says the opposite - "Odd ksel number: red;
     * even: blue" - and implementing THAT emptied the green channel across
     * the whole frame.
     *
     * The game settles it. Its eight KSEL registers decode, under libogc's
     * layout, to exactly the four tables `GXInit` installs:
     *
     *     table 0  (r,g,b,a) = R,G,B,A      identity
     *     table 1              R,R,R,A
     *     table 2              G,G,G,A
     *     table 3              B,B,B,A
     *
     * Under Dolphin's they decode to nothing meaningful. A reading that
     * reproduces the SDK's own initialisation is the right one.
     *
     * (A first attempt read these at 40M steps, before GXInit had finished,
     * saw all-zero swap fields, and concluded the game did not use them.
     * Sampling a register before the thing that writes it says nothing.) */
    out->swap_set = 0;
    for (i = 0; i < 8u; ++i)
        if (bp->written[BP_TEV_KSEL + i]) out->swap_set = 1;
    for (i = 0; i < 4u; ++i) {
        uint32_t ra = mgs_bp_get(bp, (uint8_t)(BP_TEV_KSEL + i * 2u));
        uint32_t ba = mgs_bp_get(bp, (uint8_t)(BP_TEV_KSEL + i * 2u + 1u));
        if (out->swap_set) {
            out->swap[i][0] = (unsigned)(ra & 3u);           /* red   */
            out->swap[i][1] = (unsigned)((ra >> 2) & 3u);    /* green */
            out->swap[i][2] = (unsigned)(ba & 3u);           /* blue  */
            out->swap[i][3] = (unsigned)((ba >> 2) & 3u);    /* alpha */
        } else {
            out->swap[i][0] = 0; out->swap[i][1] = 1;
            out->swap[i][2] = 2; out->swap[i][3] = 3;
        }
    }
}

/* Reorder one ARGB colour through a swap table. */
static uint32_t swap_argb(uint32_t c, const unsigned* tab)
{
    int ch[4];
    ch[0] = (int)((c >> 16) & 0xFFu);   /* red   */
    ch[1] = (int)((c >> 8) & 0xFFu);    /* green */
    ch[2] = (int)(c & 0xFFu);           /* blue  */
    ch[3] = (int)((c >> 24) & 0xFFu);   /* alpha */
    return ((uint32_t)ch[tab[3]] << 24) | ((uint32_t)ch[tab[0]] << 16) |
           ((uint32_t)ch[tab[1]] << 8)  | (uint32_t)ch[tab[2]];
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
        const int* konst_c = t->kc[s];
        int konst_a = t->ka[s];
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

        /* The swap tables reorder this stage's inputs. Skipped entirely when
         * both are the identity, which is the overwhelmingly common case and
         * the one that must not pay for this. */
        {
            const unsigned* ts = t->swap[(ae >> 2) & 3u];
            const unsigned* rs = t->swap[ae & 3u];
            if (ts[0] != 0u || ts[1] != 1u || ts[2] != 2u || ts[3] != 3u ||
                rs[0] != 0u || rs[1] != 1u || rs[2] != 2u || rs[3] != 3u) {
                if (cur != &sv) { sv = *in; cur = &sv; }
                sv.texture = swap_argb(sv.texture, ts);
                sv.raster  = swap_argb(sv.raster, rs);
            }
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

/* ONE IMPLEMENTATION, NOT TWO.
 *
 * This was a second copy of the stage loop that read its registers straight
 * from the BP state. Two copies of the same arithmetic drift: the compiled
 * one gained per-stage textures and the konst and swap work, and this one
 * silently did not - so a caller that used it got a combiner a generation
 * behind. It now compiles and runs the same path. Nothing calls it on a
 * per-pixel route; the rasteriser compiles once per draw. */
uint32_t mgs_tev_run(const MgsGxBp* bp, const MgsTevInput* in)
{
    MgsTevCompiled t;
    mgs_tev_compile(bp, &t);
    return mgs_tev_run_compiled(&t, in);
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
