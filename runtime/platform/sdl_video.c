#include "sdl_video.h"
#include "controls.h"

#include <stdio.h>
#include <stdlib.h>
#include "font8x8.h"

#include <SDL3/SDL.h>
#include <string.h>

static SDL_Window*   s_window;
static SDL_Renderer* s_renderer;
static SDL_Texture*  s_texture;
static uint32_t      s_fb[MGS_XFB_WIDTH * MGS_XFB_HEIGHT];

int mgs_video_init(const char* title)
{
    if (s_window) return 1;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return 0;

    /* Resizable, and starting at double the XFB so 640x480 is not a postage
     * stamp on a modern display. The framebuffer stays 640x480 regardless:
     * the window scales it, so what the eventual GX path produces is what is
     * shown, unscaled and unguessed.
     */
    /* PRESENT ON THE SAME API WE RENDER ON.
     *
     * `SDL_CreateWindowAndRenderer` with no hint takes SDL's default 2D
     * renderer, which on Linux is OpenGL - so the process ended up holding
     * TWO graphics drivers at once: Vulkan for the GPU device that draws the
     * frame, and OpenGL for the window that shows it. Ben noticed from the
     * outside, with MangoHud reporting the game as OpenGL, and he was right
     * to: an overlay hooks the window, and the window was the OpenGL half.
     *
     * Asking for Vulkan here costs nothing and leaves one driver loaded.
     * It is a HINT, not a demand: if the host has no Vulkan renderer SDL
     * falls back to whatever it does have, which is why the chosen driver
     * is printed rather than assumed.
     *
     * This does NOT move the picture onto the GPU path. The frame still
     * goes GPU -> readback -> the game's own framebuffer -> YUV -> here,
     * because the framebuffer the video interface scans out is the game's
     * and not ours, and short-circuiting that would show a different
     * picture from the one the console shows. Presenting through the GPU
     * device's own swapchain is a separate change with its own comparison. */
    {
        const char* want = getenv("MGS_RENDER_DRIVER");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, want && *want ? want : "vulkan");
    }
    if (!SDL_CreateWindowAndRenderer(title, MGS_XFB_WIDTH * 2, MGS_XFB_HEIGHT * 2,
                                     SDL_WINDOW_RESIZABLE, &s_window, &s_renderer)) {
        /* A host with no Vulkan renderer at all: clear the hint and let SDL
         * choose, rather than failing to open a window over a preference. */
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, NULL);
        if (!SDL_CreateWindowAndRenderer(title, MGS_XFB_WIDTH * 2,
                                         MGS_XFB_HEIGHT * 2,
                                         SDL_WINDOW_RESIZABLE,
                                         &s_window, &s_renderer))
            return 0;
    }
    fprintf(stderr, "[video] presenting with SDL's %s renderer\n",
            SDL_GetRendererName(s_renderer));

    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  MGS_XFB_WIDTH, MGS_XFB_HEIGHT);
    if (!s_texture) return 0;

    /* Nearest, not linear: this is a 640x480 image on a 4K display and the
     * pixels are the point. Smoothing them is a decision for a later
     * upscaling pass, not a default.
     */
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(s_renderer, MGS_XFB_WIDTH, MGS_XFB_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return 1;
}

void mgs_video_shutdown(void)
{
    if (s_texture)  { SDL_DestroyTexture(s_texture);   s_texture = NULL; }
    if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = NULL; }
    if (s_window)   { SDL_DestroyWindow(s_window);     s_window = NULL; }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

uint32_t* mgs_video_framebuffer(void) { return s_fb; }

/* For the launcher, which draws in the same window before the game. */
SDL_Window*   mgs_video_window(void);
SDL_Renderer* mgs_video_renderer(void);
SDL_Window*   mgs_video_window(void)   { return s_window; }
SDL_Renderer* mgs_video_renderer(void) { return s_renderer; }

void mgs_video_clear(uint32_t argb)
{
    size_t i, n = MGS_XFB_WIDTH * MGS_XFB_HEIGHT;
    for (i = 0; i < n; ++i) s_fb[i] = argb;
}

void mgs_video_rect(int x, int y, int w, int h, uint32_t argb)
{
    int px, py;
    for (py = y; py < y + h; ++py) {
        if (py < 0 || py >= MGS_XFB_HEIGHT) continue;
        for (px = x; px < x + w; ++px) {
            if (px < 0 || px >= MGS_XFB_WIDTH) continue;
            s_fb[py * MGS_XFB_WIDTH + px] = argb;
        }
    }
}

void mgs_video_text(int x, int y, uint32_t argb, const char* text)
{
    int cx = x;
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        const unsigned char* glyph;
        int row;

        if (c == '\n') { cx = x; y += 9; continue; }
        if (c < 32 || c > 127) c = '?';
        glyph = k_font8x8[c - 32];

        for (row = 0; row < 8; ++row) {
            int col;
            int py = y + row;
            if (py < 0 || py >= MGS_XFB_HEIGHT) continue;
            for (col = 0; col < 8; ++col) {
                int px = cx + col;
                if (px < 0 || px >= MGS_XFB_WIDTH) continue;
                /* Bit 7 is the leftmost pixel, matching how the glyph rows
                 * are written in the generator. */
                if (glyph[row] & (0x80u >> col))
                    s_fb[py * MGS_XFB_WIDTH + px] = argb;
            }
        }
        cx += 8;
    }
}

/* KEYBOARD TO GAMECUBE CONTROLLER.
 *
 * The serial interface reports a controller in port 1 and polls it every
 * field (F153); this is what puts something in the reply. The button word is
 * the one the poll response carries, so the mapping is to GameCube bits, not
 * to anything of ours.
 *
 * Keyboard first because it needs no hardware to test with. A real gamepad
 * goes through SDL_Gamepad and ORs into the same word.
 */
/* Bindings come from controls.ini (controls.c): presets there, the
 * launcher's Controls section edits them, and a hand-edited file is read
 * the same way. Two keys may share a button (WASD and the arrows). */
uint16_t mgs_video_pad(void)
{
    const MgsControls* c = mgs_controls_get();
    const bool* keys = SDL_GetKeyboardState(NULL);
    uint16_t held = 0u;
    int b, s;

    if (keys) {
        for (b = 0; b < MGS_BTN_COUNT; ++b)
            for (s = 0; s < MGS_KEYS_PER_BUTTON; ++s) {
                SDL_Scancode sc;
                if (!c->key[b][s]) continue;
                sc = SDL_GetScancodeFromKey((SDL_Keycode)c->key[b][s], NULL);
                if (sc != SDL_SCANCODE_UNKNOWN && keys[sc])
                    held |= mgs_controls_button_bit(b);
            }
    }
    /* Mouse clicks as A and B, with the game window focused - reading the
     * global mouse state without the focus check would let a click on
     * another window press the game's buttons. */
    if (c->mouse.enabled && c->mouse.clicks &&
        SDL_GetKeyboardFocus() == s_window) {
        Uint32 m = SDL_GetMouseState(NULL, NULL);
        if (m & SDL_BUTTON_LMASK) held |= mgs_controls_button_bit(MGS_BTN_A);
        if (m & SDL_BUTTON_RMASK) held |= mgs_controls_button_bit(MGS_BTN_B);
    }
    return held;
}

/* The mouse as the C-stick (camera, in this game). Relative motion charges
 * a deflection that decays every frame, so the stick follows the hand and
 * centres when the hand stops - the way a thumbstick pushed and released
 * behaves, rather than a position the cursor happens to sit at. */
void mgs_video_mouse_substick(uint8_t* out_x, uint8_t* out_y)
{
    static float s_mouse_acc_x, s_mouse_acc_y;
    const MgsControls* c = mgs_controls_get();
    float dx = 0.0f, dy = 0.0f, vx, vy;

    *out_x = 128u;
    *out_y = 128u;
    if (!c->mouse.enabled) {
        s_mouse_acc_x = 0.0f;
        s_mouse_acc_y = 0.0f;
        return;
    }
    SDL_GetRelativeMouseState(&dx, &dy);
    if (SDL_GetKeyboardFocus() == s_window) {
        s_mouse_acc_x += dx * 0.02f * c->mouse.sensitivity;
        s_mouse_acc_y += dy * 0.02f * c->mouse.sensitivity
                       * (c->mouse.invert_y ? -1.0f : 1.0f);
    }
    s_mouse_acc_x *= 0.75f;
    s_mouse_acc_y *= 0.75f;
    if (s_mouse_acc_x > 1.0f) s_mouse_acc_x = 1.0f;
    if (s_mouse_acc_x < -1.0f) s_mouse_acc_x = -1.0f;
    if (s_mouse_acc_y > 1.0f) s_mouse_acc_y = 1.0f;
    if (s_mouse_acc_y < -1.0f) s_mouse_acc_y = -1.0f;
    vx = s_mouse_acc_x;
    vy = s_mouse_acc_y;
    if (vx > -0.05f && vx < 0.05f) vx = 0.0f;
    if (vy > -0.05f && vy < 0.05f) vy = 0.0f;
    *out_x = (uint8_t)(128 + (int)(vx * 100.0f));
    *out_y = (uint8_t)(128 - (int)(vy * 100.0f));
}


/* Events only: no conversion, no upload, no present.
 *
 * The host pumps this every time it ticks a frame, which is far more often
 * than the game produces one. Presenting on that tick meant an SDL present
 * per 2000 guest instructions - about 20,000 in a boot against some 60 real
 * frames - and with vsync each of those waits for the display. That is how a
 * port ends up slow while using almost no processor.
 */
int mgs_video_pump(void)
{
    SDL_Event ev;

    if (!s_window) return 0;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) return 0;
        /* A controller plugged in or pulled out: rescan the ports, and
         * say so, so a pad that is not seen is visible rather than silent. */
        if (ev.type == SDL_EVENT_GAMEPAD_ADDED ||
            ev.type == SDL_EVENT_GAMEPAD_REMOVED) {
            void mgs_input_poll(void);
            const char* mgs_input_name(unsigned port);
            const char* name;
            mgs_input_poll();
            name = mgs_input_name(0u);
            fprintf(stderr, "[pad] controller %s: port 1 is %s\n",
                    ev.type == SDL_EVENT_GAMEPAD_ADDED ? "connected"
                                                       : "removed",
                    name ? name : "the keyboard");
        }
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) return 0;
    }
    return 1;
}

int mgs_video_present(void)
{
    if (!mgs_video_pump()) return 0;


    SDL_UpdateTexture(s_texture, NULL, s_fb, MGS_XFB_WIDTH * (int)sizeof(uint32_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
    return 1;
}
