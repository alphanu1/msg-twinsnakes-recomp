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

    /* .bss is not in the file - it is an address and a length to clear. The
     * SDK's own startup relies on it being zero, and guest memory is already
     * zeroed, but clearing explicitly means a reused GuestMemory behaves the
     * same as a fresh one. */
    info->bss_address = be32(dol + 0xD8u);
    info->bss_size    = be32(dol + 0xDCu);
    if (info->bss_address && info->bss_size) {
        uint8_t* bss = guest_ptr(mem, info->bss_address, info->bss_size);
        if (bss) memset(bss, 0, info->bss_size);
    }

    info->entry_point = be32(dol + 0xE0u);
    return info->section_count != 0u;
}

int mgs_dol_load_from_disc(GuestMemory* mem, MgsDisc* disc, MgsDolInfo* info)
{
    /* main.dol lives in sys/, outside the FST, so it is read from the disc
     * layer's own path rather than looked up. */
    char path[1200];
    FILE* f;
    long n;
    uint8_t* buf;
    int ok;

    if (disc->kind != MGS_DISC_FOLDER) return 0;   /* image path: TODO */
    snprintf(path, sizeof path, "%s/sys/main.dol", disc->root);

    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }

    buf = (uint8_t*)malloc((size_t)n);
    if (!buf) { fclose(f); return 0; }
    ok = fread(buf, 1, (size_t)n, f) == (size_t)n;
    fclose(f);

    ok = ok && mgs_dol_load(mem, buf, (size_t)n, info);
    free(buf);
    return ok;
}
