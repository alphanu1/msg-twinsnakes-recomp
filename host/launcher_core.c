#include "launcher_core.h"

#include "sha1.h"
#include "dvd/disc.h"
#include "dvd/disc_locate.h"
#include "dvd/dol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* THE EXECUTABLES THIS PORT WAS BUILT FROM, as config/GGSPA4.toml records
 * them - repeated here because the launcher ships to players, who have no
 * source tree to read the config from. tests/test_launcher.c checks the two
 * agree, so they cannot drift apart. */
#define MGS_GAME_ID        "GGSPA4"
#define MGS_MAIN_DOL_SHA1  "124bca49886033df5053d3be1f8748a218ddf60f"
#define MGS_REL_SHA1       "99910b99144cf1c1cf7f8a2183b6ca5e595f65d4"
#define MGS_REL_PATH       "shared/mgso_pal.rel"

/* ---- settings --------------------------------------------------------- */

void mgs_settings_defaults(MgsSettings* s)
{
    memset(s, 0, sizeof *s);
    s->volume = 60;           /* Ben's choice by ear (HANDOFF F377) */
}

static int settings_path(char* out, size_t n)
{
    char dir[900];
    if (!mgs_config_dir(dir, sizeof dir)) return 0;
    snprintf(out, n, "%s/settings.ini", dir);
    return 1;
}

int mgs_settings_load(MgsSettings* s)
{
    char path[1024], line[1200];
    FILE* f;
    mgs_settings_defaults(s);
    if (!settings_path(path, sizeof path) || !(f = fopen(path, "r"))) return 0;
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        char* v;
        size_t len;
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        v = eq + 1;
        len = strlen(v);
        while (len && (v[len - 1] == '\n' || v[len - 1] == '\r')) v[--len] = '\0';
        if (!strcmp(line, "volume"))             s->volume = atoi(v);
        else if (!strcmp(line, "fullscreen"))    s->fullscreen = atoi(v) != 0;
        else if (!strcmp(line, "skip_launcher")) s->skip_launcher = atoi(v) != 0;
        else if (!strcmp(line, "disc1")) snprintf(s->disc1, sizeof s->disc1, "%s", v);
        else if (!strcmp(line, "disc2")) snprintf(s->disc2, sizeof s->disc2, "%s", v);
    }
    fclose(f);
    if (s->volume < 0) s->volume = 0;
    if (s->volume > 200) s->volume = 200;
    return 1;
}

int mgs_settings_save(const MgsSettings* s)
{
    char path[1024];
    FILE* f;
    if (!mgs_config_make_dir() || !settings_path(path, sizeof path)) return 0;
    if (!(f = fopen(path, "w"))) return 0;
    fprintf(f, "# Metal Gear Solid: The Twin Snakes, native port - launcher settings\n");
    fprintf(f, "volume=%d\nfullscreen=%d\nskip_launcher=%d\ndisc1=%s\ndisc2=%s\n",
            s->volume, s->fullscreen, s->skip_launcher, s->disc1, s->disc2);
    fclose(f);
    /* The game finds discs through the remembered paths too, so a run
     * without the launcher (--play) uses what was chosen here. */
    if (s->disc1[0]) mgs_disc_remember(1u, s->disc1);
    if (s->disc2[0]) mgs_disc_remember(2u, s->disc2);
    return 1;
}

void mgs_settings_apply(const MgsSettings* s)
{
    char v[16];
    snprintf(v, sizeof v, "%d", s->volume);
#ifdef _WIN32
    _putenv_s("MGS_VOLUME", v);
#else
    setenv("MGS_VOLUME", v, 1);
#endif
}

/* ---- the disc check --------------------------------------------------- */


MgsDiscStatus mgs_launcher_check_disc(const char* path, unsigned number,
                                      char* msg, size_t msg_size)
{
    MgsDisc d;
    struct stat st;
    uint8_t* buf;
    size_t len = 0;
    long rel;
    char hex[41];

    if (!path || !*path || stat(path, &st) != 0) {
        snprintf(msg, msg_size, "Not found.");
        return MGS_DISC_MISSING;
    }
    memset(&d, 0, sizeof d);
    if (!mgs_disc_mount(&d, path)) {
        snprintf(msg, msg_size, "Not a GameCube disc image this port can read "
                                "(.iso, .gcm, NKit, or an extracted folder).");
        return MGS_DISC_UNREADABLE;
    }
    if (strcmp(d.game_id, MGS_GAME_ID) != 0) {
        snprintf(msg, msg_size, "This is %s. The port needs %s: The Twin Snakes, "
                                "European release.", d.game_id, MGS_GAME_ID);
        mgs_disc_unmount(&d);
        return MGS_DISC_WRONG_GAME;
    }
    if ((unsigned)d.disc_number + 1u != number) {
        snprintf(msg, msg_size, "This is disc %u; this slot wants disc %u.",
                 (unsigned)d.disc_number + 1u, number);
        mgs_disc_unmount(&d);
        return MGS_DISC_WRONG_NUMBER;
    }
    buf = mgs_disc_read_main_dol(&d, &len);
    if (!buf) {
        snprintf(msg, msg_size, "The disc's main.dol could not be read.");
        mgs_disc_unmount(&d);
        return MGS_DISC_UNREADABLE;
    }
    mgs_sha1_hex(buf, len, hex);
    free(buf);
    if (strcmp(hex, MGS_MAIN_DOL_SHA1) != 0) {
        snprintf(msg, msg_size, "A different revision of the game (main.dol %.12s...).", hex);
        mgs_disc_unmount(&d);
        return MGS_DISC_WRONG_BUILD;
    }
    rel = mgs_disc_file_size(&d, MGS_REL_PATH);
    if (rel <= 0 || !(buf = (uint8_t*)malloc((size_t)rel)) ||
        mgs_disc_read(&d, MGS_REL_PATH, buf, 0u, (uint32_t)rel) != rel) {
        if (rel > 0) free(buf);
        snprintf(msg, msg_size, "The game's engine (%s) could not be read.", MGS_REL_PATH);
        mgs_disc_unmount(&d);
        return MGS_DISC_UNREADABLE;
    }
    mgs_sha1_hex(buf, (size_t)rel, hex);
    free(buf);
    mgs_disc_unmount(&d);
    if (strcmp(hex, MGS_REL_SHA1) != 0) {
        snprintf(msg, msg_size, "A different revision of the game's engine (%.12s...).", hex);
        return MGS_DISC_WRONG_BUILD;
    }
    snprintf(msg, msg_size, "Disc %u of the European release; executables verified.", number);
    return MGS_DISC_OK;
}

/* ---- the memory card (F380) ------------------------------------------- */

const char* mgs_card_path(void)
{
    const char* cp = getenv("MGS_CARD_PATH");
    return cp && *cp ? cp : "saves/slot_a.raw";
}

int mgs_card_reset(char* backup, size_t backup_size)
{
    const char* path = mgs_card_path();
    struct stat st;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    if (backup_size) backup[0] = '\0';
    if (stat(path, &st) != 0) return 1;         /* nothing there: already fresh */
    snprintf(backup, backup_size, "%s.backup-%04d%02d%02d-%02d%02d%02d.raw", path,
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    return rename(path, backup) == 0;
}

