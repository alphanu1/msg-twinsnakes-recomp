#include "fst.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define FST_ENTRY_SIZE 12u

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int mgs_fst_load(MgsFst* fst, const void* data, size_t size)
{
    uint32_t count;
    size_t strings_off;

    memset(fst, 0, sizeof *fst);
    if (!data || size < FST_ENTRY_SIZE) return 0;

    /* The root's length field is the entry count, including the root. */
    count = be32((const uint8_t*)data + 8);
    if (count == 0u) return 0;

    strings_off = (size_t)count * FST_ENTRY_SIZE;
    if (strings_off > size) return 0;      /* truncated or not an FST */

    fst->raw = (uint8_t*)malloc(size);
    if (!fst->raw) return 0;
    memcpy(fst->raw, data, size);

    fst->size = size;
    fst->entry_count = count;
    fst->strings = (const char*)fst->raw + strings_off;
    fst->strings_size = size - strings_off;
    return 1;
}

void mgs_fst_free(MgsFst* fst)
{
    free(fst->raw);
    memset(fst, 0, sizeof *fst);
}

int mgs_fst_entry(const MgsFst* fst, uint32_t index, MgsFstEntry* out)
{
    const uint8_t* p;
    if (!fst->raw || index >= fst->entry_count) return 0;
    p = fst->raw + (size_t)index * FST_ENTRY_SIZE;

    out->is_dir = p[0] != 0;
    /* 24-bit name offset, sharing its word with the type byte. Masking a
     * be32 would fold the type in and put the name megabytes away.
     */
    out->name_offset = ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    out->offset_or_parent = be32(p + 4);
    out->length_or_next = be32(p + 8);
    return 1;
}

const char* mgs_fst_name(const MgsFst* fst, const MgsFstEntry* e)
{
    if (!fst->strings || e->name_offset >= fst->strings_size) return "";
    return fst->strings + e->name_offset;
}

/* Does the entry name `a` equal the first `n` characters of `b`, exactly?
 *
 * The final test is on `a`, not `b`: we are asking whether the entry's name
 * ENDS at n, not whether the path does. Testing `b[n]` instead passes for a
 * top-level file, where the path really does end there, and fails for every
 * nested one - which is exactly how this first presented.
 */
static int name_eq(const char* a, const char* b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (!a[i]) return 0;                       /* name shorter than segment */
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    }
    return a[n] == '\0';                           /* and no longer, either */
}

uint32_t mgs_fst_find(const MgsFst* fst, const char* path)
{
    uint32_t dir = 0u;                 /* start at the root */
    uint32_t dir_end;
    MgsFstEntry root;

    if (!fst->raw || !path) return 0u;
    if (!mgs_fst_entry(fst, 0u, &root)) return 0u;
    dir_end = root.length_or_next;

    while (*path == '/') ++path;

    while (*path) {
        const char* slash = strchr(path, '/');
        size_t seg = slash ? (size_t)(slash - path) : strlen(path);
        uint32_t i;
        uint32_t found = 0u;

        if (seg == 0u) break;

        /* Scan only this directory's range. Children of a directory at index
         * i occupy i+1 .. next-1, so nested directories are skipped over by
         * jumping to their own `next` rather than descending into them.
         */
        for (i = dir + 1u; i < dir_end; ) {
            MgsFstEntry e;
            if (!mgs_fst_entry(fst, i, &e)) break;
            if (name_eq(mgs_fst_name(fst, &e), path, seg)) { found = i; break; }
            i = e.is_dir ? e.length_or_next : i + 1u;
        }

        if (!found) return 0u;

        if (!slash) return found;      /* last segment: this is the answer */

        {
            MgsFstEntry e;
            if (!mgs_fst_entry(fst, found, &e) || !e.is_dir) return 0u;
            dir = found;
            dir_end = e.length_or_next;
        }
        path = slash + 1;
        while (*path == '/') ++path;
    }
    return 0u;
}

int mgs_fst_file(const MgsFst* fst, const char* path,
                 uint32_t* offset, uint32_t* length)
{
    uint32_t index = mgs_fst_find(fst, path);
    MgsFstEntry e;

    if (!index || !mgs_fst_entry(fst, index, &e) || e.is_dir) return 0;
    if (offset) *offset = e.offset_or_parent;
    if (length) *length = e.length_or_next;
    return 1;
}
