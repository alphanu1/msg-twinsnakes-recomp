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
    /* VAT_A bit 31, NormalIndex3. When the normal is INDEXED and this is set,
     * the vertex carries THREE indices - normal, binormal, tangent - not one.
     * Reading bits 0..30 and stopping makes every such vertex short by two
     * indices, which ends the vertex run early and leaves the rest of it to be
     * parsed as commands. */
    out->nrm_index3 = (a >> 31) & 1u;
    out->nrm_format = (a >> 10) & 7u;
    out->clr_count[0]  = ((a >> 13) & 1u) ? 1u : 0u;
    out->clr_format[0] = (a >> 14) & 7u;
    out->clr_count[1]  = ((a >> 17) & 1u) ? 1u : 0u;
    out->clr_format[1] = (a >> 18) & 7u;

    out->tex_count[0]  = ((a >> 21) & 1u) ? 2u : 1u;
    out->tex_format[0] = (a >> 22) & 7u;
    out->tex_shift[0]  = (a >> 25) & 0x1Fu;

    /* THE FRACTION SITS AFTER THE FORMAT, AND THE FORMAT IS THREE BITS.
     *
     * Each texture coordinate occupies nine bits of the table: one for the
     * component count, three for the numeric format, five for the number of
     * FRACTIONAL BITS. So within a group the fraction begins four bits in,
     * not two - reading it at two overlaps the format field and returns a
     * shift built from the wrong bits.
     *
     * Only coordinate 0 escaped, because VAT_A spells its fields out and
     * they were written literally (22..24 format, 25..29 fraction). The
     * other seven were generated from a stride and were all wrong, and
     * coordinates 1, 2 and 3 are the ones this game uses most - 20.9M,
     * 19.4M and 20.9M vertices against 10.6M for coordinate 0.
     *
     * The fraction is a power-of-two divisor, so getting it wrong scales
     * every coordinate by a power of two. Too large, they run off the end
     * of the texture and CLAMP, and every triangle samples the same corner
     * texel: the models draw in one flat colour, which is exactly how this
     * presented. Bit layout from Dolphin's CPMemory.h (rule 10).
     */
    for (i = 1; i < 4u; ++i) {
        unsigned s = (i - 1u) * 9u;               /* 0, 9, 18 */
        out->tex_count[i]  = ((b >> s) & 1u) ? 2u : 1u;
        out->tex_format[i] = (b >> (s + 1u)) & 7u;
        out->tex_shift[i]  = (b >> (s + 4u)) & 0x1Fu;
    }
    /* Coordinate 4 is split ACROSS the two registers, but not in the middle
     * of a field: VAT_B ends with its count and format (bit 31 is
     * VCacheEnhance, nothing to do with it) and VAT_C BEGINS with its five
     * fraction bits. The old reading took VAT_B's bit 31 as the fraction's
     * low bit and shifted VAT_C's up by one, so it mixed the cache flag
     * into the scale and dropped the fraction's top bit. */
    out->tex_count[4]  = ((b >> 27) & 1u) ? 2u : 1u;
    out->tex_format[4] = (b >> 28) & 7u;
    out->tex_shift[4]  = c & 0x1Fu;               /* VAT_C bits 0..4 */
    for (i = 5; i < 8u; ++i) {
        unsigned s = 5u + (i - 5u) * 9u;          /* 5, 14, 23 */
        out->tex_count[i]  = ((c >> s) & 1u) ? 2u : 1u;
        out->tex_format[i] = (c >> (s + 1u)) & 7u;
        out->tex_shift[i]  = (c >> (s + 4u)) & 0x1Fu;
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
            case GX_ATTR_INDEX8:
                n += (i == GX_VA_NRM && f->nrm_index3) ? 3u : 1u;
                continue;
            case GX_ATTR_INDEX16:
                n += (i == GX_VA_NRM && f->nrm_index3) ? 6u : 2u;
                continue;
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
    {   /* The transform unit as GXInit leaves it: output i from input
         * TEXi through a 2x4 matrix, post-matrix GX_PTIDENTITY (row 61),
         * dual texture off. */
        unsigned i;
        for (i = 0; i < 8u; ++i) {
            gx->xf_texgen[i]   = (5u + i) << 7;
            gx->xf_postinfo[i] = 61u;
        }
        gx->xf_num_texgen = 8u;
        gx->xf_post[61 * 4 + 0] = 1.0f;
        gx->xf_post[62 * 4 + 1] = 1.0f;
        gx->xf_post[63 * 4 + 2] = 1.0f;
    }
    {
        const char* e;
        gx->trace_teximg = getenv("MGS_TRACE_TEXIMG") != NULL;
        e = getenv("MGS_TRACE_DLADDR");
        gx->trace_dl_addr = e ? (uint32_t)strtoull(e, NULL, 0) : 0u;
        e = getenv("MGS_TRACE_GXDESYNC");
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

    /* WHICH desync, not how many. 388,030 of them with one instance examined
     * says nothing about the other 388,029: the first one found need not be
     * the common one, and fixing it moved the total by 0.5%. Counting by
     * reason says where the mass actually is. Reasons are string literals, so
     * the pointer identifies them. */
    {
        unsigned k;
        for (k = 0; k < gx->why_n; ++k)
            if (gx->why_key[k] == why) { ++gx->why_hit[k]; break; }
        if (k == gx->why_n && gx->why_n < 8u) {
            gx->why_key[gx->why_n] = why;
            gx->why_hit[gx->why_n] = 1u;
            ++gx->why_n;
        }
    }
    if (!gx->trace_desync || gx->desyncs > gx->trace_desync) return;
    {   /* the 32 bytes leading up to this, oldest first */
        unsigned k;
        fprintf(stderr, "[gx] bytes before desync %llu:",
                (unsigned long long)gx->desyncs);
        for (k = 256u; k > 0u; --k)
            fprintf(stderr, " %02X", gx->recent[(gx->recent_at - k) & 511u]);
        fprintf(stderr, "\n");
    }
    {   /* The last display lists called. A GX display list is 32-byte
         * aligned, so a low nibble here says we jumped somewhere that was
         * never a display list - and a bogus call feeds an arbitrary region
         * of memory back through the parser. */
        unsigned k;
        fprintf(stderr, "[gx] last display lists (addr/size align):");
        for (k = 6u; k > 0u; --k) {
            uint64_t e = gx->dlring[(gx->dlring_at - k) & 7u];
            fprintf(stderr, " %08X/%u a%u", (unsigned)(e >> 32),
                    (unsigned)(e & 0xFFFFFFFFu), (unsigned)((e >> 32) & 0x1Fu));
        }
        fprintf(stderr, "\n");
    }
    {   /* The last draws: op count*vsize=len. If len-2 is not count*vsize,
         * the stream and our descriptor disagree about the vertex. */
        unsigned k;
        fprintf(stderr, "[gx] last draws (op cnt*vsz=len):");
        for (k = 12u; k > 0u; --k) {
            uint64_t e = gx->drawring[(gx->drawring_at - k) & 31u];
            fprintf(stderr, " %02X %u*%u=%u", (unsigned)(e >> 56),
                    (unsigned)((e >> 40) & 0xFFFFu),
                    (unsigned)((e >> 24) & 0xFFFFu),
                    (unsigned)(e & 0xFFFFFFu));
        }
        fprintf(stderr, "\n");
    }
    {   /* THE LAST COMMANDS PARSED CLEANLY, opcode:length.
         *
         * The byte ring says where the stream stopped making sense; this says
         * what we believed up to that point. A run of plausible commands ending
         * in one whose length is wrong is what a sizing bug looks like; a clean
         * run that simply stops is a pointer or buffer problem instead. */
        unsigned k;
        fprintf(stderr, "[gx] last commands (op:len):");
        for (k = 64u; k > 0u; --k) {
            uint32_t e = gx->cmdring[(gx->cmdring_at - k) & 127u];
            fprintf(stderr, " %02X:%u", e >> 24, e & 0xFFFFFFu);
        }
        fprintf(stderr, "\n");
    }
    {   /* THE SIZE WE COMPUTE FOR EVERY FORMAT.
         *
         * A desync that lands on 0xFFFF is an INDEX16 attribute read as an
         * opcode, which means the vertex was sized too small and the run
         * ended early. The opcode in the report is the garbage byte, so its
         * format is meaningless - print all eight. */
        unsigned f;
        fprintf(stderr, "[gx] vertex sizes by format:");
        for (f = 0; f < 8u; ++f) {
            MgsGxVertexFormat vf;
            mgs_gx_vertex_format(gx, f, &vf);
            fprintf(stderr, " %u:%u", f, mgs_gx_vertex_size(&vf));
        }
        fprintf(stderr, "  vcd=%08X/%08X\n", gx->vcd_lo, gx->vcd_hi);
    }
    fprintf(stderr,
            "[gx] desync %llu (dl_depth=%u): %s  op=0x%02X fmt=%u prim=%u detail=%u  "
            "vcd=%08X/%08X vat=%08X/%08X/%08X  cmds=%llu tris=%llu\n",
            (unsigned long long)gx->desyncs, gx->dl_depth, why, op, op & 7u,
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

    /* WHICH command-processor registers the game actually writes.
     *
     * Nine and a quarter million indexed positions failed to fetch because
     * the position array's STRIDE was zero, while its base was set. Either
     * the game never writes 0xB0-0xBF or we never see the write, and those
     * need completely different fixes. A count per register group says
     * which in one run. */
    if (reg < 0x100u) ++gx->cp_writes[reg >> 4];

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
        /* What is left is dropped, and counted while it is dropped. The
         * texgen block below is KEPT as well as counted - see fifo.h. */
        else if (a == 0x1012u) gx->xf_dualtex = words[i] & 1u;
        else if (a == 0x1005u) {
            /* CLIP DISABLE. Bit 0 turns off clipping detection, and with
             * it set the hardware does not clip triangles at the near
             * plane (Dolphin: skip_clipping when every w >= 0). Kept, and
             * the values seen are counted, because honouring the near
             * plane when the game has switched it off removes geometry
             * the console draws. */
            gx->xf_clip_disable = words[i];
            ++gx->xf_clip_disable_writes[words[i] & 1u];
        }
        else if (a == 0x103Fu) gx->xf_num_texgen = words[i] & 0xFu;
        else if (a >= 0x1050u && a < 0x1058u) gx->xf_postinfo[a - 0x1050u] = words[i];
        else if (a >= 0x1040u && a < 0x1050u) {
            unsigned k;
            if (a < 0x1048u) gx->xf_texgen[a - 0x1040u] = words[i];
            ++gx->xf_texgen_writes;
            for (k = 0; k < gx->xf_texgen_n; ++k)
                if (gx->xf_texgen_key[k] == words[i]) break;
            if (k == gx->xf_texgen_n && gx->xf_texgen_n < 8u) {
                gx->xf_texgen_key[gx->xf_texgen_n] = words[i];
                gx->xf_texgen_n++;
            }
            if (k < 8u) ++gx->xf_texgen_hits[k];
        }
        else if (a >= 0x0500u && a < 0x0600u) {
            gx->xf_post[a - 0x0500u] = f;
            ++gx->xf_texmtx_writes;
        }
        else {
            unsigned k;
            ++gx->xf_other_writes;
            for (k = 0; k < gx->xf_other_n; ++k)
                if (gx->xf_other_first[k] == a) break;
            if (k == gx->xf_other_n && gx->xf_other_n < 8u)
                gx->xf_other_first[gx->xf_other_n++] = a;
        }
    }
}

/* No real display list approaches this. It is a guard against a wild size
 * read out of a lost stream, not a statement about the hardware. */
#define MGS_GX_DL_MAX (4u * 1024u * 1024u)

static void run_dl(MgsGx* gx, uint32_t addr, uint32_t size);

/* The handful of blitting-processor registers that DO something rather than
 * merely configure something. A palette load is the only one here: it names a
 * main-memory address and a destination in texture memory, and the pair has
 * to be remembered together because they arrive as two separate writes. */
static void bp_side_effect(MgsGx* gx, uint8_t reg, uint32_t val)
{
    if (reg == BP_LOAD_TLUT0) {
        /* The address, in 32-byte units, as everything in the command stream
         * is. Using it unshifted lands 32 times too low.
         *
         * AND THE TOP BITS ARE RUBBISH THE HARDWARE IGNORES. This game sets
         * bits above the 25 the GameCube decodes, so the shifted value
         * arrives as 0x88DC2600 or 0x84DC1C00 - folded by `guest_ptr` those
         * are offsets of 148 MB and 81 MB into a 24 MB block, so every
         * paletted texture was refused: 3,198 of them in a run, which is
         * every CI-format texture the movie draws.
         *
         * Masking to 25 bits collapses all three observed values onto two
         * real palettes (0x80DC2600 and 0x80DC1C00, the latter from two
         * different encodings - which is the check that the mask is right
         * rather than merely plausible).
         *
         * Dolphin does the same and says why, in BPStructs.cpp: "The
         * GameCube ignores the upper bits of this address. Some games (WW,
         * MKDD) set them." Twin Snakes is another. */
        gx->bp.pending_tlut_addr =
            guest_from_phys26((val & 0x00FFFFFFu) << 5);
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
        /* ONE SLOT. A second copy issued before the first is drained
         * REPLACES it, and the first never happens at all.
         *
         * The hardware executes a copy where the command sits in the
         * stream, between the draws around it. Here the copy is deferred to
         * whenever the display hook next runs, so a frame that copies the
         * embedded buffer to a texture, draws with that texture, and then
         * copies to the framebuffer collapses: the draws all happen first
         * and only the last copy survives. Counted, so the cost is a number
         * rather than a suspicion. */
        if (gx->copy_exec) { gx->copy_exec(gx->copy_user, val & 0x00FFFFFFu);
                             return; }
        if (gx->copy_pending & 0x80000000u) ++gx->copies_dropped;
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

    /* INDEXED XF LOADS - the commands that were SIZED and then dropped.
     *
     * `GXLoadPosMtxIndx` and its siblings do not put a matrix in the stream.
     * They put an INDEX in it, and the hardware fetches the matrix from an
     * array in main memory whose base and stride are CP registers - the same
     * mechanism indexed vertex attributes use, pointed at XF memory instead
     * of at a vertex.
     *
     * The parser knew how long these commands were and threw them away, so
     * every matrix loaded that way stayed at its power-on value of zero. A
     * vertex multiplied by an all-zero matrix lands at the origin, behind
     * the eye, and is rejected: 406,076 triangles a run, with the counter
     * `behind_zero_matrix` sitting there naming them. That is Ben's "objects
     * are either incorrect or not even there" in the cinematic - animated
     * models load their bone matrices exactly this way.
     *
     * The field layout is Dolphin's (`OpcodeDecoding.h`, the GX_LOAD_INDX_*
     * case) and the array mapping is its comment: opcode / 8 + 8, so 0x20 ->
     * array 12, 0x28 -> 13, 0x30 -> 14, 0x38 -> 15. Recorded in
     * THIRD_PARTY.md. */
    if (op == GX_OP_LOAD_INDX_A || op == GX_OP_LOAD_INDX_B ||
        op == GX_OP_LOAD_INDX_C || op == GX_OP_LOAD_INDX_D) {
        uint32_t value = be32(body);
        uint32_t index = value >> 16;
        uint32_t address = value & 0xFFFu;
        unsigned count = ((value >> 12) & 0xFu) + 1u;
        unsigned array = ((unsigned)op / 8u) + 8u;
        uint32_t base = gx->array_base[array & 0xFu];
        uint32_t stride = gx->array_stride[array & 0xFu];
        const uint8_t* src;

        ++gx->indexed_xf_loads;
        if (!base || !stride) { ++gx->indexed_xf_no_array; return; }
        src = guest_ptr(gx->mem, base + index * stride, count * 4u);
        if (!src) { ++gx->indexed_xf_bad_addr; return; }
        {
            uint32_t words[16];
            unsigned i;
            if (count > 16u) count = 16u;
            for (i = 0; i < count; ++i) words[i] = be32(src + i * 4u);
            xf_write(gx, address, words, count);
        }
        return;
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
    if (op == GX_OP_CALL_DL) {
        /* The operand bytes as they arrived. Lists that declare 83 bytes sit
         * 64 apart and therefore overlap, so either this size or this address
         * is not what the game wrote - and the raw bytes settle which. */
        if (gx->trace_desync && (be32(body) & 0x1Fu) &&
            gx->dl_misaligned_traced++ < 8u)
            fprintf(stderr, "[gx] CALL_DL operand: %02X %02X %02X %02X  "
                            "%02X %02X %02X %02X\n",
                    body[0], body[1], body[2], body[3],
                    body[4], body[5], body[6], body[7]);
        run_dl(gx, be32(body), be32(body + 4));
        return;
    }
    if (op == GX_OP_LOAD_BP) {
        uint32_t packed = be32(body);
        uint8_t  reg = (uint8_t)(packed >> 24);
        uint32_t val = packed & 0x00FFFFFFu;
        /* EVERY TEXTURE BASE ADDRESS THE GAME WRITES, kept as a small set of
         * distinct values. One texture is refused 2,964 times for an address
         * 53 MB into a 24 MB machine, and the question is whether the game
         * wrote that or we synthesised it - which a histogram of what was
         * actually written answers and nothing else does. */
        if (gx->trace_teximg &&
            ((reg >= 0x94u && reg <= 0x97u) || (reg >= 0xB4u && reg <= 0xB7u))) {
            unsigned k;
            for (k = 0; k < gx->teximg_n; ++k)
                if (gx->teximg[k] == val) break;
            if (k == gx->teximg_n && gx->teximg_n < 32u)
                gx->teximg[gx->teximg_n++] = val;
        }
        mgs_bp_write(&gx->bp, reg, val);
        bp_side_effect(gx, reg, val);
        return;
    }
    gx->cmdring[gx->cmdring_at & 127u] = ((uint32_t)op << 24) | (len & 0xFFFFFFu);
    ++gx->cmdring_at;

    if (gx->dl_follow && gx->dl_follow_n < 400u) {
        ++gx->dl_follow_n;
        fprintf(stderr, "[dl] %4u  op=%02X len=%u\n",
                gx->dl_follow_n, op, len);
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
        /* A ring of what the parser has just seen. A desync reports a byte
         * that could not be an opcode, but the byte alone does not say
         * whether the stream is padded, misaligned, or carrying data we
         * mis-sized upstream - the bytes around it do. */
        gx->recent[gx->recent_at++ & 511u] = data[i];

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
            if (!len) {
                /* A LENGTH OF ZERO MEANS "I DO NOT KNOW THIS OPCODE".
                 *
                 * This used to report it only for opcodes at or above the
                 * draw range, which let every unknown byte BELOW 0x80 pass as
                 * a valid zero-length command. The parser then walked through
                 * garbage one byte at a time, silently, until it happened to
                 * land on a byte >= 0x80 it could not size - so the first
                 * desync reported was never the first desync that happened,
                 * and the trace pointed at a draw when the stream had already
                 * been lost somewhere upstream. NOP and the vertex-cache
                 * invalidate are the only legitimate zero-length commands and
                 * both are consumed before this point. */
                desync(gx, gx->opcode >= GX_OP_DRAW_FIRST
                           ? "draw command cannot be sized"
                           : "unknown opcode",
                       gx->opcode, gx->have);
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

    /* WHAT THE HARDWARE REQUIRES, used here as a validity check - AND THE
     * SIZE IS NOT PART OF IT.
     *
     * `GXCallDisplayList` takes a 32-byte-aligned address, and the command
     * processor fetches from it in 32-byte units. The SDK asserts on the
     * SIZE being a 32-byte multiple too, and this refused anything else on
     * that basis. That was wrong in a way that cost a display list every
     * single frame: asserts are compiled out of a release build, so a game
     * can pass any size it likes, and this one does - `GXCallDisplayList(
     * 0x8097CAE0, 83)`, at the same address with the same size, once per
     * frame for the whole run. 6,317 refusals in a 40,000,000-step boot,
     * every one of them a real list thrown away.
     *
     * The ADDRESS is what carries the signal. A parser that has lost the
     * stream produces a wild pointer, and the alignment plus a mapped-memory
     * check catches that; a legitimate call that happens to be 83 bytes long
     * does not deserve to be treated as corruption.
     *
     * Checking still matters far more than it looks. A bogus call is not one
     * bad command, it is an ARBITRARY REGION OF MEMORY fed back through the
     * parser - and a region of zeroes parses as one NOP per byte. That is how
     * a single lost byte turned into 23,157,036,840 "commands" and
     * 841,627,908 desyncs. So the address check stays, the mapped-memory
     * check stays, and a size beyond any plausible list is still refused.
     */
    /* MGS_TRACE_DLADDR=<addr> follows ONE list command by command, from its
     * first byte. The desync report shows where the parser noticed, which is
     * later than where it went wrong; this shows the whole list so the two can
     * be compared. */
    if (gx->trace_dl_addr && addr == gx->trace_dl_addr && !gx->dl_followed) {
        gx->dl_followed = 1;
        gx->dl_follow = 1;
        fprintf(stderr, "[gx] following list 0x%08X/%u\n", addr, size);
    }

    gx->dlring[gx->dlring_at & 7u] = ((uint64_t)addr << 32) | (size & 0xFFFFFFFFu);
    ++gx->dlring_at;

    /* A GX display list is a whole number of 32-byte fetch units. A size that
     * is not says the operand is wrong, and running past the end of a real
     * list feeds whatever follows it back through the parser. Counted rather
     * than assumed: if the game legitimately passes ragged sizes this will be
     * most of them, and if it is corruption it will be a handful. */
    ++gx->dl_calls;
    if (size & 0x1Fu) ++gx->dl_ragged;

    /* THE COMMAND PROCESSOR FETCHES IN 32-BYTE UNITS.
     *
     * It therefore cannot honour the low five bits of an address: hardware
     * masks them, and a list whose pointer is not aligned is fetched from the
     * unit containing it. Refusing such a call instead THREW THE LIST AWAY -
     * 388,030 of them in a run that reaches the movie, every one a piece of
     * geometry that never got drawn.
     *
     * Masking is what the silicon does, and rule 12 puts the hardware's
     * behaviour above our own reading. The mapped-memory and size checks
     * below still stand, so a pointer that is merely wild is still refused;
     * this only stops us discarding lists the hardware would have run.
     */
    if (addr & 0x1Fu) {
        if (gx->trace_desync && gx->dl_misaligned_traced < 4u)
            fprintf(stderr, "[gx] CALL_DL addr=0x%08X size=%u  addr%%32=%u"
                            "  -> masking to 0x%08X\n",
                    addr, size, addr & 0x1Fu, addr & ~0x1Fu);
        ++gx->dl_masked;
        /* MASKED DOWN TO THE FETCH UNIT, because that is what works.
         *
         * The argument for using the byte address as given is that the
         * command processor only FETCHES in 32-byte units and executes from
         * the address it was handed. It is a good argument and it is wrong
         * here: measured over a run that reaches the movie,
         *
         *     masked to the 32-byte boundary   0 desyncs, 77,772,467 triangles
         *     exact byte address              26,323,104 desyncs, 58,854,957
         *
         * so the list really does begin at the unit boundary. Isolated with
         * MGS_NO_RTT, which showed the texture copies were innocent and this
         * was the variable. */
        addr &= ~0x1Fu;
    }
    /* THE SIZE OF A LIST RECORDED IN THE SECOND WINDOW IS OFF BY 64 MB.
     *
     * GXEndDisplayList returns wrPtr - base, with wrPtr rebuilt by
     * OSPhysicalToCached from a 26-bit register: for a list at 0x7F4AD180
     * that is 0x834AD1A5 - 0x7F4AD180 = 0x04000025, the real 0x25 plus the
     * distance between the window (0x7C000000 upwards) and where the
     * physical form lands (0x80000000). On a console the list is in MEM1 and
     * the two agree. Every list in this window carries exactly that
     * 0x04000000, so it is removed here, where the list is called. See
     * mgs_mmio_cpu_fifo_base for why the lists are in this window at all. */
    if (addr - 0x7E000000u < 0x02000000u && size >= 0x04000000u &&
        (size & 0x03FFFFFFu) <= MGS_GX_DL_MAX) {
        size &= 0x03FFFFFFu;
        ++gx->dl_vmem_size_fixed;
    }
    if (size > MGS_GX_DL_MAX) {
        /* A size no display list has - so the pointer and size the game
         * handed over are not a display list at all. That is a symptom of
         * memory the game relies on having been overwritten, and the same
         * runs end with a return address overwritten by a float. Say what
         * was asked for, and from how deep, so the structure it came from
         * can be found. */
        static unsigned said;
        if (said < 12u) {
            ++said;
            fprintf(stderr, "[gx] display list CALL of %u bytes at 0x%08X "
                            "refused (nesting %u, command %llu)\n",
                    size, addr, gx->dl_depth,
                    (unsigned long long)gx->commands);
        }
        desync(gx, "display list is implausibly large", GX_OP_CALL_DL, size);
        return;
    }

    /* EXACTLY `size` BYTES, NOT ROUNDED UP TO THE FETCH UNIT.
     *
     * The command processor fetches in 32-byte units, so rounding a size of
     * 83 up to 96 looks like the faithful thing to do. It is not: doing it
     * took this boot from 0 desyncs to 231, because the 13 bytes past the
     * game's own length are padding and the parser reads them as commands.
     * Tried, measured, reverted - and recorded so it is not tried again. */

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

        /* A WELL-FORMED LIST ENDS ON A COMMAND BOUNDARY.
         *
         * If bytes are still staged here, the last command in the list ran off
         * its end - which means either the list's size is wrong or we sized a
         * command inside it wrongly. The caller's stream is protected either
         * way, so this is invisible unless counted, and it is precisely the
         * fault that would make a list render as garbage without ever
         * reporting a desync. */
        if (gx->have || gx->want) {
            ++gx->dl_truncated;
            if (gx->trace_desync && gx->dl_truncated <= 2u) {
                unsigned k;
                fprintf(stderr, "[gx] display list 0x%08X/%u ended mid-command:"
                                " op=0x%02X have=%u  vcd=%08X/%08X\n",
                        addr, size, gx->opcode, gx->have,
                        gx->vcd_lo, gx->vcd_hi);
                /* The whole list, so it can be parsed by hand. A short one
                 * that we cannot follow is worth more than any counter. */
                fprintf(stderr, "[gx]   bytes:");
                for (k = 0; k < size && k < 128u; ++k)
                    fprintf(stderr, " %02X", p[k]);
                fprintf(stderr, "\n[gx]   vertex sizes:");
                for (k = 0; k < 8u; ++k) {
                    MgsGxVertexFormat vf;
                    mgs_gx_vertex_format(gx, k, &vf);
                    fprintf(stderr, " %u:%u", k, mgs_gx_vertex_size(&vf));
                }
                fprintf(stderr, "\n");
            }
        }

        if (gx->dl_follow && addr == gx->trace_dl_addr) gx->dl_follow = 0;

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

    /* The last draws, for the desync report: opcode, how many vertices the
     * stream said, and how big we believed each one was. A body that is not a
     * whole number of vertices is a sizing bug caught in the act. */
    gx->drawring[gx->drawring_at & 31u] =
        ((uint64_t)op << 56) | ((uint64_t)count << 40) |
        ((uint64_t)vsize << 24) | (len & 0xFFFFFFu);
    ++gx->drawring_at;

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
