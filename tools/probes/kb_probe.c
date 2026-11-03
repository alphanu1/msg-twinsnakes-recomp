/* Live keyboard and mouse probe.
 *
 * The twin of pad_probe for the keyboard table and the mouse: prints the
 * GameCube button word mgs_video_pad() produces, and the C-stick bytes
 * mgs_video_mouse_substick() produces, whenever either changes. Run it,
 * press keys, move the mouse; what prints is what the guest would read
 * (module.c ORs exactly this word into the serial poll).
 *
 *   kb_probe           run until the window closes (or 60 seconds)
 */
#include "platform/sdl_video.h"
#include "platform/controls.h"
#include "pad/pad.h"

#include <stdio.h>
#include <unistd.h>

static void print_buttons(uint16_t b)
{
    struct { uint16_t bit; const char* name; } map[] = {
        {PAD_BUTTON_A,"A"}, {PAD_BUTTON_B,"B"}, {PAD_BUTTON_X,"X"},
        {PAD_BUTTON_Y,"Y"}, {PAD_BUTTON_START,"START"}, {PAD_TRIGGER_Z,"Z"},
        {PAD_TRIGGER_L,"L"}, {PAD_TRIGGER_R,"R"},
        {PAD_BUTTON_UP,"up"}, {PAD_BUTTON_DOWN,"down"},
        {PAD_BUTTON_LEFT,"left"}, {PAD_BUTTON_RIGHT,"right"},
    };
    size_t i; int first = 1;
    if (!b) { printf("-"); return; }
    for (i = 0; i < sizeof map / sizeof map[0]; ++i)
        if (b & map[i].bit) { printf("%s%s", first ? "" : "+", map[i].name); first = 0; }
}

int main(void)
{
    uint16_t last = 0xFFFFu;
    uint8_t last_x = 0, last_y = 0;
    int i;

    if (!mgs_video_init("kb probe")) { printf("video init failed\n"); return 1; }
    printf("preset=%d  (0 classic, 1 wasd, 2 custom)\n",
           mgs_controls_get()->preset);
    printf("press keys / move the mouse; close the window to stop\n");
    fflush(stdout);

    for (i = 0; i < 60 * 20; ++i) {
        uint16_t held;
        uint8_t mx, my;
        if (!mgs_video_pump()) break;
        held = mgs_video_pad();
        mgs_video_mouse_substick(&mx, &my);
        if (held != last || mx != last_x || my != last_y) {
            printf("buttons 0x%04X: ", held);
            print_buttons(held);
            printf("   mouse c-stick (%u,%u)\n", mx, my);
            fflush(stdout);
            last = held; last_x = mx; last_y = my;
        }
        usleep(50000);
    }
    mgs_video_shutdown();
    return 0;
}
