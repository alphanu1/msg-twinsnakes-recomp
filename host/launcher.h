/* The launcher screen, shown in the game's window before the game starts
 * (host/launcher.cpp). Returns 1 for Play, 0 for Quit. */
#ifndef MGS_LAUNCHER_H
#define MGS_LAUNCHER_H

#ifdef __cplusplus
extern "C" {
#endif

struct SDL_Window;
struct SDL_Renderer;

typedef struct MgsLaunchChoice {
    /* In: discs the game found on its own, offered when nothing was
     * chosen before (the development layout, a portable folder). */
    char found_disc1[1024];
    char found_disc2[1024];
    /* Out: what to play. disc2 may be empty. */
    char disc1[1024];
    char disc2[1024];
    int  fullscreen;
} MgsLaunchChoice;

int mgs_launcher_run(struct SDL_Window* window, struct SDL_Renderer* renderer,
                     const char* module_path, MgsLaunchChoice* choice);

#ifdef __cplusplus
}
#endif

#endif
