/* Loading a DOL into guest memory.
 *
 * On hardware the apploader does this: it reads main.dol off the disc and
 * copies each section to the address the header names, then clears .bss and
 * jumps to the entry point. A host that allocates guest RAM and jumps
 * straight to the entry point is running the right code against the wrong
 * memory - and the failure is quiet, because an unloaded .sdata reads as
 * zeros and a zero is a plausible value for almost anything.
 *
 * That is exactly how it presented: VIGetTvFormat read the TV mode through
 * r13, the small-data-area base, got zero, and branched through a null
 * pointer 413 steps into the boot.
 *
 * A DOL header is 0x100 bytes: 7 text sections then 11 data sections, each
 * with a file offset, a load address and a size, followed by the .bss address
 * and size and the entry point.
 */
#include "dol.h"

#include <stdio.h>
#include <string.h>

#define DOL_TEXT_COUNT 7u
#define DOL_DATA_COUNT 11u
#define DOL_SECTIONS   (DOL_TEXT_COUNT + DOL_DATA_COUNT)

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int mgs_dol_load(GuestMemory* mem, const void* data, size_t size, MgsDolInfo* info)
{
    const uint8_t* dol = (const uint8_t*)data;
    unsigned i;

    if (!dol || size < 0x100u) return 0;
    memset(info, 0, sizeof *info);

    /* CLEAR .bss FIRST, THEN LOAD SECTIONS. The order is not arbitrary.
     *
     * A DOL header's bss entry is a single address and length covering
     * everything the linker did not put in the file - and on this game it
     * SPANS .sdata: bss runs 0x801E7DC0 + 615068, ending at 0x8027DFC4, while
     * .sdata sits at 0x8027D980 in the middle of it. Clearing afterwards
     * wipes initialised data that was just copied in.
     *
     * The symptom is as quiet as it gets: every small-data-area read returns
     * zero, which is a plausible value everywhere. It showed up as
     * __OSThreadInit branching through a null SwitchThreadCallback - a
     * function pointer whose correct value, 0x80022E7C, was sitting in the
     * DOL the whole time.
     */
    info->bss_address = be32(dol + 0xD8u);
    info->bss_size    = be32(dol + 0xDCu);
    if (info->bss_address && info->bss_size) {
        uint8_t* bss = guest_ptr(mem, info->bss_address, info->bss_size);
        if (bss) memset(bss, 0, info->bss_size);
    }

    for (i = 0; i < DOL_SECTIONS; ++i) {
        uint32_t off  = be32(dol + 0x00u + i * 4u);
        uint32_t addr = be32(dol + 0x48u + i * 4u);
        uint32_t len  = be32(dol + 0x90u + i * 4u);
        uint8_t* dst;

        if (!off || !addr || !len) continue;      /* unused slot */
        if ((size_t)off + len > size) return 0;   /* truncated file */

        dst = guest_ptr(mem, addr, len);
        if (!dst) return 0;                       /* outside guest RAM */
        memcpy(dst, dol + off, len);

        info->sections[info->section_count].address = addr;
        info->sections[info->section_count].size = len;
        info->sections[info->section_count].is_text = (i < DOL_TEXT_COUNT);
        ++info->section_count;
        info->loaded_bytes += len;
    }

    info->entry_point = be32(dol + 0xE0u);
    return info->section_count != 0u;
}

static uint32_t dol_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* main.dol as the console loads it: from the offset in the disc header
 * (0x420) for an image - raw, GCM or NKit alike - and sys/main.dol for an
 * extracted folder. main.dol lives outside the FST. A DOL's length is where
 * its furthest section ends. The caller frees. */
uint8_t* mgs_disc_read_main_dol(MgsDisc* disc, size_t* len)
{
    uint8_t hdr[0x100];
    uint8_t* buf;
    uint32_t off, end = 0, i;
    if (disc->kind == MGS_DISC_FOLDER) {
        char path[1200];
        FILE* f;
        long n;
        snprintf(path, sizeof path, "%s/sys/main.dol", disc->root);
        if (!(f = fopen(path, "rb"))) return NULL;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n <= 0 || !(buf = (uint8_t*)malloc((size_t)n))) { fclose(f); return NULL; }
        if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
        fclose(f);
        *len = (size_t)n;
        return buf;
    }
    if (mgs_disc_read_abs(disc, hdr, 0x420u, 4u) != 4) return NULL;
    off = dol_be32(hdr);
    if (mgs_disc_read_abs(disc, hdr, off, sizeof hdr) != (long)sizeof hdr) return NULL;
    for (i = 0; i < 18u; ++i) {
        uint32_t o = dol_be32(hdr + 4u * i), sz = dol_be32(hdr + 0x90u + 4u * i);
        if (sz && o + sz > end) end = o + sz;
    }
    if (!end || end > 16u * 1024u * 1024u) return NULL;
    if (!(buf = (uint8_t*)malloc(end))) return NULL;
    if (mgs_disc_read_abs(disc, buf, off, end) != (long)end) { free(buf); return NULL; }
    *len = end;
    return buf;
}

int mgs_dol_load_from_disc(GuestMemory* mem, MgsDisc* disc, MgsDolInfo* info)
{
    size_t n = 0;
    uint8_t* buf = mgs_disc_read_main_dol(disc, &n);
    int ok;
    if (!buf) return 0;
    ok = mgs_dol_load(mem, buf, n, info);
    free(buf);
    return ok;
}
