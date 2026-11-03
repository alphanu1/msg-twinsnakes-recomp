/* Remappable controls: keyboard, gamepad and mouse, saved to controls.ini
 * in the same per-user config folder as the launcher's settings.ini.
 *
 * Three layers share this file. The launcher shows it and lets the player
 * change it; the SDL input code (sdl_video.c, sdl_input.c) reads it once
 * and then every frame; the parsing and the presets are plain data, so
 * tests/test_controls.c can check them without a window or a controller.
 *
 * A binding is a list of physical inputs for one GameCube button. The
 * keyboard allows two keys per button (WASD and the arrows both move);
 * the gamepad allows one button per GameCube button, which is all a pad
 * has to spare.
 */
#ifndef MGS_CONTROLS_H
#define MGS_CONTROLS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GameCube buttons, in the order every table here uses. The value is the
 * PAD_BUTTON_/PAD_TRIGGER_ bit the guest reads (runtime/pad/pad.h). */
enum {
    MGS_BTN_A = 0, MGS_BTN_B, MGS_BTN_X, MGS_BTN_Y,
    MGS_BTN_L, MGS_BTN_R, MGS_BTN_Z, MGS_BTN_START,
    MGS_BTN_UP, MGS_BTN_DOWN, MGS_BTN_LEFT, MGS_BTN_RIGHT,
    MGS_BTN_COUNT
};

/* SDL_GamepadButton values, named so the config file and the launcher do
 * not need SDL's enum order memorised. -1 = unbound. */
enum {
    MGS_PADBTN_NONE = -1,
    MGS_PADBTN_SOUTH = 0, MGS_PADBTN_EAST, MGS_PADBTN_WEST, MGS_PADBTN_NORTH,
    MGS_PADBTN_BACK, MGS_PADBTN_GUIDE, MGS_PADBTN_START,
    MGS_PADBTN_LEFT_STICK, MGS_PADBTN_RIGHT_STICK,
    MGS_PADBTN_LEFT_SHOULDER, MGS_PADBTN_RIGHT_SHOULDER,
    MGS_PADBTN_DPAD_UP, MGS_PADBTN_DPAD_DOWN, MGS_PADBTN_DPAD_LEFT, MGS_PADBTN_DPAD_RIGHT,
    MGS_PADBTN_COUNT
};

enum {
    MGS_PRESET_CLASSIC = 0,   /* the mapping the game shipped with */
    MGS_PRESET_WASD,          /* WASD moves, buttons shift right */
    MGS_PRESET_CUSTOM,        /* anything the player changed by hand */
    MGS_PRESET_COUNT
};

#define MGS_KEYS_PER_BUTTON 2

typedef struct MgsControls {
    int preset;                              /* MGS_PRESET_* */
    /* Keyboard: SDL_Keycode values (SDL3/SDL_keycode.h), 0 = empty slot. */
    int32_t key[MGS_BTN_COUNT][MGS_KEYS_PER_BUTTON];
    /* Gamepad: MGS_PADBTN_* per GameCube button (D-pad included). */
    int8_t  pad[MGS_BTN_COUNT];
    float   deadzone;                        /* stick dead zone, 0..0.5 */
    struct {
        int   enabled;                       /* mouse drives the C-stick */
        int   clicks;                        /* left=A, right=B */
        int   invert_y;
        float sensitivity;                   /* 0.2..3.0 */
    } mouse;
} MgsControls;

/* The button's guest bit (PAD_BUTTON_A etc.) and its short name ("A",
 * "D-Up"), for tables, the config file and the launcher alike. */
uint16_t   mgs_controls_button_bit(int gc_button);
const char* mgs_controls_button_name(int gc_button);
/* Config-file key for a button ("a", "d_up"...), or NULL. */
const char* mgs_controls_button_id(int gc_button);
int         mgs_controls_button_from_id(const char* id);   /* -1 if none */

/* SDL gamepad button <-> the name the config file uses ("south"...). */
const char* mgs_controls_padbtn_name(int padbtn);
int         mgs_controls_padbtn_from_name(const char* name);  /* -1 if none */

/* Fill `c` with a preset's bindings (also the defaults when no file). */
void mgs_controls_defaults(MgsControls* c, int preset);
/* Parse INI text over `c` (start from defaults). Unknown keys and values
 * are ignored, so a hand-edited or newer file never breaks the game. */
void mgs_controls_parse_text(MgsControls* c, const char* text);
/* Write `c` as INI text. Returns the byte count, or 0 if it did not fit. */
int  mgs_controls_write_text(const MgsControls* c, char* out, int out_size);

/* controls.ini beside settings.ini. Load returns 1 when a file was read. */
int mgs_controls_load(MgsControls* c);
int mgs_controls_save(const MgsControls* c);

/* The process-wide copy the input code reads. Loaded from disk on first
 * use; the launcher replaces it wholesale when the player edits. */
const MgsControls* mgs_controls_get(void);
void               mgs_controls_set(const MgsControls* c);

#ifdef __cplusplus
}
#endif

#endif
