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

/* Per-file read totals, reported at exit by mgs_disc_report.
 *
 * A fixed table rather than a growing one: a boot touches a few dozen files
 * and an overflow row is more useful than an allocation that can fail in the
 * middle of a read. */
#define DISC_TALLY_MAX 96
static struct {
    char     path[128];
    uint64_t reads, bytes;
    uint32_t last_offset, max_end;
    uint32_t first_lr[3], last_lr[3];  /* the call chain in, first and last */
} s_tally[DISC_TALLY_MAX];
/* Set by the DVD shims from the guest call chain before each read. */
static uint32_t s_requester[3];
void mgs_disc_set_requester3(uint32_t lr0, uint32_t lr1, uint32_t lr2)
{
    s_requester[0] = lr0; s_requester[1] = lr1; s_requester[2] = lr2;
}
static unsigned s_tally_n;
static uint64_t s_tally_lost_reads, s_tally_lost_bytes;

static void disc_tally(const char* path, uint32_t length, uint32_t offset)
{
    unsigned i;
    for (i = 0u; i < s_tally_n; ++i) {
        if (!strcmp(s_tally[i].path, path)) break;
    }
    if (i == s_tally_n) {
        if (s_tally_n >= DISC_TALLY_MAX || strlen(path) >= sizeof s_tally[0].path) {
            ++s_tally_lost_reads; s_tally_lost_bytes += length;
            return;
        }
        ++s_tally_n;
        strcpy(s_tally[i].path, path);
    }
    if (!s_tally[i].reads) memcpy(s_tally[i].first_lr, s_requester, sizeof s_requester);
    memcpy(s_tally[i].last_lr, s_requester, sizeof s_requester);
    ++s_tally[i].reads;
    s_tally[i].bytes += length;
    s_tally[i].last_offset = offset;
    if (offset + length > s_tally[i].max_end) s_tally[i].max_end = offset + length;
}

void mgs_disc_report(FILE* out)
{
    unsigned i, j;
    if (!s_tally_n) { fprintf(out, "disc: no reads\n"); return; }
    fprintf(out, "disc: %u files read\n", s_tally_n);
    /* Busiest first: the file a stall is in is usually the one being read
     * hardest just before it. */
    for (j = 0u; j < s_tally_n; ++j) {
        unsigned best = j;
        for (i = j + 1u; i < s_tally_n; ++i)
            if (s_tally[i].bytes > s_tally[best].bytes) best = i;
        if (best != j) {
            char t[sizeof s_tally[0]];
            memcpy(t, &s_tally[j], sizeof s_tally[0]);
            memcpy(&s_tally[j], &s_tally[best], sizeof s_tally[0]);
            memcpy(&s_tally[best], t, sizeof s_tally[0]);
        }
        fprintf(out, "  %-40s %5llu reads  %8llu bytes  last +0x%X  reached +0x%X\n",
                s_tally[j].path,
                (unsigned long long)s_tally[j].reads,
                (unsigned long long)s_tally[j].bytes,
                s_tally[j].last_offset, s_tally[j].max_end);
        /* Both ends, because they differ exactly when it matters: a file
         * opened by one piece of code and streamed by another says where to
         * look when the streaming stops. */
        if (s_tally[j].first_lr[0])
            fprintf(out, "      first asked by 0x%08X < 0x%08X < 0x%08X\n",
                    s_tally[j].first_lr[0], s_tally[j].first_lr[1],
                    s_tally[j].first_lr[2]);
        if (s_tally[j].last_lr[0] &&
            memcmp(s_tally[j].last_lr, s_tally[j].first_lr,
                   sizeof s_tally[j].last_lr))
            fprintf(out, "      last  asked by 0x%08X < 0x%08X < 0x%08X\n",
                    s_tally[j].last_lr[0], s_tally[j].last_lr[1],
                    s_tally[j].last_lr[2]);
    }
    if (s_tally_lost_reads)
        fprintf(out, "  (%llu reads of %llu bytes not attributed: table full)\n",
                (unsigned long long)s_tally_lost_reads,
                (unsigned long long)s_tally_lost_bytes);
}

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
    long got;
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
        /* NAMED BY DEFAULT, NOT BEHIND A SWITCH.
         *
         * Which file the game is reading, and where in it, is the first
         * question asked whenever loading looks stuck - and it was behind an
         * environment variable, so an ordinary run showed nothing and the
         * question could only be answered by knowing to re-run. Reads are
         * few: a whole boot is about four hundred. The first hundred are
         * named, then every twentieth, so a long session cannot flood the
         * log but a stall still leaves a trail. */
        {
            static unsigned long seen;
            ++seen;
            if (getenv("MGS_TRACE_DVD") || seen <= 100ul || (seen % 20ul) == 0ul)
                fprintf(stderr, "[disc] read %7u bytes  %s + 0x%X\n",
                        length, path, offset - e.offset_or_parent);
        }
        /* AND A TALLY PER FILE, because the sampled log above answers
         * "what is it reading" but not "how much, and where did it stop".
         * Chasing a stalled movie, the trace showed a single read of
         * movie.dat because the other seven fell between samples - which is
         * indistinguishable from the game having read it once. A count and
         * a last offset per file are a few bytes of state and remove a whole
         * class of re-run. */
        disc_tally(path, length, offset - e.offset_or_parent);
        /* AN ABSOLUTE READ IS NOT A FILE READ, AND MUST NOT BE CLAMPED.
         *
         * mgs_disc_read trims a request to the end of the file it names,
         * which is right when a game reads a file: the SDK returns a length
         * and the game checks it. It is wrong here. This is a read by DISC
         * OFFSET, and on real media the bytes after a file are its alignment
         * padding, so the drive returns everything that was asked for and the
         * caller is never told the file ended.
         *
         * The intro movie is what this cost. Five of its 406 reads were
         * trimmed, by four to twenty-six bytes, and each time the game
         * resumed from where the trim left it - so every frame boundary after
         * that point was shifted, the decoder lost the stream, and playback
         * stopped after eleven frames. A frozen picture is a long way from a
         * disc read being two dozen bytes light, which is why this took a
         * trace of every read to find rather than a guess.
         *
         * So the tail is zeroed, which is what that padding holds.
         */
        got = mgs_disc_read(disc, path, out, offset - e.offset_or_parent, length);
        if (got >= 0 && (uint32_t)got < length) {
            memset((uint8_t*)out + got, 0, length - (uint32_t)got);
            got = (long)length;
        }
        return got;
    }
    return -1;
}
