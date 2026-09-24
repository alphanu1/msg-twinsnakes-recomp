/* The TEV combiner, on the GPU, INTERPRETED FROM UNIFORMS.
 *
 * WHY NOT A GENERATED SHADER. The design document's phase 3 called for a
 * TEV-to-shader generator: build GLSL from the combiner state, compile it
 * with shaderc at runtime, cache it by hash. This is the same combiner and
 * it is not generated - the state arrives in a uniform block and this shader
 * walks it. The reasons are specific, and the document is updated to match:
 *
 *   - The combiner state is UNIFORM ACROSS A DRAW. Every branch below is
 *     the same for every fragment in the draw, so the divergence a loop and
 *     a switch would normally cost is not paid: the warp takes one path.
 *   - It compiles at BUILD time with glslc, like the other two. No shaderc
 *     in the link, no GLSL compiler on the frame path, no cache to get
 *     wrong, and no first-appearance stall the moment the game writes a
 *     combination never seen before.
 *   - It is the SAME ARITHMETIC as runtime/gx/tev.c, in the same integer
 *     0-255 range, so the two can be compared pixel for pixel rather than
 *     approximately. That check is phase 3's exit criterion; a generated
 *     shader would have made it a comparison between two different
 *     implementations of the same idea.
 *
 * If a profile ever shows the interpretation costing real time, generating
 * is still open - but it would be an optimisation with a measurement behind
 * it, which is not what it was going to be.
 *
 * SET 2 for the sampler and SET 3 for this uniform block, which is what
 * SDL_CreateGPUShader's table says a FRAGMENT shader uses. See gx.frag.
 */
#version 450

layout(location = 0) in vec4 v_colour;
layout(location = 1) in vec2 v_uv;

layout(location = 0) out vec4 out_colour;

layout(set = 2, binding = 0) uniform sampler2D u_tex;

layout(set = 3, binding = 0) uniform TevState {
    ivec4 reg[4];     /* the four TEV registers as the draw starts, r,g,b,a */
    uvec4 env[16];    /* x = colour environment, y = alpha environment      */
    ivec4 konst[16];  /* xyz = the stage's konst colour, w = its konst alpha*/
    ivec4 swap[4];    /* the four swap tables, as source channel indices    */
    ivec4 ctl;        /* x stages, y configured, z has_texture, w swap_set  */
    ivec4 atest;      /* x ref0, y ref1, z op0, w op1                       */
    ivec4 atest2;     /* x logic, y enabled                                 */
} T;

/* The registers the stages chain through. A global because GLSL's array
 * parameters would copy them at every call, four times a stage. */
ivec4 reg[4];

int clamp255(int v) { return clamp(v, 0, 255); }

/* Line for line runtime/gx/tev.c's `combine`, including the divisions.
 * Signed integer division truncates toward zero in GLSL as it does in C, so
 * writing them as shifts would round differently for negative values - and
 * TEV registers are signed 11-bit, so negative values happen. */
int combine(int a, int b, int c, int d,
            uint op, uint bias, uint scale, uint clamp_out)
{
    int bias_v = (bias == 1u) ? 128 : ((bias == 2u) ? -128 : 0);
    int cc = c + (c >> 7);     /* 0-255 read as 0-256 so 255 is a full one */
    int v;

    v = d * 256 + a * (256 - cc) + b * cc;
    v = v / 256;

    if (op == 1u) v = d - (v - d);
    v += bias_v;

    if (scale == 1u)      v *= 2;
    else if (scale == 2u) v *= 4;
    else if (scale == 3u) v /= 2;

    return (clamp_out != 0u) ? clamp255(v) : v;
}

ivec3 color_input(uint sel, ivec4 tx, ivec4 ra, ivec3 kc)
{
    switch (sel) {
        case 0u:  return reg[0].rgb;         /* cprev */
        case 1u:  return ivec3(reg[0].a);    /* aprev */
        case 2u:  return reg[1].rgb;         /* c0    */
        case 3u:  return ivec3(reg[1].a);    /* a0    */
        case 4u:  return reg[2].rgb;         /* c1    */
        case 5u:  return ivec3(reg[2].a);    /* a1    */
        case 6u:  return reg[3].rgb;         /* c2    */
        case 7u:  return ivec3(reg[3].a);    /* a2    */
        case 8u:  return tx.rgb;             /* texc  */
        case 9u:  return ivec3(tx.a);        /* texa  */
        case 10u: return ra.rgb;             /* rasc  */
        case 11u: return ivec3(ra.a);        /* rasa  */
        case 12u: return ivec3(255);         /* one   */
        case 13u: return ivec3(128);         /* half  */
        case 14u: return kc;                 /* konst */
        default:  return ivec3(0);           /* zero  */
    }
}

int alpha_input(uint sel, ivec4 tx, ivec4 ra, int ka)
{
    switch (sel) {
        case 0u: return reg[0].a;
        case 1u: return reg[1].a;
        case 2u: return reg[2].a;
        case 3u: return reg[3].a;
        case 4u: return tx.a;
        case 5u: return ra.a;
        case 6u: return ka;
        default: return 0;
    }
}

/* Reorder one colour through a swap table. The table holds source channel
 * indices in r,g,b,a order, which is libogc's layout - see tev.c, where
 * following Dolphin's comment instead emptied the green channel. */
ivec4 swap_of(ivec4 c, ivec4 t)
{
    int ch[4] = int[4](c.r, c.g, c.b, c.a);
    return ivec4(ch[t.x], ch[t.y], ch[t.z], ch[t.w]);
}

bool atest_one(int a, int op, int ref)
{
    switch (op) {
        case 0: return false;
        case 1: return a <  ref;
        case 2: return a == ref;
        case 3: return a <= ref;
        case 4: return a >  ref;
        case 5: return a != ref;
        case 6: return a >= ref;
        default: return true;
    }
}

void main()
{
    vec4  t = texture(u_tex, v_uv);
    ivec4 texc = ivec4(round(t * 255.0));
    ivec4 rasc = ivec4(round(v_colour * 255.0));
    int s;

    /* GEN_MODE never written: the hardware's power-on default, which the SDK
     * describes as "modulate". Same fallback as the CPU path. */
    if (T.ctl.y == 0) {
        out_colour = (T.ctl.z != 0) ? v_colour * t : v_colour;
        return;
    }

    reg[0] = T.reg[0]; reg[1] = T.reg[1];
    reg[2] = T.reg[2]; reg[3] = T.reg[3];

    for (s = 0; s < T.ctl.x; ++s) {
        uint  ce = T.env[s].x, ae = T.env[s].y;
        ivec4 tc = texc, rc = rasc;
        ivec3 kc = T.konst[s].rgb;
        int   ka = T.konst[s].w;
        int   dst_c = int((ce >> 22) & 3u), dst_a = int((ae >> 22) & 3u);
        ivec3 a, b, c, d, oc;
        uint  op, bias, sc, cl;
        int   oa;

        if (T.ctl.w != 0) {
            tc = swap_of(texc, T.swap[(ae >> 2) & 3u]);
            rc = swap_of(rasc, T.swap[ae & 3u]);
        }


        a = color_input((ce >> 12) & 0xFu, tc, rc, kc);
        b = color_input((ce >>  8) & 0xFu, tc, rc, kc);
        c = color_input((ce >>  4) & 0xFu, tc, rc, kc);
        d = color_input((ce      ) & 0xFu, tc, rc, kc);

        op = (ce >> 18) & 1u; bias = (ce >> 16) & 3u;
        sc = (ce >> 20) & 3u; cl   = (ce >> 19) & 1u;
        oc.x = combine(a.x, b.x, c.x, d.x, op, bias, sc, cl);
        oc.y = combine(a.y, b.y, c.y, d.y, op, bias, sc, cl);
        oc.z = combine(a.z, b.z, c.z, d.z, op, bias, sc, cl);

        oa = combine(alpha_input((ae >> 13) & 7u, tc, rc, ka),
                     alpha_input((ae >> 10) & 7u, tc, rc, ka),
                     alpha_input((ae >>  7) & 7u, tc, rc, ka),
                     alpha_input((ae >>  4) & 7u, tc, rc, ka),
                     (ae >> 18) & 1u, (ae >> 16) & 3u,
                     (ae >> 20) & 3u, (ae >> 19) & 1u);

        reg[dst_c].rgb = oc;
        reg[dst_a].a   = oa;
    }

    {
        ivec4 fin = ivec4(clamp255(reg[0].r), clamp255(reg[0].g),
                          clamp255(reg[0].b), clamp255(reg[0].a));
        if (T.atest2.y != 0) {
            bool r0 = atest_one(fin.a, T.atest.z, T.atest.x);
            bool r1 = atest_one(fin.a, T.atest.w, T.atest.y);
            bool pass;
            switch (T.atest2.x) {
                case 0:  pass = r0 && r1; break;
                case 1:  pass = r0 || r1; break;
                case 2:  pass = r0 != r1; break;
                default: pass = r0 == r1; break;
            }
            if (!pass) discard;
        }
        out_colour = vec4(fin) / 255.0;
    }
}
