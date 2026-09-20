#include "disc_locate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int exists(const char* path)
{
    struct stat st;
    return path && *path && stat(path, &st) == 0;
}

/* The user's config directory, per the XDG basedir spec on Linux. Config and
 * game data are kept apart deliberately: the remembered PATH is ours to
 * write, the image it points at is the user's and we never copy or move it.
 */
static int config_path(char* out, size_t n, unsigned number)
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(out, n, "%s/twin-snakes/disc%u.path", xdg, number);
    else if (home && *home)
        snprintf(out, n, "%s/.config/twin-snakes/disc%u.path", home, number);
    else
        return 0;
    return 1;
}

static int read_remembered(unsigned number, char* out, size_t n)
{
    char cfg[1024];
    FILE* f;
    size_t len;

    if (!config_path(cfg, sizeof cfg, number)) return 0;
    f = fopen(cfg, "r");
    if (!f) return 0;
    if (!fgets(out, (int)n, f)) { fclose(f); return 0; }
    fclose(f);

    len = strlen(out);
    while (len && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
    return len != 0;
}

MgsDiscSource mgs_disc_locate(unsigned number, const char* explicit_path,
                              const char* exe_dir, const char* game_id,
                              char* out, size_t out_size)
{
    char buf[1024];

    if (number != 1u && number != 2u) return MGS_DISC_SOURCE_NONE;

    /* 1. Explicit wins, and is NOT checked for existence here: if the user
     * named a path, the right error is "that path did not mount", naming
     * what they asked for, rather than silently falling through to something
     * else they did not.
     */
    if (explicit_path && *explicit_path) {
        snprintf(out, out_size, "%s", explicit_path);
        return MGS_DISC_SOURCE_ARGUMENT;
    }

    /* 2. Environment, for scripting and CI. */
    {
        const char* env = getenv(number == 1u ? "MGS_DISC1" : "MGS_DISC2");
        if (env && *env) {
            snprintf(out, out_size, "%s", env);
            return MGS_DISC_SOURCE_ENVIRONMENT;
        }
    }

    /* 3. What the launcher remembered. */
    if (read_remembered(number, buf, sizeof buf) && exists(buf)) {
        snprintf(out, out_size, "%s", buf);
        return MGS_DISC_SOURCE_REMEMBERED;
    }

    /* 4. The development layout, which is what this repository uses.
     *
     * SEARCHED RELATIVE TO THE EXECUTABLE AS WELL AS THE WORKING DIRECTORY,
     * and upwards from it. Looking only in the working directory meant the
     * binary ran from the repository root and nowhere else - which is the
     * entire reason a launcher script was still needed - and "it only works
     * if you start it from the right folder" is not a property an executable
     * should have. Four levels covers a build tree this deep
     * (build/runtime/host) reaching a checkout root. */
    if (game_id && *game_id) {
        const char* bases[5];
        char up[4][1024];
        unsigned nb = 0, k;

        bases[nb++] = ".";
        if (exe_dir && *exe_dir) {
            bases[nb++] = exe_dir;
            snprintf(up[0], sizeof up[0], "%s/..", exe_dir);
            snprintf(up[1], sizeof up[1], "%s/../..", exe_dir);
            snprintf(up[2], sizeof up[2], "%s/../../..", exe_dir);
            bases[nb++] = up[0];
            bases[nb++] = up[1];
            bases[nb++] = up[2];
        }
        for (k = 0; k < nb; ++k) {
            /* The working directory is spelled without a "./" prefix: it is
             * what every log line and every test has always shown, and a
             * gratuitous "./" in front of a path people read is noise. */
            if (k == 0u)
                snprintf(buf, sizeof buf, "discs/%s/disc%u", game_id, number);
            else
                snprintf(buf, sizeof buf, "%s/discs/%s/disc%u", bases[k], game_id, number);
            if (exists(buf)) {
                snprintf(out, out_size, "%s", buf);
                return MGS_DISC_SOURCE_DEV_LAYOUT;
            }
        }
    }

    /* 5. Beside the executable: the portable-zip case, supported but last. */
    if (exe_dir && *exe_dir) {
        static const char* const names[] = { "disc%u.iso", "disc%u.gcm", "Disc%u.iso" };
        size_t i;
        for (i = 0; i < sizeof names / sizeof names[0]; ++i) {
            char leaf[64];
            snprintf(leaf, sizeof leaf, names[i], number);
            snprintf(buf, sizeof buf, "%s/%s", exe_dir, leaf);
            if (exists(buf)) {
                snprintf(out, out_size, "%s", buf);
                return MGS_DISC_SOURCE_BESIDE_EXE;
            }
        }
    }

    out[0] = '\0';
    return MGS_DISC_SOURCE_NONE;
}

const char* mgs_disc_source_name(MgsDiscSource source)
{
    switch (source) {
    case MGS_DISC_SOURCE_ARGUMENT:    return "command line";
    case MGS_DISC_SOURCE_ENVIRONMENT: return "environment";
    case MGS_DISC_SOURCE_REMEMBERED:  return "remembered path";
    case MGS_DISC_SOURCE_DEV_LAYOUT:  return "development layout";
    case MGS_DISC_SOURCE_BESIDE_EXE:  return "beside the executable";
    default:                          return "not found";
    }
}

int mgs_disc_remember(unsigned number, const char* path)
{
    char cfg[1024];
    char dir[1024];
    char* slash;
    FILE* f;

    if (!path || !*path) return 0;
    if (!config_path(cfg, sizeof cfg, number)) return 0;

    /* mkdir -p: create every missing parent, not just the last one. The
     * platform guarantees ~/.config exists, but $XDG_CONFIG_HOME may be
     * anywhere - including a path the caller has only just chosen - so
     * assuming the parent is there is wrong in exactly the case where this
     * matters.
     */
    snprintf(dir, sizeof dir, "%s", cfg);
    slash = strrchr(dir, '/');
    if (slash) {
        char* p;
        *slash = '\0';
        for (p = dir + 1; *p; ++p) {
            if (*p != '/') continue;
            *p = '\0';
            mkdir(dir, 0755);        /* EEXIST is the normal case */
            *p = '/';
        }
        mkdir(dir, 0755);
    }

    f = fopen(cfg, "w");
    if (!f) return 0;
    fprintf(f, "%s\n", path);
    fclose(f);
    return 1;
}
