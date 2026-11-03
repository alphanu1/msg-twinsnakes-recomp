/* Remappable controls: presets, the controls.ini file, and the one copy
 * the input code reads. See controls.h for the shape of the thing.
 *
 * SDL is used only to turn key names into keycodes and back
 * (SDL_GetKeyFromName/SDL_GetKeyName); the tables and the parser are data,
 * which is what tests/test_controls.c exercises.
 */
#include "controls.h"

#include "../dvd/disc_locate.h"   /* mgs_config_dir, with the settings */

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the buttons ------------------------------------------------------- */

static const struct { const char* id; const char* name; uint16_t bit; }
s_controls_buttons[MGS_BTN_COUNT] = {
    { "a",     "A",      0x0100u },
    { "b",     "B",      0x0200u },
    { "x",     "X",      0x0400u },
    { "y",     "Y",      0x0800u },
    { "l",     "L",      0x0040u },
    { "r",     "R",      0x0020u },
    { "z",     "Z",      0x0010u },
    { "start", "Start",  0x1000u },
    { "up",    "D-Up",   0x0008u },
    { "down",  "D-Down", 0x0004u },
    { "left",  "D-Left", 0x0001u },
    { "right", "D-Right",0x0002u },
};

uint16_t mgs_controls_button_bit(int gc_button)
{
    return (gc_button >= 0 && gc_button < MGS_BTN_COUNT)
         ? s_controls_buttons[gc_button].bit : 0u;
}

const char* mgs_controls_button_name(int gc_button)
{
    return (gc_button >= 0 && gc_button < MGS_BTN_COUNT)
         ? s_controls_buttons[gc_button].name : "?";
}

const char* mgs_controls_button_id(int gc_button)
{
    return (gc_button >= 0 && gc_button < MGS_BTN_COUNT)
         ? s_controls_buttons[gc_button].id : NULL;
}

int mgs_controls_button_from_id(const char* id)
{
    int i;
    if (!id) return -1;
    for (i = 0; i < MGS_BTN_COUNT; ++i)
        if (!strcmp(id, s_controls_buttons[i].id)) return i;
    return -1;
}

/* ---- gamepad button names ---------------------------------------------- */

static const char* const s_controls_padbtn_names[MGS_PADBTN_COUNT] = {
    "south", "east", "west", "north", "back", "guide", "start",
    "left_stick", "right_stick", "left_shoulder", "right_shoulder",
    "dpad_up", "dpad_down", "dpad_left", "dpad_right",
};

const char* mgs_controls_padbtn_name(int padbtn)
{
    return (padbtn >= 0 && padbtn < MGS_PADBTN_COUNT)
         ? s_controls_padbtn_names[padbtn] : "none";
}

int mgs_controls_padbtn_from_name(const char* name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < MGS_PADBTN_COUNT; ++i)
        if (!strcmp(name, s_controls_padbtn_names[i])) return i;
    return -1;
}

/* ---- presets ------------------------------------------------------------- */

/* One keyboard preset as SDL key names, two slots per button. */
static const char* const s_controls_preset_keys[2][MGS_BTN_COUNT][MGS_KEYS_PER_BUTTON] = {
    {   /* CLASSIC: what the game shipped with. */
        { "X", NULL }, { "Z", NULL }, { "S", NULL }, { "A", NULL },
        { "Q", NULL }, { "W", NULL }, { "E", NULL }, { "Return", NULL },
        { "Up", NULL }, { "Down", NULL }, { "Left", NULL }, { "Right", NULL },
    },
    {   /* WASD: WASD and the arrows both move; X/Y/R shift right. */
        { "X", NULL }, { "Z", NULL }, { "F", NULL }, { "C", NULL },
        { "Q", NULL }, { "R", NULL }, { "E", NULL }, { "Return", NULL },
        { "W", "Up" }, { "S", "Down" }, { "A", "Left" }, { "D", "Right" },
    },
};

static void controls_apply_keyboard_preset(MgsControls* c, int preset)
{
    int b, s;
    for (b = 0; b < MGS_BTN_COUNT; ++b)
        for (s = 0; s < MGS_KEYS_PER_BUTTON; ++s) {
            const char* name = s_controls_preset_keys[preset][b][s];
            c->key[b][s] = name ? (int32_t)SDL_GetKeyFromName(name) : 0;
        }
}

static void controls_apply_gamepad_defaults(MgsControls* c)
{
    int b;
    for (b = 0; b < MGS_BTN_COUNT; ++b) c->pad[b] = MGS_PADBTN_NONE;
    c->pad[MGS_BTN_A]     = MGS_PADBTN_SOUTH;
    c->pad[MGS_BTN_B]     = MGS_PADBTN_EAST;
    c->pad[MGS_BTN_X]     = MGS_PADBTN_WEST;
    c->pad[MGS_BTN_Y]     = MGS_PADBTN_NORTH;
    c->pad[MGS_BTN_Z]     = MGS_PADBTN_RIGHT_SHOULDER;
    c->pad[MGS_BTN_START] = MGS_PADBTN_START;
    c->pad[MGS_BTN_UP]    = MGS_PADBTN_DPAD_UP;
    c->pad[MGS_BTN_DOWN]  = MGS_PADBTN_DPAD_DOWN;
    c->pad[MGS_BTN_LEFT]  = MGS_PADBTN_DPAD_LEFT;
    c->pad[MGS_BTN_RIGHT] = MGS_PADBTN_DPAD_RIGHT;
    /* L and R stay on the analog triggers (with their click), as the
     * hardware does it; binding them here too would double them up. */
}

void mgs_controls_defaults(MgsControls* c, int preset)
{
    memset(c, 0, sizeof *c);
    if (preset < 0 || preset >= MGS_PRESET_COUNT) preset = MGS_PRESET_WASD;
    if (preset == MGS_PRESET_CUSTOM) preset = MGS_PRESET_WASD;
    c->preset = preset;
    controls_apply_keyboard_preset(c, preset);
    controls_apply_gamepad_defaults(c);
    c->deadzone = 0.12f;
    c->mouse.enabled = 1;
    c->mouse.clicks = 1;
    c->mouse.invert_y = 0;
    c->mouse.sensitivity = 1.0f;
}

/* Which preset (if any) the bindings match exactly; CUSTOM otherwise.
 * The launcher shows this, so a hand edit reads as Custom, not a lie. */
static int controls_detect_preset(const MgsControls* c)
{
    int p;
    for (p = 0; p < MGS_PRESET_WASD + 1; ++p) {
        MgsControls d;
        mgs_controls_defaults(&d, p);
        if (!memcmp(d.key, c->key, sizeof d.key) &&
            !memcmp(d.pad, c->pad, sizeof d.pad))
            return p;
    }
    return MGS_PRESET_CUSTOM;
}

/* ---- the INI text -------------------------------------------------------- */

static char* controls_trim(char* s)
{
    size_t n;
    while (*s == ' ' || *s == '\t') ++s;
    n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
                 s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = '\0';
    return s;
}

static void controls_parse_keyboard_value(MgsControls* c, int btn,
                                          const char* value)
{
    char buf[128];
    char* tok;
    int slot = 0;
    snprintf(buf, sizeof buf, "%s", value);
    c->key[btn][0] = 0;
    c->key[btn][1] = 0;
    for (tok = strtok(buf, ","); tok && slot < MGS_KEYS_PER_BUTTON;
         tok = strtok(NULL, ",")) {
        SDL_Keycode k = SDL_GetKeyFromName(controls_trim(tok));
        if (k != SDLK_UNKNOWN) c->key[btn][slot++] = (int32_t)k;
    }
}

void mgs_controls_parse_text(MgsControls* c, const char* text)
{
    char line[512];
    const char* p = text;
    int section = 0;   /* 0 top, 1 keyboard, 2 gamepad, 3 mouse */
    if (!p) return;
    while (*p) {
        size_t n = 0;
        char *key, *val, *eq;
        while (p[n] && p[n] != '\n' && n < sizeof line - 1) { line[n] = p[n]; ++n; }
        line[n] = '\0';
        p += n + (p[n] == '\n' ? 1 : 0);
        key = controls_trim(line);
        if (!*key || *key == '#' || *key == ';') continue;
        if (*key == '[') {
            if (!strncmp(key, "[keyboard]", 10)) section = 1;
            else if (!strncmp(key, "[gamepad]", 9)) section = 2;
            else if (!strncmp(key, "[mouse]", 7)) section = 3;
            else section = 0;
            continue;
        }
        eq = strchr(key, '=');
        if (!eq) continue;
        *eq = '\0';
        key = controls_trim(key);
        val = controls_trim(eq + 1);
        if (section == 1 || (section == 0 && mgs_controls_button_from_id(key) >= 0)) {
            int b = mgs_controls_button_from_id(key);
            if (b >= 0) controls_parse_keyboard_value(c, b, val);
        } else if (section == 2) {
            int b = mgs_controls_button_from_id(key);
            if (b >= 0) c->pad[b] = (int8_t)mgs_controls_padbtn_from_name(val);
            else if (!strcmp(key, "deadzone")) c->deadzone = (float)atof(val);
        } else if (section == 3) {
            if (!strcmp(key, "enabled")) c->mouse.enabled = atoi(val) != 0;
            else if (!strcmp(key, "clicks")) c->mouse.clicks = atoi(val) != 0;
            else if (!strcmp(key, "invert_y")) c->mouse.invert_y = atoi(val) != 0;
            else if (!strcmp(key, "sensitivity")) c->mouse.sensitivity = (float)atof(val);
        } else if (!strcmp(key, "deadzone")) {
            c->deadzone = (float)atof(val);
        }
    }
    if (c->deadzone < 0.0f) c->deadzone = 0.0f;
    if (c->deadzone > 0.5f) c->deadzone = 0.5f;
    if (c->mouse.sensitivity < 0.2f) c->mouse.sensitivity = 0.2f;
    if (c->mouse.sensitivity > 3.0f) c->mouse.sensitivity = 3.0f;
    c->preset = controls_detect_preset(c);
}

int mgs_controls_write_text(const MgsControls* c, char* out, int out_size)
{
    static const char* const preset_names[MGS_PRESET_COUNT] = {
        "classic", "wasd", "custom",
    };
    int n = 0, b, s;
    if (!out || out_size <= 0) return 0;
    n += snprintf(out + n, (size_t)(out_size - n),
                  "# Metal Gear Solid: The Twin Snakes - controls\n"
                  "# Changed in the launcher's Controls section, or here.\n"
                  "preset=%s\n\n[keyboard]\n",
                  preset_names[c->preset >= 0 && c->preset < MGS_PRESET_COUNT
                               ? c->preset : MGS_PRESET_CUSTOM]);
    for (b = 0; b < MGS_BTN_COUNT && n < out_size; ++b) {
        n += snprintf(out + n, (size_t)(out_size - n), "%s=",
                      s_controls_buttons[b].id);
        for (s = 0; s < MGS_KEYS_PER_BUTTON; ++s) {
            if (!c->key[b][s]) continue;
            n += snprintf(out + n, (size_t)(out_size - n), "%s%s",
                          s ? ", " : "",
                          SDL_GetKeyName((SDL_Keycode)c->key[b][s]));
        }
        n += snprintf(out + n, (size_t)(out_size - n), "\n");
    }
    n += snprintf(out + n, (size_t)(out_size - n), "\n[gamepad]\n");
    for (b = 0; b < MGS_BTN_COUNT && n < out_size; ++b)
        n += snprintf(out + n, (size_t)(out_size - n), "%s=%s\n",
                      s_controls_buttons[b].id,
                      mgs_controls_padbtn_name(c->pad[b]));
    n += snprintf(out + n, (size_t)(out_size - n),
                  "deadzone=%.2f\n\n[mouse]\nenabled=%d\nclicks=%d\n"
                  "invert_y=%d\nsensitivity=%.2f\n",
                  (double)c->deadzone, c->mouse.enabled, c->mouse.clicks,
                  c->mouse.invert_y, (double)c->mouse.sensitivity);
    return n < out_size ? n : 0;
}

/* ---- the file -------------------------------------------------------------- */

static int controls_path(char* out, size_t n)
{
    char dir[900];
    if (!mgs_config_dir(dir, sizeof dir)) return 0;
    snprintf(out, n, "%s/controls.ini", dir);
    return 1;
}

int mgs_controls_load(MgsControls* c)
{
    char path[1024];
    FILE* f;
    mgs_controls_defaults(c, MGS_PRESET_WASD);
    if (!controls_path(path, sizeof path) || !(f = fopen(path, "r"))) return 0;
    {
        char text[8192];
        size_t got = fread(text, 1, sizeof text - 1, f);
        text[got] = '\0';
        mgs_controls_parse_text(c, text);
    }
    fclose(f);
    return 1;
}

int mgs_controls_save(const MgsControls* c)
{
    char path[1024], text[8192];
    FILE* f;
    if (!mgs_config_make_dir() || !controls_path(path, sizeof path)) return 0;
    if (!mgs_controls_write_text(c, text, (int)sizeof text)) return 0;
    if (!(f = fopen(path, "w"))) return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

/* ---- the shared copy -------------------------------------------------------- */

static MgsControls s_controls_current;
static int         s_controls_current_loaded;

const MgsControls* mgs_controls_get(void)
{
    if (!s_controls_current_loaded) {
        mgs_controls_load(&s_controls_current);
        s_controls_current_loaded = 1;
    }
    return &s_controls_current;
}

void mgs_controls_set(const MgsControls* c)
{
    s_controls_current = *c;
    s_controls_current_loaded = 1;
}
