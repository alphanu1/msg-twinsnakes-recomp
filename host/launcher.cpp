/* THE LAUNCHER'S SCREEN (Ben, 2026-09-25: part of the game, Dear ImGui).
 *
 * Drawn in the game's own window, before the game starts: the two discs
 * (chosen with the system's file dialog, checked by hash), whether the
 * native game exists, the settings, and Play. Everything that is not
 * drawing is in launcher_core.c; this file only shows it and collects
 * choices. Works with a mouse, a keyboard or a controller - the last so a
 * Steam Deck or a sofa never needs a mouse.
 */
#include "launcher.h"
#include "launcher_core.h"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <stdio.h>
#include <string.h>

extern "C" const char* mgs_input_name(unsigned port);

namespace {

struct DiscSlot {
    char          path[1024];
    MgsDiscStatus status;
    char          message[256];
};

/* The file dialog answers on whatever thread the platform chooses, so it
 * only leaves a path here; the frame loop picks it up. */
struct Pending {
    SDL_Mutex* lock;
    char       path[1024];
    int        slot;           /* 1 or 2, 0 = nothing waiting */
};

Pending g_pending;

void SDLCALL on_file_chosen(void* user, const char* const* files, int filter)
{
    (void)filter;
    int slot = (int)(intptr_t)user;
    if (!files || !files[0]) return;           /* cancelled, or an error */
    SDL_LockMutex(g_pending.lock);
    SDL_strlcpy(g_pending.path, files[0], sizeof g_pending.path);
    g_pending.slot = slot;
    SDL_UnlockMutex(g_pending.lock);
}

void check(DiscSlot& d, unsigned number)
{
    d.status = mgs_launcher_check_disc(d.path, number, d.message, sizeof d.message);
}

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kBad(0.95f, 0.45f, 0.40f, 1.0f);
const ImVec4 kDim(0.62f, 0.66f, 0.70f, 1.0f);

void disc_row(SDL_Window* window, DiscSlot& d, unsigned number)
{
    static const SDL_DialogFileFilter filters[] = {
        { "GameCube disc images", "iso;gcm" },
        { "All files", "*" },
    };
    ImGui::PushID((int)number);
    ImGui::Text("Disc %u", number);
    ImGui::SameLine(110.0f);
    if (d.path[0]) {
        const char* base = strrchr(d.path, '/');
        const char* base2 = strrchr(d.path, '\\');
        if (base2 > base) base = base2;
        ImGui::TextUnformatted(base ? base + 1 : d.path);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.path);
    } else {
        ImGui::TextColored(kDim, "not chosen");
    }
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 140.0f);
    if (ImGui::Button("Choose...", ImVec2(140.0f, 0.0f)))
        SDL_ShowOpenFileDialog(on_file_chosen, (void*)(intptr_t)number, window,
                               filters, 2, d.path[0] ? d.path : NULL, false);
    ImGui::Dummy(ImVec2(110.0f - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::SameLine(110.0f);
    if (d.status == MGS_DISC_OK)
        ImGui::TextColored(kGood, "%s", d.message);
    else if (d.status != MGS_DISC_UNCHECKED)
        ImGui::TextColored(kBad, "%s", d.message);
    else
        ImGui::TextColored(kDim, "Choose your disc image (.iso, .gcm or NKit).");
    ImGui::PopID();
}

}  // namespace

extern "C" int mgs_launcher_run(SDL_Window* window, SDL_Renderer* renderer,
                                const char* module_path, MgsLaunchChoice* out)
{
    MgsSettings set;
    DiscSlot disc[2];
    int result = 0, done = 0;

    memset(out->disc1, 0, sizeof out->disc1);
    memset(out->disc2, 0, sizeof out->disc2);
    memset(disc, 0, sizeof disc);
    mgs_settings_load(&set);
    SDL_strlcpy(disc[0].path, set.disc1, sizeof disc[0].path);
    SDL_strlcpy(disc[1].path, set.disc2, sizeof disc[1].path);
    if (!disc[0].path[0] && out->found_disc1[0])
        SDL_strlcpy(disc[0].path, out->found_disc1, sizeof disc[0].path);
    if (!disc[1].path[0] && out->found_disc2[0])
        SDL_strlcpy(disc[1].path, out->found_disc2, sizeof disc[1].path);
    if (disc[0].path[0]) check(disc[0], 1u);
    if (disc[1].path[0]) check(disc[1], 2u);

    g_pending.lock = SDL_CreateMutex();
    g_pending.slot = 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                       /* no imgui.ini beside the game */
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = 20.0f;
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    /* The game draws a 640x480 picture scaled into the window; the launcher
     * wants the window's real pixels. Restored before the game starts. */
    SDL_RendererLogicalPresentation mode;
    int lw = 0, lh = 0;
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderVSync(renderer, 1);

    while (!done) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) { done = 1; result = 0; }
        }
        SDL_LockMutex(g_pending.lock);
        if (g_pending.slot) {
            DiscSlot& d = disc[g_pending.slot - 1];
            SDL_strlcpy(d.path, g_pending.path, sizeof d.path);
            check(d, (unsigned)g_pending.slot);
            g_pending.slot = 0;
        }
        SDL_UnlockMutex(g_pending.lock);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::Begin("launcher", NULL, ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::PushFont(NULL, style.FontSizeBase * 1.8f);
        ImGui::TextUnformatted("METAL GEAR SOLID");
        ImGui::PopFont();
        ImGui::PushFont(NULL, style.FontSizeBase * 1.2f);
        ImGui::TextUnformatted("The Twin Snakes");
        ImGui::PopFont();
        ImGui::TextColored(kDim, "Native PC port - played from your own discs.");
        ImGui::Spacing();

        ImGui::SeparatorText("Discs");
        disc_row(window, disc[0], 1u);
        disc_row(window, disc[1], 2u);

        ImGui::SeparatorText("Native game");
        if (module_path && module_path[0]) {
            ImGui::TextColored(kGood, "Ready.");
            ImGui::SameLine();
            ImGui::TextColored(kDim, "%s", module_path);
        } else {
            ImGui::TextColored(kBad, "Not built yet.");
            ImGui::TextColored(kDim, "Building it from your discs is the next step "
                                     "of the launcher; for now build it with the "
                                     "development tools.");
        }

        ImGui::SeparatorText("Settings");
        ImGui::SetNextItemWidth(360.0f);
        ImGui::SliderInt("Volume", &set.volume, 0, 200, "%d%%");
        ImGui::SameLine();
        ImGui::TextColored(kDim, "100%% is the console's own level");
        {
            bool fs = set.fullscreen != 0, skip = set.skip_launcher != 0;
            if (ImGui::Checkbox("Fullscreen", &fs)) set.fullscreen = fs;
            if (ImGui::Checkbox("Start the game straight away next time", &skip))
                set.skip_launcher = skip;
            if (skip)
                ImGui::TextColored(kDim, "To see this screen again, start with --launcher.");
        }
        {
            const char* pad = mgs_input_name(0u);
            ImGui::Text("Controller:");
            ImGui::SameLine();
            if (pad) ImGui::TextColored(kGood, "%s", pad);
            else ImGui::TextColored(kDim, "none - the keyboard works too; plug one in at any time");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const bool ready = disc[0].status == MGS_DISC_OK &&
                           (disc[1].status == MGS_DISC_OK || !disc[1].path[0]) &&
                           module_path && module_path[0];
        ImGui::BeginDisabled(!ready);
        ImGui::PushFont(NULL, style.FontSizeBase * 1.3f);
        /* MGS_LAUNCHER_AUTOPLAY=1 presses Play once it can be pressed -
         * for scripted runs and tests of the whole path, which have no
         * hand on the mouse. */
        static int autoplay = -1;
        if (autoplay < 0) autoplay = SDL_getenv("MGS_LAUNCHER_AUTOPLAY") != NULL;
        if (ImGui::Button("PLAY", ImVec2(220.0f, 56.0f)) || (ready && autoplay) ||
            (ready && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive())) {
            done = 1; result = 1;
        }
        ImGui::PopFont();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(140.0f, 56.0f))) { done = 1; result = 0; }
        if (!ready) {
            ImGui::TextColored(kDim, "%s", disc[0].status != MGS_DISC_OK
                               ? "Choose disc 1 to play. Disc 2 is needed later in the game."
                               : "The native game has to be built first.");
        } else if (!disc[1].path[0]) {
            ImGui::TextColored(kDim, "Disc 2 is not chosen: the game will ask for it "
                                     "part way through.");
        }
        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 20, 24, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_SetRenderVSync(renderer, 0);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
    SDL_DestroyMutex(g_pending.lock);

    SDL_strlcpy(set.disc1, disc[0].path, sizeof set.disc1);
    SDL_strlcpy(set.disc2, disc[1].path, sizeof set.disc2);
    mgs_settings_save(&set);
    if (result) {
        mgs_settings_apply(&set);
        SDL_strlcpy(out->disc1, disc[0].path, sizeof out->disc1);
        if (disc[1].status == MGS_DISC_OK)
            SDL_strlcpy(out->disc2, disc[1].path, sizeof out->disc2);
        out->fullscreen = set.fullscreen;
    }
    return result;
}
