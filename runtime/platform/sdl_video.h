/* The window, and what goes in it.
 *
 * Opened early and deliberately. The screen is the only output channel that
 * survives into a shipped build: a terminal is available now and will not be
 * later, and a boot that fails in front of a black window tells you nothing.
 * So this presents a framebuffer from the first frame, showing real state
 * until there is game output to replace it.
 *
 * The framebuffer is the GameCube's own XFB geometry, 640x480. Presenting at
 * native size and letting the window scale keeps the eventual GX path honest:
 * whatever the renderer produces will land in exactly this buffer.
 */
#ifndef MGS_SDL_VIDEO_H
#define MGS_SDL_VIDEO_H

#include <stdint.h>

#define MGS_XFB_WIDTH  640
#define MGS_XFB_HEIGHT 480

int  mgs_video_init(const char* title);
void mgs_video_shutdown(void);

/* XRGB8888, MGS_XFB_WIDTH * MGS_XFB_HEIGHT. Written directly. */
uint32_t* mgs_video_framebuffer(void);

/* Push the framebuffer to the window. Returns 0 if the user closed it. */
int  mgs_video_present(void);

/* Pump input without presenting; returns 0 when the user quit. */
int  mgs_video_pump(void);

/* What the keyboard is holding down, as a GameCube button word, sampled when
 * called. The host feeds this to the serial interface once a frame; held
 * state rather than accumulated edges, because the guest samples the port
 * every field and wants to know what is down now. */
uint16_t mgs_video_pad(void);

/* Minimal drawing, enough for a boot overlay. A real font is not the point;
 * legible hex and short labels are.
 */
void mgs_video_clear(uint32_t argb);
void mgs_video_text(int x, int y, uint32_t argb, const char* text);
void mgs_video_rect(int x, int y, int w, int h, uint32_t argb);

#endif
