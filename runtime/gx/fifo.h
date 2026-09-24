/* The graphics command stream.
 *
 * Everything the graphics processor does arrives as one byte stream through
 * the write-gather pipe. It is not a sequence of API calls: `GXBegin` and the
 * vertex writes that follow are, by the time they reach here, an opcode byte,
 * a vertex count and a run of packed attribute data whose LAYOUT IS NOT IN THE
 * STREAM. It is in registers set earlier - the vertex descriptor says which
 * attributes are present and how each is indexed, and the attribute table says
 * what format each one is in.
 *
 * So the parser cannot be written as a switch over opcodes alone. It has to
 * carry the register state forward, because the length of a draw command is a
 * function of registers set arbitrarily far back. Getting one length wrong
 * does not lose one command; it desynchronises the stream and every command
 * after it is garbage. That is the single most important property here, and
 * it is why the parser keeps a strict byte budget and refuses to guess.
 *
 * WHAT THIS IS NOT. It is not a renderer. It decodes and it maintains state,
 * and it hands whole primitives to a callback. Rasterisation, transform and
 * texturing are separate files so that each can be wrong on its own.
 */
#ifndef MGS_GX_FIFO_H
#define MGS_GX_FIFO_H

#include <stdint.h>
#include "../memory/guest.h"
#include "bp.h"

/* Opcodes, from the command processor's encoding. The draw opcodes carry the
 * attribute-table index in their low three bits. */
#define GX_OP_NOP            0x00u
#define GX_OP_LOAD_CP        0x08u
#define GX_OP_LOAD_XF        0x10u
#define GX_OP_LOAD_INDX_A    0x20u
#define GX_OP_LOAD_INDX_B    0x28u
#define GX_OP_LOAD_INDX_C    0x30u
#define GX_OP_LOAD_INDX_D    0x38u
#define GX_OP_CALL_DL        0x40u
#define GX_OP_INVL_VC        0x48u
#define GX_OP_LOAD_BP        0x61u
#define GX_OP_DRAW_FIRST     0x80u
#define GX_OP_DRAW_LAST      0xB8u

/* Primitive kinds, as the opcode's top five bits encode them. */
typedef enum {
    GX_PRIM_QUADS = 0,
    GX_PRIM_QUADS2,
    GX_PRIM_TRIANGLES,
    GX_PRIM_TRIANGLE_STRIP,
    GX_PRIM_TRIANGLE_FAN,
    GX_PRIM_LINES,
    GX_PRIM_LINE_STRIP,
    GX_PRIM_POINTS,
    GX_PRIM_COUNT
} MgsGxPrim;

/* How an attribute reaches the stream. The GameCube can inline a value, or
 * send an index into an array whose base and stride are in registers. */
typedef enum {
    GX_ATTR_NONE = 0,
    GX_ATTR_DIRECT = 1,
    GX_ATTR_INDEX8 = 2,
    GX_ATTR_INDEX16 = 3
} MgsGxAttrKind;

/* Vertex attributes, in the order they appear in a vertex. The order is the
 * hardware's and is not negotiable: a vertex is these fields packed in this
 * sequence, with the absent ones simply missing. */
typedef enum {
    GX_VA_PNMTXIDX = 0,
    GX_VA_TEX0MTXIDX, GX_VA_TEX1MTXIDX, GX_VA_TEX2MTXIDX, GX_VA_TEX3MTXIDX,
    GX_VA_TEX4MTXIDX, GX_VA_TEX5MTXIDX, GX_VA_TEX6MTXIDX, GX_VA_TEX7MTXIDX,
    GX_VA_POS, GX_VA_NRM,
    GX_VA_CLR0, GX_VA_CLR1,
    GX_VA_TEX0, GX_VA_TEX1, GX_VA_TEX2, GX_VA_TEX3,
    GX_VA_TEX4, GX_VA_TEX5, GX_VA_TEX6, GX_VA_TEX7,
    GX_VA_COUNT
} MgsGxAttr;

typedef struct MgsGxVertexFormat {
    /* From the vertex descriptor, CP registers 0x50 and 0x60. */
    MgsGxAttrKind kind[GX_VA_COUNT];

    /* From the attribute table, CP registers 0x70/0x80/0x90 + index. */
    unsigned pos_count;      /* 2 = xy, 3 = xyz */
    unsigned pos_format;     /* 0 u8, 1 s8, 2 u16, 3 s16, 4 f32 */
    unsigned pos_shift;      /* fixed-point fractional bits */
    unsigned nrm_count, nrm_format;
    unsigned nrm_index3;     /* VAT_A bit 31: an indexed normal is THREE indices */
    unsigned clr_count[2];   /* 0 = rgb, 1 = rgba */
    unsigned clr_format[2];  /* 0 565, 1 888, 2 888x, 3 4444, 4 6666, 5 8888 */
    unsigned tex_count[8], tex_format[8], tex_shift[8];
} MgsGxVertexFormat;

/* One decoded vertex, in the host's own terms. Everything that reaches the
 * rasteriser has been through this, so the rasteriser sees exactly one
 * vertex layout however the game encoded it - which is the whole point of
 * decoding at all. */
typedef struct MgsGxVertex {
    float x, y, z;
    float nx, ny, nz;
    /* The two binormals, when the vertex carries NBT. Texgen reads them
     * as source rows 3 and 4; nothing else does yet. */
    float bt[2][3];
    uint32_t color[2];       /* ARGB */
    float u[8], v[8];
    unsigned pos_matrix;     /* position/normal matrix index */
    unsigned tex_matrix[8];
} MgsGxVertex;

struct MgsGx;

/* Called with a complete primitive, already converted to a triangle list.
 * Strips, fans and quads are expanded here so nothing downstream has to know
 * they existed. */
typedef void (*MgsGxTriangleFn)(struct MgsGx* gx, const MgsGxVertex* a,
                                const MgsGxVertex* b, const MgsGxVertex* c);

typedef struct MgsGx {
    GuestMemory* mem;

    /* Pixel pipeline configuration, set by 0x61 commands in this same
     * stream. Held here so the rasteriser has one place to read it from. */
    MgsGxBp bp;

    /* Command processor state. */
    uint32_t vcd_lo, vcd_hi;             /* CP 0x50, 0x60 */
    uint32_t vat_a[8], vat_b[8], vat_c[8];
    /* Indexed XF loads: how many arrived, and how many could not be
     * served. A matrix that never loads leaves its vertices at the
     * origin and invisible, so "we saw the command" and "we acted on
     * it" have to be separate numbers. */
    uint64_t indexed_xf_loads, indexed_xf_no_array, indexed_xf_bad_addr;
    /* XF state we PARSE AND DROP, counted before deciding whether it
     * matters. The indexed matrix loads were exactly this shape - a
     * command handled well enough to keep the stream in sync and then
     * ignored - and cost 406,076 invisible triangles (F322), so the
     * rest of the XF address space gets counted rather than assumed
     * harmless. 0x1040-0x104F is texgen, 0x0500-0x05FF the texture
     * (post-transform) matrices, 0x1000-0x1017 the vertex spec and
     * lighting control. */
    uint64_t xf_texgen_writes, xf_texmtx_writes, xf_other_writes;
    /* The DISTINCT texgen configurations written, because "144,332
     * writes we ignore" does not say whether ignoring them matters.
     * A coordinate configured as plain 2x4 from its own stream row is
     * what the code already assumes; anything else - generated from
     * position or normal, 3x4 projected, emboss, or a colour channel -
     * is a coordinate we are computing wrongly. */
    uint32_t xf_texgen_key[8]; uint64_t xf_texgen_hits[8];
    unsigned xf_texgen_n;
    uint32_t xf_other_first[8];
    unsigned xf_other_n;
    /* Vertices whose stream carries a texture-matrix index that is not
     * GX_IDENTITY (60). If this is zero the matrices do not matter. */
    uint64_t tex_mtx_nonidentity, tex_mtx_seen;
    /* Which COORDINATE each transform lands on, and how many asked for
     * a row that is a POSITION matrix rather than a texture one.
     * Coordinate 0 is the one a single-stage draw samples; a matrix on
     * coordinates 1-3 only shows up through a later TEV stage. */
    uint64_t tex_mtx_applied[8];
    uint64_t tex_mtx_position_row;
    /* Of the transforms applied, how many actually MOVED the
     * coordinate. "18 million vertices use a texture matrix" and
     * "applying it changes the picture" are different claims, and a
     * matrix can be identity-VALUED at a non-identity INDEX. Thirteen
     * cinematic frames came out byte-identical with the transform on
     * and off, and this is the number that explains why. */
    uint64_t tex_mtx_moved, tex_mtx_unmoved;
    uint32_t array_base[16];             /* CP 0xA0-0xAF */
    uint32_t array_stride[16];           /* CP 0xB0-0xBF */
    uint32_t cp_matrix_index_a, cp_matrix_index_b;

    /* Transform unit memory: matrices at 0x0000, and the projection at
     * 0x1020. Held as raw words and interpreted on use. */
    float xf_matrix[64 * 4];             /* 64 rows of 4 floats */
    float xf_normal[32 * 3];
    float xf_projection[7];
    unsigned xf_projection_ortho;

    /* TEXTURE COORDINATE GENERATION, which was parsed and thrown away.
     *
     * Each output coordinate names its own SOURCE - the position, the normal
     * or any of the eight input coordinates - in its texgen register, and
     * this game builds its outputs from TEX0 and TEX1. Assuming output i
     * came from input TEXi fed zeros to outputs 1-3, and a matrix applied
     * to (0,0,1,1) gives the same answer for every vertex: one texel per
     * draw, and every model flat. See vertex.c.
     *
     * Initialised to what GXInit leaves (output i from TEXi, 2x4, identity
     * post-matrix), so a game that never writes them behaves as before. */
    uint32_t xf_texgen[8];               /* XF 0x1040-0x1047 */
    uint32_t xf_postinfo[8];             /* XF 0x1050-0x1057 */
    float    xf_post[64 * 4];            /* XF 0x0500-0x05FF, post-matrices */
    uint32_t xf_num_texgen;              /* XF 0x103F */
    uint32_t xf_dualtex;                 /* XF 0x1012, bit 0 */
    uint32_t xf_clip_disable;            /* XF 0x1005: bit 0 = no clipping */
    uint64_t xf_clip_disable_writes[2];  /* writes with bit 0 clear / set */
    /* Texgen the vertex path does not implement yet, counted rather than
     * guessed: a normal or binormal source (normals are skipped, not read),
     * emboss, and colour-as-coordinate. And coordinates whose q is not 1,
     * which are divided per vertex where the hardware divides per pixel. */
    uint64_t texgen_unsupported, texgen_q_not_one, texgen_regular;
    /* The unsupported ones by what they asked for. The list of distinct
     * texgen VALUES holds eight, and GXInit's own eight defaults fill it
     * before the game writes anything, so the configurations that matter
     * were never shown. */
    uint64_t texgen_unsup_type[8], texgen_unsup_row[32];

    /* Indexed positions that could not be fetched. See read_position. */
    uint64_t pos_fetch_failed, pos_no_base, pos_no_stride, pos_out_of_range;
    uint64_t cp_writes[16];   /* CP register writes, by high nibble */
    uint32_t viewport[6];

    /* Parser state. The stream arrives in fragments of one to four bytes, so
     * a partially decoded command has to survive between writes. */
    /* THE STAGING BUFFER, and it must hold a WHOLE primitive.
     *
     * 512 bytes was too small by a factor of three for the first thing the
     * engine draws. `GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT2, 66)` with a
     * 20-byte vertex is 2 + 66 * 20 = 1,322 bytes, so the command was
     * dropped - and dropping a draw does not lose one triangle, it loses the
     * STREAM: every byte after it is read at the wrong offset. That single
     * undersized buffer produced 841,627,908 desyncs and 23,157,036,840
     * bogus commands, because the garbage that followed eventually parsed as
     * a display-list call into arbitrary memory.
     *
     * 64 KB covers about 3,200 vertices at this game's vertex size. It is
     * NOT the hardware's worst case - GX allows 65,535 vertices in one
     * primitive, which with a large vertex format is megabytes - so the
     * oversize path remains, and now says so rather than counting silently.
     * Handling the true maximum means decoding vertices as they arrive
     * instead of buffering the primitive, which is a larger change than this
     * one and is not needed yet. */
    uint8_t  buf[65536];

    /* Where a nested display list parks the partial command it interrupted.
     * Four deep at most, and a partial command is small - a whole primitive
     * cannot be in progress here, because a command is only partial while
     * its bytes are still arriving. */
    uint8_t  dl_save[4096];
    unsigned have;
    unsigned want;                       /* 0 = opcode not yet decoded */
    uint8_t  opcode;

    /* Display lists are executed inline, and may nest. A depth limit turns a
     * corrupt pointer into a refusal rather than a hang. */
    unsigned dl_depth;

    MgsGxTriangleFn triangle;
    void* user;

    /* Returns non-zero when the host has been asked to quit and this parser
     * should stop. Checked once per command. Optional; NULL parses
     * everything. See feed() for why the rasteriser's own check is not
     * enough - a display list is one write from the guest's point of view,
     * and most of its cost is here rather than in the triangles. */
    int (*abandon)(void);

    /* MGS_TRACE_GXDESYNC=N explains the first N lost-stream events. Only the
     * first few can mean anything: once the stream is lost every byte after
     * it is read at the wrong offset, so the rest are consequences. */
    uint64_t trace_desync;
    uint64_t trace_cp;            /* MGS_TRACE_GXCP: command-processor writes */
    uint64_t trace_draw;          /* MGS_TRACE_GXDRAW: each draw's size inputs */
    uint64_t trace_win_from;      /* MGS_TRACE_GXWINDOW: every command in a range */
    uint64_t trace_win_n;
    uint64_t trace_bytes_from;    /* MGS_TRACE_GXBYTES: the raw stream */
    uint64_t trace_bytes_n;
    uint64_t stream_pos;          /* bytes written to the pipe so far */

    /* Work the host must do, noticed here because this is the only place
     * that knows where a command starts. The naive alternative - scanning the
     * byte stream for a register opcode - matches 0x61 bytes inside vertex
     * data too, and acting on one of those writes a framebuffer over the
     * game's memory. Counting them was harmless; acting on them was not. */
    uint64_t draw_done_tokens;   /* BP 0x45 with the interrupt bit */
    uint32_t teximg[32];         /* distinct TX_SETIMAGE3 values written */
    unsigned teximg_n;
    int      trace_teximg;       /* MGS_TRACE_TEXIMG */
    /* Run the copy WHERE IT IS READ, not later. Without this the command
     * waits in copy_pending until something drains it, and a second copy
     * overwrites the first. See run_copy() in host/display.c. */
    void (*copy_exec)(void* user, uint32_t cmd);
    void*  copy_user;
    uint32_t copy_pending;       /* BP 0x52, the command, or 0 */
    uint64_t copies_dropped;     /* issued while one was still pending */

    uint64_t commands, primitives, vertices, triangles, desyncs;
    uint8_t  recent[512];    /* the bytes just parsed, for desync reports */
    unsigned recent_at;
    uint32_t cmdring[128];   /* opcode<<24 | length, as parsed */
    unsigned cmdring_at;
    uint64_t drawring[32];   /* op<<56 | count<<40 | vsize<<24 | len */
    unsigned drawring_at;
    uint64_t dlring[8];
    unsigned dlring_at;
    uint64_t dl_calls, dl_ragged, dl_truncated, dl_masked;
    uint32_t trace_dl_addr;  /* MGS_TRACE_DLADDR: follow one list */
    int      dl_follow, dl_followed;
    unsigned dl_follow_n;
    const char* why_key[8];  /* desyncs by reason */
    uint64_t why_hit[8];
    unsigned why_n;
    unsigned dl_misaligned_traced;
} MgsGx;

void mgs_gx_init(MgsGx* gx, GuestMemory* mem);

/* Feed bytes from the write-gather pipe, in order, in whatever sizes they
 * arrive. */
void mgs_gx_write(MgsGx* gx, uint32_t value, unsigned size);

/* Resolve the current vertex descriptor and attribute table into one format.
 * Exposed for testing: it is where a misread register shows up first. */
void mgs_gx_vertex_format(const MgsGx* gx, unsigned vat, MgsGxVertexFormat* out);

/* Decode one vertex, texture coordinate generation included. Returns the
 * bytes consumed. Declared here so the test can drive it directly. */
unsigned mgs_gx_decode_vertex(const MgsGx* gx, const MgsGxVertexFormat* f,
                              const uint8_t* data, unsigned n, MgsGxVertex* v);

/* Bytes one vertex occupies in the stream, for the given format. Returns 0
 * if the format is one this cannot size, which is a refusal to desynchronise
 * rather than a guess. */
unsigned mgs_gx_vertex_size(const MgsGxVertexFormat* f);

/* Take the pending framebuffer copy, if the game has asked for one. */
int mgs_gx_take_copy(MgsGx* gx, uint32_t* cmd);

/* Take one draw-done token, if the game has sent one. */
int mgs_gx_take_draw_done(MgsGx* gx);


/* A marker saying which part of the renderer is running, for a host that has
 * to report where it was when it could not be stopped. Defined in fifo.c and
 * read by whatever is doing the reporting; a pointer store to a string
 * literal, so it is safe to read from a signal handler. */
extern volatile const char* mgs_gx_phase;

#endif
