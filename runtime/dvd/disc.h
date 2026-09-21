/* A mounted disc: one seam, two backends.
 *
 * Physical GameCube discs cannot be read by a PC drive - they are 8 cm
 * miniDVDs with Nintendo's own encoding, and a PC drive cannot see the
 * filesystem at all. So a disc here is always something the user dumped:
 * an image file, or a folder extracted from one.
 *
 * Both resolve paths through the same FST, so DVDOpen does not care which it
 * has. For an image we seek to the FST's offset; for a folder we open the
 * file the FST names. Keeping that behind one interface is what stops the
 * development path and the shipped path diverging.
 */
#ifndef MGS_DISC_H
#define MGS_DISC_H

#include "fst.h"
#include <stdint.h>
#include <stdio.h>

typedef enum {
    MGS_DISC_NONE = 0,
    MGS_DISC_IMAGE,     /* .iso / .gcm: raw, FST offsets index the file */
    MGS_DISC_FOLDER     /* extracted: FST gives the path, the OS gives the data */
} MgsDiscKind;

typedef struct MgsDisc {
    MgsDiscKind kind;
    FILE*       image;          /* IMAGE only */
    char        root[1024];     /* FOLDER only: the directory holding sys/ and files/ */
    MgsFst      fst;
    char        game_id[8];
    uint8_t     disc_number;
    int         mounted;
} MgsDisc;

/* Mount an image or a folder; the kind is detected, not declared, so the
 * caller can pass whatever the user gave it.
 */
int  mgs_disc_mount(MgsDisc* disc, const char* path);
void mgs_disc_unmount(MgsDisc* disc);

/* Read `length` bytes of a file at `path`, starting `offset` bytes in.
 * Returns bytes read, or -1. Partial reads are honest: the SDK's own
 * DVDRead returns a length, and the game checks it.
 */
long mgs_disc_read(MgsDisc* disc, const char* path,
                   void* out, uint32_t offset, uint32_t length);

/* Read by absolute disc offset. The SDK's synchronous read path never names
 * a file: DVDReadPrio resolves the file info to a disc offset itself. */
long mgs_disc_read_abs(MgsDisc* disc, void* out, uint32_t offset, uint32_t length);

/* File length, or -1 if absent. DVDOpen needs this to fill its file info. */
long mgs_disc_file_size(MgsDisc* disc, const char* path);

/* What was read, per file, hottest first - counts, totals, and how far into
 * each the game got. Answers "where did it stop" without a re-run. */
void mgs_disc_report(FILE* out);

/* Who asked for the next read. Set by the DVD shims from the guest link
 * register just before the read happens, so the tally can say which code
 * reads each file - the question that matters when reads stop. */
void mgs_disc_set_requester3(uint32_t lr0, uint32_t lr1, uint32_t lr2);

#endif
