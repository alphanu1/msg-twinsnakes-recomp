#include "sdl_video.h"

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
static const struct { SDL_Keycode key; uint16_t button; } s_pad_keys[] = {
    { SDLK_RETURN, 0x1000u },   /* Start  */
    { SDLK_X,      0x0100u },   /* A      */
    { SDLK_Z,      0x0200u },   /* B      */
    { SDLK_S,      0x0400u },   /* X      */
    { SDLK_A,      0x0800u },   /* Y      */
    { SDLK_Q,      0x0040u },   /* L      */
    { SDLK_W,      0x0020u },   /* R      */
    { SDLK_E,      0x0010u },   /* Z      */
    { SDLK_LEFT,   0x0001u },
    { SDLK_RIGHT,  0x0002u },
    { SDLK_DOWN,   0x0004u },
    { SDLK_UP,     0x0008u },
};

uint16_t mgs_video_pad(void)
{
    const SDL_Keycode* unused = NULL;
    const bool* keys = SDL_GetKeyboardState(NULL);
    uint16_t held = 0u;
    size_t i;

    (void)unused;
    if (!keys) return 0u;

    for (i = 0; i < sizeof s_pad_keys / sizeof s_pad_keys[0]; ++i) {
        SDL_Scancode sc = SDL_GetScancodeFromKey(s_pad_keys[i].key, NULL);
        if (sc != SDL_SCANCODE_UNKNOWN && keys[sc]) held |= s_pad_keys[i].button;
    }
    return held;
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
