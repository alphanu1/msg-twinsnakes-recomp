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

    /* Command processor state. */
    uint32_t vcd_lo, vcd_hi;             /* CP 0x50, 0x60 */
    uint32_t vat_a[8], vat_b[8], vat_c[8];
    uint32_t array_base[16];             /* CP 0xA0-0xAF */
    uint32_t array_stride[16];           /* CP 0xB0-0xBF */
    uint32_t cp_matrix_index_a, cp_matrix_index_b;

    /* Transform unit memory: matrices at 0x0000, and the projection at
     * 0x1020. Held as raw words and interpreted on use. */
    float xf_matrix[64 * 4];             /* 64 rows of 4 floats */
    float xf_normal[32 * 3];
    float xf_projection[7];
    unsigned xf_projection_ortho;
    uint32_t viewport[6];

    /* Parser state. The stream arrives in fragments of one to four bytes, so
     * a partially decoded command has to survive between writes. */
    uint8_t  buf[512];
    unsigned have;
    unsigned want;                       /* 0 = opcode not yet decoded */
    uint8_t  opcode;

    /* Display lists are executed inline, and may nest. A depth limit turns a
     * corrupt pointer into a refusal rather than a hang. */
    unsigned dl_depth;

    MgsGxTriangleFn triangle;
    void* user;

    uint64_t commands, primitives, vertices, triangles, desyncs;
} MgsGx;

void mgs_gx_init(MgsGx* gx, GuestMemory* mem);

/* Feed bytes from the write-gather pipe, in order, in whatever sizes they
 * arrive. */
void mgs_gx_write(MgsGx* gx, uint32_t value, unsigned size);

/* Resolve the current vertex descriptor and attribute table into one format.
 * Exposed for testing: it is where a misread register shows up first. */
void mgs_gx_vertex_format(const MgsGx* gx, unsigned vat, MgsGxVertexFormat* out);

/* Bytes one vertex occupies in the stream, for the given format. Returns 0
 * if the format is one this cannot size, which is a refusal to desynchronise
 * rather than a guess. */
unsigned mgs_gx_vertex_size(const MgsGxVertexFormat* f);

#endif
