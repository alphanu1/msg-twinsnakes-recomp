/* The launcher's work that is not drawing: settings, the disc check, and
 * knowing whether the native game exists. C, so it can be tested and used
 * without the UI; host/launcher.cpp is only the screens. */
#ifndef MGS_LAUNCHER_CORE_H
#define MGS_LAUNCHER_CORE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Saved to settings.ini in the per-user config folder. Every field is
 * something that was an environment variable before the launcher existed;
 * the launcher sets those variables from here before the game starts, so
 * the game has one way of reading each setting. */
typedef struct MgsSettings {
    int  volume;          /* percent, 0-200; MGS_VOLUME. 100 = the console */
    int  fullscreen;      /* 0 windowed, 1 fullscreen desktop */
    int  skip_launcher;   /* start straight into the game next time */
    char disc1[1024];
    char disc2[1024];
} MgsSettings;

void mgs_settings_defaults(MgsSettings* s);
int  mgs_settings_load(MgsSettings* s);     /* defaults when absent */
int  mgs_settings_save(const MgsSettings* s);
/* The environment variables the game reads, set from the settings. */
void mgs_settings_apply(const MgsSettings* s);

typedef enum {
    MGS_DISC_UNCHECKED = 0,
    MGS_DISC_OK,              /* the right game, disc and executables */
    MGS_DISC_MISSING,         /* no file at that path */
    MGS_DISC_UNREADABLE,      /* not a GameCube image or folder */
    MGS_DISC_WRONG_GAME,      /* another game, or another region */
    MGS_DISC_WRONG_NUMBER,    /* disc 2 given as disc 1, or the reverse */
    MGS_DISC_WRONG_BUILD      /* the right game, a different revision */
} MgsDiscStatus;

/* Check `path` is disc `number` (1 or 2) of the PAL release with the
 * executables this port was built from, hashed as extracted (so an ISO, a
 * GCM, an NKit image and an extracted folder all check the same way).
 * `message` gets one line for a person to read. */
MgsDiscStatus mgs_launcher_check_disc(const char* path, unsigned number,
                                      char* message, size_t message_size);

/* The memory card file the game uses: $MGS_CARD_PATH, else
 * saves/slot_a.raw from where the game was started. The game and the
 * launcher both ask here, so they cannot disagree. */
const char* mgs_card_path(void);

/* Put the current card aside as <card>.backup-<date>-<time>.raw; the game
 * formats a fresh one at the next start. Writes the backup's name into
 * `backup`. Returns 1 when moved (or when there was nothing to move). */
int mgs_card_reset(char* backup, size_t backup_size);

#ifdef __cplusplus
}
#endif

#endif
