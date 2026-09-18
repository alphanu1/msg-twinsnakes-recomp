#include "sdl_video.h"
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
    if (!SDL_CreateWindowAndRenderer(title, MGS_XFB_WIDTH * 2, MGS_XFB_HEIGHT * 2,
                                     SDL_WINDOW_RESIZABLE, &s_window, &s_renderer))
        return 0;

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

int mgs_video_present(void)
{
    SDL_Event ev;

    if (!s_window) return 0;

    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) return 0;
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) return 0;
    }

    SDL_UpdateTexture(s_texture, NULL, s_fb, MGS_XFB_WIDTH * (int)sizeof(uint32_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderTexture(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
    return 1;
}
