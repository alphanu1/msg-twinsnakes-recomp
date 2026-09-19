#include "disc.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Offsets in a GameCube disc header (boot.bin, which is the first 0x440
 * bytes of an image). The magic is what tells an image from anything else.
 */
#define DISC_GAME_ID_OFF   0x000u
#define DISC_NUMBER_OFF    0x006u
#define DISC_MAGIC_OFF     0x01Cu
#define DISC_MAGIC         0xC2339F3Du
#define DISC_FST_OFF       0x424u
#define DISC_FST_SIZE      0x428u

static uint32_t be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static int is_directory(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int read_at(FILE* f, long offset, void* out, size_t n)
{
    if (fseek(f, offset, SEEK_SET) != 0) return 0;
    return fread(out, 1, n, f) == n;
}

/* --- image ---------------------------------------------------------------- */

static int mount_image(MgsDisc* disc, const char* path)
{
    uint8_t header[0x440];
    uint32_t fst_off, fst_size;
    uint8_t* fst_data;

    disc->image = fopen(path, "rb");
    if (!disc->image) return 0;

    if (!read_at(disc->image, 0, header, sizeof header)) goto fail;

    /* Refuse anything that is not a GameCube image rather than guessing.
     * An NKit or a compressed format will fail here, which is the intent:
     * the design document refuses NKit as not byte-exact.
     */
    if (be32(header + DISC_MAGIC_OFF) != DISC_MAGIC) goto fail;

    memcpy(disc->game_id, header + DISC_GAME_ID_OFF, 6);
    disc->game_id[6] = '\0';
    disc->disc_number = header[DISC_NUMBER_OFF];

    fst_off  = be32(header + DISC_FST_OFF);
    fst_size = be32(header + DISC_FST_SIZE);
    if (!fst_off || !fst_size || fst_size > (32u * 1024u * 1024u)) goto fail;

    fst_data = (uint8_t*)malloc(fst_size);
    if (!fst_data) goto fail;
    if (!read_at(disc->image, (long)fst_off, fst_data, fst_size) ||
        !mgs_fst_load(&disc->fst, fst_data, fst_size)) {
        free(fst_data);
        goto fail;
    }
    free(fst_data);

    disc->kind = MGS_DISC_IMAGE;
    disc->mounted = 1;
    return 1;

fail:
    fclose(disc->image);
    disc->image = NULL;
    return 0;
}

/* --- folder --------------------------------------------------------------- */

static int mount_folder(MgsDisc* disc, const char* path)
{
    char buf[1200];
    FILE* f;
    uint8_t header[0x440];
    long n;
    uint8_t* fst_data;

    snprintf(buf, sizeof buf, "%s/sys/boot.bin", path);
    f = fopen(buf, "rb");
    if (!f) return 0;
    if (fread(header, 1, sizeof header, f) != sizeof header) { fclose(f); return 0; }
    fclose(f);

    if (be32(header + DISC_MAGIC_OFF) != DISC_MAGIC) return 0;
    memcpy(disc->game_id, header + DISC_GAME_ID_OFF, 6);
    disc->game_id[6] = '\0';
    disc->disc_number = header[DISC_NUMBER_OFF];

    /* An extracted disc keeps its FST as sys/fst.bin, so the same table
     * serves both backends and path lookup is identical either way.
     */
    snprintf(buf, sizeof buf, "%s/sys/fst.bin", path);
    f = fopen(buf, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 0; }

    fst_data = (uint8_t*)malloc((size_t)n);
    if (!fst_data) { fclose(f); return 0; }
    if (fread(fst_data, 1, (size_t)n, f) != (size_t)n ||
        !mgs_fst_load(&disc->fst, fst_data, (size_t)n)) {
        free(fst_data); fclose(f); return 0;
    }
    free(fst_data);
    fclose(f);

    snprintf(disc->root, sizeof disc->root, "%s", path);
    disc->kind = MGS_DISC_FOLDER;
    disc->mounted = 1;
    return 1;
}

/* --- public --------------------------------------------------------------- */

int mgs_disc_mount(MgsDisc* disc, const char* path)
{
    memset(disc, 0, sizeof *disc);
    if (!path) return 0;
    return is_directory(path) ? mount_folder(disc, path)
                              : mount_image(disc, path);
}

void mgs_disc_unmount(MgsDisc* disc)
{
    if (disc->image) fclose(disc->image);
    mgs_fst_free(&disc->fst);
    memset(disc, 0, sizeof *disc);
}

long mgs_disc_file_size(MgsDisc* disc, const char* path)
{
    uint32_t off, len;
    if (!disc->mounted) return -1;
    if (!mgs_fst_file(&disc->fst, path, &off, &len)) return -1;
    return (long)len;
}

long mgs_disc_read(MgsDisc* disc, const char* path,
                   void* out, uint32_t offset, uint32_t length)
{
    uint32_t file_off, file_len;

    if (!disc->mounted || !out) return -1;
    if (!mgs_fst_file(&disc->fst, path, &file_off, &file_len)) return -1;

    /* Clamp rather than fail. The SDK's DVDRead returns a length and the game
     * checks it, so a read running off the end of a file is a short read, not
     * an error - and treating it as an error here would diverge from the
     * hardware the game was written against.
     */
    if (offset >= file_len) return 0;
    if (length > file_len - offset) length = file_len - offset;

    if (disc->kind == MGS_DISC_IMAGE) {
        if (!read_at(disc->image, (long)file_off + (long)offset, out, length))
            return -1;
        return (long)length;
    } else {
        char buf[1400];
        FILE* f;
        size_t n;
        /* The FST path is relative to files/ on an extracted disc. */
        snprintf(buf, sizeof buf, "%s/files/%s", disc->root, path);
        f = fopen(buf, "rb");
        if (!f) return -1;
        if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return -1; }
        n = fread(out, 1, length, f);
        fclose(f);
        return (long)n;
    }
}

/* Read by ABSOLUTE disc offset.
 *
 * The SDK's synchronous read does not go through a path. DVDReadPrio turns a
 * DVDFileInfo into a disc offset and calls DVDReadAbsAsyncPrio, so the game's
 * loader never names a file at all - which is why a path-only disc layer
 * looked complete and still left the boot blocked in DVDReadPrio.
 *
 * An image can seek. An extracted FOLDER cannot: its files are separate on
 * the host filesystem and there is no image to index into. But the FST
 * records every file's disc offset, so the mapping BACK from an offset to a
 * file is available, and that is what this does. It is the same table the
 * forward lookup uses, read the other way.
 *
 * A read that falls outside every file - the boot header, bi2, the apploader
 * or the FST itself - is refused rather than guessed at. Those live in sys/
 * on an extracted disc and a caller wanting them should ask for them by name.
 */
long mgs_disc_read_abs(MgsDisc* disc, void* out, uint32_t offset, uint32_t length)
{
    uint32_t i;

    if (!disc->mounted || !out) return -1;

    if (disc->kind == MGS_DISC_IMAGE) {
        if (!read_at(disc->image, (long)offset, out, length)) return -1;
        return (long)length;
    }

    for (i = 0; i < disc->fst.entry_count; ++i) {
        MgsFstEntry e;
        char path[1024];

        if (!mgs_fst_entry(&disc->fst, i, &e) || e.is_dir) continue;
        if (offset < e.offset_or_parent ||
            offset >= e.offset_or_parent + e.length_or_next)
            continue;

        if (!mgs_fst_path(&disc->fst, i, path, sizeof path)) return -1;
        if (getenv("MGS_TRACE_DVD"))
            fprintf(stderr, "[disc] abs 0x%08X -> %s + 0x%X (file at 0x%08X, %u bytes)\n",
                    offset, path, offset - e.offset_or_parent,
                    e.offset_or_parent, e.length_or_next);
        /* A read may legitimately be shorter than asked for; mgs_disc_read
         * already clamps to the file's length. */
        return mgs_disc_read(disc, path, out, offset - e.offset_or_parent, length);
    }
    return -1;
}
