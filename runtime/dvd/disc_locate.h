/* Where the user's disc images live.
 *
 * NOT required to sit beside the executable. Two reasons: the images are the
 * user's property and shouldn't be tied to an install that updates or gets
 * reinstalled under them, and the design document specifies a launcher that
 * asks for the images and hash-checks them.
 *
 * So the program folder is SUPPORTED but never assumed. Resolution order,
 * most explicit first:
 *
 *   1. an explicit path passed on the command line
 *   2. $MGS_DISC1 / $MGS_DISC2                    - scripting and CI
 *   3. the remembered path from a previous run    - what the launcher writes
 *   4. ./discs/<id>/discN                         - the development layout
 *   5. beside the executable                      - the portable-zip case
 *
 * Nothing here ever searches the whole filesystem for a disc image. A port
 * that goes hunting for game data it was not pointed at is doing something
 * the user did not ask for.
 */
#ifndef MGS_DISC_LOCATE_H
#define MGS_DISC_LOCATE_H

#include <stddef.h>

typedef enum {
    MGS_DISC_SOURCE_NONE = 0,
    MGS_DISC_SOURCE_ARGUMENT,
    MGS_DISC_SOURCE_ENVIRONMENT,
    MGS_DISC_SOURCE_REMEMBERED,
    MGS_DISC_SOURCE_DEV_LAYOUT,
    MGS_DISC_SOURCE_BESIDE_EXE
} MgsDiscSource;

/* Fill `out` with a path to disc `number` (1 or 2), or return NONE.
 * `explicit_path` may be NULL. `exe_dir` may be NULL if unknown.
 */
MgsDiscSource mgs_disc_locate(unsigned number, const char* explicit_path,
                              const char* exe_dir, const char* game_id,
                              char* out, size_t out_size);

const char* mgs_disc_source_name(MgsDiscSource source);

/* Remember a path the user chose, so they are asked once rather than every
 * launch. Written to the user's config directory, never into the install.
 */
int mgs_disc_remember(unsigned number, const char* path);

#endif
