#include "fifo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile const char* mgs_gx_phase = "idle";

/* ---- register decoding ------------------------------------------------ */

/* The vertex descriptor, CP 0x50 and 0x60. Two bits per attribute for the
 * ones that can be indexed, one bit for the matrix indices - which are
 * present or absent and never indexed. */
static MgsGxAttrKind vcd_kind(const MgsGx* gx, MgsGxAttr a)
{
    if (a <= GX_VA_TEX7MTXIDX)
        return ((gx->vcd_lo >> (unsigned)a) & 1u) ? GX_ATTR_DIRECT : GX_ATTR_NONE;
    if (a == GX_VA_POS)  return (MgsGxAttrKind)((gx->vcd_lo >> 9) & 3u);
    if (a == GX_VA_NRM)  return (MgsGxAttrKind)((gx->vcd_lo >> 11) & 3u);
    if (a == GX_VA_CLR0) return (MgsGxAttrKind)((gx->vcd_lo >> 13) & 3u);
    if (a == GX_VA_CLR1) return (MgsGxAttrKind)((gx->vcd_lo >> 15) & 3u);
    return (MgsGxAttrKind)((gx->vcd_hi >> (2u * (unsigned)(a - GX_VA_TEX0))) & 3u);
}

void mgs_gx_vertex_format(const MgsGx* gx, unsigned vat, MgsGxVertexFormat* out)
{
    uint32_t a, b, c;
    unsigned i;

    memset(out, 0, sizeof *out);
    vat &= 7u;
    a = gx->vat_a[vat]; b = gx->vat_b[vat]; c = gx->vat_c[vat];

    for (i = 0; i < GX_VA_COUNT; ++i)
        out->kind[i] = vcd_kind(gx, (MgsGxAttr)i);

    out->pos_count  = ((a >> 0) & 1u) ? 3u : 2u;
    out->pos_format = (a >> 1) & 7u;
    out->pos_shift  = (a >> 4) & 0x1Fu;
    out->nrm_count  = ((a >> 9) & 1u) ? 3u : 1u;   /* 1 = one normal, 3 = nbt */
    out->nrm_format = (a >> 10) & 7u;
    out->clr_count[0]  = ((a >> 13) & 1u) ? 1u : 0u;
    out->clr_format[0] = (a >> 14) & 7u;
    out->clr_count[1]  = ((a >> 17) & 1u) ? 1u : 0u;
    out->clr_format[1] = (a >> 18) & 7u;

    out->tex_count[0]  = ((a >> 21) & 1u) ? 2u : 1u;
    out->tex_format[0] = (a >> 22) & 7u;
    out->tex_shift[0]  = (a >> 25) & 0x1Fu;

    for (i = 1; i < 4u; ++i) {
        unsigned s = (i - 1u) * 9u;
        out->tex_count[i]  = ((b >> s) & 1u) ? 2u : 1u;
        out->tex_format[i] = (b >> (s + 1u)) & 7u;
        out->tex_shift[i]  = (b >> (s + 2u)) & 0x1Fu;
    }
    /* Texture 4 straddles the two registers: its low bits are the top of B
     * and its high bits the bottom of C. Reading it from one alone gives a
     * plausible wrong answer rather than an obvious one. */
    out->tex_count[4]  = ((b >> 27) & 1u) ? 2u : 1u;
    out->tex_format[4] = (b >> 28) & 7u;
    out->tex_shift[4]  = ((b >> 31) & 1u) | (((c >> 0) & 0xFu) << 1);
    for (i = 5; i < 8u; ++i) {
        unsigned s = 4u + (i - 5u) * 9u;
        out->tex_count[i]  = ((c >> s) & 1u) ? 2u : 1u;
        out->tex_format[i] = (c >> (s + 1u)) & 7u;
        out->tex_shift[i]  = (c >> (s + 2u)) & 0x1Fu;
    }
}

/* Bytes one component occupies. Format 4 is f32; 0-3 are u8/s8/u16/s16. */
static unsigned comp_size(unsigned format)
{
    switch (format) {
        case 0: case 1: return 1u;
        case 2: case 3: return 2u;
        case 4:         return 4u;
        default:        return 0u;     /* not a size we can claim to know */
    }
}

static unsigned color_size(unsigned format)
{
    switch (format) {
        case 0: return 2u;   /* rgb565 */
        case 1: return 3u;   /* rgb888 */
        case 2: return 4u;   /* rgb888x */
        case 3: return 2u;   /* rgba4444 */
        case 4: return 3u;   /* rgba6666 */
        case 5: return 4u;   /* rgba8888 */
        default: return 0u;
    }
}

unsigned mgs_gx_vertex_size(const MgsGxVertexFormat* f)
{
    unsigned n = 0, i, c;

    for (i = 0; i < GX_VA_COUNT; ++i) {
        switch (f->kind[i]) {
            case GX_ATTR_NONE:    continue;
            case GX_ATTR_INDEX8:  n += 1u; continue;
            case GX_ATTR_INDEX16: n += 2u; continue;
            case GX_ATTR_DIRECT:  break;
        }

        if (i <= GX_VA_TEX7MTXIDX) { n += 1u; continue; }

        if (i == GX_VA_POS) {
            c = comp_size(f->pos_format);
            if (!c) return 0u;
            n += c * f->pos_count;
        } else if (i == GX_VA_NRM) {
            c = comp_size(f->nrm_format);
            if (!c) return 0u;
            n += c * (f->nrm_count == 3u ? 9u : 3u);
        } else if (i == GX_VA_CLR0 || i == GX_VA_CLR1) {
            c = color_size(f->clr_format[i - GX_VA_CLR0]);
            if (!c) return 0u;
            n += c;
        } else {
            unsigned t = (unsigned)(i - GX_VA_TEX0);
            c = comp_size(f->tex_format[t]);
            if (!c) return 0u;
            n += c * f->tex_count[t];
        }
    }
    return n;
}

/* ---- the parser ------------------------------------------------------- */

void mgs_gx_init(MgsGx* gx, GuestMemory* mem)
{
    memset(gx, 0, sizeof *gx);
    gx->mem = mem;
    mgs_bp_init(&gx->bp);
    {
        const char* e = getenv("MGS_TRACE_GXDESYNC");
        gx->trace_desync = e ? strtoull(e, NULL, 0) : 0u;
        e = getenv("MGS_TRACE_GXCP");
        gx->trace_cp = e ? strtoull(e, NULL, 0) : 0u;
        e = getenv("MGS_TRACE_GXDRAW");
        gx->trace_draw = e ? strtoull(e, NULL, 0) : 0u;
        e = getenv("MGS_TRACE_GXBYTES");
        if (e) {
            gx->trace_bytes_from = strtoull(e, (char**)&e, 0);
            if (*e == ',') gx->trace_bytes_n = strtoull(e + 1, NULL, 0);
        }
        e = getenv("MGS_TRACE_GXWINDOW");
        if (e) {
            gx->trace_win_from = strtoull(e, (char**)&e, 0);
            if (*e == ',') gx->trace_win_n = strtoull(e + 1, NULL, 0);
        }
    }
}

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Say WHY the stream was lost, for the first few times it happens.
 *
 * A desync count alone is not actionable: 841,627,908 of them says the
 * parser is manufacturing noise, and nothing about where it started. Only
 * the FIRST few matter - everything after the stream is lost is a
 * consequence - so this prints a bounded number and then goes quiet.
 *
 * The state printed is what decides a command's length: the opcode, the
 * vertex descriptor, and the attribute table the opcode selects. A draw
 * command sized wrongly consumes the wrong number of bytes, and from then
 * on every byte is read at the wrong offset.
 */
static void desync(MgsGx* gx, const char* why, uint8_t op, unsigned detail)
{
    ++gx->desyncs;
    if (!gx->trace_desync || gx->desyncs > gx->trace_desync) return;
    fprintf(stderr,
            "[gx] desync %llu: %s  op=0x%02X fmt=%u prim=%u detail=%u  "
            "vcd=%08X/%08X vat=%08X/%08X/%08X  cmds=%llu tris=%llu\n",
            (unsigned long long)gx->desyncs, why, op, op & 7u,
            (op >> 3) & 7u, detail,
            gx->vcd_lo, gx->vcd_hi,
            gx->vat_a[op & 7u], gx->vat_b[op & 7u], gx->vat_c[op & 7u],
            (unsigned long long)gx->commands, (unsigned long long)gx->triangles);
}

static void cp_write(MgsGx* gx, uint8_t reg, uint32_t value)
{
    /* MGS_TRACE_GXCP=N shows the first N command-processor writes.
     *
     * These decide how long every draw command is, so a missed or misread
     * one makes the parser consume the wrong number of bytes per vertex -
     * which looks like the game drawing nonsense rather than like a parsing
     * fault. Tracing them is the only way to tell "the game never set this"
     * from "we never saw it". */
    /* The VERTEX DESCRIPTOR registers unconditionally when tracing, because
     * they are rare and they are the ones that decide every draw's length.
     * Gating them on a count of commands hides exactly the case worth
     * seeing: a descriptor written once, thousands of commands in. */
    if (gx->trace_cp) {
        if (reg == 0x50u || reg == 0x60u || (reg & 0xF8u) == 0x70u)
            fprintf(stderr, "[gx] cp  reg=0x%02X value=0x%08X  (cmd %llu)\n",
                    reg, value, (unsigned long long)gx->commands);
    }

    if (reg == 0x50u) gx->vcd_lo = value;
    else if (reg == 0x60u) gx->vcd_hi = value;
    else if ((reg & 0xF8u) == 0x70u) gx->vat_a[reg & 7u] = value;
    else if ((reg & 0xF8u) == 0x80u) gx->vat_b[reg & 7u] = value;
    else if ((reg & 0xF8u) == 0x90u) gx->vat_c[reg & 7u] = value;
    else if ((reg & 0xF0u) == 0xA0u) gx->array_base[reg & 0xFu] = value;
    else if ((reg & 0xF0u) == 0xB0u) gx->array_stride[reg & 0xFu] = value & 0xFFu;
    else if (reg == 0x30u) gx->cp_matrix_index_a = value;
    else if (reg == 0x40u) gx->cp_matrix_index_b = value;
}

static void xf_write(MgsGx* gx, uint32_t addr, const uint32_t* words, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; ++i) {
        uint32_t a = addr + i;
        float f;
        memcpy(&f, &words[i], sizeof f);
        if (a < 64u * 4u) gx->xf_matrix[a] = f;
        else if (a >= 0x0400u && a < 0x0400u + 32u * 3u) gx->xf_normal[a - 0x0400u] = f;
        else if (a >= 0x101Au && a < 0x1020u) gx->viewport[a - 0x101Au] = words[i];
        /* The projection is SIX floats followed by a type word, not seven
         * floats. Reading the type as a float puts a denormal in the last
         * coefficient and leaves the perspective/orthographic choice unset,
         * which is enough to send every vertex behind the eye. */
        else if (a >= 0x1020u && a < 0x1026u) gx->xf_projection[a - 0x1020u] = f;
        else if (a == 0x1026u) gx->xf_projection_ortho = words[i] & 1u;
    }
}

static void run_dl(MgsGx* gx, uint32_t addr, uint32_t size);

/* The handful of blitting-processor registers that DO something rather than
 * merely configure something. A palette load is the only one here: it names a
 * main-memory address and a destination in texture memory, and the pair has
 * to be remembered together because they arrive as two separate writes. */
static void bp_side_effect(MgsGx* gx, uint8_t reg, uint32_t val)
{
    if (reg == BP_LOAD_TLUT0) {
        /* The address, in 32-byte units, as everything in the command stream
         * is. Using it unshifted lands 32 times too low. */
        gx->bp.pending_tlut_addr = 0x80000000u | ((val & 0x00FFFFFFu) << 5);
        return;
    }
    if (reg == BP_LOAD_TLUT1) {
        unsigned offset = val & 0x3FFu;
        gx->bp.tlut_src[offset] = gx->bp.pending_tlut_addr;
        return;
    }
    if (reg == 0x45u) {
        /* The draw-done token. Bit 1 asks for the finish interrupt; the
         * SETDRAWSYNC form uses register 0x47 and a different wakeup. */
        if (val & 0x2u) ++gx->draw_done_tokens;
        return;
    }
    if (reg == BP_COPY_EXECUTE) {
        gx->copy_pending = val | 0x80000000u;
        return;
    }
}

/* How many bytes the command that starts with `op` needs, beyond the opcode.
 * Returns 0 when the length is not yet knowable - a draw command's size
 * depends on a vertex count that has not arrived. */
static unsigned command_length(const MgsGx* gx, uint8_t op, const uint8_t* body,
                               unsigned have, int* need_more)
{
    *need_more = 0;

    if (op == GX_OP_NOP) return 0u;
    if (op == GX_OP_LOAD_CP) return 5u;                  /* reg + u32 */
    if (op == GX_OP_LOAD_BP) return 4u;                  /* reg:u32 packed */
    if (op == GX_OP_LOAD_XF) {
        if (have < 4u) { *need_more = 1; return 0u; }
        return 4u + 4u * (((be32(body) >> 16) & 0xFu) + 1u);
    }
    if (op == GX_OP_LOAD_INDX_A || op == GX_OP_LOAD_INDX_B ||
        op == GX_OP_LOAD_INDX_C || op == GX_OP_LOAD_INDX_D) return 4u;
    if (op == GX_OP_CALL_DL) return 8u;
    if (op == GX_OP_INVL_VC) return 0u;

    if (op >= GX_OP_DRAW_FIRST && op <= GX_OP_DRAW_LAST + 7u) {
        MgsGxVertexFormat f;
        unsigned count, vsize;
        if (have < 2u) { *need_more = 1; return 0u; }
        count = ((unsigned)body[0] << 8) | body[1];
        mgs_gx_vertex_format(gx, op & 7u, &f);
        vsize = mgs_gx_vertex_size(&f);
        if (!vsize && count) return 0u;                  /* refuse to guess */
        return 2u + count * vsize;
    }
    return 0u;
}

static void emit_primitive(MgsGx* gx, uint8_t op, const uint8_t* body, unsigned len);

static void dispatch(MgsGx* gx, uint8_t op, const uint8_t* body, unsigned len)
{
    ++gx->commands;

    /* MGS_TRACE_GXWINDOW=first,count prints every command in a range.
     *
     * The question this answers is "what happened between these two points",
     * and nothing else can: the per-kind traces each show their own kind and
     * so cannot show what is MISSING. The vertex descriptor in force at the
     * sphere was the logo's, and the only way to see why is to look at every
     * command in between rather than at the ones already known about. */
    if (gx->trace_win_n &&
        gx->commands >= gx->trace_win_from &&
        gx->commands < gx->trace_win_from + gx->trace_win_n) {
        fprintf(stderr, "[gx] %6llu @%08llu  op=0x%02X len=%-6u",
                (unsigned long long)gx->commands,
                (unsigned long long)gx->stream_pos, op, len);
        if (op == GX_OP_LOAD_CP)
            fprintf(stderr, "  CP  reg=0x%02X val=0x%08X",
                    body[0], be32(body + 1));
        else if (op == GX_OP_LOAD_BP)
            fprintf(stderr, "  BP  reg=0x%02X val=0x%06X",
                    (unsigned)(be32(body) >> 24), be32(body) & 0x00FFFFFFu);
        else if (op == GX_OP_LOAD_XF)
            fprintf(stderr, "  XF  addr=0x%04X n=%u",
                    (unsigned)(be32(body) & 0xFFFFu),
                    (unsigned)(((be32(body) >> 16) & 0xFu) + 1u));
        else if (op == GX_OP_CALL_DL)
            fprintf(stderr, "  DL  addr=0x%08X size=%u", be32(body), be32(body + 4));
        else if (op >= GX_OP_DRAW_FIRST)
            fprintf(stderr, "  DRAW fmt=%u count=%u",
                    op & 7u, ((unsigned)body[0] << 8) | body[1]);
        fprintf(stderr, "\n");
    }

    if (op == GX_OP_LOAD_CP) { cp_write(gx, body[0], be32(body + 1)); return; }
    if (op == GX_OP_LOAD_XF) {
        uint32_t head = be32(body);
        unsigned n = ((head >> 16) & 0xFu) + 1u;
        uint32_t words[16];
        unsigned i;
        for (i = 0; i < n && i < 16u; ++i) words[i] = be32(body + 4u + i * 4u);
        xf_write(gx, head & 0xFFFFu, words, n < 16u ? n : 16u);
        return;
    }
    if (op == GX_OP_CALL_DL) { run_dl(gx, be32(body), be32(body + 4)); return; }
    if (op == GX_OP_LOAD_BP) {
        uint32_t packed = be32(body);
        uint8_t  reg = (uint8_t)(packed >> 24);
        uint32_t val = packed & 0x00FFFFFFu;
        mgs_bp_write(&gx->bp, reg, val);
        bp_side_effect(gx, reg, val);
        return;
    }
    if (op >= GX_OP_DRAW_FIRST) emit_primitive(gx, op, body, len);
    /* Index loads address transform-unit memory the renderer does not use
     * yet; counted, not acted on. */
}

/* Feed a whole buffer. Shared by the pipe and by display lists, so a command
 * split across a display-list boundary behaves the same as one split across
 * two pipe writes. */
static void feed(MgsGx* gx, const uint8_t* data, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; ++i) {
        if (!gx->want && !gx->have) {
            gx->opcode = data[i];
            if (gx->opcode == GX_OP_NOP || gx->opcode == GX_OP_INVL_VC) {
                ++gx->commands;
                continue;
            }
            gx->want = 1u;                      /* body length not known yet */
            continue;
        }

        if (gx->have >= sizeof gx->buf) {       /* a vertex run longer than
                                                 * the staging buffer */
            desync(gx, "command longer than the staging buffer",
                   gx->opcode, gx->have);
            gx->have = gx->want = 0u;
            continue;
        }
        gx->buf[gx->have++] = data[i];

        {
            int need_more = 0;
            unsigned len = command_length(gx, gx->opcode, gx->buf, gx->have,
                                          &need_more);
            if (need_more) continue;
            if (!len && gx->opcode >= GX_OP_DRAW_FIRST) {
                /* Unsizable draw: the stream cannot be followed from here. */
                desync(gx, "draw command cannot be sized", gx->opcode, gx->have);
                gx->have = gx->want = 0u;
                continue;
            }
            if (len > sizeof gx->buf) {
                /* A legitimate but very long vertex run. Dropped rather than
                 * mis-parsed, and counted so it is visible. */
                desync(gx, "vertex run longer than the staging buffer",
                       gx->opcode, len);
                gx->have = gx->want = 0u;
                continue;
            }
            /* THE REST OF THE COMMAND IN ONE GO.
             *
             * `command_length` is a pure function of the opcode and the
             * bytes already seen, and once it has returned a definite
             * length that length cannot change - a draw command's size is
             * fixed by the vertex count, which is in the first two body
             * bytes. So re-deriving it for every remaining byte is pure
             * waste, and for the sizes the engine actually sends it is the
             * dominant cost in the parser: a strip of 66 vertices is around
             * 1,500 bytes, which meant 1,500 calls instead of three.
             *
             * The bytes land in the buffer in the same order either way, so
             * this changes nothing about what is parsed. */
            if (gx->have < len) {
                unsigned need  = len - gx->have;
                unsigned avail = n - (i + 1u);
                unsigned take  = need < avail ? need : avail;
                if (take) {
                    memcpy(gx->buf + gx->have, data + i + 1u, take);
                    gx->have += take;
                    i += take;
                }
            }

            if (gx->have >= len) {
                dispatch(gx, gx->opcode, gx->buf, len);
                gx->have = gx->want = 0u;

                /* Once per COMMAND, which is the granularity that matters:
                 * a display list is a single write from the guest's point of
                 * view, so without a check here the host cannot be stopped
                 * for as long as the list takes - and the engine's lists run
                 * for minutes. The rasteriser's own checks do not help,
                 * because most of that time is spent in this parser. */
                if (gx->abandon && gx->abandon()) return;
            }
        }
    }
}

static void run_dl(MgsGx* gx, uint32_t addr, uint32_t size)
{
    const uint8_t* p;

    /* Display lists nest. Four deep is well past anything a game does and
     * turns a corrupt pointer into a refusal rather than a stack overflow. */
    if (gx->dl_depth >= 4u || !size) return;

    /* WHAT THE HARDWARE REQUIRES, used here as a validity check.
     *
     * `GXCallDisplayList` takes a 32-byte-aligned address and a 32-byte
     * multiple of a size - the command processor fetches display lists in
     * 32-byte units and the SDK asserts on both. A list that satisfies
     * neither did not come from the game; it came from this parser reading a
     * byte of vertex data as an opcode.
     *
     * Checking matters far more than it looks. A bogus call is not one bad
     * command, it is an ARBITRARY REGION OF MEMORY fed back through the
     * parser - and a region of zeroes parses as one NOP per byte. That is
     * how a single lost byte turned into 23,157,036,840 "commands" and
     * 841,627,908 desyncs: the stream desynchronised once, read garbage as a
     * display-list call, and executed megabytes of whatever was there. */
    if ((addr & 0x1Fu) || (size & 0x1Fu)) {
        desync(gx, "display list is not 32-byte aligned",
               GX_OP_CALL_DL, size & 0x1Fu);
        return;
    }

    p = guest_ptr(gx->mem, addr, size);
    if (!p) {
        desync(gx, "display list is not in mapped memory", GX_OP_CALL_DL, size);
        return;
    }

    ++gx->dl_depth;
    {
        /* A display list is its own stream: a command cannot straddle its
         * end, and carrying parser state across would corrupt the caller's. */
        uint8_t  saved_buf_op = gx->opcode;
        unsigned saved_have = gx->have, saved_want = gx->want;
        /* Only the bytes actually held, not the whole buffer. The buffer is
         * now 64 KB and this runs on every display-list call, so copying all
         * of it would cost more than the list usually does - and everything
         * past `have` is stale in any case. */
        uint8_t* saved = gx->dl_save;
        if (saved_have > sizeof gx->dl_save) saved_have = sizeof gx->dl_save;
        memcpy(saved, gx->buf, saved_have);
        gx->have = gx->want = 0u;

        feed(gx, p, size);

        memcpy(gx->buf, saved, saved_have);
        gx->opcode = saved_buf_op;
        gx->have = saved_have;
        gx->want = saved_want;
    }
    --gx->dl_depth;
}

void mgs_gx_write(MgsGx* gx, uint32_t value, unsigned size)
{
    uint8_t b[4];
    unsigned i;

    /* MGS_TRACE_GXBYTES=first,count dumps the RAW stream by byte position,
     * before any interpretation.
     *
     * Every other trace here reports what the parser made of the bytes, and
     * when the parser's reading is the thing in doubt that is circular. The
     * vertex descriptor says this game's sphere vertex is 20 bytes and the
     * bytes repeat every 24; only the undecoded stream can say which. */
    if (gx->trace_bytes_n &&
        gx->stream_pos >= gx->trace_bytes_from &&
        gx->stream_pos < gx->trace_bytes_from + gx->trace_bytes_n) {
        unsigned k;
        for (k = 0; k < size; ++k) {
            if (((gx->stream_pos + k) & 15u) == 0u)
                fprintf(stderr, "\n[gx] %08llu ",
                        (unsigned long long)(gx->stream_pos + k));
            fprintf(stderr, " %02X",
                    (unsigned)((value >> (8u * (size - 1u - k))) & 0xFFu));
        }
    }
    gx->stream_pos += size;

    /* Set on the way in and CLEARED ON THE WAY OUT. A marker that is only
     * ever set says what happened last, not what is happening now, and that
     * is actively misleading: a wedged run reported "raster" when the
     * rasteriser had finished long before and the host was in translated
     * code. Clearing it here means "raster" means still in the rasteriser. */
    mgs_gx_phase = "gx-parse";

    if (size > 4u) size = 4u;
    for (i = 0; i < size; ++i)
        b[i] = (uint8_t)(value >> (8u * (size - 1u - i)));   /* big-endian */
    feed(gx, b, size);

    mgs_gx_phase = "idle";
}

/* ---- primitives -------------------------------------------------------- */

unsigned mgs_gx_decode_vertex(const MgsGx* gx, const MgsGxVertexFormat* f,
                              const uint8_t* data, unsigned n, MgsGxVertex* v);

/* Expand whatever the game sent into triangles.
 *
 * Strips, fans and quads are conveniences for the game and a liability for
 * everything downstream, so they stop here. The WINDING matters and is the
 * easy thing to get wrong: a triangle strip alternates orientation, so every
 * second triangle has two of its vertices swapped. Emitting them all the same
 * way round makes back-face culling remove exactly the wrong half of every
 * model - which looks like missing geometry rather than a winding bug.
 */
static void emit_primitive(MgsGx* gx, uint8_t op, const uint8_t* body, unsigned len)
{
    MgsGxVertexFormat f;
    MgsGxVertex v[3];
    MgsGxVertex quad[4];
    MgsGxVertex first, prev;
    unsigned count, vsize, i, at;
    MgsGxPrim prim = (MgsGxPrim)((op >> 3) & 7u);
    unsigned have = 0;

    count = ((unsigned)body[0] << 8) | body[1];
    if (!count) return;

    mgs_gx_vertex_format(gx, op & 7u, &f);
    vsize = mgs_gx_vertex_size(&f);

    if (gx->trace_draw && gx->primitives < gx->trace_draw) {
        unsigned k;
        fprintf(stderr, "[gx] draw op=0x%02X prim=%u fmt=%u count=%u "
                        "vsize=%u len=%u  vcd=%08X/%08X vat=%08X\n",
                op, (unsigned)prim, op & 7u, count, vsize, len,
                gx->vcd_lo, gx->vcd_hi, gx->vat_a[op & 7u]);
        /* THE BYTES THEMSELVES, as floats.
         *
         * The descriptor says this vertex is 20 bytes and the code that
         * writes it emits 24, and the two cannot both be right. Printing the
         * actual stream settles which: a run of plausible coordinates ending
         * where the descriptor says it should is one answer, and a fourth
         * and fifth float where the next vertex ought to start is the other.
         * Inferring it from register layouts had already produced two
         * confident and opposite readings. */
        for (k = 0; k + 4u <= 48u && 2u + k + 4u <= len; k += 4u) {
            uint32_t bits = be32(body + 2u + k);
            float f;
            memcpy(&f, &bits, sizeof f);
            fprintf(stderr, "      +%02u  %08X  % .4f%s", k, bits, (double)f,
                    ((k / 4u) % 3u == 2u) ? "\n" : "");
        }
        fprintf(stderr, "\n");
    }
    if (!vsize) { desync(gx, "vertex size is zero", op, 0u); return; }

    ++gx->primitives;
    at = 2u;

    for (i = 0; i < count; ++i) {
        MgsGxVertex cur;
        unsigned used;

        if (at + vsize > len) break;
        used = mgs_gx_decode_vertex(gx, &f, body + at, vsize, &cur);
        if (used != vsize) {
            desync(gx, "vertex decoded to a different size", op, used);
            return;
        }
        at += vsize;
        ++gx->vertices;

        if (!gx->triangle) continue;

        switch (prim) {
            case GX_PRIM_TRIANGLES:
                v[have++] = cur;
                if (have == 3u) {
                    gx->triangle(gx, &v[0], &v[1], &v[2]);
                    ++gx->triangles;
                    have = 0;
                }
                break;

            case GX_PRIM_TRIANGLE_STRIP:
                if (i < 2u) { v[i] = cur; break; }
                /* Odd triangles are wound the other way. */
                if (i & 1u) gx->triangle(gx, &v[1], &v[0], &cur);
                else        gx->triangle(gx, &v[0], &v[1], &cur);
                ++gx->triangles;
                v[0] = v[1]; v[1] = cur;
                break;

            case GX_PRIM_TRIANGLE_FAN:
                if (i == 0u) { first = cur; break; }
                if (i == 1u) { prev = cur; break; }
                gx->triangle(gx, &first, &prev, &cur);
                ++gx->triangles;
                prev = cur;
                break;

            case GX_PRIM_QUADS:
            case GX_PRIM_QUADS2:
                /* Four vertices, THEN two triangles: 0-1-2 and 0-2-3. All
                 * four have to be held, because the second triangle needs
                 * vertex 0 again - reusing a three-vertex buffer overwrites
                 * it with the fourth and emits a degenerate triangle, which
                 * the rasteriser silently discards as zero-area. Half of
                 * every quad then disappears, and the half that survives
                 * looks correct. */
                quad[i & 3u] = cur;
                if ((i & 3u) == 3u) {
                    gx->triangle(gx, &quad[0], &quad[1], &quad[2]);
                    gx->triangle(gx, &quad[0], &quad[2], &quad[3]);
                    gx->triangles += 2u;
                }
                break;

            default:
                /* Lines and points do not become triangles. Counted so the
                 * stream stays accounted for, and otherwise left alone. */
                break;
        }
    }
}

int mgs_gx_take_copy(MgsGx* gx, uint32_t* cmd)
{
    if (!gx || !(gx->copy_pending & 0x80000000u)) return 0;
    if (cmd) *cmd = gx->copy_pending & 0x00FFFFFFu;
    gx->copy_pending = 0u;
    return 1;
}

int mgs_gx_take_draw_done(MgsGx* gx)
{
    if (!gx || !gx->draw_done_tokens) return 0;
    --gx->draw_done_tokens;
    return 1;
}
