/* The disc filesystem table.
 *
 * A GameCube FST is a flat array of 12-byte entries followed by a string
 * table. Entry 0 is the root, and its length field carries the total entry
 * count - so the string table's position is derived, not stored.
 *
 *   byte 0     : 0 = file, 1 = directory
 *   bytes 1..3 : offset of the name in the string table (24-bit!)
 *   bytes 4..7 : file   -> offset of the data on the disc
 *                directory -> index of the PARENT entry
 *   bytes 8..11: file   -> length in bytes
 *                directory -> index of the entry AFTER this directory's last
 *                             child, which is what makes the tree walkable
 *                             without recursion
 *
 * Directories are ranges rather than links: a directory at index i owns
 * entries i+1 .. next-1. That is why a path lookup can be a scan bounded by
 * the current directory rather than a tree walk.
 *
 * Everything is big-endian, and the name offset is 24-bit, packed into the
 * same word as the type byte. Reading it as a 32-bit value would fold the
 * type in and give a name offset megabytes wrong.
 */
#ifndef MGS_FST_H
#define MGS_FST_H

#include <stdint.h>
#include <stddef.h>

typedef struct MgsFstEntry {
    int      is_dir;
    uint32_t name_offset;
    uint32_t offset_or_parent;
    uint32_t length_or_next;
} MgsFstEntry;

typedef struct MgsFst {
    uint8_t*    raw;
    size_t      size;
    uint32_t    entry_count;
    const char* strings;     /* into raw; not separately owned */
    size_t      strings_size;
} MgsFst;

int  mgs_fst_load(MgsFst* fst, const void* data, size_t size);
void mgs_fst_free(MgsFst* fst);

int  mgs_fst_entry(const MgsFst* fst, uint32_t index, MgsFstEntry* out);
const char* mgs_fst_name(const MgsFst* fst, const MgsFstEntry* e);

/* Resolve a '/'-separated path to an entry index, or return 0 (the root,
 * which is never a file) if it is not found. Case-insensitive, because the
 * SDK's own DVDConvertPathToEntrynum is.
 */
uint32_t mgs_fst_find(const MgsFst* fst, const char* path);

/* Convenience: resolve and return a file's disc offset and length. */
int mgs_fst_file(const MgsFst* fst, const char* path,
                 uint32_t* offset, uint32_t* length);

#endif
