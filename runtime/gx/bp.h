/* Blitting-processor registers: the pixel pipeline's entire configuration.
 *
 * Everything after the rasteriser reads its settings from here - which texture
 * is bound, how it is filtered and wrapped, what the texture environment does
 * with the sample, whether the depth test runs, what blending applies. The
 * game sets them through GX calls that become `0x61` commands in the stream,
 * so by the time they reach us they are 8 bits of register number and 24 bits
 * of value, with no names and no types.
 *
 * Kept as the raw 24-bit values, decoded at the point of use. That is
 * deliberate: a struct of decoded fields has to be re-derived every time the
 * hardware's meaning of a bit is corrected, and the raw value is what can be
 * compared against Dolphin when something disagrees.
 */
#ifndef MGS_GX_BP_H
#define MGS_GX_BP_H

#include <stdint.h>

/* Register numbers, from the command processor's blitting-processor map. */
#define BP_GEN_MODE        0x00u
#define BP_IND_CMD         0x10u   /* 0x10-0x1F */
#define BP_SCISSOR_TL      0x20u
#define BP_SCISSOR_BR      0x21u
#define BP_TEV_ORDER       0x28u   /* 0x28-0x2F, two stages each */
#define BP_ZMODE           0x40u
#define BP_BLEND_MODE      0x41u
#define BP_CONSTANT_ALPHA  0x42u
#define BP_EFB_BOX_TL      0x49u   /* source rectangle: top-left */
#define BP_EFB_BOX_WH      0x4Au   /* source rectangle: width-1, height-1 */
#define BP_EFB_ADDR        0x4Bu
/* 0x4D, not 0x4E. 0x4E is the copy's vertical scale; taking it as the stride
 * gives a line pitch that is plausible and wrong, which lays the image over
 * itself diagonally rather than failing. */
#define BP_COPY_STRIDE     0x4Du
#define BP_COPY_YSCALE     0x4Eu
#define BP_COPY_CLEAR_AR   0x4Fu
#define BP_COPY_CLEAR_GB   0x50u
#define BP_COPY_CLEAR_Z    0x51u
#define BP_COPY_EXECUTE    0x52u
#define BP_LOAD_TLUT0      0x64u
#define BP_LOAD_TLUT1      0x65u
#define BP_TX_SETMODE0     0x80u   /* 0x80-0x83 texture 0-3 */
#define BP_TX_SETMODE1     0x84u
#define BP_TX_SETIMAGE0    0x88u
#define BP_TX_SETIMAGE1    0x8Cu
#define BP_TX_SETIMAGE2    0x90u
#define BP_TX_SETIMAGE3    0x94u
#define BP_TX_SETTLUT      0x98u
#define BP_TX_SETMODE0_4   0xA0u   /* 0xA0-0xA3 texture 4-7 */
#define BP_TX_SETMODE1_4   0xA4u
#define BP_TX_SETIMAGE0_4  0xA8u
#define BP_TX_SETIMAGE1_4  0xACu
#define BP_TX_SETIMAGE2_4  0xB0u
#define BP_TX_SETIMAGE3_4  0xB4u
#define BP_TX_SETTLUT_4    0xB8u
#define BP_TEV_COLOR_ENV   0xC0u   /* 0xC0, 0xC2, ... per stage */
#define BP_TEV_ALPHA_ENV   0xC1u
#define BP_TEV_REGISTER_L  0xE0u   /* 0xE0-0xE7, colour registers */
#define BP_TEV_KSEL        0xF6u   /* 0xF6-0xFD */
#define BP_ALPHA_COMPARE   0xF3u

/* Texture formats, as TX_SETIMAGE0 encodes them. */
#define GX_TF_I4      0x0u
#define GX_TF_I8      0x1u
#define GX_TF_IA4     0x2u
#define GX_TF_IA8     0x3u
#define GX_TF_RGB565  0x4u
#define GX_TF_RGB5A3  0x5u
#define GX_TF_RGBA8   0x6u
#define GX_TF_C4      0x8u
#define GX_TF_C8      0x9u
#define GX_TF_C14X2   0xAu
#define GX_TF_CMPR    0xEu

/* Palette formats, as TX_SETTLUT encodes them. */
#define GX_TL_IA8     0x0u
#define GX_TL_RGB565  0x1u
#define GX_TL_RGB5A3  0x2u

typedef struct MgsGxBp {
    uint32_t reg[256];        /* every register's last written value */
    uint8_t  written[256];    /* whether the game has set it at all */

    /* Texture memory, as the game loads it. The hardware's is 1 MB of on-die
     * memory addressed in 32-byte lines; the palette area is separate. Both
     * are held as flat arrays because nothing here benefits from modelling
     * the banking, and the banking is what a game gets wrong, not us. */
    uint8_t  tmem[1024u * 1024u];
    uint16_t tlut[16384];     /* palette entries, still in their source format */

    /* Where each palette came from in MAIN memory.
     *
     * A texture names its palette by an offset into texture memory, not by an
     * address - `GXLoadTlut` copied it there earlier. Rather than model that
     * copy, this records which guest address was loaded to which offset, so
     * the decoder can read the palette from where the game actually keeps it.
     * That keeps one copy of the data and means a palette the game edits in
     * place is seen, which emulating the copy would miss. */
    uint32_t tlut_src[1024];
    uint32_t pending_tlut_addr;   /* the address half of a load, awaiting the
                                   * offset half, which arrives as a second
                                   * register write */
} MgsGxBp;

void mgs_bp_init(MgsGxBp* bp);
void mgs_bp_write(MgsGxBp* bp, uint8_t reg, uint32_t value);

static inline uint32_t mgs_bp_get(const MgsGxBp* bp, uint8_t reg)
{
    return bp->reg[reg];
}

#endif
